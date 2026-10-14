// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/ManifestJson.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

const char* kSample = R"({
  // comments are allowed
  "schemaVersion": 1,
  "effect": {
    "ofxId": "com.example.glow", "uuid": "123", "name": "Glow", "grouping": "Stylize", "kind": "filter",
    "version": "2.3.1", "deprecatedMajors": [1],
    "capabilities": { "tiles": true, "depths": ["8u", "32f"], "gpu": ["metal"], "threadSafety": "full", "noSpatialAwareness": true }
  },
  "clips": [ { "name": "Mask", "optional": true, "mask": true } ],
  "parameters": [
    { "id": 100, "key": "radius", "type": "float", "label": "Radius", "hint": "Blur radius", "default": 10, "range": [0, 1000], "displayRange": [0, 200],
      "flags": ["hidden", "nostate"], "tags": ["shape"], "meta": { "unit": "px" }, "visibleWhen": "mode == 'soft'", "hosts": { "resolve": { "exclude": true } } },
    { "id": 101, "key": "mode", "type": "choice", "options": [ { "key": "soft", "label": "Soft" }, "Hard" ], "default": 0 },
    { "id": 102, "key": "center", "type": "point", "default": [0.5, 0.5] },
    { "id": 103, "key": "tint", "type": "rgba", "default": { "r": 1, "g": 0.5, "b": 0.25, "a": 1 } },
    { "id": 200, "key": "group", "type": "group", "parameters": [ { "id": 201, "key": "inner", "type": "toggle", "default": true } ] }
  ],
  "migrations": [ { "from": 0, "rename": { "rad": "radius" } } ]
})";

}  // namespace

TEST(ManifestJson, ParsesTheSample)
{
	EffectManifest m;
	Error e = parseManifestJson(kSample, m);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(m.identity.ofxId, "com.example.glow");
	EXPECT_EQ(m.version, (Version{2, 3, 1}));
	EXPECT_EQ(m.deprecatedMajors, std::vector<unsigned>{1});
	EXPECT_TRUE(m.capabilities.metalRender);
	EXPECT_TRUE(m.capabilities.noSpatialAwareness);
	ASSERT_EQ(m.capabilities.depths.size(), 2u);
	EXPECT_EQ(m.capabilities.depths[1], BitDepth::Float);
	ASSERT_EQ(m.clips.size(), 1u);
	EXPECT_TRUE(m.clips[0].isMask);
	ASSERT_EQ(m.parameters.size(), 5u);
	const ParameterSpec& radius = m.parameters[0];
	EXPECT_EQ(radius.type, ParameterType::Float);
	EXPECT_DOUBLE_EQ(radius.defaultValue.toDouble(), 10.0);
	EXPECT_EQ(radius.maximum[0], 1000.0);
	EXPECT_EQ(radius.displayMaximum[0], 200.0);
	EXPECT_TRUE(radius.hostFlags.has(HostFlag::Hidden));
	EXPECT_TRUE(radius.appFlags.has(AppFlag::NoState));
	EXPECT_EQ(radius.meta.at("unit").as<std::string>(), "px");
	EXPECT_TRUE(radius.hosts.at("resolve").exclude);
	EXPECT_EQ(m.parameters[1].options[1].key, "Hard");
	EXPECT_EQ(m.parameters[2].defaultValue.as<Point2D>().x, 0.5);
	EXPECT_EQ(m.parameters[3].defaultValue.as<RGBA>().b, 0.25);
	ASSERT_EQ(m.parameters[4].children.size(), 1u);
	EXPECT_TRUE(m.parameters[4].children[0].defaultValue.as<bool>());
	ASSERT_EQ(m.migrations.size(), 1u);
	EXPECT_EQ(m.migrations[0].renames.at("rad"), "radius");
	EXPECT_TRUE(m.validate().ok()) << m.validate().describe();
}

TEST(ManifestJson, RoundTripsThroughWriter)
{
	EffectManifest m;
	ASSERT_TRUE(parseManifestJson(kSample, m).ok());
	const std::string text = writeManifestJson(m);
	EffectManifest again;
	Error e = parseManifestJson(text, again);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(again.identity, m.identity);
	EXPECT_EQ(again.version, m.version);
	EXPECT_EQ(again.capabilities, m.capabilities);
	EXPECT_EQ(again.clips, m.clips);
	ASSERT_EQ(again.parameters.size(), m.parameters.size());
	EXPECT_EQ(again.parameters[0].key, "radius");
	EXPECT_EQ(again.parameters[0].hostFlags, m.parameters[0].hostFlags);
	EXPECT_EQ(again.parameters[3].defaultValue, m.parameters[3].defaultValue);
	EXPECT_EQ(again.parameters[4].children[0].key, "inner");
	EXPECT_EQ(again.migrations, m.migrations);
}

TEST(ManifestJson, ReportsBadInputWithAPath)
{
	EffectManifest m;
	EXPECT_EQ(parseManifestJson("not json", m).code(), ErrorCode::InvalidManifest);
	EXPECT_FALSE(parseManifestJson("[]", m).ok());
	Error e = parseManifestJson(R"({"parameters": [ { "id": 1, "key": "a", "type": "nonsense" } ]})", m);
	EXPECT_FALSE(e.ok());
	EXPECT_NE(e.describe().find("$.parameters[0].type"), std::string::npos);
	e = parseManifestJson(R"({"parameters": [ { "id": 1, "key": "a", "type": "float", "flags": ["bogus"] } ]})", m);
	EXPECT_NE(e.describe().find("flags"), std::string::npos);
	e = parseManifestJson(R"({"effect": {"kind": "weird"}})", m);
	EXPECT_NE(e.describe().find("kind"), std::string::npos);
}

TEST(ManifestJson, AcceptsFxGripStyleKeys)
{
	EffectManifest m;
	Error e = parseManifestJson(R"({"parameters": [ { "id": 5, "key": "k", "type": "integer", "name": "Name", "description": "Desc", "parentid": 0, "slidermin": 1, "slidermax": 9, "items": ["A", "B"] } ]})", m);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(m.parameters[0].label, "Name");
	EXPECT_EQ(m.parameters[0].hint, "Desc");
	EXPECT_EQ(m.parameters[0].displayMaximum[0], 9.0);
	EXPECT_EQ(m.parameters[0].options.size(), 2u);
}

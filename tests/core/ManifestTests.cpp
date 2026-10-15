// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Manifest.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

EffectManifest validManifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.example.test";
	m.identity.name = "Test";
	m.identity.grouping = "Tests";
	m.version = Version{1, 0, 0};
	m.parameters.push_back(ParameterSpec::floatParam(100, "gain", "Gain", 1.0, 0.0, 10.0));
	m.parameters.push_back(ParameterSpec::menuParam(101, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}));
	m.parameters.push_back(ParameterSpec::groupParam(200, "group", "Group", {ParameterSpec::boolParam(201, "flag", "Flag", true)}));
	return m;
}

}  // namespace

TEST(Manifest, ValidManifestPasses)
{
	EXPECT_TRUE(validManifest().validate().ok());
}

TEST(Manifest, RejectsMissingIdentity)
{
	EffectManifest m = validManifest();
	m.identity.ofxId.clear();
	EXPECT_EQ(m.validate().code(), ErrorCode::InvalidManifest);
	m = validManifest();
	m.identity.ofxId = "has space";
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.identity.name.clear();
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.version = Version{};
	EXPECT_FALSE(m.validate().ok());
}

TEST(Manifest, RejectsDuplicateIdsAndKeys)
{
	EffectManifest m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(100, "other", "Other", 0, 0, 1));
	Error e = m.validate();
	EXPECT_FALSE(e.ok());
	EXPECT_NE(e.describe().find("identifier"), std::string::npos);
	m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(300, "gain", "Dup", 0, 0, 1));
	EXPECT_NE(m.validate().describe().find("key"), std::string::npos);
}

TEST(Manifest, RejectsReservedIdsAndBadKeys)
{
	EffectManifest m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(9995, "meta", "Meta", 0, 0, 1));
	EXPECT_NE(m.validate().describe().find("reserved"), std::string::npos);
	m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(300, "bad key", "Bad", 0, 0, 1));
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(kParameterIdNone, "none", "None", 0, 0, 1));
	EXPECT_FALSE(m.validate().ok());
}

TEST(Manifest, RejectsDuplicateOptionKeysAndInvertedRanges)
{
	EffectManifest m = validManifest();
	m.parameters.push_back(ParameterSpec::menuParam(300, "dup", "Dup", {{"a", "A"}, {"a", "B"}}));
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(300, "inverted", "Inverted", 0, 5, 1));
	EXPECT_FALSE(m.validate().ok());
}

TEST(Manifest, RejectsUnknownParentAndDeprecatedMajorTooHigh)
{
	EffectManifest m = validManifest();
	m.parameters.push_back(ParameterSpec::floatParam(300, "orphan", "Orphan", 0, 0, 1).withParent(999));
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.deprecatedMajors = {1};
	EXPECT_FALSE(m.validate().ok());
	m.version = Version{2, 0, 0};
	EXPECT_TRUE(m.validate().ok());
}

TEST(Manifest, FlattensGroupsWithParents)
{
	const std::vector<ParameterSpec> flat = validManifest().flattenedParameters();
	ASSERT_EQ(flat.size(), 4u);
	EXPECT_EQ(flat[2].id, 200u);
	EXPECT_EQ(flat[3].id, 201u);
	EXPECT_EQ(flat[3].parent, 200u);
	EXPECT_TRUE(flat[2].children.empty());
}

TEST(Manifest, FindsByIdAndKeyThroughGroups)
{
	EffectManifest m = validManifest();
	ASSERT_NE(m.find(201), nullptr);
	EXPECT_EQ(m.find(201)->key, "flag");
	ASSERT_NE(m.find("gain"), nullptr);
	EXPECT_EQ(m.find("gain")->id, 100u);
	EXPECT_EQ(m.find(555), nullptr);
	EXPECT_EQ(m.find("missing"), nullptr);
}

TEST(Manifest, OptionKeysAndIndices)
{
	const EffectManifest manifest = validManifest();
	const ParameterSpec* mode = manifest.find("mode");
	ASSERT_NE(mode, nullptr);
	EXPECT_EQ(mode->optionIndex("hard"), 1);
	EXPECT_EQ(mode->optionIndex("none"), -1);
	EXPECT_EQ(mode->optionKey(0), "soft");
	EXPECT_TRUE(mode->optionKey(9).empty());
}

TEST(Manifest, StateAndDimensions)
{
	EXPECT_TRUE(ParameterSpec::floatParam(1, "a", "A", 0, 0, 1).hasState());
	EXPECT_FALSE(ParameterSpec::buttonParam(2, "b", "B").hasState());
	EXPECT_FALSE(ParameterSpec::groupParam(3, "g", "G").hasState());
	ParameterSpec noState = ParameterSpec::floatParam(4, "n", "N", 0, 0, 1).withFlags({}, AppFlags(AppFlag::NoState));
	EXPECT_FALSE(noState.hasState());
	EXPECT_EQ(ParameterSpec::pointParam(5, "p", "P", {}).dimensions(), 2);
	EXPECT_EQ(ParameterSpec::rgbParam(6, "c", "C", {}).dimensions(), 3);
	EXPECT_EQ(ParameterSpec::rgbaParam(7, "d", "D", {}).dimensions(), 4);
	EXPECT_EQ(ParameterSpec::stringParam(8, "s", "S", "").dimensions(), 0);
}

TEST(Manifest, EffectiveClipsPerKind)
{
	EffectManifest m = validManifest();
	ASSERT_EQ(m.effectiveClips().size(), 1u);
	EXPECT_EQ(m.effectiveClips()[0].name, "Source");
	m.identity.kind = EffectKind::Transition;
	EXPECT_EQ(m.effectiveClips().size(), 2u);
	m.identity.kind = EffectKind::Generator;
	EXPECT_TRUE(m.effectiveClips().empty());
	m.clips.push_back(ClipSpec{"Mask", "Mask", true, true, false, {Components::Alpha}});
	EXPECT_EQ(m.effectiveClips().size(), 1u);
	EXPECT_NE(m.findClip("Mask"), nullptr);
}

TEST(Manifest, ClipValidation)
{
	EffectManifest m = validManifest();
	m.clips.push_back(ClipSpec{"Output", "Output", false, false, false, {}});
	EXPECT_FALSE(m.validate().ok());
	m = validManifest();
	m.clips.push_back(ClipSpec{"A", "A", false, false, false, {}});
	m.clips.push_back(ClipSpec{"A", "A", false, false, false, {}});
	EXPECT_FALSE(m.validate().ok());
}

TEST(Manifest, TypeNamesRoundTripWithFxGripStrings)
{
	for (ParameterType t : {ParameterType::Float, ParameterType::Int, ParameterType::Toggle, ParameterType::Menu, ParameterType::PushButton, ParameterType::Group, ParameterType::Section,
	                        ParameterType::Status, ParameterType::ObjectTracker, ParameterType::Point3D, ParameterType::Blob}) {
		ParameterType parsed{};
		ASSERT_TRUE(parseParameterType(toString(t), parsed)) << toString(t);
		EXPECT_EQ(parsed, t);
	}
	ParameterType parsed{};
	EXPECT_TRUE(parseParameterType("integer", parsed));
	EXPECT_EQ(parsed, ParameterType::Int);
	EXPECT_TRUE(parseParameterType("int", parsed));
	EXPECT_TRUE(parseParameterType("choice", parsed));
	EXPECT_EQ(parsed, ParameterType::Menu);
	EXPECT_TRUE(parseParameterType("bool", parsed));
	EXPECT_EQ(parsed, ParameterType::Toggle);
	EXPECT_FALSE(parseParameterType("nope", parsed));
	EXPECT_EQ(static_cast<int>(ParameterType::Float), 5);
	EXPECT_EQ(static_cast<int>(ParameterType::Section), 120);
}

TEST(Manifest, ChainingSetters)
{
	ParameterSpec spec = ParameterSpec::floatParam(1, "a", "A", 0, 0, 1).withHint("h").withDisplayRange(0, 0.5).withTags({"t"}).withMeta("unit", Value("px")).withVisibleWhen("x").withEnabledWhen("y").withHost("resolve", HostOverride{true, {}});
	EXPECT_EQ(spec.hint, "h");
	EXPECT_EQ(spec.displayMaximum[0], 0.5);
	EXPECT_EQ(spec.tags[0], "t");
	EXPECT_EQ(spec.meta.at("unit").as<std::string>(), "px");
	EXPECT_EQ(spec.visibleWhen, "x");
	EXPECT_EQ(spec.enabledWhen, "y");
	EXPECT_TRUE(spec.hosts.at("resolve").exclude);
}

TEST(Manifest, ValidatesRulesAndTargetPresets)
{
	auto base = []() {
		EffectManifest m;
		m.identity.ofxId = "com.example.rules";
		m.identity.name = "Rules";
		m.parameters.push_back(ParameterSpec::menuParam(1, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}));
		m.parameters.push_back(ParameterSpec::floatParam(2, "amount", "Amount", 1, 0, 1));
		m.parameters.push_back(ParameterSpec::boolParam(3, "on", "On", true));
		return m;
	};
	EffectManifest ok = base();
	ok.parameters[1].withVisibleWhen("mode == 'soft' && on").withEnabledWhen("amount > 0");
	TargetPreset preset;
	preset.values["amount"] = Value(0.5);
	preset.flags["amount"] = {"+hidden"};
	ok.parameters[0].withTargetPreset("soft", preset);
	TargetPreset toggle;
	toggle.labels["amount"] = "A";
	ok.parameters[2].withTargetPreset("true", toggle).withResetValue(Value(false));
	EXPECT_TRUE(ok.validate().ok()) << ok.validate().describe();
	EXPECT_EQ(ok.parameters[2].resetValue, Value(false));

	auto expectInvalid = [](EffectManifest m, const char* why) {
		Error e = m.validate();
		EXPECT_EQ(e.code(), ErrorCode::InvalidManifest) << why;
		EXPECT_NE(e.message().find(why), std::string::npos) << e.describe();
	};
	EffectManifest m = base();
	m.parameters[1].visibleWhen = "mode ==";
	expectInvalid(m, "visibleWhen rule does not parse");
	m = base();
	m.parameters[1].visibleWhen = "ghost == 1";
	expectInvalid(m, "visibleWhen rule names an undeclared");
	m = base();
	m.parameters[1].enabledWhen = "(";
	expectInvalid(m, "enabledWhen rule does not parse");
	m = base();
	m.parameters[1].enabledWhen = "ghost";
	expectInvalid(m, "enabledWhen rule names an undeclared");
	m = base();
	m.parameters[1].withTargetPreset("x", preset);
	expectInvalid(m, "Only a choice or a toggle");
	m = base();
	m.parameters[0].withTargetPreset("nonesuch", preset);
	expectInvalid(m, "unknown option");
	m = base();
	m.parameters[2].withTargetPreset("yes", preset);
	expectInvalid(m, "keyed by");
	TargetPreset badKey;
	badKey.values["ghost"] = Value(1);
	m = base();
	m.parameters[0].withTargetPreset("soft", badKey);
	expectInvalid(m, "undeclared parameter ghost");
	for (int section = 0; section < 4; ++section) {
		TargetPreset bad;
		switch (section) {
		case 0: bad.flags["ghost"] = {"+hidden"}; break;
		case 1: bad.tags["ghost"] = {"t"}; break;
		case 2: bad.meta["ghost"] = {{"k", Value(1)}}; break;
		default: bad.labels["ghost"] = "L"; break;
		}
		m = base();
		m.parameters[0].withTargetPreset("soft", bad);
		expectInvalid(m, "undeclared parameter ghost");
	}
	TargetPreset badFlag;
	badFlag.flags["amount"] = {"+sparkly"};
	m = base();
	m.parameters[0].withTargetPreset("soft", badFlag);
	expectInvalid(m, "unknown flag");
}

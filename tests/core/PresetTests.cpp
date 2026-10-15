// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Preset.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace ofxgrip;

namespace {

Preset samplePreset()
{
	Preset p;
	p.createdBy = 117;
	p.values[104] = Value(12);
	p.values[107] = Value(RGBA{0.97, 1.0, 0.2, 1.0});
	p.values[109] = Value(220.0);
	p.values[111] = Value(false);
	p.values[112] = Value(Point2D{0.5, 0.5});
	p.values[118] = Value("Curve/InverseQuadratic.png");
	p.values[119] = Value(Bytes{9, 8});
	p.tags[104] = {"shape", "size"};
	p.meta[107] = {{"unit", Value("px")}, {"gamma", Value(2.2)}};
	p.uuid = "0F2B3A4C-5D6E-4A6E-9C5D-7B1F4E8A0001";
	p.name = "Warm";
	p.tag = "look";
	p.createdTime = "2026-10-14T12:00:00Z";
	p.pluginAuthor = "Belisoful";
	p.pluginLocalizedName = "Circle";
	p.pluginUuid = "77C0D916-29A6-4D97-A09A-21DE2858C8E5";
	p.pluginVersion = "1.0";
	p.productId = "ofxgrip";
	return p;
}

// The shape FxFactory writes: the seven FxFactory keys only, with a per-language name.
const char* kFxFactoryStyle = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>FxFactoryPresetCreatedByParameterID</key>
	<integer>117</integer>
	<key>FxFactoryPresetParameterValues</key>
	<dict>
		<key>104</key>
		<integer>12</integer>
		<key>107</key>
		<dict>
			<key>alpha</key>
			<real>1</real>
			<key>blue</key>
			<real>0.2</real>
			<key>colorspace</key>
			<integer>1</integer>
			<key>green</key>
			<real>1</real>
			<key>red</key>
			<real>0.97</real>
		</dict>
		<key>112</key>
		<dict>
			<key>x</key>
			<real>0.5</real>
			<key>y</key>
			<real>0.5</real>
		</dict>
		<key>118</key>
		<string>Curve/InverseQuadratic.png</string>
		<key>bad</key>
		<integer>1</integer>
		<key>120</key>
		<array/>
	</dict>
	<key>FxFactoryPresetPlugInAuthor</key>
	<string>Noise Industries, LLC</string>
	<key>FxFactoryPresetPlugInLocalizedName</key>
	<dict>
		<key>English</key>
		<string>Circle</string>
	</dict>
	<key>FxFactoryPresetPlugInUUID</key>
	<string>77C0D916-29A6-4D97-A09A-21DE2858C8E5</string>
	<key>FxFactoryPresetPlugInVersion</key>
	<integer>2</integer>
	<key>FxFactoryPresetProductID</key>
	<string>fxfactorypro</string>
</dict>
</plist>)";

}  // namespace

TEST(Preset, RoundTripsThroughTheFileForm)
{
	const Preset p = samplePreset();
	const std::string xml = p.serialize();
	for (const char* key : {"FxFactoryPresetCreatedByParameterID", "FxFactoryPresetParameterValues", "FxFactoryPresetPlugInAuthor", "FxFactoryPresetPlugInLocalizedName", "FxFactoryPresetPlugInUUID", "FxFactoryPresetPlugInVersion",
	                        "FxFactoryPresetProductID", "FxGripPresetFramework", "FxGripPresetUUID", "FxGripPresetDisplayName", "FxGripPresetTag", "FxGripPresetCreatedTime", "FxGripPresetParameterMeta", "FxGripPresetParameterTags"}) {
		EXPECT_NE(xml.find(key), std::string::npos) << key;
	}
	Preset back;
	Error e = Preset::parse(xml, back);
	ASSERT_TRUE(e.ok()) << e.describe();
	// The toggle is written as an integer, as FxFactory does; everything else is identical.
	EXPECT_EQ(back.values.at(111), Value(0));
	back.values[111] = Value(false);
	EXPECT_EQ(back, p);
	EXPECT_EQ(back.framework, "OfxGrip");
	EXPECT_EQ(back.tags.at(104), (std::vector<std::string>{"shape", "size"}));
	EXPECT_EQ(back.meta.at(107).at("unit"), Value("px"));
}

TEST(Preset, ReadsAnFxFactoryFile)
{
	Preset p;
	Error e = Preset::parse(kFxFactoryStyle, p);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(p.framework, "FxFactory");
	EXPECT_EQ(p.createdBy, 117u);
	EXPECT_EQ(p.values.size(), 4u);  // "bad" and the array are skipped
	EXPECT_EQ(p.values.at(104), Value(12));
	EXPECT_EQ(p.values.at(107), Value(RGBA{0.97, 1.0, 0.2, 1.0}));
	EXPECT_EQ(p.values.at(112), Value(Point2D{0.5, 0.5}));
	EXPECT_EQ(p.values.at(118), Value("Curve/InverseQuadratic.png"));
	EXPECT_EQ(p.pluginAuthor, "Noise Industries, LLC");
	EXPECT_EQ(p.pluginLocalizedName, "Circle");
	EXPECT_EQ(p.pluginLocalizedNames.at("English"), "Circle");
	EXPECT_EQ(p.pluginUuid, "77C0D916-29A6-4D97-A09A-21DE2858C8E5");
	EXPECT_EQ(p.pluginVersion, "2");
	EXPECT_EQ(p.productId, "fxfactorypro");
	EXPECT_TRUE(p.name.empty());
	EXPECT_TRUE(p.tags.empty());
	// Writing it back keeps the per-language name dictionary verbatim.
	const std::string xml = p.serialize();
	EXPECT_NE(xml.find("<key>English</key>"), std::string::npos);
	Preset again;
	ASSERT_TRUE(Preset::parse(xml, again).ok());
	EXPECT_EQ(again.pluginLocalizedNames, p.pluginLocalizedNames);

	// FxGrip's own sample from the sibling checkout, when present: the reference FxFactory file.
	const std::filesystem::path sibling = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "FxGrip" / "Local" / "FxFactory" / "FxFactory Circle Preset xml.fxpreset";
	if (std::filesystem::exists(sibling)) {
		Preset circle;
		ASSERT_TRUE(Preset::load(sibling, circle).ok());
		EXPECT_EQ(circle.pluginUuid, "77C0D916-29A6-4D97-A09A-21DE2858C8E5");
		EXPECT_EQ(circle.values.at(109), Value(220.0));
		EXPECT_EQ(circle.source, PresetSource::File);
		EXPECT_EQ(circle.name, "FxFactory Circle Preset xml");
	}
}

TEST(Preset, RejectsBadInputAndMissingFiles)
{
	Preset p;
	EXPECT_EQ(Preset::parse("<plist><array/></plist>", p).code(), ErrorCode::Codec);
	EXPECT_EQ(Preset::parse("not xml", p).code(), ErrorCode::Codec);
	EXPECT_EQ(Preset::load(std::filesystem::temp_directory_path() / "ofxgrip-missing-preset.fxpreset", p).code(), ErrorCode::NotFound);
	PlistValue notDict(1);
	EXPECT_EQ(Preset::fromPlist(notDict, p).code(), ErrorCode::Codec);
	// Keys that are not numbers, non-array tags, non-dict meta, and non-dict values are skipped.
	PlistValue root(PlistDict{{"FxFactoryPresetParameterValues", PlistValue(1)}, {"FxGripPresetParameterTags", PlistValue(PlistDict{{"x", PlistValue(1)}, {"5", PlistValue(1)}, {"6", PlistValue(PlistArray{PlistValue("t"), PlistValue(2)})}})},
	                           {"FxGripPresetParameterMeta", PlistValue(PlistDict{{"y", PlistValue(1)}, {"7", PlistValue(PlistDict{{"k", PlistValue(PlistArray{})}, {"m", PlistValue(1)}})}})},
	                           {"FxGripPresetCreatedTime", PlistValue(PlistDate{"2026-01-01T00:00:00Z"})}, {"FxFactoryPresetPlugInVersion", PlistValue(PlistDict{})}});
	ASSERT_TRUE(Preset::fromPlist(root, p).ok());
	EXPECT_TRUE(p.values.empty());
	EXPECT_EQ(p.tags.size(), 1u);
	EXPECT_EQ(p.tags.at(6), std::vector<std::string>{"t"});
	EXPECT_EQ(p.meta.at(7).size(), 1u);
	EXPECT_EQ(p.createdTime, "2026-01-01T00:00:00Z");
	EXPECT_TRUE(p.pluginVersion.empty());
}

TEST(Preset, SavesAndLoadsFiles)
{
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ofxgrip-preset-tests" / "look";
	const Preset p = samplePreset();
	const std::filesystem::path file = dir / Preset::fileNameFor(p.name);
	Error e = p.save(file);
	ASSERT_TRUE(e.ok()) << e.describe();
	Preset back;
	ASSERT_TRUE(Preset::load(file, back).ok());
	EXPECT_EQ(back.name, "Warm");
	EXPECT_EQ(back.path, file);
	EXPECT_EQ(back.source, PresetSource::File);
	EXPECT_EQ(Preset::fileNameFor("A/B:C*D?\"<>| ."), "A_B_C_D_____.fxpreset");
	EXPECT_EQ(Preset::fileNameFor(""), "Preset.fxpreset");
	EXPECT_EQ(Preset::fileNameFor(" ."), "Preset.fxpreset");
	const std::string uuid = Preset::makeUuid();
	EXPECT_EQ(uuid.size(), 36u);
	EXPECT_EQ(uuid[8], '-');
	EXPECT_EQ(uuid[14], '4');
	EXPECT_NE(uuid, Preset::makeUuid());
	EXPECT_EQ(Preset::nowIso8601().size(), 20u);
	EXPECT_EQ(p.save(dir / "nonexistent-dir-marker" / "").code(), ErrorCode::Failed);
}

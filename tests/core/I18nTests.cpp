// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/I18n.hpp>

#include "FakeServices.hpp"

#include "../TestEnvironment.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

const char* kStrings = R"(/* A comment */
"Gain" = "Verstärkung";
// a line comment
"Per-channel gains" = "Kanalweise";
"Soft" = "Weich";
"Hard" = "Hart";
hello = "Hallo \"Welt\"\n";
"About" = "Info";
"Gain" = "Pegel";
)";

std::filesystem::path freshBundle()
{
	static int counter = 0;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ofxgrip-i18n-" + std::to_string(++counter)) / "Test.ofx.bundle";
	std::error_code ec;
	std::filesystem::remove_all(dir.parent_path(), ec);
	std::filesystem::create_directories(dir / "Contents" / "Resources" / "de.lproj", ec);
	std::ofstream(dir / "Contents" / "Resources" / "de.lproj" / "Localizable.strings") << kStrings;
	std::filesystem::create_directories(dir / "Contents" / "Resources" / "Base.lproj", ec);
	std::ofstream(dir / "Contents" / "Resources" / "Base.lproj" / "Localizable.strings") << "\"Gain\" = \"Gain (base)\";";
	return dir;
}

}  // namespace

TEST(StringTable, ParsesStringsFilesAndMapsBothWays)
{
	StringTable table;
	Error e = StringTable::parseStrings(kStrings, table);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(table.size(), 6u);
	EXPECT_EQ(table.localize("Gain"), "Pegel");  // the later entry wins
	EXPECT_EQ(table.delocalize("Pegel"), "Gain");
	EXPECT_EQ(table.delocalize("Verstärkung"), "Verstärkung");  // the replaced value no longer maps back
	EXPECT_EQ(table.localize("hello"), "Hallo \"Welt\"\n");
	EXPECT_EQ(table.localize("unknown"), "unknown");
	EXPECT_EQ(table.delocalize("unknown"), "unknown");
	EXPECT_TRUE(table.has("Soft"));
	EXPECT_FALSE(table.has("soft"));
	EXPECT_FALSE(table.empty());
	EXPECT_EQ(table.entries().at("About"), "Info");
	StringTable bom;
	ASSERT_TRUE(StringTable::parseStrings("\xEF\xBB\xBF\"a\" = \"b\";", bom).ok());
	EXPECT_EQ(bom.localize("a"), "b");
	StringTable empty;
	ASSERT_TRUE(StringTable::parseStrings("  /* nothing */ ", empty).ok());
	EXPECT_TRUE(empty.empty());
	for (const char* bad : {"\"a\" \"b\";", "\"a\" = b;", "\"a\" = \"b\"", "= \"b\";", "\"a\" = \"unterminated;", "\"unterminated = \"b\";"}) {
		StringTable out;
		EXPECT_EQ(StringTable::parseStrings(bad, out).code(), ErrorCode::Codec) << bad;
	}
	StringTable missing;
	EXPECT_EQ(StringTable::loadFile(std::filesystem::temp_directory_path() / "ofxgrip-no-such.strings", missing).code(), ErrorCode::NotFound);
}

TEST(StringTable, FindsTheBundleTableByLanguage)
{
	const std::filesystem::path bundle = freshBundle();
	std::optional<StringTable> de = findStringTable(bundle / "Contents" / "MacOS" / "Test.ofx", {"de", "en"});
	ASSERT_TRUE(de.has_value());
	EXPECT_EQ(de->localize("Gain"), "Pegel");
	std::optional<StringTable> base = findStringTable(bundle, {"fr"});
	ASSERT_TRUE(base.has_value());
	EXPECT_EQ(base->localize("Gain"), "Gain (base)");
	EXPECT_FALSE(findStringTable({}, {"de"}).has_value());
	EXPECT_FALSE(findStringTable(std::filesystem::temp_directory_path() / "ofxgrip-no-bundle", {"de"}).has_value());
	std::ofstream(bundle / "Contents" / "Resources" / "Base.lproj" / "Localizable.strings") << "broken";
	std::ofstream(bundle / "Contents" / "Resources" / "Localizable.strings") << "\"Gain\" = \"Gain (bare)\";";
	std::optional<StringTable> bare = findStringTable(bundle, {"fr"});
	ASSERT_TRUE(bare.has_value());
	EXPECT_EQ(bare->localize("Gain"), "Gain (bare)");

	const std::vector<std::string> languages = preferredLanguages();
	EXPECT_FALSE(languages.empty());
	EXPECT_EQ(languages.back(), "en");
	setTestEnv("OFXGRIP_LANGUAGE", "de_DE.UTF-8");
	setTestEnv("LC_ALL", "C");
	const std::vector<std::string> withOverride = preferredLanguages();
	EXPECT_EQ(withOverride.front(), "de");
	EXPECT_EQ(std::count(withOverride.begin(), withOverride.end(), "C"), 0);
	unsetTestEnv("OFXGRIP_LANGUAGE");
	unsetTestEnv("LC_ALL");
}

TEST(I18n, LocalizesTheManifestAndRuntimeTraffic)
{
	StringTable table;
	ASSERT_TRUE(StringTable::parseStrings(kStrings, table).ok());
	EffectManifest m;
	m.identity.ofxId = "com.test.i18n";
	m.identity.name = "Gain";
	m.identity.shortName = "Gain";
	m.identity.description = "hello";
	m.identity.grouping = "About";
	m.parameters.push_back(ParameterSpec::floatParam(1, "gain", "Gain", 1, 0, 2).withHint("hello").withHost("nuke", HostOverride{false, std::string("Gain")}));
	m.parameters.push_back(ParameterSpec::menuParam(2, "mode", "Per-channel gains", {{"soft", "Soft"}, {"hard", ""}}, 0));
	m.parameters.push_back(ParameterSpec::stringParam(3, "label", "About", "Soft"));
	m.parameters.push_back(ParameterSpec::groupParam(4, "group", "Gain", {ParameterSpec::boolParam(5, "inner", "Soft", true)}));
	m.about.name = "About";
	m.about.mainText = "hello";
	AboutEntry link;
	link.label = "Soft";
	link.url = "https://example.com";
	m.about.entries.push_back(link);
	AboutEntry dialog;
	dialog.kind = AboutEntry::Kind::Dialog;
	dialog.label = "Hard";
	dialog.dialogText = "Gain";
	m.about.entries.push_back(dialog);
	ManifestPreset preset;
	preset.name = "Soft";
	m.presets.push_back(preset);
	localizeManifest(m, table);
	EXPECT_EQ(m.identity.name, "Pegel");
	EXPECT_EQ(m.identity.description, "Hallo \"Welt\"\n");
	EXPECT_EQ(m.identity.grouping, "Info");
	EXPECT_EQ(m.parameters[0].label, "Pegel");
	EXPECT_EQ(m.parameters[0].hint, "Hallo \"Welt\"\n");
	EXPECT_EQ(*m.parameters[0].hosts.at("nuke").label, "Pegel");
	EXPECT_EQ(m.parameters[1].label, "Kanalweise");
	EXPECT_EQ(m.parameters[1].options[0].label, "Weich");
	EXPECT_EQ(m.parameters[1].options[1].label, "hard");
	EXPECT_EQ(m.parameters[2].defaultValue, Value("Weich"));
	EXPECT_EQ(m.parameters[3].children[0].label, "Weich");
	EXPECT_EQ(m.about.name, "Info");
	EXPECT_EQ(m.about.entries[0].label, "Weich");
	EXPECT_EQ(m.about.entries[1].dialogText, "Pegel");
	EXPECT_EQ(m.presets[0].name, "Weich");

	EffectManifest runtime;
	runtime.identity.ofxId = "com.test.i18n";
	runtime.identity.name = "X";
	runtime.parameters.push_back(ParameterSpec::floatParam(1, "gain", "Gain", 1, 0, 2));
	runtime.parameters.push_back(ParameterSpec::stringParam(3, "label", "Label", "Soft"));
	runtime.parameters.push_back(ParameterSpec::menuParam(2, "mode", "Mode", {{"soft", "Soft"}}, 0));
	FakeEffectHost host(runtime);
	ExtensionSystem system(host);
	auto* ext = static_cast<I18nExtension*>(system.load(std::make_unique<I18nExtension>(table)));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->table().size(), 6u);
	EXPECT_TRUE(ext->switches().localizeNames);
	FakeParameterService& params = host.params();
	// Labels: localized on the way in, delocalized on the way out (the fake stores what it gets).
	ASSERT_TRUE(params.setLabel(1, "Gain").ok());
	EXPECT_EQ(params.labelOf(1), "Gain");  // the fake has no pre-event for labels; the extension sees get/set events only through the bus
	ApiEvent setLabel(EventId::ApiSetLabelPre, 1);
	setLabel.label = "Gain";
	host.bus().post(setLabel);
	EXPECT_EQ(setLabel.label, "Pegel");
	ApiEvent getLabel(EventId::ApiGetLabel, 1);
	getLabel.label = "Pegel";
	host.bus().post(getLabel);
	EXPECT_EQ(getLabel.label, "Gain");
	// String values round-trip through the real set/get path of the fake service.
	params.valueRef(3) = Value("Weich");
	Value read;
	ASSERT_TRUE(params.getCurrent(3, read).ok());
	EXPECT_EQ(read, Value("Weich"));  // no spec on the fake's get event: untouched
	ApiEvent getValue(EventId::ApiGetValue, 3);
	getValue.spec = params.spec(3);
	getValue.value = Value("Weich");
	host.bus().post(getValue);
	EXPECT_EQ(getValue.value, Value("Soft"));
	ApiEvent setValue(EventId::ApiSetValuePre, 3);
	setValue.spec = params.spec(3);
	setValue.value = Value("Soft");
	host.bus().post(setValue);
	EXPECT_EQ(setValue.value, Value("Weich"));
	ApiEvent numeric(EventId::ApiSetValuePre, 1);
	numeric.spec = params.spec(1);
	numeric.value = Value(2.0);
	host.bus().post(numeric);
	EXPECT_EQ(numeric.value, Value(2.0));
	ApiEvent options(EventId::ApiSetOptionsPre, 2);
	options.options = {{"soft", "Soft"}, {"hard", ""}};
	host.bus().post(options);
	EXPECT_EQ(options.options[0].label, "Weich");
	EXPECT_EQ(options.options[1].label, "hard");

	// Every switch off: nothing changes.
	I18nExtension::Switches off;
	off.localizeNames = off.localizeValues = off.localizeMenus = off.delocalizeNames = off.delocalizeValues = off.delocalizeMenus = false;
	FakeEffectHost quiet(runtime);
	ExtensionSystem system2(quiet);
	system2.load(std::make_unique<I18nExtension>(table, off));
	ApiEvent untouched(EventId::ApiSetLabelPre, 1);
	untouched.label = "Gain";
	quiet.bus().post(untouched);
	EXPECT_EQ(untouched.label, "Gain");
	ApiEvent untouchedGet(EventId::ApiGetLabel, 1);
	untouchedGet.label = "Pegel";
	quiet.bus().post(untouchedGet);
	EXPECT_EQ(untouchedGet.label, "Pegel");
	ApiEvent untouchedValue(EventId::ApiGetValue, 3);
	untouchedValue.spec = quiet.params().spec(3);
	untouchedValue.value = Value("Weich");
	quiet.bus().post(untouchedValue);
	EXPECT_EQ(untouchedValue.value, Value("Weich"));
	ApiEvent untouchedSet(EventId::ApiSetValuePre, 3);
	untouchedSet.spec = quiet.params().spec(3);
	untouchedSet.value = Value("Soft");
	quiet.bus().post(untouchedSet);
	EXPECT_EQ(untouchedSet.value, Value("Soft"));
	ApiEvent untouchedOptions(EventId::ApiSetOptionsPre, 2);
	untouchedOptions.options = {{"soft", "Soft"}};
	quiet.bus().post(untouchedOptions);
	EXPECT_EQ(untouchedOptions.options[0].label, "Soft");
}

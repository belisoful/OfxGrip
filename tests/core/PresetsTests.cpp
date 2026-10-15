// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The preset service and the presets control over the fake services: capture, apply inside the
// tag boundary, listing from the manifest, the bundle, and the user folder, and the menu rows.
#include <ofxgrip/core/MetaExtension.hpp>
#include <ofxgrip/core/Presets.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kAmount = 1, kMode = 2, kColor = 3, kTint = 4, kCenter = 5, kOn = 6, kName = 7, kNoValue = 8, kNoTags = 9, kNoMeta = 10, kControl = 11, kGroup = 12, kOther = 13 };

std::filesystem::path freshDirectory(const char* name)
{
	static int counter = 0;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ofxgrip-presets-" + std::to_string(++counter)) / name;
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

EffectManifest manifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.test.presets";
	m.identity.uuid = "11111111-2222-4333-8444-555555555555";
	m.identity.name = "Presets";
	m.identity.author = "Belisoful";
	m.identity.productId = "ofxgrip";
	m.version = Version{1, 2, 3};
	m.priorUuids = {"00000000-0000-4000-8000-000000000000"};
	m.parameters.push_back(ParameterSpec::floatParam(kAmount, "amount", "Amount", 0.5, 0, 1).withTags({"look"}).withMeta("unit", Value("px")));
	m.parameters.push_back(ParameterSpec::menuParam(kMode, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}, 0).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::rgbParam(kColor, "color", "Color", {0.1, 0.2, 0.3}).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::rgbaParam(kTint, "tint", "Tint", {0.1, 0.2, 0.3, 0.4}).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::pointParam(kCenter, "center", "Center", {1, 2}).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::boolParam(kOn, "on", "On", false).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::stringParam(kName, "name", "Name", "x").withTags({"look"}));
	m.parameters.push_back(ParameterSpec::floatParam(kNoValue, "noValue", "No value", 0.5, 0, 1).withFlags({}, AppFlags(AppFlag::PresetNoValue)).withTags({"look"}));
	m.parameters.push_back(ParameterSpec::floatParam(kNoTags, "noTags", "No tags", 0.5, 0, 1).withFlags({}, AppFlags(AppFlag::PresetNoTags)).withTags({"look"}).withMeta("k", Value(1)));
	m.parameters.push_back(ParameterSpec::floatParam(kNoMeta, "noMeta", "No meta", 0.5, 0, 1).withFlags({}, AppFlags(AppFlag::PresetNoMeta)).withTags({"look"}).withMeta("k", Value(1)));
	ParameterSpec control = ParameterSpec::menuParam(kControl, "presets", "Presets", {{"default", "Default"}}, 0);
	control.type = ParameterType::Presets;
	control.tags = {"look"};
	m.parameters.push_back(control);
	m.parameters.push_back(ParameterSpec::groupParam(kGroup, "group", "Group"));
	m.parameters.push_back(ParameterSpec::floatParam(kOther, "other", "Other", 0.5, 0, 1));
	ManifestPreset warm;
	warm.name = "Warm";
	warm.tag = "look";
	warm.values["amount"] = Value(0.9);
	warm.values["mode"] = Value("hard");
	warm.values["ghost"] = Value(1);
	warm.tags["amount"] = {"look", "warm"};
	warm.tags["ghost"] = {"x"};
	warm.meta["amount"] = {{"unit", Value("cm")}};
	warm.meta["ghost"] = {{"k", Value(1)}};
	m.presets.push_back(warm);
	ManifestPreset cold;
	cold.name = "Cold";
	cold.tag = "look";
	cold.values["amount"] = Value(0.1);
	m.presets.push_back(cold);
	ManifestPreset elsewhere;
	elsewhere.name = "Elsewhere";
	elsewhere.tag = "other";
	m.presets.push_back(elsewhere);
	return m;
}

void writePreset(const std::filesystem::path& dir, const std::string& name, double amount, const std::string& pluginUuid = "11111111-2222-4333-8444-555555555555")
{
	Preset p;
	p.name = name;
	p.tag = "look";
	p.pluginUuid = pluginUuid;
	p.values[kAmount] = Value(amount);
	ASSERT_TRUE(p.save(dir / Preset::fileNameFor(name)).ok());
}

}  // namespace

TEST(Presets, ResolvesManifestPresetsAndCoercesValues)
{
	const EffectManifest m = manifest();
	const Preset warm = resolveManifestPreset(m, m.presets[0]);
	EXPECT_EQ(warm.name, "Warm");
	EXPECT_EQ(warm.source, PresetSource::Manifest);
	EXPECT_EQ(warm.pluginUuid, m.identity.uuid);
	EXPECT_EQ(warm.pluginVersion, "1.2.3");
	EXPECT_EQ(warm.pluginAuthor, "Belisoful");
	EXPECT_EQ(warm.values.size(), 2u);
	EXPECT_EQ(warm.values.at(kMode), Value("hard"));
	EXPECT_EQ(warm.tags.size(), 1u);
	EXPECT_EQ(warm.meta.size(), 1u);

	Value out;
	const ParameterSpec& mode = m.parameters[1];
	EXPECT_TRUE(coercePresetValue(mode, Value("hard"), out));
	EXPECT_EQ(out, Value(1));
	EXPECT_FALSE(coercePresetValue(mode, Value("nope"), out));
	EXPECT_TRUE(coercePresetValue(mode, Value(1.0), out));
	EXPECT_EQ(out, Value(1));
	EXPECT_FALSE(coercePresetValue(mode, Value(Point2D{}), out));
	const ParameterSpec& on = m.parameters[5];
	EXPECT_TRUE(coercePresetValue(on, Value(1), out));
	EXPECT_EQ(out, Value(true));
	EXPECT_TRUE(coercePresetValue(on, Value("true"), out));
	EXPECT_FALSE(coercePresetValue(on, Value(Bytes{1}), out));
	const ParameterSpec intSpec = ParameterSpec::intParam(99, "i", "I", 0, 0, 9);
	EXPECT_TRUE(coercePresetValue(intSpec, Value(2.7), out));
	EXPECT_EQ(out, Value(2));
	EXPECT_FALSE(coercePresetValue(intSpec, Value("2"), out));
	const ParameterSpec& amount = m.parameters[0];
	EXPECT_TRUE(coercePresetValue(amount, Value(true), out));
	EXPECT_EQ(out, Value(1.0));
	EXPECT_FALSE(coercePresetValue(amount, Value("x"), out));
	const ParameterSpec& center = m.parameters[4];
	EXPECT_TRUE(coercePresetValue(center, Value(Point3D{1, 2, 3}), out));
	EXPECT_EQ(out, Value(Point2D{1, 2}));
	EXPECT_TRUE(coercePresetValue(center, Value(Point2D{3, 4}), out));
	EXPECT_FALSE(coercePresetValue(center, Value(1), out));
	ParameterSpec p3;
	p3.type = ParameterType::Point3D;
	EXPECT_TRUE(coercePresetValue(p3, Value(Point2D{1, 2}), out));
	EXPECT_EQ(out, Value(Point3D{1, 2, 0}));
	EXPECT_TRUE(coercePresetValue(p3, Value(Point3D{1, 2, 3}), out));
	EXPECT_FALSE(coercePresetValue(p3, Value(1), out));
	const ParameterSpec& color = m.parameters[2];
	EXPECT_TRUE(coercePresetValue(color, Value(RGBA{1, 2, 3, 4}), out));
	EXPECT_EQ(out, Value(RGB{1, 2, 3}));
	EXPECT_TRUE(coercePresetValue(color, Value(RGB{1, 2, 3}), out));
	EXPECT_FALSE(coercePresetValue(color, Value(1), out));
	const ParameterSpec& tint = m.parameters[3];
	EXPECT_TRUE(coercePresetValue(tint, Value(RGB{1, 2, 3}), out));
	EXPECT_EQ(out, Value(RGBA{1, 2, 3, 1}));
	EXPECT_TRUE(coercePresetValue(tint, Value(RGBA{1, 2, 3, 0.5}), out));
	EXPECT_FALSE(coercePresetValue(tint, Value(1), out));
	const ParameterSpec& name = m.parameters[6];
	EXPECT_TRUE(coercePresetValue(name, Value(2), out));
	EXPECT_EQ(out, Value("2"));
	EXPECT_TRUE(coercePresetValue(name, Value("s"), out));
	EXPECT_FALSE(coercePresetValue(name, Value(Bytes{1}), out));
	EXPECT_FALSE(coercePresetValue(name, Value(), out));
	ParameterSpec custom;
	custom.type = ParameterType::Custom;
	EXPECT_TRUE(coercePresetValue(custom, Value(Bytes{1}), out));
	EXPECT_TRUE(coercePresetValue(custom, Value("s"), out));
	EXPECT_FALSE(coercePresetValue(custom, Value(1), out));
	EXPECT_FALSE(coercePresetValue(m.parameters[11], Value(1), out));  // a group
	ParameterSpec button = ParameterSpec::buttonParam(98, "b", "B");
	EXPECT_FALSE(coercePresetValue(button, Value(1), out));
}

TEST(Presets, BundleDirectoriesAndFileListings)
{
	EXPECT_TRUE(bundleDirectoryFor({}).empty());
	EXPECT_EQ(bundleDirectoryFor("/Library/OFX/Plugins/X.ofx.bundle/Contents/MacOS/X.ofx"), std::filesystem::path("/Library/OFX/Plugins/X.ofx.bundle"));
	EXPECT_EQ(bundleDirectoryFor("/Library/OFX/Plugins/X.ofx.bundle"), std::filesystem::path("/Library/OFX/Plugins/X.ofx.bundle"));
	const std::filesystem::path dir = freshDirectory("bundle");
	EXPECT_EQ(bundleDirectoryFor(dir), dir);
	EXPECT_EQ(bundleDirectoryFor(dir / "plugin.ofx"), dir);

	const std::filesystem::path presets = dir / "Contents" / "Resources" / "Presets" / "look";
	std::filesystem::create_directories(presets);
	writePreset(presets, "Zed", 0.3);
	writePreset(presets, "Alpha", 0.2);
	std::ofstream(presets / "notes.txt") << "ignored";
	std::ofstream(presets / "broken.fxpreset") << "not a plist";
	std::filesystem::create_directories(presets / "folder.fxpreset");
	const std::vector<Preset> listed = presetsInDirectory(presets, "look", PresetSource::Bundle);
	ASSERT_EQ(listed.size(), 2u);
	EXPECT_EQ(listed[0].name, "Alpha");
	EXPECT_EQ(listed[1].name, "Zed");
	EXPECT_EQ(listed[0].source, PresetSource::Bundle);
	EXPECT_TRUE(presetsInDirectory(dir / "missing", "look", PresetSource::User).empty());
	EXPECT_TRUE(presetsInDirectory({}, "look", PresetSource::User).empty());

	const std::filesystem::path support = freshDirectory("support");
	std::filesystem::create_directories(support / "Presets" / "look");
	writePreset(support / "Presets" / "look", "Mine", 0.7);
	const EffectManifest m = manifest();
	const std::vector<PresetMenuEntry> entries = presetMenuEntriesForTag(m, dir, support, "look");
	ASSERT_EQ(entries.size(), 11u);
	EXPECT_EQ(entries[0].kind, PresetMenuEntry::Kind::Default);
	EXPECT_EQ(entries[1].kind, PresetMenuEntry::Kind::Separator);
	EXPECT_EQ(entries[2].name, "Mine");
	EXPECT_EQ(entries[2].kind, PresetMenuEntry::Kind::User);
	EXPECT_EQ(entries[3].kind, PresetMenuEntry::Kind::Separator);
	EXPECT_EQ(entries[4].name, "Warm");
	EXPECT_EQ(entries[5].name, "Cold");
	EXPECT_EQ(entries[6].name, "Alpha");
	EXPECT_EQ(entries[7].name, "Zed");
	EXPECT_EQ(entries[8].kind, PresetMenuEntry::Kind::Separator);
	EXPECT_EQ(entries[9].kind, PresetMenuEntry::Kind::Reveal);
	EXPECT_EQ(entries[10].kind, PresetMenuEntry::Kind::Save);
	const std::vector<ChoiceOption> options = presetMenuOptions(entries);
	ASSERT_EQ(options.size(), 11u);
	EXPECT_EQ(options[0].key, "default");
	EXPECT_EQ(options[1].key, "separator1");
	EXPECT_EQ(options[2].key, "user:Mine");
	EXPECT_EQ(options[4].key, "plugin:Warm");
	EXPECT_EQ(options[8].key, "separator3");
	EXPECT_EQ(options[9].key, "reveal");
	EXPECT_EQ(options[10].key, "save");
	// No tag, no bundle, no user folder: only the fixed rows.
	const std::vector<PresetMenuEntry> bare = presetMenuEntriesForTag(m, {}, {}, "");
	ASSERT_EQ(bare.size(), 4u);
	EXPECT_EQ(bare[1].kind, PresetMenuEntry::Kind::Separator);
	EXPECT_EQ(bare[2].kind, PresetMenuEntry::Kind::Reveal);
}

TEST(Presets, CapturesAndAppliesInsideTheTagBoundary)
{
	FakeEffectHost host(manifest());
	MetaManager meta;
	for (const ParameterSpec& spec : host.manifest().flattenedParameters()) {
		meta.seed(spec);
	}
	PresetManager presets(host, &meta);
	FakeParameterService& params = host.params();
	params.valueRef(kAmount) = Value(0.75);
	params.valueRef(kMode) = Value(1);
	meta.setMeta(kControl, MetaManager::kSelectedPresetKey, Value("x"));
	meta.setMeta(kAmount, MetaManager::kSelectedPresetKey, Value("hidden"));

	Preset captured = presets.capture("Mine", "look");
	EXPECT_EQ(captured.name, "Mine");
	EXPECT_EQ(captured.tag, "look");
	EXPECT_EQ(captured.pluginUuid, host.manifest().identity.uuid);
	EXPECT_EQ(captured.pluginVersion, "1.2.3");
	EXPECT_EQ(captured.productId, "ofxgrip");
	EXPECT_FALSE(captured.uuid.empty());
	EXPECT_FALSE(captured.createdTime.empty());
	EXPECT_EQ(captured.values.count(kControl), 0u);
	EXPECT_EQ(captured.values.count(kGroup), 0u);
	EXPECT_EQ(captured.values.count(kNoValue), 0u);
	EXPECT_EQ(captured.values.at(kAmount), Value(0.75));
	EXPECT_EQ(captured.values.at(kOther), Value(0.5));
	EXPECT_EQ(captured.tags.at(kAmount), std::vector<std::string>{"look"});
	EXPECT_EQ(captured.tags.count(kNoTags), 0u);
	EXPECT_EQ(captured.tags.count(kOther), 0u);
	EXPECT_EQ(captured.meta.at(kAmount).size(), 1u);  // the selection key is never captured
	EXPECT_EQ(captured.meta.count(kNoMeta), 0u);
	EXPECT_EQ(captured.meta.at(kNoTags).at("k"), Value(1));

	// Compatibility: the effect's UUID, a prior UUID, or none.
	Preset foreign = captured;
	foreign.pluginUuid = "99999999-0000-4000-8000-000000000000";
	EXPECT_FALSE(presets.compatible(foreign));
	EXPECT_EQ(presets.apply(foreign).code(), ErrorCode::Unsupported);
	foreign.pluginUuid = "00000000-0000-4000-8000-000000000000";
	EXPECT_TRUE(presets.compatible(foreign));
	foreign.pluginUuid.clear();
	EXPECT_TRUE(presets.compatible(foreign));

	// Apply: values coerce per type; a parameter outside the tag is skipped; ghosts are skipped.
	Preset p;
	p.tag = "look";
	p.createdBy = kAmount;  // the creating parameter never receives its own value
	p.values[kAmount] = Value(0.2);
	p.values[kMode] = Value("soft");
	p.values[kOther] = Value(0.9);
	p.values[kOn] = Value(1);
	p.values[kName] = Value(42);
	p.values[kCenter] = Value("bad");
	p.values[kControl] = Value(1);
	p.values[777] = Value(1);
	p.tags[kMode] = {"look", "soft"};
	p.tags[kNoTags] = {"zzz"};
	p.tags[kOther] = {"out"};
	p.meta[kMode] = {{"applied", Value(true)}};
	p.meta[kNoMeta] = {{"zzz", Value(1)}};
	p.meta[kOther] = {{"out", Value(1)}};
	params.writes.clear();
	Error e = presets.apply(p);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(params.valueRef(kAmount), Value(0.75));
	EXPECT_EQ(params.valueRef(kMode), Value(0));
	EXPECT_EQ(params.valueRef(kOther), Value(0.5));
	EXPECT_EQ(params.valueRef(kOn), Value(true));
	EXPECT_EQ(params.valueRef(kName), Value("42"));
	EXPECT_EQ(params.valueRef(kCenter), Value(Point2D{1, 2}));
	EXPECT_EQ(meta.tags(kMode), (std::vector<std::string>{"look", "soft"}));
	EXPECT_FALSE(meta.hasTag(kNoTags, "zzz"));
	EXPECT_FALSE(meta.hasTag(kOther, "out"));
	EXPECT_EQ(meta.meta(kMode, "applied"), Value(true));
	EXPECT_FALSE(meta.hasMeta(kNoMeta, "zzz"));
	EXPECT_FALSE(meta.hasMeta(kOther, "out"));

	// Options: the tag boundary, the sections, and a remap.
	PresetApplyOptions options;
	options.ignoreTagBoundary = true;
	options.ignoreTags = true;
	options.ignoreMeta = true;
	options.remap = [](const ParameterSpec& spec, const Value& v) -> std::optional<Value> {
		if (spec.id == kOther) {
			return Value(v.toDouble() / 3.0);
		}
		return std::nullopt;
	};
	ASSERT_TRUE(presets.apply(p, options, Time{4}).ok());
	EXPECT_DOUBLE_EQ(params.valueRef(kOther).toDouble(), 0.3);
	EXPECT_FALSE(meta.hasTag(kOther, "out"));
	EXPECT_FALSE(meta.hasMeta(kOther, "out"));
	// An untagged preset reaches every parameter.
	Preset untagged;
	untagged.values[kOther] = Value(0.1);
	ASSERT_TRUE(presets.apply(untagged).ok());
	EXPECT_EQ(params.valueRef(kOther), Value(0.1));
	// A write failure is reported after the rest applied.
	params.writable = false;
	EXPECT_EQ(presets.apply(untagged).code(), ErrorCode::WrongPhase);
	params.writable = true;
	// Without a meta manager the tags and meta sections are skipped and tags come from the spec.
	PresetManager plain(host, nullptr);
	ASSERT_TRUE(plain.apply(p).ok());
	Preset plainCapture = plain.capture("P", "look");
	EXPECT_EQ(plainCapture.tags.at(kAmount), std::vector<std::string>{"look"});
	EXPECT_EQ(plainCapture.meta.at(kAmount).at("unit"), Value("px"));
}

TEST(Presets, ListsManifestBundleAndUserPresetsAndSavesUserFiles)
{
	const std::filesystem::path bundle = freshDirectory("Plugin.ofx.bundle");
	const std::filesystem::path support = freshDirectory("list-support");
	std::filesystem::create_directories(bundle / "Contents" / "Resources" / "Presets" / "look");
	writePreset(bundle / "Contents" / "Resources" / "Presets" / "look", "Bundled", 0.3);
	writePreset(bundle / "Contents" / "Resources" / "Presets" / "look", "Warm", 0.33);  // shadowed by neither: same name as the manifest's
	FakeEffectHost host(manifest());
	host.fakes().ident.bundlePath = bundle / "Contents" / "MacOS" / "plugin.ofx";
	host.fakes().pathService.support = support;
	PresetManager presets(host, nullptr);
	EXPECT_EQ(presets.bundlePresetDirectory("look"), bundle / "Contents" / "Resources" / "Presets" / "look");
	EXPECT_TRUE(presets.bundlePresetDirectory("").empty());
	EXPECT_EQ(presets.userPresetDirectory("look"), support / "Presets" / "look");
	EXPECT_TRUE(presets.userPresetDirectory("").empty());
	ASSERT_EQ(presets.manifestPresets("look").size(), 2u);
	ASSERT_EQ(presets.bundlePresets("look").size(), 2u);
	ASSERT_EQ(presets.pluginPresets("look").size(), 4u);
	EXPECT_TRUE(presets.userPresets("look").empty());
	EXPECT_EQ(presets.presetsForTag("look").size(), 4u);
	EXPECT_EQ(presets.presetsForTag("other").size(), 1u);

	Preset mine = presets.capture("Mine", "look");
	Error e = presets.saveUserPreset(mine);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(mine.source, PresetSource::User);
	EXPECT_EQ(mine.path, support / "Presets" / "look" / "Mine.fxpreset");
	ASSERT_EQ(presets.userPresets("look").size(), 1u);
	EXPECT_EQ(presets.presetsForTag("look").size(), 5u);
	EXPECT_EQ(presets.presetsForTag("look").back().name, "Mine");
	Preset shadow = presets.capture("Warm", "look");
	shadow.values[kAmount] = Value(0.99);
	ASSERT_TRUE(presets.saveUserPreset(shadow).ok());
	// The user preset shadows the manifest one of the same name.
	std::optional<Preset> found = presets.find("look", "Warm");
	ASSERT_TRUE(found.has_value());
	EXPECT_EQ(found->source, PresetSource::User);
	EXPECT_EQ(found->values.at(kAmount), Value(0.99));
	EXPECT_EQ(presets.find("look", "Cold")->source, PresetSource::Manifest);
	EXPECT_FALSE(presets.find("look", "Nope").has_value());
	Preset untagged;
	untagged.name = "Untagged";
	EXPECT_EQ(presets.saveUserPreset(untagged).code(), ErrorCode::Value);
	const std::vector<PresetMenuEntry> entries = presets.menuEntries("look");
	EXPECT_EQ(entries.size(), 12u);  // default, sep, 2 user, sep, 4 plugin, sep, reveal, save
	(void)entries;
}

TEST(PresetsExtension, DrivesTheControl)
{
	const std::filesystem::path support = freshDirectory("ext-support");
	FakeEffectHost host(manifest());
	host.fakes().pathService.support = support;
	host.fakes().hostProfile.override(Quirk::NoDynamicChoices, false);
	ExtensionSystem system(host);
	auto* metaExt = static_cast<MetaExtension*>(system.load(std::make_unique<MetaExtension>()));
	auto* ext = static_cast<PresetsExtension*>(system.load(std::make_unique<PresetsExtension>(&metaExt->manager())));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->priority(EventId::CreateInstance), kPriorityFrameworkReconstruct + 1);
	EXPECT_EQ(ext->priority(EventId::InstanceChanged), MetaExtension::kPriorityTargetPresets - 2);
	EXPECT_EQ(ext->priority(EventId::Flush), kPriorityDefault);
	ASSERT_EQ(ext->controls(), std::vector<ParameterId>{kControl});
	EXPECT_EQ(ext->tagOf(kControl), "look");
	EXPECT_EQ(ext->tagOf(999), "");
	EXPECT_TRUE(ext->entries(999).empty());
	std::vector<PresetMenuEntry> entries = ext->entries(kControl);
	ASSERT_EQ(entries.size(), 7u);  // default, sep, Warm, Cold, sep, reveal, save
	FakeParameterService& params = host.params();

	host.post(EventId::CreateInstance);
	EXPECT_TRUE(ext->selectedName(kControl).empty());

	// Pick the manifest preset "Cold" (index 3): its values apply and the name is recorded.
	params.valueRef(kControl) = Value(3);
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(params.valueRef(kAmount), Value(0.1));
	EXPECT_EQ(ext->selectedName(kControl), "Cold");
	EXPECT_EQ(params.valueRef(kControl), Value(3));

	// A separator snaps back to the last real selection.
	params.valueRef(kControl) = Value(1);
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(params.valueRef(kControl), Value(3));
	// An out-of-range index too, and a plug-in edit is ignored.
	params.valueRef(kControl) = Value(99);
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(params.valueRef(kControl), Value(3));
	params.valueRef(kControl) = Value(0);
	ASSERT_TRUE(host.change("presets", ChangeReason::PluginEdited).ok());
	EXPECT_EQ(ext->selectedName(kControl), "Cold");
	// Default records itself and applies nothing.
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(ext->selectedName(kControl), "Default");
	EXPECT_EQ(params.valueRef(kAmount), Value(0.1));

	// Reveal opens the user folder and snaps back.
	params.valueRef(kControl) = Value(5);
	ASSERT_TRUE(host.change("presets").ok());
	ASSERT_EQ(host.fakes().pathService.revealed.size(), 1u);
	EXPECT_EQ(host.fakes().pathService.revealed[0], support / "Presets" / "look");
	EXPECT_TRUE(std::filesystem::is_directory(support / "Presets" / "look"));
	EXPECT_EQ(params.valueRef(kControl), Value(0));
	host.fakes().pathService.revealSucceeds = false;
	params.valueRef(kControl) = Value(5);
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(host.fakes().feedbackService.messages.size(), 1u);

	// Save captures the instance into the user folder and refreshes the options on a host with
	// dynamic choices.
	params.valueRef(kAmount) = Value(0.42);
	params.valueRef(kControl) = Value(6);
	ASSERT_TRUE(host.change("presets").ok());
	ASSERT_EQ(ext->manager().userPresets("look").size(), 1u);
	EXPECT_EQ(ext->manager().userPresets("look")[0].values.at(kAmount), Value(0.42));
	EXPECT_EQ(ext->manager().userPresets("look")[0].createdBy, kControl);
	EXPECT_EQ(params.optionWrites.count(kControl), 1u);
	EXPECT_EQ(params.optionWrites[kControl].size(), 9u);
	EXPECT_EQ(params.valueRef(kControl), Value(0));
	entries = ext->entries(kControl);
	ASSERT_EQ(entries.size(), 9u);
	EXPECT_EQ(entries[2].kind, PresetMenuEntry::Kind::User);

	// On a host that freezes choices, saving reports instead of refreshing.
	host.fakes().hostProfile.override(Quirk::NoDynamicChoices, true);
	host.fakes().feedbackService.messages.clear();
	params.valueRef(kControl) = Value(8);
	ASSERT_TRUE(host.change("presets").ok());
	ASSERT_EQ(host.fakes().feedbackService.messages.size(), 1u);
	EXPECT_NE(host.fakes().feedbackService.messages[0].find("restarts"), std::string::npos);
	EXPECT_EQ(params.optionWrites[kControl].size(), 9u);

	// The user preset applies (index 2) and the selection name survives a reordered menu.
	const std::string userName = entries[2].name;
	params.valueRef(kAmount) = Value(0.0);
	params.valueRef(kControl) = Value(2);
	ASSERT_TRUE(host.change("presets").ok());
	EXPECT_EQ(params.valueRef(kAmount), Value(0.42));
	EXPECT_EQ(ext->selectedName(kControl), userName);
	params.valueRef(kControl) = Value(0);
	host.post(EventId::CreateInstance);
	EXPECT_EQ(params.valueRef(kControl), Value(2));
	// A recorded name that no longer lists leaves the index alone.
	metaExt->manager().setMeta(kControl, MetaManager::kSelectedPresetKey, Value("Gone"));
	params.valueRef(kControl) = Value(4);
	host.post(EventId::CreateInstance);
	EXPECT_EQ(params.valueRef(kControl), Value(4));
	// Save failing is reported: a folder under a regular file cannot be created on any platform.
	const std::filesystem::path blocker = freshDirectory("blocker") / "file";
	std::ofstream(blocker) << "not a directory";
	host.fakes().pathService.support = blocker / "nowhere";
	host.fakes().feedbackService.messages.clear();
	entries = ext->entries(kControl);
	params.valueRef(kControl) = Value(static_cast<int>(entries.size() - 1));
	ASSERT_TRUE(host.change("presets").ok());
	ASSERT_EQ(host.fakes().feedbackService.messages.size(), 1u);
	EXPECT_NE(host.fakes().feedbackService.messages[0].find("could not be saved"), std::string::npos);
	// Without a meta manager, selections are not recorded.
	ExtensionSystem bare(host);
	auto* plain = static_cast<PresetsExtension*>(bare.load(std::make_unique<PresetsExtension>()));
	EXPECT_TRUE(plain->selectedName(kControl).empty());
	PlainEvent other(EventId::Render);
	host.bus().post(other);
}

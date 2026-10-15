// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The three framework extensions over the fake services: meta (seeding, loading, target presets,
// momentary resets, flush), visibility (rules on create, change, sync), and versioning (records,
// migrations, failures).
#include <ofxgrip/core/MetaExtension.hpp>
#include <ofxgrip/core/VersioningExtension.hpp>
#include <ofxgrip/core/VisibilityExtension.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kMode = 1, kSoftness = 2, kEdge = 3, kBypass = 4, kAmount = 5, kMomentary = 6, kSwitch = 7, kOld = 8, kNew = 9, kBad = 10 };

EffectManifest manifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.test.ext";
	m.identity.name = "Ext";
	m.version = Version{2, 1, 0};
	m.schemaVersion = 3;
	TargetPreset soft;
	soft.values["softness"] = Value(0.75);
	soft.flags["edge"] = {"+hidden", "-disabled"};
	soft.tags["edge"] = {"soft-only"};
	soft.meta["edge"] = {{"mode", Value("soft")}};
	soft.labels["softness"] = "Feather";
	soft.values["nonesuch"] = Value(1);
	soft.flags["nonesuch"] = {"+hidden"};
	soft.tags["nonesuch"] = {"x"};
	soft.meta["nonesuch"] = {{"a", Value(1)}};
	soft.labels["nonesuch"] = "x";
	TargetPreset hard;
	hard.flags["edge"] = {"-hidden"};
	hard.labels["softness"] = "Softness";
	m.parameters.push_back(ParameterSpec::menuParam(kMode, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}, 1).withTargetPreset("soft", soft).withTargetPreset("hard", hard));
	m.parameters.push_back(ParameterSpec::floatParam(kSoftness, "softness", "Softness", 0.5, 0, 1).withVisibleWhen("mode == 'soft'").withTags({"seeded"}).withMeta("unit", Value("px")));
	m.parameters.push_back(ParameterSpec::floatParam(kEdge, "edge", "Edge", 0.5, 0, 1).withEnabledWhen("!bypass && amount > 0"));
	m.parameters.push_back(ParameterSpec::boolParam(kBypass, "bypass", "Bypass", false));
	m.parameters.push_back(ParameterSpec::floatParam(kAmount, "amount", "Amount", 1, 0, 1).withVisibleWhen("mode in ('soft', 'hard')").withEnabledWhen("!bypass"));
	m.parameters.push_back(ParameterSpec::menuParam(kMomentary, "momentary", "Momentary", {{"main", "Main"}, {"go", "Go"}}, 0).withResetValue(Value(0)));
	TargetPreset on;
	on.values["amount"] = Value(0.25);
	m.parameters.push_back(ParameterSpec::boolParam(kSwitch, "switch", "Switch", false).withTargetPreset("true", on));
	m.parameters.push_back(ParameterSpec::floatParam(kOld, "old", "Old", 0.1, 0, 1).withFlags(HostFlags(HostFlag::Hidden), AppFlags(AppFlag::NoState)));
	m.parameters.push_back(ParameterSpec::floatParam(kNew, "new", "New", 0.0, 0, 1));
	m.parameters.push_back(ParameterSpec::floatParam(kBad, "bad", "Bad", 0.0, 0, 1).withVisibleWhen("mode == ").withEnabledWhen("edge >"));
	Migration m1;
	m1.fromSchemaVersion = 1;
	m1.renames["old"] = "new";
	m1.renames["ghost"] = "new";
	Migration m2;
	m2.fromSchemaVersion = 2;
	m.migrations = {m2, m1};
	return m;
}

}  // namespace

// ---- MetaExtension ------------------------------------------------------------------------------

TEST(MetaExtension, SeedsFromTheManifestAndFlushesTheStore)
{
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<MetaExtension*>(system.load(std::make_unique<MetaExtension>()));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->key(), "Meta");
	EXPECT_EQ(ext->priority(EventId::CreateInstance), kPriorityFrameworkCapture);
	EXPECT_EQ(ext->priority(EventId::Flush), kPriorityFrameworkFlush);
	EXPECT_EQ(ext->priority(EventId::InstanceChanged), MetaExtension::kPriorityTargetPresets);
	EXPECT_EQ(ext->priority(EventId::Render), kPriorityDefault);
	EXPECT_TRUE(ext->manager().hasTag(kSoftness, "seeded"));
	EXPECT_EQ(ext->manager().meta(kSoftness, "unit")->as<std::string>(), "px");
	EXPECT_TRUE(ext->manager().has(kMode));
	EXPECT_TRUE(ext->manager().dirty());
	host.post(EventId::CreateInstance);
	EXPECT_FALSE(ext->loadedFromDocument());
	host.post(EventId::Flush);
	EXPECT_FALSE(ext->manager().dirty());
	ASSERT_EQ(host.fakes().persist.blobs.count(std::string(MetaExtension::kPersistenceKey)), 1u);
	host.post(EventId::Flush);
	EXPECT_EQ(host.fakes().persist.blobs.size(), 1u);
}

TEST(MetaExtension, LoadsAndMergesTheStoredManager)
{
	FakeEffectHost host(manifest());
	MetaManager stored;
	stored.setTags(kSoftness, {"user"});
	stored.setMeta(kSoftness, "unit", Value("cm"));
	host.fakes().persist.blobs[std::string(MetaExtension::kPersistenceKey)] = stored.encode();
	ExtensionSystem system(host);
	auto* ext = static_cast<MetaExtension*>(system.load(std::make_unique<MetaExtension>()));
	host.post(EventId::CreateInstance);
	EXPECT_TRUE(ext->loadedFromDocument());
	EXPECT_EQ(ext->manager().tags(kSoftness), (std::vector<std::string>{"user", "seeded"}));
	EXPECT_EQ(ext->manager().meta(kSoftness, "unit")->as<std::string>(), "cm");

	FakeEffectHost broken(manifest());
	broken.fakes().persist.blobs[std::string(MetaExtension::kPersistenceKey)] = Bytes{1, 2, 3};
	ExtensionSystem system2(broken);
	auto* ext2 = static_cast<MetaExtension*>(system2.load(std::make_unique<MetaExtension>()));
	broken.post(EventId::CreateInstance);
	EXPECT_FALSE(ext2->loadedFromDocument());
	EXPECT_TRUE(broken.fakes().logService.contains("unreadable"));
}

TEST(MetaExtension, AppliesTargetPresetsInSectionOrder)
{
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<MetaExtension*>(system.load(std::make_unique<MetaExtension>()));
	FakeParameterService& params = host.params();
	params.valueRef(kMode) = Value(0);
	ASSERT_TRUE(host.change("mode").ok());
	EXPECT_DOUBLE_EQ(params.valueRef(kSoftness).toDouble(), 0.75);
	EXPECT_TRUE(params.flagsOf(kEdge).has(HostFlag::Hidden));
	EXPECT_FALSE(params.flagsOf(kEdge).has(HostFlag::Disabled));
	EXPECT_EQ(ext->manager().tags(kEdge), std::vector<std::string>{"soft-only"});
	EXPECT_EQ(ext->manager().meta(kEdge, "mode")->as<std::string>(), "soft");
	EXPECT_EQ(params.labelOf(kSoftness), "Feather");
	// Values before labels: the write log shows the value landed before the label.
	ASSERT_FALSE(params.writes.empty());
	EXPECT_EQ(params.writes.front(), "softness");
	EXPECT_EQ(params.labelWrites.back(), "softness");

	params.valueRef(kMode) = Value(1);
	ASSERT_TRUE(host.change("mode").ok());
	EXPECT_FALSE(params.flagsOf(kEdge).has(HostFlag::Hidden));
	EXPECT_EQ(params.labelOf(kSoftness), "Softness");

	// A plug-in edit applies nothing; a clip change applies nothing.
	params.valueRef(kMode) = Value(0);
	params.labelWrites.clear();
	ASSERT_TRUE(host.change("mode", ChangeReason::PluginEdited).ok());
	EXPECT_TRUE(params.labelWrites.empty());
	ChangeEvent clip(EventId::InstanceChanged, kParameterIdNone, "Source", 0, ChangeReason::UserEdited);
	host.bus().post(clip);
	EXPECT_TRUE(params.labelWrites.empty());
	// A change of a parameter without presets and a change of an unknown key are ignored.
	ASSERT_TRUE(host.change("softness").ok());
	ChangeEvent unknown(EventId::InstanceChanged, 999, "zzz", 0, ChangeReason::UserEdited);
	host.bus().post(unknown);

	// A toggle keys its presets by "true" and "false".
	params.valueRef(kSwitch) = Value(true);
	ASSERT_TRUE(host.change("switch").ok());
	EXPECT_DOUBLE_EQ(params.valueRef(kAmount).toDouble(), 0.25);

	// A failing write is reported, and the rest still applies.
	params.writable = false;
	TargetPreset preset;
	preset.values["amount"] = Value(0.5);
	preset.labels["amount"] = "Changed";
	EXPECT_EQ(ext->applyTargetPreset(preset).code(), ErrorCode::WrongPhase);
	EXPECT_EQ(params.labelOf(kAmount), "Changed");
	params.writable = true;
	params.valueRef(kMode) = Value(0);
	ASSERT_TRUE(host.change("mode").ok());
	params.writable = false;
	ASSERT_TRUE(host.change("mode").ok());
	EXPECT_TRUE(host.fakes().logService.contains("Target preset failed"));
}

TEST(MetaExtension, MomentaryControlsSnapBackToTheResetValue)
{
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	system.load(std::make_unique<MetaExtension>());
	FakeParameterService& params = host.params();
	params.valueRef(kMomentary) = Value(1);
	ASSERT_TRUE(host.change("momentary").ok());
	EXPECT_EQ(params.valueRef(kMomentary).toInt(), 0);
	EXPECT_EQ(params.writes.back(), "momentary");
	params.writes.clear();
	ASSERT_TRUE(host.change("momentary").ok());
	EXPECT_TRUE(params.writes.empty());
}

// ---- VisibilityExtension ------------------------------------------------------------------------

TEST(VisibilityExtension, AppliesRulesOnCreateChangeAndSync)
{
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<VisibilityExtension*>(system.load(std::make_unique<VisibilityExtension>()));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->priority(EventId::CreateInstance), kPriorityFrameworkReconstruct);
	EXPECT_EQ(ext->priority(EventId::InstanceChanged), VisibilityExtension::kPriorityRules);
	EXPECT_EQ(ext->priority(EventId::Flush), kPriorityDefault);
	EXPECT_EQ(ext->ruled(), (std::vector<ParameterId>{kSoftness, kEdge, kAmount}));
	EXPECT_EQ(ext->parseFailures().size(), 1u);
	EXPECT_TRUE(host.fakes().logService.contains("visibleWhen on bad ignored"));
	EXPECT_TRUE(host.fakes().logService.contains("enabledWhen on bad ignored"));
	FakeParameterService& params = host.params();

	host.post(EventId::CreateInstance);
	EXPECT_TRUE(params.flagsOf(kSoftness).has(HostFlag::Hidden));   // mode is hard
	EXPECT_FALSE(params.flagsOf(kEdge).has(HostFlag::Disabled));    // !bypass && amount > 0
	EXPECT_FALSE(params.flagsOf(kAmount).has(HostFlag::Hidden));
	EXPECT_FALSE(params.flagsOf(kAmount).has(HostFlag::Disabled));

	params.valueRef(kMode) = Value(0);
	params.flagWrites.clear();
	host.change("mode");
	EXPECT_FALSE(params.flagsOf(kSoftness).has(HostFlag::Hidden));
	EXPECT_EQ(params.flagWrites, std::vector<std::string>{"softness"});

	params.valueRef(kBypass) = Value(true);
	host.change("bypass");
	EXPECT_TRUE(params.flagsOf(kEdge).has(HostFlag::Disabled));
	EXPECT_TRUE(params.flagsOf(kAmount).has(HostFlag::Disabled));
	EXPECT_FALSE(params.flagsOf(kAmount).has(HostFlag::Hidden));

	// A change of an unreferenced key or a clip applies nothing.
	params.flagWrites.clear();
	host.change("softness");
	ChangeEvent clip(EventId::InstanceChanged, kParameterIdNone, "Source", 0, ChangeReason::UserEdited);
	host.bus().post(clip);
	EXPECT_TRUE(params.flagWrites.empty());

	params.valueRef(kBypass) = Value(false);
	params.valueRef(kAmount) = Value(0.0);
	host.post(EventId::SyncPrivateData);
	EXPECT_TRUE(params.flagsOf(kEdge).has(HostFlag::Disabled));
	EXPECT_FALSE(params.flagsOf(kAmount).has(HostFlag::Disabled));
	EXPECT_TRUE(ext->applyDependents("nothing").ok());
	EXPECT_TRUE(ext->applyAll().ok());
}

TEST(VisibilityExtension, LowersVisibilityToEnabledWhereSecretIsPermanent)
{
	FakeEffectHost host(manifest());
	host.fakes().hostProfile.override(Quirk::SecretIsPermanent, true);
	ExtensionSystem system(host);
	system.load(std::make_unique<VisibilityExtension>());
	FakeParameterService& params = host.params();
	host.post(EventId::CreateInstance);
	EXPECT_FALSE(params.flagsOf(kSoftness).has(HostFlag::Hidden));
	EXPECT_TRUE(params.flagsOf(kSoftness).has(HostFlag::Disabled));
	params.valueRef(kMode) = Value(0);
	host.change("mode");
	EXPECT_FALSE(params.flagsOf(kSoftness).has(HostFlag::Disabled));
}

TEST(VisibilityExtension, ReportsEvaluationErrors)
{
	EffectManifest m = manifest();
	m.parameters.push_back(ParameterSpec::floatParam(50, "ref", "Ref", 0, 0, 1).withVisibleWhen("softness == 'text'"));
	FakeEffectHost host(m);
	ExtensionSystem system(host);
	auto* ext = static_cast<VisibilityExtension*>(system.load(std::make_unique<VisibilityExtension>()));
	EXPECT_EQ(ext->applyAll().code(), ErrorCode::Value);
	EXPECT_EQ(ext->applyDependents("softness").code(), ErrorCode::Value);
}

// ---- VersioningExtension ------------------------------------------------------------------------

TEST(VersioningExtension, RecordsTheCurrentVersionOnAFreshInstance)
{
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<VersioningExtension*>(system.load(std::make_unique<VersioningExtension>()));
	EXPECT_EQ(ext->priority(EventId::CreateInstance), kPriorityFrameworkCapture - 1);
	EXPECT_EQ(ext->priority(EventId::Flush), kPriorityDefault);
	host.post(EventId::CreateInstance);
	EXPECT_FALSE(ext->stored().has_value());
	EXPECT_FALSE(ext->migrated());
	Bytes bytes;
	ASSERT_TRUE(host.fakes().persist.load(VersioningExtension::kPersistenceKey, bytes).ok());
	StoredVersion v;
	ASSERT_TRUE(VersioningExtension::decode(bytes, v).ok());
	EXPECT_EQ(v.schemaVersion, 3u);
	EXPECT_EQ(v.pluginVersion, (Version{2, 1, 0}));
	for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
		EXPECT_FALSE(VersioningExtension::decode(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut)), v).ok());
	}
	Bytes newer = bytes;
	newer[4] = 9;
	EXPECT_EQ(VersioningExtension::decode(newer, v).code(), ErrorCode::Codec);
}

TEST(VersioningExtension, MigratesAnOlderSchemaAndRecordsOnlyOnSuccess)
{
	FakeEffectHost host(manifest());
	host.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{1, Version{1, 0, 0}});
	std::vector<std::string> calls;
	ExtensionSystem system(host);
	auto* ext = static_cast<VersioningExtension*>(system.load(std::make_unique<VersioningExtension>([&calls](unsigned from, unsigned to, Version fromVersion) {
		calls.push_back(std::to_string(from) + "->" + std::to_string(to) + " from " + fromVersion.toString());
		return Error::none();
	})));
	host.post(EventId::CreateInstance);
	ASSERT_TRUE(ext->stored().has_value());
	EXPECT_EQ(ext->stored()->schemaVersion, 1u);
	EXPECT_TRUE(ext->migrated());
	EXPECT_TRUE(ext->migrationError().ok());
	EXPECT_EQ(calls, std::vector<std::string>{"1->3 from 1.0.0"});
	EXPECT_DOUBLE_EQ(host.params().valueRef(kNew).toDouble(), 0.1);
	EXPECT_TRUE(host.fakes().logService.contains("ghost"));
	StoredVersion v;
	ASSERT_TRUE(VersioningExtension::decode(host.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)], v).ok());
	EXPECT_EQ(v.schemaVersion, 3u);

	// A failing hook leaves the stored version alone.
	FakeEffectHost failing(manifest());
	failing.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{2, Version{2, 0, 0}});
	ExtensionSystem system2(failing);
	auto* ext2 = static_cast<VersioningExtension*>(system2.load(std::make_unique<VersioningExtension>([](unsigned, unsigned, Version) { return Error::make(ErrorCode::Failed, "nope"); })));
	failing.post(EventId::CreateInstance);
	EXPECT_FALSE(ext2->migrated());
	EXPECT_EQ(ext2->migrationError().code(), ErrorCode::Failed);
	ASSERT_TRUE(VersioningExtension::decode(failing.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)], v).ok());
	EXPECT_EQ(v.schemaVersion, 2u);
	EXPECT_TRUE(failing.fakes().logService.contains("Migration failed"));

	// A failing rename write stops the migration.
	FakeEffectHost locked(manifest());
	locked.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{1, Version{1, 0, 0}});
	locked.params().writable = false;
	ExtensionSystem system3(locked);
	auto* ext3 = static_cast<VersioningExtension*>(system3.load(std::make_unique<VersioningExtension>()));
	locked.post(EventId::CreateInstance);
	EXPECT_EQ(ext3->migrationError().code(), ErrorCode::WrongPhase);
}

TEST(VersioningExtension, HandlesSameNewerAndMissingRecords)
{
	// Same version: nothing changes.
	FakeEffectHost same(manifest());
	same.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{3, Version{2, 1, 0}});
	ExtensionSystem s1(same);
	auto* e1 = static_cast<VersioningExtension*>(s1.load(std::make_unique<VersioningExtension>()));
	same.post(EventId::CreateInstance);
	EXPECT_FALSE(e1->migrated());
	EXPECT_EQ(same.fakes().persist.blobs.size(), 1u);

	// A newer schema is left alone with a warning.
	FakeEffectHost newer(manifest());
	newer.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{9, Version{9, 0, 0}});
	ExtensionSystem s2(newer);
	auto* e2 = static_cast<VersioningExtension*>(s2.load(std::make_unique<VersioningExtension>()));
	newer.post(EventId::CreateInstance);
	EXPECT_FALSE(e2->migrated());
	EXPECT_TRUE(newer.fakes().logService.contains("newer schema"));

	// Same schema, older plug-in version: the hook runs with the same schema on both sides.
	FakeEffectHost patch(manifest());
	patch.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = VersioningExtension::encode(StoredVersion{3, Version{2, 0, 5}});
	unsigned seenFrom = 99;
	ExtensionSystem s3(patch);
	auto* e3 = static_cast<VersioningExtension*>(s3.load(std::make_unique<VersioningExtension>([&seenFrom](unsigned from, unsigned, Version) {
		seenFrom = from;
		return Error::none();
	})));
	patch.post(EventId::CreateInstance);
	EXPECT_TRUE(e3->migrated());
	EXPECT_EQ(seenFrom, 3u);

	// Other state without a version record counts as schema 0.
	FakeEffectHost legacy(manifest());
	legacy.fakes().persist.blobs["something"] = Bytes{1};
	ExtensionSystem s4(legacy);
	auto* e4 = static_cast<VersioningExtension*>(s4.load(std::make_unique<VersioningExtension>()));
	legacy.post(EventId::CreateInstance);
	ASSERT_TRUE(e4->stored().has_value());
	EXPECT_EQ(e4->stored()->schemaVersion, 0u);
	EXPECT_TRUE(e4->migrated());
	EXPECT_DOUBLE_EQ(legacy.params().valueRef(kNew).toDouble(), 0.1);

	// An unreadable record counts as schema 0 too.
	FakeEffectHost garbage(manifest());
	garbage.fakes().persist.blobs[std::string(VersioningExtension::kPersistenceKey)] = Bytes{1, 2};
	ExtensionSystem s5(garbage);
	auto* e5 = static_cast<VersioningExtension*>(s5.load(std::make_unique<VersioningExtension>()));
	garbage.post(EventId::CreateInstance);
	EXPECT_TRUE(e5->migrated());
	EXPECT_TRUE(garbage.fakes().logService.contains("unreadable"));
	PlainEvent other(EventId::Render);
	garbage.bus().post(other);
}

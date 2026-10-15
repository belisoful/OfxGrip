// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The About and Debug menus, the instance tracker, and the regression checks over the fakes.
#include <ofxgrip/core/About.hpp>
#include <ofxgrip/core/Debug.hpp>
#include <ofxgrip/core/InstanceTracker.hpp>
#include <ofxgrip/core/Regression.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kAbout = 9993, kDebug = 9994, kGain = 1, kSecret = 2, kNoDebug = 3, kDebugOnly = 4, kGroup = 5 };

AboutMenu sampleMenu()
{
	AboutMenu menu;
	menu.name = "About Gain";
	menu.mainText = "OfxGrip Gain 1.0";
	AboutEntry site;
	site.label = "Website";
	site.url = "https://example.com/gain";
	site.fallbacks = {"https://example.org/gain"};
	menu.entries.push_back(site);
	AboutEntry sep;
	sep.kind = AboutEntry::Kind::Separator;
	menu.entries.push_back(sep);
	AboutEntry text;
	text.kind = AboutEntry::Kind::Text;
	text.label = "MIT License";
	menu.entries.push_back(text);
	AboutEntry dialog;
	dialog.kind = AboutEntry::Kind::Dialog;
	dialog.label = "Credits";
	dialog.dialogText = "Made by Belisoful";
	menu.entries.push_back(dialog);
	AboutEntry bare;
	bare.kind = AboutEntry::Kind::Dialog;
	bare.label = "Bare";
	menu.entries.push_back(bare);
	AboutEntry empty;
	empty.label = "Nowhere";
	menu.entries.push_back(empty);
	return menu;
}

EffectManifest manifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.test.menus";
	m.identity.uuid = "11111111-2222-4333-8444-555555555555";
	m.identity.name = "Menus";
	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1, 0, 2).withHint("h"));
	m.parameters.push_back(ParameterSpec::floatParam(kSecret, "secret", "Secret", 1, 0, 2).withFlags(HostFlags(HostFlag::Hidden)).withHint("h"));
	m.parameters.push_back(ParameterSpec::floatParam(kNoDebug, "noDebug", "No debug", 1, 0, 2).withFlags(HostFlags(HostFlag::Hidden), AppFlags(AppFlag::NoDebug)).withHint("h"));
	m.parameters.push_back(ParameterSpec::floatParam(kDebugOnly, "debugOnly", "Debug only", 1, 0, 2).withFlags(HostFlags(HostFlag::Hidden), AppFlags(AppFlag::InDebugMode)).withHint("h"));
	m.parameters.push_back(ParameterSpec::groupParam(kGroup, "group", "Group").withFlags(HostFlags(HostFlag::Hidden)));
	m.about = sampleMenu();
	m.parameters.push_back(aboutMenuParameter(m.about, kAbout, "ofxgrip_about"));
	m.parameters.push_back(debugMenuParameter(kDebug, "ofxgrip_debug"));
	return m;
}

}  // namespace

TEST(AboutMenu, BuildsTheParameterAndRunsTheRows)
{
	const AboutMenu menu = sampleMenu();
	const ParameterSpec spec = aboutMenuParameter(menu, kAbout, "ofxgrip_about");
	EXPECT_EQ(spec.type, ParameterType::Menu);
	EXPECT_EQ(spec.label, "About Gain");
	ASSERT_EQ(spec.options.size(), 7u);
	EXPECT_EQ(spec.options[0].label, "OfxGrip Gain 1.0");
	EXPECT_EQ(spec.options[1].label, "Website");
	EXPECT_EQ(spec.options[2].key, "separator1");
	EXPECT_EQ(spec.options[3].label, "MIT License");
	EXPECT_EQ(spec.options[4].label, "Credits...");
	EXPECT_TRUE(spec.hostFlags.has(HostFlag::NotAnimatable));
	EXPECT_TRUE(spec.appFlags.has(AppFlag::NoState));
	EXPECT_FALSE(spec.hasState());
	AboutMenu bare;
	EXPECT_EQ(aboutMenuParameter(bare, kAbout, "a").options[0].label, "About");
	bare.name.clear();
	EXPECT_EQ(aboutMenuParameter(bare, kAbout, "a").label, "About");

	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<AboutExtension*>(system.load(std::make_unique<AboutExtension>(menu, kAbout)));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->parameter(), kAbout);
	EXPECT_EQ(ext->menu().entries.size(), 6u);
	EXPECT_EQ(ext->priority(EventId::InstanceChanged), -12);
	FakeParameterService& params = host.params();
	FakePaths& paths = host.fakes().pathService;
	// A link opens and the menu snaps back to the header row.
	params.valueRef(kAbout) = Value(1);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	EXPECT_EQ(paths.openedUrls, std::vector<std::string>{"https://example.com/gain"});
	EXPECT_EQ(params.valueRef(kAbout), Value(0));
	EXPECT_EQ(ext->opened().size(), 1u);
	// When the first URL fails the fallback is tried; when all fail the link is shown as text.
	paths.openSucceeds = false;
	params.valueRef(kAbout) = Value(1);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	EXPECT_EQ(paths.openedUrls.size(), 3u);
	ASSERT_EQ(host.fakes().feedbackService.messages.size(), 1u);
	EXPECT_NE(host.fakes().feedbackService.messages[0].find("https://example.com/gain"), std::string::npos);
	paths.openSucceeds = true;
	// A separator and a text row do nothing but snap back.
	host.fakes().feedbackService.messages.clear();
	params.valueRef(kAbout) = Value(2);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	params.valueRef(kAbout) = Value(3);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	EXPECT_TRUE(host.fakes().feedbackService.messages.empty());
	EXPECT_EQ(params.valueRef(kAbout), Value(0));
	// A dialog shows its text, or its label when it has none.
	params.valueRef(kAbout) = Value(4);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	params.valueRef(kAbout) = Value(5);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	ASSERT_EQ(host.fakes().feedbackService.messages.size(), 2u);
	EXPECT_EQ(host.fakes().feedbackService.messages[0], "Made by Belisoful");
	EXPECT_EQ(host.fakes().feedbackService.messages[1], "Bare");
	// An empty link opens nothing; out-of-range rows, other parameters, and plug-in edits are ignored.
	paths.openedUrls.clear();
	params.valueRef(kAbout) = Value(6);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	EXPECT_TRUE(paths.openedUrls.empty());
	params.valueRef(kAbout) = Value(42);
	ASSERT_TRUE(host.change("ofxgrip_about").ok());
	EXPECT_EQ(params.valueRef(kAbout), Value(0));
	params.valueRef(kAbout) = Value(1);
	ASSERT_TRUE(host.change("ofxgrip_about", ChangeReason::PluginEdited).ok());
	ASSERT_TRUE(host.change("gain").ok());
	EXPECT_EQ(params.valueRef(kAbout), Value(1));
	EXPECT_TRUE(paths.openedUrls.empty());
	PlainEvent other(EventId::Render);
	host.bus().post(other);
}

TEST(DebugMenu, RevealsAndHidesParameters)
{
	const ParameterSpec spec = debugMenuParameter(kDebug, "ofxgrip_debug");
	EXPECT_EQ(spec.options.size(), 3u);
	EXPECT_TRUE(spec.appFlags.has(AppFlag::NoDebug));
	FakeEffectHost host(manifest());
	ExtensionSystem system(host);
	auto* ext = static_cast<DebugExtension*>(system.load(std::make_unique<DebugExtension>(kDebug)));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->parameter(), kDebug);
	EXPECT_FALSE(ext->debugMode());
	FakeParameterService& params = host.params();
	params.valueRef(kDebug) = Value(1);
	ASSERT_TRUE(host.change("ofxgrip_debug").ok());
	EXPECT_TRUE(ext->debugMode());
	EXPECT_EQ(params.valueRef(kDebug), Value(0));
	EXPECT_FALSE(params.flagsOf(kSecret).has(HostFlag::Hidden));
	EXPECT_FALSE(params.flagsOf(kDebugOnly).has(HostFlag::Hidden));
	EXPECT_FALSE(params.flagsOf(kGroup).has(HostFlag::Hidden));
	EXPECT_TRUE(params.flagsOf(kNoDebug).has(HostFlag::Hidden));
	EXPECT_FALSE(params.flagsOf(kGain).has(HostFlag::Hidden));
	EXPECT_EQ(ext->revealed(), (std::set<ParameterId>{kSecret, kDebugOnly, kGroup}));
	params.valueRef(kDebug) = Value(2);
	ASSERT_TRUE(host.change("ofxgrip_debug").ok());
	EXPECT_FALSE(ext->debugMode());
	EXPECT_TRUE(params.flagsOf(kSecret).has(HostFlag::Hidden));
	EXPECT_TRUE(params.flagsOf(kDebugOnly).has(HostFlag::Hidden));
	EXPECT_TRUE(ext->revealed().empty());
	// Row 0, other parameters, and plug-in edits do nothing.
	params.valueRef(kDebug) = Value(0);
	ASSERT_TRUE(host.change("ofxgrip_debug").ok());
	params.valueRef(kDebug) = Value(1);
	ASSERT_TRUE(host.change("ofxgrip_debug", ChangeReason::PluginEdited).ok());
	ASSERT_TRUE(host.change("gain").ok());
	EXPECT_FALSE(ext->debugMode());
	EXPECT_TRUE(params.flagsOf(kSecret).has(HostFlag::Hidden));
	// Direct calls report write failures.
	params.writable = true;
	EXPECT_TRUE(ext->reveal().ok());
	EXPECT_TRUE(ext->hide().ok());
	PlainEvent other(EventId::Render);
	host.bus().post(other);
}

TEST(InstanceTracker, RegistersInstancesByUuid)
{
	InstanceRegistry& registry = InstanceRegistry::shared();
	const std::size_t uuidsBefore = registry.uuidCount();
	EffectManifest m = manifest();
	FakeEffectHost a(m);
	FakeEffectHost b(m);
	ExtensionSystem systemA(a);
	ExtensionSystem systemB(b);
	auto* extA = static_cast<InstanceTrackerExtension*>(systemA.load(std::make_unique<InstanceTrackerExtension>()));
	auto* extB = static_cast<InstanceTrackerExtension*>(systemB.load(std::make_unique<InstanceTrackerExtension>()));
	ASSERT_NE(extA, nullptr);
	EXPECT_EQ(extA->uuid(), m.identity.uuid);
	EXPECT_EQ(extA->siblingCount(), 2u);
	EXPECT_EQ(extB->siblings(), (std::vector<EffectHost*>{&a, &b}));
	EXPECT_EQ(registry.uuidCount(), uuidsBefore + 1);
	registry.add(m.identity.uuid, &a);  // adding twice keeps one entry
	EXPECT_EQ(registry.count(m.identity.uuid), 2u);
	registry.add("", &a);
	registry.add("x", nullptr);
	EXPECT_EQ(registry.count(""), 0u);
	b.post(EventId::DestroyInstance);
	EXPECT_EQ(extA->siblingCount(), 1u);
	b.post(EventId::DestroyInstance);  // idempotent
	systemA.unloadAll();
	EXPECT_EQ(registry.count(m.identity.uuid), 0u);
	EXPECT_EQ(registry.uuidCount(), uuidsBefore);
	registry.remove("nonesuch", &a);
	EXPECT_TRUE(registry.instances("nonesuch").empty());
	// Without a UUID the OFX identifier keys the registry.
	m.identity.uuid.clear();
	FakeEffectHost c(m);
	ExtensionSystem systemC(c);
	auto* extC = static_cast<InstanceTrackerExtension*>(systemC.load(std::make_unique<InstanceTrackerExtension>()));
	EXPECT_EQ(extC->uuid(), "com.test.menus");
	PlainEvent other(EventId::Render);
	c.bus().post(other);
}

TEST(Regression, ReportsWhatLooksWrong)
{
	EXPECT_TRUE(looksLikeUuid("11111111-2222-4333-8444-555555555555"));
	EXPECT_FALSE(looksLikeUuid("11111111-2222-4333-8444-55555555555"));
	EXPECT_FALSE(looksLikeUuid("11111111x2222-4333-8444-555555555555"));
	EXPECT_FALSE(looksLikeUuid("1111111G-2222-4333-8444-555555555555"));
	EffectManifest clean = manifest();
	clean.version = Version{1, 0, 0};
	clean.about.entries.pop_back();  // the link without a URL
	const std::vector<RegressionReport> cleanReports = checkManifest(clean);
	EXPECT_TRUE(cleanReports.empty()) << (cleanReports.empty() ? "" : cleanReports[0].check + ": " + cleanReports[0].message);

	EffectManifest bad;
	bad.identity.ofxId = "noDots";
	bad.identity.name = "Bad";
	bad.identity.uuid = "not-a-uuid";
	bad.version = Version{0, 1, 0};
	bad.parameters.push_back(ParameterSpec::floatParam(1, "flat", "", 1, 2, 2));
	bad.parameters.push_back(ParameterSpec::menuParam(2, "one", "One", {{"a", "A"}}, 0).withHint("h"));
	ParameterSpec control = ParameterSpec::menuParam(3, "presets", "Presets", {}, 0);
	control.type = ParameterType::Presets;
	bad.parameters.push_back(control);
	ParameterSpec tagged = control;
	tagged.id = 4;
	tagged.key = "presets2";
	tagged.tags = {"look"};
	bad.parameters.push_back(tagged);
	ParameterSpec divider;
	divider.id = 5;
	divider.key = "divider";
	divider.type = ParameterType::Divider;
	bad.parameters.push_back(divider);
	AboutEntry link;
	link.label = "Odd";
	link.url = "ftp://example.com";
	bad.about.entries.push_back(link);
	const std::vector<RegressionReport> reports = checkManifest(bad);
	std::vector<std::string> checks;
	for (const RegressionReport& r : reports) {
		checks.push_back(r.check);
	}
	EXPECT_EQ(checks, (std::vector<std::string>{"uuid", "version", "identifier", "label", "hint", "range", "options", "hint", "presets", "hint", "presets", "about"}));
	EXPECT_EQ(reports[0].level, LogLevel::Error);
	bad.identity.uuid.clear();
	EXPECT_EQ(checkManifest(bad)[0].level, LogLevel::Warning);

	FakeEffectHost host(bad);
	ExtensionSystem system(host);
	auto* ext = static_cast<RegressionExtension*>(system.load(std::make_unique<RegressionExtension>()));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->reports().size(), 12u);
	EXPECT_TRUE(host.fakes().logService.contains("Regression (identifier)"));
	EXPECT_TRUE(ext->interests().empty());
	PlainEvent other(EventId::Render);
	host.bus().post(other);
}

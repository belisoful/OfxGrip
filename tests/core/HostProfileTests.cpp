// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/HostProfile.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(HostProfile, ResolveTableEntriesMatchByPrefix)
{
	HostProfile p;
	p.hostName = "DaVinciResolveLite";
	p.hostLabel = "DaVinci Resolve Lite";
	p.hostVersion = Version{21, 1, 1};
	p.deriveIdentity();
	EXPECT_EQ(p.family, HostFamily::Resolve);
	EXPECT_EQ(p.edition, HostEdition::Free);
	EXPECT_TRUE(p.has(Quirk::MaskAlwaysConnected));
	EXPECT_TRUE(p.has(Quirk::ParamRangeDefaultClamp));
	EXPECT_TRUE(p.has(Quirk::ThreadSuiteDummy));
	EXPECT_FALSE(p.has(Quirk::SecretIsPermanent));
	EXPECT_FALSE(p.has(Quirk::NatronFakeHostName));
}

TEST(HostProfile, ClosedRangesStopMatching)
{
	HostProfile p;
	p.hostName = "DaVinciResolve";
	p.hostVersion = Version{14, 0, 0};
	EXPECT_TRUE(p.has(Quirk::MessageSuiteV2Broken));
	p.hostVersion = Version{21, 1, 1};
	EXPECT_FALSE(p.has(Quirk::MessageSuiteV2Broken));
	EXPECT_TRUE(quirkMatchesHost(Quirk::FxFactoryRequiresResolve20, "DaVinciResolve", Version{18, 0, 0}));
	EXPECT_FALSE(quirkMatchesHost(Quirk::FxFactoryRequiresResolve20, "DaVinciResolve", Version{20, 0, 0}));
}

TEST(HostProfile, GenericEntriesApplyToEveryHost)
{
	HostProfile p;
	p.hostName = "com.example.somehost";
	EXPECT_TRUE(p.has(Quirk::ChoicePersistedByIndex));
	EXPECT_TRUE(p.has(Quirk::ImageRenderScaleUntrusted));
	EXPECT_FALSE(p.has(Quirk::MaskAlwaysConnected));
}

TEST(HostProfile, OverridesWinAndClear)
{
	HostProfile p;
	p.hostName = "uk.co.thefoundry.nuke";
	p.deriveIdentity();
	EXPECT_EQ(p.family, HostFamily::Nuke);
	EXPECT_TRUE(p.has(Quirk::SecretIsPermanent));
	p.override(Quirk::SecretIsPermanent, false);
	EXPECT_FALSE(p.has(Quirk::SecretIsPermanent));
	p.override(Quirk::MaskAlwaysConnected, true);
	EXPECT_TRUE(p.has(Quirk::MaskAlwaysConnected));
	p.clearOverride(Quirk::SecretIsPermanent);
	EXPECT_TRUE(p.has(Quirk::SecretIsPermanent));
}

TEST(HostProfile, ProbedCapabilitiesOverrideTheTable)
{
	HostProfile p;
	p.hostName = "com.example.tiling";
	p.capabilities.supportsTiles = false;
	p.capabilities.supportsMultiResolution = false;
	EXPECT_TRUE(p.has(Quirk::NoTilesNoMultiResolution));
	p.capabilities.supportsTiles = true;
	EXPECT_FALSE(p.has(Quirk::NoTilesNoMultiResolution));
	p.suites.multiThread = false;
	EXPECT_TRUE(p.has(Quirk::NullThreadSuite));
	p.suites.multiThread = true;
	EXPECT_FALSE(p.has(Quirk::NullThreadSuite));
	p.suites.parametric = false;
	EXPECT_TRUE(p.has(Quirk::NoParametricParams));
	p.hostName = "DaVinciResolve";
	p.hostVersion = Version{14, 0, 0};
	p.suites.messageV2 = true;
	EXPECT_FALSE(p.has(Quirk::MessageSuiteV2Broken));
}

TEST(HostProfile, FamiliesAndNames)
{
	HostProfile p;
	p.hostName = "fr.inria.Natron";
	p.deriveIdentity();
	EXPECT_EQ(p.family, HostFamily::Natron);
	EXPECT_TRUE(p.has(Quirk::MarkdownDescriptionsOK));
	p.hostName = "com.belisoful.ofxgrip.minihost";
	p.deriveIdentity();
	EXPECT_EQ(p.family, HostFamily::OfxGripMiniHost);
	EXPECT_EQ(toString(HostFamily::Resolve), "resolve");
	p.hostName = "whatever";
	p.deriveIdentity();
	EXPECT_EQ(p.family, HostFamily::Unknown);
}

TEST(HostProfile, RegistryTableIsConsistent)
{
	const auto& registry = quirkRegistry();
	EXPECT_EQ(registry.size(), static_cast<std::size_t>(Quirk::Count));
	for (std::size_t i = 0; i < registry.size(); ++i) {
		EXPECT_EQ(static_cast<std::size_t>(registry[i].key), i) << registry[i].name;
		Quirk parsed{};
		EXPECT_TRUE(parseQuirk(registry[i].name, parsed));
		EXPECT_EQ(parsed, registry[i].key);
		EXPECT_TRUE(parseQuirk(registry[i].registryId, parsed));
		EXPECT_EQ(toString(registry[i].key), registry[i].name);
	}
	Quirk none{};
	EXPECT_FALSE(parseQuirk("NoSuchQuirk", none));
}

TEST(HostProfile, DescribeListsActiveQuirks)
{
	HostProfile p;
	p.hostName = "DaVinciResolveLite";
	p.hostVersion = Version{21, 1, 1};
	p.deriveIdentity();
	const std::string text = p.describe();
	EXPECT_NE(text.find("Q-RESOLVE-002"), std::string::npos);
	EXPECT_NE(text.find("edition=free"), std::string::npos);
	EXPECT_FALSE(p.activeQuirks().empty());
}

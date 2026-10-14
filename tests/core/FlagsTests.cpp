// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Flags.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Flags, SetTestClear)
{
	HostFlags f;
	EXPECT_TRUE(f.none());
	f.set(HostFlag::Hidden);
	EXPECT_TRUE(f.has(HostFlag::Hidden));
	EXPECT_FALSE(f.has(HostFlag::Disabled));
	f.clear(HostFlag::Hidden);
	EXPECT_TRUE(f.none());
	HostFlags both = HostFlag::Hidden | HostFlag::Disabled;
	EXPECT_TRUE(both.has(HostFlag::Hidden));
	EXPECT_TRUE(both.has(HostFlag::Disabled));
}

TEST(Flags, HostAndAppSetsAreSeparate)
{
	ParsedFlags parsed = parseFlags(std::vector<std::string>{"hidden", "nostate", "presetnovalue"});
	EXPECT_TRUE(parsed.host.has(HostFlag::Hidden));
	EXPECT_TRUE(parsed.app.has(AppFlag::NoState));
	EXPECT_TRUE(parsed.app.has(AppFlag::PresetNoValue));
	EXPECT_EQ(parsed.host.bits(), static_cast<std::uint32_t>(HostFlag::Hidden));
	EXPECT_TRUE(parsed.unknown.empty());
}

TEST(Flags, PlusAndMinusPrefixesAdjustDefaults)
{
	HostFlags initial = HostFlags(HostFlag::Hidden) | HostFlags(HostFlag::Disabled);
	ParsedFlags parsed = parseFlags("-hidden, +notanimatable", initial, {});
	EXPECT_FALSE(parsed.host.has(HostFlag::Hidden));
	EXPECT_TRUE(parsed.host.has(HostFlag::Disabled));
	EXPECT_TRUE(parsed.host.has(HostFlag::NotAnimatable));
}

TEST(Flags, UnknownNamesAreReported)
{
	ParsedFlags parsed = parseFlags("hidden bogus");
	ASSERT_EQ(parsed.unknown.size(), 1u);
	EXPECT_EQ(parsed.unknown[0], "bogus");
	EXPECT_TRUE(parsed.host.has(HostFlag::Hidden));
}

TEST(Flags, NamesRoundTrip)
{
	for (HostFlag f : {HostFlag::NotAnimatable, HostFlag::Hidden, HostFlag::Disabled, HostFlag::Collapsed, HostFlag::DontSave, HostFlag::NoEvaluateOnChange, HostFlag::NoUndo, HostFlag::CustomUI,
	                   HostFlag::IgnoreMinMax, HostFlag::UseFullViewWidth}) {
		HostFlag parsed{};
		ASSERT_TRUE(parseHostFlag(toString(f), parsed)) << toString(f);
		EXPECT_EQ(parsed, f);
	}
	for (AppFlag f : {AppFlag::NoState, AppFlag::PresetNoValue, AppFlag::PresetNoTags, AppFlag::PresetNoMeta, AppFlag::HiddenProxy, AppFlag::InDebugMode, AppFlag::NoDebug}) {
		AppFlag parsed{};
		ASSERT_TRUE(parseAppFlag(toString(f), parsed)) << toString(f);
		EXPECT_EQ(parsed, f);
	}
	const std::vector<std::string> names = flagNames(HostFlags(HostFlag::Hidden), AppFlags(AppFlag::NoDebug));
	ASSERT_EQ(names.size(), 2u);
	EXPECT_EQ(names[0], "hidden");
	EXPECT_EQ(names[1], "nodebug");
}

TEST(Flags, CaseInsensitiveAndSeparators)
{
	ParsedFlags parsed = parseFlags("HIDDEN;Disabled|nostate");
	EXPECT_TRUE(parsed.host.has(HostFlag::Hidden));
	EXPECT_TRUE(parsed.host.has(HostFlag::Disabled));
	EXPECT_TRUE(parsed.app.has(AppFlag::NoState));
}

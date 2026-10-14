// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Types.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Types, VersionParsesOneToThreeFields)
{
	Version v;
	ASSERT_TRUE(Version::parse("1", v));
	EXPECT_EQ(v, (Version{1, 0, 0}));
	ASSERT_TRUE(Version::parse("1.2", v));
	EXPECT_EQ(v, (Version{1, 2, 0}));
	ASSERT_TRUE(Version::parse("21.1.1", v));
	EXPECT_EQ(v, (Version{21, 1, 1}));
	EXPECT_EQ(v.toString(), "21.1.1");
}

TEST(Types, VersionRejectsMalformedText)
{
	Version v;
	EXPECT_FALSE(Version::parse("", v));
	EXPECT_FALSE(Version::parse("1.", v));
	EXPECT_FALSE(Version::parse("a.b", v));
	EXPECT_FALSE(Version::parse("1..2", v));
}

TEST(Types, VersionOrdersLexicographically)
{
	EXPECT_LT((Version{1, 9, 9}), (Version{2, 0, 0}));
	EXPECT_LT((Version{16, 0, 0}), (Version{16, 0, 1}));
	EXPECT_EQ((Version{1, 2, 3}), (Version{1, 2, 3}));
}

TEST(Types, RectIntersectionAndUnion)
{
	const RectI a{0, 0, 10, 10};
	const RectI b{5, 5, 20, 20};
	EXPECT_EQ(a.intersect(b), (RectI{5, 5, 10, 10}));
	EXPECT_EQ(a.unite(b), (RectI{0, 0, 20, 20}));
	EXPECT_TRUE(a.intersect(RectI{20, 20, 30, 30}).empty());
	EXPECT_EQ(RectI{}.unite(a), a);
	EXPECT_EQ(a.width(), 10);
	EXPECT_TRUE(a.contains(9, 9));
	EXPECT_FALSE(a.contains(10, 9));
}

TEST(Types, CanonicalPixelConversionRoundsOutward)
{
	const RectD canonical{0.5, 0.5, 9.5, 9.5};
	const RectI pixels = toPixels(canonical, {1, 1}, 1.0);
	EXPECT_EQ(pixels, (RectI{0, 0, 10, 10}));
	const RectI half = toPixels(RectD{0, 0, 100, 100}, {0.5, 0.5}, 1.0);
	EXPECT_EQ(half, (RectI{0, 0, 50, 50}));
	const RectD back = toCanonical(half, {0.5, 0.5}, 1.0);
	EXPECT_DOUBLE_EQ(back.x2, 100.0);
	const RectI par = toPixels(RectD{0, 0, 200, 100}, {1, 1}, 2.0);
	EXPECT_EQ(par.x2, 100);
}

TEST(Types, ReservedParameterIds)
{
	EXPECT_TRUE(isReservedParameterId(9991));
	EXPECT_TRUE(isReservedParameterId(9999));
	EXPECT_FALSE(isReservedParameterId(9990));
	EXPECT_FALSE(isReservedParameterId(100));
}

TEST(Types, PixelFormatSizes)
{
	EXPECT_EQ(bytesPerComponent(BitDepth::UInt8), 1u);
	EXPECT_EQ(bytesPerComponent(BitDepth::UInt16), 2u);
	EXPECT_EQ(bytesPerComponent(BitDepth::Half), 2u);
	EXPECT_EQ(bytesPerComponent(BitDepth::Float), 4u);
	EXPECT_EQ(bytesPerComponent(BitDepth::None), 0u);
	EXPECT_EQ(componentCount(Components::RGBA), 4u);
	EXPECT_EQ(componentCount(Components::RGB), 3u);
	EXPECT_EQ(componentCount(Components::Alpha), 1u);
	EXPECT_EQ(componentCount(Components::None), 0u);
}

TEST(Types, EnumNamesRoundTrip)
{
	for (EffectKind k : {EffectKind::Filter, EffectKind::Generator, EffectKind::Transition, EffectKind::General, EffectKind::Retimer}) {
		EffectKind parsed{};
		ASSERT_TRUE(parseEffectKind(toString(k), parsed));
		EXPECT_EQ(parsed, k);
	}
	for (BitDepth d : {BitDepth::None, BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		BitDepth parsed{};
		ASSERT_TRUE(parseBitDepth(toString(d), parsed));
		EXPECT_EQ(parsed, d);
	}
	EffectKind kind{};
	EXPECT_FALSE(parseEffectKind("nope", kind));
	EXPECT_EQ(toString(Components::RGB), "rgb");
	EXPECT_EQ(toString(Field::Lower), "lower");
}

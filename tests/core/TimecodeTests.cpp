// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Timecode against FxGrip's reference vectors: exact rates, drop-frame counting at the minute
// and ten-minute boundaries, the fields, the text, and the inverse.
#include <ofxgrip/core/Timecode.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace ofxgrip;

namespace {

constexpr FrameRate k2997{30000, 1001};
constexpr FrameRate k5994{60000, 1001};
constexpr FrameRate k11988{120000, 1001};

}  // namespace

TEST(Timecode, StandardRatesAndNames)
{
	EXPECT_EQ(frameRateFor(StandardFrameRate::Fps29_97), k2997);
	EXPECT_EQ(frameRateFor(StandardFrameRate::Fps24), (FrameRate{24, 1}));
	EXPECT_EQ(frameRateFor(StandardFrameRate::Fps119_88), k11988);
	EXPECT_EQ(frameRateFor(static_cast<StandardFrameRate>(99)), (FrameRate{30, 1}));
	const std::span<const std::string_view> names = standardFrameRateNames();
	ASSERT_EQ(names.size(), 12u);
	EXPECT_EQ(names[static_cast<std::size_t>(StandardFrameRate::Fps23_976)], "23.98");
	EXPECT_EQ(names[static_cast<std::size_t>(StandardFrameRate::Fps29_97)], "29.97");
	EXPECT_EQ(names[static_cast<std::size_t>(StandardFrameRate::Fps120)], "120");
	for (int i = 0; i < 12; ++i) {
		const FrameRate rate = frameRateFor(static_cast<StandardFrameRate>(i));
		EXPECT_TRUE(rate.valid());
		EXPECT_EQ(FrameRate::fromFramesPerSecond(rate.framesPerSecond()), rate) << names[static_cast<std::size_t>(i)];
	}
}

TEST(Timecode, RatesFromAHostsDouble)
{
	EXPECT_EQ(FrameRate::fromFramesPerSecond(29.97), k2997);
	EXPECT_EQ(FrameRate::fromFramesPerSecond(29.97002997), k2997);
	EXPECT_EQ(FrameRate::fromFramesPerSecond(23.976), (FrameRate{24000, 1001}));
	EXPECT_EQ(FrameRate::fromFramesPerSecond(59.94), k5994);
	EXPECT_EQ(FrameRate::fromFramesPerSecond(25.0), (FrameRate{25, 1}));
	EXPECT_EQ(FrameRate::fromFramesPerSecond(12.5), (FrameRate{12500, 1000}));
	EXPECT_FALSE(FrameRate::fromFramesPerSecond(0.0).valid());
	EXPECT_FALSE(FrameRate::fromFramesPerSecond(-24.0).valid());
	EXPECT_FALSE(FrameRate::fromFramesPerSecond(std::numeric_limits<double>::quiet_NaN()).valid());
	EXPECT_FALSE(FrameRate::fromFramesPerSecond(std::numeric_limits<double>::infinity()).valid());
	EXPECT_EQ(k2997.nominal(), 30);
	EXPECT_EQ((FrameRate{24000, 1001}).nominal(), 24);
	EXPECT_EQ((FrameRate{0, 1}).nominal(), 0);
	EXPECT_DOUBLE_EQ((FrameRate{0, 0}).framesPerSecond(), 0.0);
	EXPECT_NEAR(k2997.framesPerSecond(), 29.97002997, 1e-8);
}

TEST(Timecode, DropFrameSupport)
{
	EXPECT_TRUE(supportsDropFrame(k2997));
	EXPECT_TRUE(supportsDropFrame(k5994));
	EXPECT_TRUE(supportsDropFrame(k11988));
	EXPECT_FALSE(supportsDropFrame({24000, 1001}));
	EXPECT_FALSE(supportsDropFrame({30, 1}));
	EXPECT_FALSE(supportsDropFrame({60, 1}));
	EXPECT_FALSE(supportsDropFrame({0, 0}));
}

TEST(Timecode, FrameIndexForSeconds)
{
	EXPECT_EQ(frameIndexForSeconds(0.0, k2997), 0);
	EXPECT_EQ(frameIndexForSeconds(1001.0 / 30000.0, k2997), 1);
	EXPECT_EQ(frameIndexForSeconds(17982.0 * 1001.0 / 30000.0, k2997), 17982);
	// A time inside a frame belongs to it.
	EXPECT_EQ(frameIndexForSeconds(1000.0 / 30000.0, k2997), 0);
	EXPECT_EQ(frameIndexForSeconds(20.0 / 600.0, k2997), 0);
	EXPECT_EQ(frameIndexForSeconds(21.0 / 600.0, k2997), 1);
	// A rounded time just under a boundary counts as the frame that starts there.
	EXPECT_EQ(frameIndexForSeconds(0.99999999, {24, 1}), 24);
	EXPECT_EQ(frameIndexForSeconds(0.9999, {24, 1}), 23);
	EXPECT_EQ(frameIndexForSeconds(-1.0 / 30.0, {30, 1}), -1);
	EXPECT_EQ(frameIndexForSeconds(-1.0 / 3000.0, {30, 1}), -1);
	EXPECT_EQ(frameIndexForSeconds(1.0, {0, 0}), 0);
	EXPECT_EQ(frameIndexForSeconds(std::numeric_limits<double>::quiet_NaN(), {30, 1}), 0);
	EXPECT_EQ(frameIndexForSeconds(1.0e300, {30, 1}), 0);
}

TEST(Timecode, FieldsAtAWholeRate)
{
	const FrameRate pal{25, 1};
	const std::int64_t index = ((1 * 60 + 2) * 60 + 3) * 25 + 4;
	const TimecodeComponents c = timecodeComponents(index, pal, false);
	EXPECT_TRUE(c.valid);
	EXPECT_EQ(c.hours, 1);
	EXPECT_EQ(c.minutes, 2);
	EXPECT_EQ(c.seconds, 3);
	EXPECT_EQ(c.frames, 4);
	EXPECT_EQ(c.nominalFramesPerSecond, 25);
	EXPECT_FALSE(c.dropFrame);
	EXPECT_EQ(timecodeString(c), "01:02:03:04");
	// A rate with nothing to drop ignores the flag.
	const TimecodeComponents asked = timecodeComponents(30 * 60, {30, 1}, true);
	EXPECT_FALSE(asked.dropFrame);
	EXPECT_EQ(timecodeString(asked), "00:01:00:00");
	// Hours wrap at 24, and a negative index counts as zero.
	EXPECT_EQ(timecodeComponents(25 * 3600 * 25, pal, false).hours, 1);
	EXPECT_EQ(timecodeString(-5, pal, false), "00:00:00:00");
	// Three frame digits from 100 frames per second.
	EXPECT_EQ(timecodeString(125, {120, 1}, false), "00:00:01:005");
	EXPECT_EQ(timecodeString(99, {100, 1}, false), "00:00:00:099");
	const TimecodeComponents invalid = timecodeComponents(10, {0, 0}, false);
	EXPECT_FALSE(invalid.valid);
	EXPECT_EQ(timecodeString(invalid), "--:--:--:--");
}

TEST(Timecode, DropFrameCounting)
{
	EXPECT_EQ(timecodeString(0, k2997, true), "00:00:00;00");
	EXPECT_EQ(timecodeString(29, k2997, true), "00:00:00;29");
	EXPECT_EQ(timecodeString(30, k2997, true), "00:00:01;00");
	EXPECT_EQ(timecodeString(1799, k2997, true), "00:00:59;29");
	// Frame numbers 00 and 01 are skipped at each minute that is not a multiple of ten.
	EXPECT_EQ(timecodeString(1800, k2997, true), "00:01:00;02");
	EXPECT_EQ(timecodeString(1801, k2997, true), "00:01:00;03");
	EXPECT_EQ(timecodeString(17981, k2997, true), "00:09:59;29");
	EXPECT_EQ(timecodeString(17982, k2997, true), "00:10:00;00");
	EXPECT_EQ(timecodeString(17982 + 1800, k2997, true), "00:11:00;02");
	// One hour of real time is one hour of drop-frame timecode.
	EXPECT_EQ(timecodeString(107892, k2997, true), "01:00:00;00");
	EXPECT_EQ(timecodeString(1800, k2997, false), "00:01:00:00");
	EXPECT_EQ(timecodeString(107892, k2997, false), "00:59:56:12");
	// 59.94 skips four numbers, 119.88 skips eight.
	EXPECT_EQ(timecodeString(3600, k5994, true), "00:01:00;04");
	EXPECT_EQ(timecodeString(215784, k5994, true), "01:00:00;00");
	EXPECT_EQ(timecodeString(7200, k11988, true), "00:01:00;008");
	EXPECT_EQ(timecodeString(431568, k11988, true), "01:00:00;000");
}

TEST(Timecode, TheInverse)
{
	for (const FrameRate& rate : {k2997, k5994, k11988, FrameRate{24000, 1001}, FrameRate{25, 1}, FrameRate{100, 1}}) {
		for (bool dropFrame : {false, true}) {
			for (std::int64_t index : {std::int64_t{0}, std::int64_t{1}, std::int64_t{1799}, std::int64_t{1800}, std::int64_t{1801}, std::int64_t{17981}, std::int64_t{17982}, std::int64_t{19782},
			                           std::int64_t{107892}, std::int64_t{123456}, std::int64_t{999999}}) {
				const TimecodeComponents c = timecodeComponents(index, rate, dropFrame);
				EXPECT_EQ(frameIndexForTimecode(c, rate), index) << timecodeString(c) << " at " << rate.framesPerSecond();
			}
		}
	}
	// A skipped frame number names the first frame after the skip.
	TimecodeComponents skipped = timecodeComponents(1800, k2997, true);
	skipped.frames = 0;
	EXPECT_EQ(frameIndexForTimecode(skipped, k2997), 1800);
	// Fields out of range clamp.
	TimecodeComponents wild = timecodeComponents(0, {25, 1}, false);
	wild.hours = 99;
	wild.minutes = 99;
	wild.seconds = 99;
	wild.frames = 99;
	EXPECT_EQ(frameIndexForTimecode(wild, {25, 1}), 24 * 3600 * 25 - 1);
	EXPECT_EQ(frameIndexForTimecode(TimecodeComponents{}, k2997), 0);
	EXPECT_EQ(frameIndexForTimecode(timecodeComponents(5, k2997, false), {0, 0}), 0);
}

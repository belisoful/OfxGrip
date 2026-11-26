// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The curve builders against tables FxGrip's builders produce for the same points, the curve
// value's sanitizing and neutral shapes, the keyframe blend, and the set's codec.
#include <ofxgrip/core/Curve.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

using namespace ofxgrip;

namespace {

void expectLut(std::span<const float> actual, std::span<const float> expected)
{
	ASSERT_EQ(actual.size(), expected.size());
	for (std::size_t i = 0; i < actual.size(); ++i) {
		// A compiler that fuses a multiply and an add rounds the last bit differently.
		EXPECT_NEAR(actual[i], expected[i], 2.5e-7f) << "entry " << i;
	}
}

}  // namespace

TEST(Curve, ClampedTableMatchesFxGrip)
{
	const std::array<CurvePoint, 4> points{CurvePoint{0.0f, 0.0f}, CurvePoint{0.25f, 0.6f}, CurvePoint{0.7f, 0.65f}, CurvePoint{1.0f, 1.0f}};
	std::array<float, 9> lut{};
	buildCurveLut(points, lut);
	const std::array<float, 9> expected{0x0p+0f, 0x1.788eb6p-2f, 0x1.333334p-1f, 0x1.3d0926p-1f, 0x1.4138p-1f, 0x1.466b0cp-1f, 0x1.59be44p-1f, 0x1.a6867cp-1f, 0x1p+0f};
	expectLut(lut, expected);
	// The spline is monotone between monotone points: it never overshoots.
	std::array<float, 257> fine{};
	buildCurveLut(points, fine);
	for (std::size_t i = 1; i < fine.size(); ++i) {
		EXPECT_GE(fine[i], fine[i - 1]) << "entry " << i;
	}
}

TEST(Curve, ClampedTableSortsDropsDuplicatesAndHoldsItsEnds)
{
	// Unsorted, with two points at x = 0.5: the first one stays.
	const std::array<CurvePoint, 4> points{CurvePoint{0.8f, 0.9f}, CurvePoint{0.2f, 0.1f}, CurvePoint{0.5f, 0.7f}, CurvePoint{0.5f, 0.2f}};
	std::array<float, 9> lut{};
	buildCurveLut(points, lut);
	const std::array<float, 9> expected{0x1.99999ap-4f, 0x1.99999ap-4f, 0x1.a7d27cp-3f, 0x1.f85b04p-2f, 0x1.666666p-1f, 0x1.98537p-1f, 0x1.bceb24p-1f, 0x1.ccccccp-1f, 0x1.ccccccp-1f};
	expectLut(lut, expected);
	EXPECT_EQ(lut[0], 0.1f);
	EXPECT_EQ(lut[8], 0.9f);
}

TEST(Curve, PeriodicTableMatchesFxGrip)
{
	const std::array<CurvePoint, 3> points{CurvePoint{0.1f, 0.2f}, CurvePoint{0.4f, 0.9f}, CurvePoint{0.75f, 0.3f}};
	std::array<float, 9> lut{};
	buildCurveLutPeriodic(points, lut);
	const std::array<float, 9> expected{0x1.adbaf4p-3f, 0x1.b5ceb2p-3f, 0x1.199998p-1f, 0x1.c5bf86p-1f, 0x1.9503e6p-1f, 0x1.001e06p-1f, 0x1.333334p-2f, 0x1.f6df88p-3f, 0x1.adbaf4p-3f};
	expectLut(lut, expected);
	EXPECT_EQ(lut.front(), lut.back());
	// x folds into [0, 1): 1.25 is 0.25 and -0.1 is 0.9.
	const std::array<CurvePoint, 3> folded{CurvePoint{1.25f, 0.5f}, CurvePoint{0.6f, 0.1f}, CurvePoint{-0.1f, 0.8f}};
	buildCurveLutPeriodic(folded, lut);
	const std::array<float, 9> expectedFolded{0x1.85626ap-1f, 0x1.46b0c8p-1f, 0x1p-1f, 0x1.54c3c2p-2f, 0x1.46439cp-3f, 0x1.d203c6p-4f, 0x1.cccccep-2f, 0x1.928c54p-1f, 0x1.85626ap-1f};
	expectLut(lut, expectedFolded);
}

TEST(Curve, DegenerateTables)
{
	std::array<float, 5> lut{};
	const std::array<float, 5> ramp{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
	buildCurveLut({}, lut);
	expectLut(lut, ramp);
	const std::array<CurvePoint, 1> one{CurvePoint{0.3f, 0.9f}};
	buildCurveLut(one, lut);
	expectLut(lut, ramp);
	buildCurveLutPeriodic(one, lut);
	expectLut(lut, ramp);
	// Two points at one x are a constant.
	const std::array<CurvePoint, 2> stacked{CurvePoint{0.4f, 0.6f}, CurvePoint{0.4f, 0.1f}};
	const std::array<float, 5> constant{0.6f, 0.6f, 0.6f, 0.6f, 0.6f};
	buildCurveLut(stacked, lut);
	expectLut(lut, constant);
	buildCurveLutPeriodic(stacked, lut);
	expectLut(lut, constant);
	// One entry is the curve at 0, and no entries is no work.
	std::array<float, 1> single{9.0f};
	buildCurveLut({}, single);
	EXPECT_EQ(single[0], 0.0f);
	const std::array<CurvePoint, 2> line{CurvePoint{0.0f, 0.25f}, CurvePoint{1.0f, 0.75f}};
	buildCurveLut(line, single);
	EXPECT_EQ(single[0], 0.25f);
	buildCurveLut(line, std::span<float>{});
	buildCurveLutPeriodic(line, std::span<float>{});
}

TEST(Curve, DataSanitizesItsPoints)
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const std::array<Point2D, 6> raw{Point2D{0.9, 2.0}, Point2D{-0.5, -1.0}, Point2D{0.5, 0.5}, Point2D{0.5, 0.1}, Point2D{nan, 0.5}, Point2D{1.5, 0.25}};
	const CurveData linear(raw, CurveRole::Remap, CurveDomain::Linear);
	// x and y clamp, the points sort, a duplicate x drops, and a NaN point drops.
	const std::vector<Point2D> expected{{0.0, 0.0}, {0.5, 0.5}, {0.9, 1.0}, {1.0, 0.25}};
	EXPECT_EQ(linear.points(), expected);
	EXPECT_EQ(linear.role(), CurveRole::Remap);
	EXPECT_EQ(linear.domain(), CurveDomain::Linear);
	// The circular domain folds x, and 1 stays 1.
	const std::array<Point2D, 3> around{Point2D{1.25, 0.5}, Point2D{1.0, 0.75}, Point2D{-0.25, 0.1}};
	const CurveData circular(around, CurveRole::Shift, CurveDomain::Circular);
	const std::vector<Point2D> expectedCircular{{0.25, 0.5}, {0.75, 0.1}, {1.0, 0.75}};
	EXPECT_EQ(circular.points(), expectedCircular);
	const std::vector<CurvePoint> pairs = circular.curvePoints();
	ASSERT_EQ(pairs.size(), 3u);
	EXPECT_EQ(pairs[1], (CurvePoint{0.75f, 0.1f}));
}

TEST(Curve, NeutralShapes)
{
	EXPECT_TRUE(CurveData().isIdentity());
	EXPECT_EQ(CurveData(), CurveData::identity(CurveRole::Remap, CurveDomain::Linear));
	for (CurveDomain domain : {CurveDomain::Linear, CurveDomain::Circular}) {
		for (CurveRole role : {CurveRole::Remap, CurveRole::Shift, CurveRole::MultiplierHalf, CurveRole::MultiplierOne}) {
			const CurveData neutral = CurveData::identity(role, domain);
			EXPECT_TRUE(neutral.isIdentity());
			std::array<float, 5> lut{};
			neutral.buildLut(lut);
			const float flat = role == CurveRole::MultiplierOne ? 1.0f : 0.5f;
			for (std::size_t i = 0; i < lut.size(); ++i) {
				EXPECT_EQ(lut[i], role == CurveRole::Remap ? static_cast<float>(i) / 4.0f : flat) << "role " << static_cast<int>(role) << " entry " << i;
			}
		}
	}
	// The circular remap neutral holds no points: the diagonal cannot close on a circle.
	EXPECT_TRUE(CurveData::identity(CurveRole::Remap, CurveDomain::Circular).points().empty());
	const std::array<Point2D, 2> lifted{Point2D{0.0, 0.1}, Point2D{1.0, 1.0}};
	EXPECT_FALSE(CurveData(lifted, CurveRole::Remap, CurveDomain::Linear).isIdentity());
	const std::array<Point2D, 3> three{Point2D{0.0, 0.0}, Point2D{0.5, 0.5}, Point2D{1.0, 1.0}};
	EXPECT_FALSE(CurveData(three, CurveRole::Remap, CurveDomain::Linear).isIdentity());
}

TEST(Curve, EvaluatesAtOnePosition)
{
	const std::array<Point2D, 4> raw{Point2D{0.0, 0.0}, Point2D{0.25, 0.6}, Point2D{0.7, 0.65}, Point2D{1.0, 1.0}};
	const CurveData curve(raw, CurveRole::Remap, CurveDomain::Linear);
	std::array<float, 9> lut{};
	curve.buildLut(lut);
	for (std::size_t k = 0; k < lut.size(); ++k) {
		EXPECT_EQ(static_cast<float>(curve.evaluate(static_cast<double>(k) / 8.0)), lut[k]) << "entry " << k;
	}
	// The curve passes through its points, and x clamps.
	EXPECT_NEAR(curve.evaluate(0.25), 0.6, 1e-7);
	EXPECT_NEAR(curve.evaluate(0.7), 0.65, 1e-7);
	EXPECT_DOUBLE_EQ(curve.evaluate(-3.0), 0.0);
	EXPECT_DOUBLE_EQ(curve.evaluate(3.0), 1.0);
	const std::array<Point2D, 3> around{Point2D{0.1, 0.2}, Point2D{0.4, 0.9}, Point2D{0.75, 0.3}};
	const CurveData circular(around, CurveRole::Shift, CurveDomain::Circular);
	EXPECT_NEAR(circular.evaluate(0.4), 0.9, 1e-7);
	EXPECT_DOUBLE_EQ(circular.evaluate(1.4), circular.evaluate(0.4));
	EXPECT_DOUBLE_EQ(circular.evaluate(-0.6), circular.evaluate(0.4));
	EXPECT_DOUBLE_EQ(CurveData::identity(CurveRole::Remap, CurveDomain::Circular).evaluate(0.3), 0.3);
}

TEST(Curve, BlendsBetweenKeyframes)
{
	const std::array<Point2D, 3> a{Point2D{0.0, 0.0}, Point2D{0.4, 0.8}, Point2D{1.0, 1.0}};
	const std::array<Point2D, 3> b{Point2D{0.0, 0.2}, Point2D{0.6, 0.4}, Point2D{1.0, 0.6}};
	const CurveData left(a, CurveRole::Remap, CurveDomain::Linear);
	const CurveData right(b, CurveRole::Remap, CurveDomain::Linear);
	// The same point count blends point by point.
	const CurveData half = left.interpolated(right, 0.5);
	const std::vector<Point2D> expected{{0.0, 0.1}, {0.5, 0.6000000000000001}, {1.0, 0.8}};
	ASSERT_EQ(half.points().size(), 3u);
	for (std::size_t i = 0; i < 3; ++i) {
		EXPECT_NEAR(half.points()[i].x, expected[i].x, 1e-12);
		EXPECT_NEAR(half.points()[i].y, expected[i].y, 1e-12);
	}
	EXPECT_EQ(left.interpolated(right, 0.0), left);
	EXPECT_EQ(left.interpolated(right, 1.0), right);
	// A different point count blends the evaluated shapes on 33 samples.
	const std::array<Point2D, 2> flat{Point2D{0.0, 0.5}, Point2D{1.0, 0.5}};
	const CurveData two(flat, CurveRole::Remap, CurveDomain::Linear);
	const CurveData sampled = left.interpolated(two, 0.25);
	ASSERT_EQ(sampled.points().size(), 33u);
	EXPECT_EQ(sampled.role(), CurveRole::Remap);
	for (const Point2D& point : sampled.points()) {
		EXPECT_NEAR(point.y, 0.75 * left.evaluate(point.x) + 0.25 * 0.5, 1e-6) << "x " << point.x;
	}
	// A different role or domain takes the same route and keeps the left curve's.
	const CurveData shifted(a, CurveRole::Shift, CurveDomain::Circular);
	const CurveData mixed = left.interpolated(shifted, 0.5);
	EXPECT_EQ(mixed.points().size(), 33u);
	EXPECT_EQ(mixed.role(), CurveRole::Remap);
	EXPECT_EQ(mixed.domain(), CurveDomain::Linear);
}

TEST(Curve, SetStoresOnlyEditedCurves)
{
	const std::array<Point2D, 3> a{Point2D{0.0, 0.0}, Point2D{0.4, 0.8}, Point2D{1.0, 1.0}};
	const CurveData edited(a, CurveRole::Remap, CurveDomain::Linear);
	CurveSet set;
	EXPECT_TRUE(set.empty());
	set.setCurve("red", edited);
	set.setCurve("luma", edited);
	set.setCurve("green", CurveData());
	EXPECT_EQ(set.size(), 2u);
	EXPECT_EQ(set.keys(), (std::vector<std::string>{"luma", "red"}));
	ASSERT_NE(set.curve("red"), nullptr);
	EXPECT_EQ(*set.curve("red"), edited);
	EXPECT_EQ(set.curve("green"), nullptr);
	// An identity curve removes the key.
	set.setCurve("red", CurveData());
	EXPECT_EQ(set.curve("red"), nullptr);
	set.removeCurve("luma");
	set.removeCurve("absent");
	EXPECT_TRUE(set.empty());
}

TEST(Curve, SetBlendsBetweenKeyframes)
{
	const std::array<Point2D, 3> a{Point2D{0.0, 0.0}, Point2D{0.4, 0.8}, Point2D{1.0, 1.0}};
	const std::array<Point2D, 3> b{Point2D{0.0, 0.2}, Point2D{0.6, 0.4}, Point2D{1.0, 0.6}};
	const std::array<Point2D, 2> gain{Point2D{0.0, 0.9}, Point2D{1.0, 0.9}};
	const CurveData first(a, CurveRole::Remap, CurveDomain::Linear);
	const CurveData second(b, CurveRole::Remap, CurveDomain::Linear);
	const CurveData lifted(gain, CurveRole::MultiplierHalf, CurveDomain::Circular);
	CurveSet left;
	left.setCurve("both", first);
	left.setCurve("leftOnly", lifted);
	CurveSet right;
	right.setCurve("both", second);
	right.setCurve("rightOnly", lifted);
	const CurveSet half = left.interpolated(right, 0.5);
	EXPECT_EQ(half.keys(), (std::vector<std::string>{"both", "leftOnly", "rightOnly"}));
	EXPECT_EQ(*half.curve("both"), first.interpolated(second, 0.5));
	// A curve on one side blends toward its role's neutral: 0.9 toward 0.5.
	for (const char* key : {"leftOnly", "rightOnly"}) {
		const CurveData* curve = half.curve(key);
		ASSERT_NE(curve, nullptr) << key;
		EXPECT_EQ(curve->role(), CurveRole::MultiplierHalf);
		EXPECT_EQ(curve->domain(), CurveDomain::Circular);
		ASSERT_EQ(curve->points().size(), 2u);
		EXPECT_NEAR(curve->points()[0].y, 0.7, 1e-12);
	}
	// At the ends the one-sided curve is neutral, so its key is gone.
	EXPECT_EQ(left.interpolated(right, 0.0), left);
	EXPECT_EQ(left.interpolated(right, 1.0), right);
}

TEST(Curve, SetCodec)
{
	const std::array<Point2D, 3> a{Point2D{0.0, 0.0}, Point2D{0.4, 0.8}, Point2D{1.0, 1.0}};
	const std::array<Point2D, 2> gain{Point2D{0.0, 0.9}, Point2D{1.0, 0.9}};
	CurveSet set;
	set.setCurve("red", CurveData(a, CurveRole::Remap, CurveDomain::Linear));
	set.setCurve("hueVsSat", CurveData(gain, CurveRole::MultiplierHalf, CurveDomain::Circular));
	const Bytes bytes = set.encode();
	CurveSet decoded;
	Error e = CurveSet::decode(bytes, decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(decoded, set);
	// An empty set round-trips.
	e = CurveSet::decode(CurveSet().encode(), decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(decoded.empty());
	// Truncated, foreign, and future bytes are refused, and the output stays empty.
	decoded = set;
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		EXPECT_EQ(CurveSet::decode(std::span<const std::uint8_t>(bytes.data(), length), decoded).code(), ErrorCode::Codec) << "length " << length;
		EXPECT_TRUE(decoded.empty());
	}
	Bytes foreign = bytes;
	foreign[0] = 'X';
	EXPECT_EQ(CurveSet::decode(foreign, decoded).code(), ErrorCode::Codec);
	Bytes future = bytes;
	future[4] = 9;
	EXPECT_EQ(CurveSet::decode(future, decoded).code(), ErrorCode::Codec);
	Bytes badDomain = CurveSet().encode();
	badDomain[8] = 1;  // one curve follows
	const Bytes tail{1, 0, 0, 0, 'k', 7, 0, 0, 0, 0, 0};
	badDomain.insert(badDomain.end(), tail.begin(), tail.end());
	EXPECT_EQ(CurveSet::decode(badDomain, decoded).code(), ErrorCode::Codec);
	Bytes tooMany = CurveSet().encode();
	tooMany[10] = 0xFF;  // a curve count past the limit
	EXPECT_EQ(CurveSet::decode(tooMany, decoded).code(), ErrorCode::Codec);
}

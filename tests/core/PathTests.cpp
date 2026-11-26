// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The path value: its edits, the cubic segments of each vertex style, flattening, the keyframe
// blend, and the codec.
#include <ofxgrip/core/Path.hpp>

#include <gtest/gtest.h>

#include <array>

using namespace ofxgrip;

namespace {

PathVertex vertex(double x, double y, PathStyle style = PathStyle::Linear)
{
	PathVertex v;
	v.location = {x, y};
	v.style = style;
	return v;
}

}  // namespace

TEST(Path, SegmentCounts)
{
	EXPECT_EQ(pathSegmentCount(0, false), 0u);
	EXPECT_EQ(pathSegmentCount(1, true), 0u);
	EXPECT_EQ(pathSegmentCount(2, false), 1u);
	EXPECT_EQ(pathSegmentCount(2, true), 2u);
	EXPECT_EQ(pathSegmentCount(5, false), 4u);
	EXPECT_EQ(pathSegmentCount(5, true), 5u);
	EXPECT_TRUE(pathCubicSegments({}, true).empty());
}

TEST(Path, EditsReturnANewPath)
{
	const std::array<Point2D, 3> locations{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}};
	const PathData path = PathData::fromLocations(locations, false);
	ASSERT_EQ(path.vertices().size(), 3u);
	EXPECT_FALSE(path.closed());
	EXPECT_EQ(path.vertices()[1], vertex(10, 0));
	EXPECT_TRUE(PathData().empty());

	const PathData inserted = path.inserting(vertex(5, 5), 1);
	ASSERT_EQ(inserted.vertices().size(), 4u);
	EXPECT_EQ(inserted.vertices()[1].location, (Point2D{5, 5}));
	EXPECT_EQ(path.vertices().size(), 3u);
	// An index past the end appends.
	EXPECT_EQ(path.inserting(vertex(7, 7), 99).vertices().back().location, (Point2D{7, 7}));
	EXPECT_EQ(inserted.removing(1), path);
	EXPECT_EQ(path.removing(99), path);
	EXPECT_EQ(path.replacing(0, vertex(1, 1)).vertices()[0].location, (Point2D{1, 1}));
	EXPECT_EQ(path.replacing(99, vertex(1, 1)), path);

	PathVertex curved = vertex(10, 0, PathStyle::Bezier);
	curved.inTangent = {-2, 0};
	curved.outTangent = {2, 1};
	const PathData withTangents = path.replacing(1, curved);
	// A moved vertex carries its tangents.
	const PathData moved = withTangents.replacingLocation(1, {20, 5});
	EXPECT_EQ(moved.vertices()[1].location, (Point2D{20, 5}));
	EXPECT_EQ(moved.vertices()[1].outTangent, (Point2D{2, 1}));
	EXPECT_EQ(withTangents.replacingLocation(99, {1, 1}), withTangents);
	const PathData shifted = withTangents.translated({1, -1});
	EXPECT_EQ(shifted.vertices()[0].location, (Point2D{1, -1}));
	EXPECT_EQ(shifted.vertices()[1].location, (Point2D{11, -1}));
	EXPECT_EQ(shifted.vertices()[1].inTangent, (Point2D{-2, 0}));
	EXPECT_TRUE(path.withClosed(true).closed());
	EXPECT_NE(path.withClosed(true), path);
}

TEST(Path, StraightStylesGiveStraightSegments)
{
	const std::array<PathVertex, 3> vertices{vertex(0, 0), vertex(10, 0, PathStyle::Rectangle), vertex(10, 10)};
	const std::vector<CubicSegment> open = pathCubicSegments(vertices, false);
	ASSERT_EQ(open.size(), 2u);
	EXPECT_EQ(open[0], (CubicSegment{{0, 0}, {0, 0}, {10, 0}, {10, 0}}));
	EXPECT_EQ(open[1], (CubicSegment{{10, 0}, {10, 0}, {10, 10}, {10, 10}}));
	// A closed path adds the segment back to the first vertex.
	const std::vector<CubicSegment> closed = pathCubicSegments(vertices, true);
	ASSERT_EQ(closed.size(), 3u);
	EXPECT_EQ(closed[2], (CubicSegment{{10, 10}, {10, 10}, {0, 0}, {0, 0}}));
	EXPECT_EQ(open[0].at(0.5), (Point2D{5, 0}));
}

TEST(Path, BezierVerticesUseTheirTangents)
{
	PathVertex a = vertex(0, 0, PathStyle::Bezier);
	a.outTangent = {0, 4};
	a.inTangent = {0, -4};
	PathVertex b = vertex(10, 0, PathStyle::SuperEllipse);
	b.inTangent = {0, 4};
	b.outTangent = {0, -4};
	const std::array<PathVertex, 2> vertices{a, b};
	const std::vector<CubicSegment> segments = pathCubicSegments(vertices, true);
	ASSERT_EQ(segments.size(), 2u);
	EXPECT_EQ(segments[0], (CubicSegment{{0, 0}, {0, 4}, {10, 4}, {10, 0}}));
	EXPECT_EQ(segments[1], (CubicSegment{{10, 0}, {10, -4}, {0, -4}, {0, 0}}));
	// The midpoint of a symmetric arch: 0.75 of the control height.
	const Point2D middle = segments[0].at(0.5);
	EXPECT_DOUBLE_EQ(middle.x, 5.0);
	EXPECT_DOUBLE_EQ(middle.y, 3.0);
	EXPECT_EQ(segments[0].at(0.0), (Point2D{0, 0}));
	EXPECT_EQ(segments[0].at(1.0), (Point2D{10, 0}));
	// A straight start and a curved end compose.
	const std::array<PathVertex, 2> mixed{vertex(0, 0), b};
	EXPECT_EQ(pathCubicSegments(mixed, false)[0], (CubicSegment{{0, 0}, {0, 0}, {10, 4}, {10, 0}}));
}

TEST(Path, XSplineVerticesDeriveTheirTangents)
{
	PathVertex middle = vertex(6, 6, PathStyle::XSpline);
	const std::array<PathVertex, 3> vertices{vertex(0, 0), middle, vertex(12, 0)};
	std::vector<CubicSegment> segments = pathCubicSegments(vertices, false);
	// The tangent is a sixth of the neighbor span: (12, 0) / 6 = (2, 0).
	EXPECT_EQ(segments[0].c2, (Point2D{4, 6}));
	EXPECT_EQ(segments[1].c1, (Point2D{8, 6}));
	// The weight scales it by 1 + weight.
	middle.splineWeight = 0.5;
	const std::array<PathVertex, 3> weighted{vertex(0, 0), middle, vertex(12, 0)};
	segments = pathCubicSegments(weighted, false);
	EXPECT_EQ(segments[0].c2, (Point2D{3, 6}));
	EXPECT_EQ(segments[1].c1, (Point2D{9, 6}));
	// An open path's end vertex uses itself for the missing neighbor.
	const std::array<PathVertex, 2> ends{vertex(0, 0, PathStyle::XSpline), vertex(6, 0, PathStyle::XSpline)};
	segments = pathCubicSegments(ends, false);
	EXPECT_EQ(segments[0].c1, (Point2D{1, 0}));
	EXPECT_EQ(segments[0].c2, (Point2D{5, 0}));
	// A closed path wraps to the other end.
	const std::array<PathVertex, 3> loop{vertex(0, 0, PathStyle::XSpline), vertex(6, 0), vertex(0, 6)};
	segments = pathCubicSegments(loop, true);
	EXPECT_EQ(segments[0].c1, (Point2D{1, -1}));
	EXPECT_EQ(segments[2].c2, (Point2D{-1, 1}));
}

TEST(Path, Flattens)
{
	EXPECT_TRUE(PathData().flattened().empty());
	const std::array<Point2D, 3> locations{Point2D{0, 0}, Point2D{10, 0}, Point2D{10, 10}};
	const PathData straight = PathData::fromLocations(locations, true);
	// A straight segment contributes its end point only, and a closed path returns to its start.
	const std::vector<Point2D> outline = straight.flattened(8);
	EXPECT_EQ(outline, (std::vector<Point2D>{{0, 0}, {10, 0}, {10, 10}, {0, 0}}));
	PathVertex a = vertex(0, 0, PathStyle::Bezier);
	a.outTangent = {0, 4};
	PathVertex b = vertex(10, 0, PathStyle::Bezier);
	b.inTangent = {0, 4};
	const std::array<PathVertex, 2> curve{a, b};
	const std::vector<Point2D> arch = PathData(curve, false).flattened(4);
	ASSERT_EQ(arch.size(), 5u);
	EXPECT_EQ(arch.front(), (Point2D{0, 0}));
	EXPECT_EQ(arch[2], (Point2D{5, 3}));
	EXPECT_EQ(arch.back(), (Point2D{10, 0}));
	// Zero steps count as one.
	EXPECT_EQ(PathData(curve, false).flattened(0).size(), 2u);
	const std::array<Point2D, 1> one{Point2D{3, 4}};
	EXPECT_EQ(PathData::fromLocations(one, true).flattened(), (std::vector<Point2D>{{3, 4}}));
}

TEST(Path, BlendsBetweenKeyframes)
{
	PathVertex a0 = vertex(0, 0, PathStyle::Bezier);
	a0.outTangent = {2, 0};
	PathVertex b0 = vertex(4, 8, PathStyle::Linear);
	b0.outTangent = {4, 4};
	b0.splineWeight = 1.0;
	const std::array<PathVertex, 2> left{a0, vertex(10, 0)};
	const std::array<PathVertex, 2> right{b0, vertex(20, 10)};
	const PathData from(left, false);
	const PathData to(right, true);
	const PathData quarter = from.interpolated(to, 0.25);
	EXPECT_EQ(quarter.vertices()[0].location, (Point2D{1, 2}));
	EXPECT_EQ(quarter.vertices()[0].outTangent, (Point2D{2.5, 1}));
	EXPECT_EQ(quarter.vertices()[1].location, (Point2D{12.5, 2.5}));
	// The style, the weight, and the closed flag step at the half.
	EXPECT_EQ(quarter.vertices()[0].style, PathStyle::Bezier);
	EXPECT_DOUBLE_EQ(quarter.vertices()[0].splineWeight, 0.0);
	EXPECT_FALSE(quarter.closed());
	const PathData late = from.interpolated(to, 0.75);
	EXPECT_EQ(late.vertices()[0].style, PathStyle::Linear);
	EXPECT_DOUBLE_EQ(late.vertices()[0].splineWeight, 1.0);
	EXPECT_TRUE(late.closed());
	EXPECT_EQ(from.interpolated(to, 0.0), from);
	EXPECT_EQ(from.interpolated(to, 1.0), to);
	// A different vertex count steps whole.
	const PathData longer = to.inserting(vertex(1, 1), 0);
	EXPECT_EQ(from.interpolated(longer, 0.4), from);
	EXPECT_EQ(from.interpolated(longer, 0.5), longer);
}

TEST(Path, Codec)
{
	PathVertex a = vertex(1.5, -2.25, PathStyle::Bezier);
	a.inTangent = {-1, 0.5};
	a.outTangent = {1, -0.5};
	PathVertex b = vertex(10, 20, PathStyle::XSpline);
	b.splineWeight = 0.75;
	const std::array<PathVertex, 3> vertices{a, b, vertex(3, 4, PathStyle::Rectangle)};
	const PathData path(vertices, true);
	const Bytes bytes = path.encode();
	PathData decoded;
	Error e = PathData::decode(bytes, decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(decoded, path);
	e = PathData::decode(PathData().encode(), decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(decoded.empty());
	decoded = path;
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		EXPECT_EQ(PathData::decode(std::span<const std::uint8_t>(bytes.data(), length), decoded).code(), ErrorCode::Codec) << "length " << length;
		EXPECT_TRUE(decoded.empty());
	}
	Bytes future = bytes;
	future[4] = 2;
	EXPECT_EQ(PathData::decode(future, decoded).code(), ErrorCode::Codec);
	Bytes badClosed = bytes;
	badClosed[8] = 2;
	EXPECT_EQ(PathData::decode(badClosed, decoded).code(), ErrorCode::Codec);
	Bytes badStyle = bytes;
	badStyle.back() = 9;
	EXPECT_EQ(PathData::decode(badStyle, decoded).code(), ErrorCode::Codec);
	Bytes tooMany = PathData().encode();
	tooMany[12] = 0xFF;
	EXPECT_EQ(PathData::decode(tooMany, decoded).code(), ErrorCode::Codec);
}

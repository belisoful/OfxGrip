// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Path.hpp
	@brief An editable path as a value: vertices with tangents and a closed flag, and its cubic
	       Bezier segments.

	Introduced in OfxGrip 0.5.0. A path backs an on-screen shape whose vertex count changes at
	run time, which a fixed set of point parameters cannot express. The whole list is one
	value. A vertex carries a location, an in tangent and an out tangent held as vectors from
	the location, a spline weight, and a style. Tangents held as vectors move with the vertex.
	The vertex fields and the style numbers match FxPlug's `FxVertex` and `FxPathStyle`, so a
	path means the same shape in FxGrip and OfxGrip.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <span>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! How a vertex shapes the segments that meet at it. */
enum class PathStyle : std::uint8_t {
	Linear = 0,        /*!< Straight segments. */
	Bezier = 1,        /*!< The vertex's own tangents. */
	XSpline = 2,       /*!< A Catmull-Rom tangent from the neighbors, scaled by 1 + the weight. */
	SuperEllipse = 3,  /*!< The vertex's own tangents. */
	Rectangle = 4,     /*!< Straight segments. */
};

/*! One path vertex. */
struct PathVertex {
	Point2D location;
	Point2D inTangent;   /*!< The incoming control point, as a vector from the location. */
	Point2D outTangent;  /*!< The outgoing control point, as a vector from the location. */
	double splineWeight{0};
	PathStyle style{PathStyle::Linear};
	constexpr bool operator==(const PathVertex&) const noexcept = default;
};

/*! One cubic Bezier segment between two vertices. `p0` and `p3` lie on the path. A straight
    segment has `c1 == p0` and `c2 == p3`. */
struct CubicSegment {
	Point2D p0;
	Point2D c1;
	Point2D c2;
	Point2D p3;
	constexpr bool operator==(const CubicSegment&) const noexcept = default;
	/*! The point at `t` in [0, 1]. */
	Point2D at(double t) const noexcept;
};

/*! The number of segments a path has: the vertex count when closed, one fewer when open, and
    none for fewer than two vertices. */
std::size_t pathSegmentCount(std::size_t vertexCount, bool closed) noexcept;

/*! The cubic segments of a vertex list. A segment's shape comes from its start vertex's
    outgoing side and its end vertex's incoming side, so mixed styles compose. */
std::vector<CubicSegment> pathCubicSegments(std::span<const PathVertex> vertices, bool closed);

/*! An immutable path. An edit returns a new path. */
class PathData {
public:
	/*! An empty open path. */
	PathData() = default;
	PathData(std::span<const PathVertex> vertices, bool closed);
	/*! A linear path through locations: no tangents, no weight. */
	static PathData fromLocations(std::span<const Point2D> locations, bool closed);

	const std::vector<PathVertex>& vertices() const noexcept { return vertices_; }
	bool closed() const noexcept { return closed_; }
	bool empty() const noexcept { return vertices_.empty(); }

	/*! A path with a vertex inserted at an index, clamped to the vertex count. */
	PathData inserting(const PathVertex& vertex, std::size_t index) const;
	/*! A path without the vertex at an index. An index out of range returns this path. */
	PathData removing(std::size_t index) const;
	/*! A path with the vertex at an index replaced. An index out of range returns this path. */
	PathData replacing(std::size_t index, const PathVertex& vertex) const;
	/*! A path with one location changed. The tangents move with it. */
	PathData replacingLocation(std::size_t index, Point2D location) const;
	/*! A path with every location offset. */
	PathData translated(Point2D delta) const;
	/*! A path with the closed flag set. */
	PathData withClosed(bool closed) const;

	/*! The path's cubic segments. */
	std::vector<CubicSegment> cubicSegments() const { return pathCubicSegments(vertices_, closed_); }
	/*! The path flattened to a polyline: `stepsPerSegment` points per curved segment and the
	    end points of a straight one. A closed path repeats its first point at the end. */
	std::vector<Point2D> flattened(unsigned stepsPerSegment = 16) const;

	/*! The path blended toward `right`, for a value between two keyframes. Paths with the
	    same vertex count blend vertex by vertex, and the style, closed flag, and weight step
	    at the half. Paths with different counts step at the half. */
	PathData interpolated(const PathData& right, double weight) const;

	/*! The path as bytes: the same bytes on every platform. */
	Bytes encode() const;
	/*! Reads a path `encode` wrote. Other bytes leave `out` empty and return an error. */
	static Error decode(std::span<const std::uint8_t> bytes, PathData& out);

	bool operator==(const PathData&) const = default;

private:
	std::vector<PathVertex> vertices_;
	bool closed_{false};
};

OFXGRIP_NAMESPACE_END

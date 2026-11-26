// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Path.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kMagic = "OGPD";
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaximumVertices = 1u << 20;

constexpr Point2D add(Point2D a, Point2D b) noexcept
{
	return {a.x + b.x, a.y + b.y};
}

constexpr Point2D scaled(Point2D a, double s) noexcept
{
	return {a.x * s, a.y * s};
}

constexpr Point2D mixed(Point2D a, Point2D b, double weight) noexcept
{
	return {(1.0 - weight) * a.x + weight * b.x, (1.0 - weight) * a.y + weight * b.y};
}

struct Controls {
	Point2D out;
	Point2D in;
};

/*! The outgoing and incoming control vectors of one vertex, relative to its location. */
Controls controlsOf(std::span<const PathVertex> vertices, bool closed, std::size_t index) noexcept
{
	const PathVertex& vertex = vertices[index];
	const std::size_t count = vertices.size();
	switch (vertex.style) {
	case PathStyle::Linear:
	case PathStyle::Rectangle: return {};
	case PathStyle::XSpline: {
		Point2D previous = vertex.location;
		Point2D next = vertex.location;
		if (closed) {
			previous = vertices[(index + count - 1) % count].location;
			next = vertices[(index + 1) % count].location;
		} else {
			if (index > 0) {
				previous = vertices[index - 1].location;
			}
			if (index + 1 < count) {
				next = vertices[index + 1].location;
			}
		}
		// The Catmull-Rom tangent is a sixth of the neighbor span.
		const Point2D tangent = scaled({next.x - previous.x, next.y - previous.y}, (1.0 / 6.0) * (1.0 + vertex.splineWeight));
		return {tangent, scaled(tangent, -1.0)};
	}
	case PathStyle::Bezier:
	case PathStyle::SuperEllipse: break;
	}
	return {vertex.outTangent, vertex.inTangent};
}

bool straight(const CubicSegment& segment) noexcept
{
	return segment.c1 == segment.p0 && segment.c2 == segment.p3;
}

}  // namespace

Point2D CubicSegment::at(double t) const noexcept
{
	const double u = 1.0 - t;
	const double b0 = u * u * u;
	const double b1 = 3.0 * u * u * t;
	const double b2 = 3.0 * u * t * t;
	const double b3 = t * t * t;
	return {b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p3.x, b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p3.y};
}

std::size_t pathSegmentCount(std::size_t vertexCount, bool closed) noexcept
{
	if (vertexCount < 2) {
		return 0;
	}
	return closed ? vertexCount : vertexCount - 1;
}

std::vector<CubicSegment> pathCubicSegments(std::span<const PathVertex> vertices, bool closed)
{
	const std::size_t count = vertices.size();
	const std::size_t segmentCount = pathSegmentCount(count, closed);
	std::vector<CubicSegment> segments;
	segments.reserve(segmentCount);
	for (std::size_t index = 0; index < segmentCount; ++index) {
		const std::size_t end = (index + 1) % count;
		CubicSegment segment;
		segment.p0 = vertices[index].location;
		segment.p3 = vertices[end].location;
		segment.c1 = add(segment.p0, controlsOf(vertices, closed, index).out);
		segment.c2 = add(segment.p3, controlsOf(vertices, closed, end).in);
		segments.push_back(segment);
	}
	return segments;
}

PathData::PathData(std::span<const PathVertex> vertices, bool closed) : vertices_(vertices.begin(), vertices.end()), closed_(closed) {}

PathData PathData::fromLocations(std::span<const Point2D> locations, bool closed)
{
	PathData out;
	out.closed_ = closed;
	out.vertices_.reserve(locations.size());
	for (const Point2D& location : locations) {
		PathVertex vertex;
		vertex.location = location;
		out.vertices_.push_back(vertex);
	}
	return out;
}

PathData PathData::inserting(const PathVertex& vertex, std::size_t index) const
{
	PathData out = *this;
	out.vertices_.insert(out.vertices_.begin() + static_cast<std::ptrdiff_t>(std::min(index, vertices_.size())), vertex);
	return out;
}

PathData PathData::removing(std::size_t index) const
{
	PathData out = *this;
	if (index < vertices_.size()) {
		out.vertices_.erase(out.vertices_.begin() + static_cast<std::ptrdiff_t>(index));
	}
	return out;
}

PathData PathData::replacing(std::size_t index, const PathVertex& vertex) const
{
	PathData out = *this;
	if (index < vertices_.size()) {
		out.vertices_[index] = vertex;
	}
	return out;
}

PathData PathData::replacingLocation(std::size_t index, Point2D location) const
{
	PathData out = *this;
	if (index < vertices_.size()) {
		out.vertices_[index].location = location;
	}
	return out;
}

PathData PathData::translated(Point2D delta) const
{
	PathData out = *this;
	for (PathVertex& vertex : out.vertices_) {
		vertex.location = add(vertex.location, delta);
	}
	return out;
}

PathData PathData::withClosed(bool closed) const
{
	PathData out = *this;
	out.closed_ = closed;
	return out;
}

std::vector<Point2D> PathData::flattened(unsigned stepsPerSegment) const
{
	std::vector<Point2D> points;
	if (vertices_.empty()) {
		return points;
	}
	const unsigned steps = std::max(stepsPerSegment, 1u);
	points.push_back(vertices_.front().location);
	for (const CubicSegment& segment : cubicSegments()) {
		if (straight(segment)) {
			points.push_back(segment.p3);
			continue;
		}
		for (unsigned step = 1; step < steps; ++step) {
			points.push_back(segment.at(static_cast<double>(step) / static_cast<double>(steps)));
		}
		points.push_back(segment.p3);
	}
	return points;
}

PathData PathData::interpolated(const PathData& right, double weight) const
{
	if (vertices_.size() != right.vertices_.size()) {
		return weight < 0.5 ? *this : right;
	}
	const PathData& stepped = weight < 0.5 ? *this : right;
	PathData out = stepped;
	for (std::size_t i = 0; i < vertices_.size(); ++i) {
		out.vertices_[i].location = mixed(vertices_[i].location, right.vertices_[i].location, weight);
		out.vertices_[i].inTangent = mixed(vertices_[i].inTangent, right.vertices_[i].inTangent, weight);
		out.vertices_[i].outTangent = mixed(vertices_[i].outTangent, right.vertices_[i].outTangent, weight);
	}
	return out;
}

Bytes PathData::encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u32(kVersion);
	w.u8(closed_ ? 1 : 0);
	w.u32(static_cast<std::uint32_t>(vertices_.size()));
	for (const PathVertex& vertex : vertices_) {
		w.f64(vertex.location.x);
		w.f64(vertex.location.y);
		w.f64(vertex.inTangent.x);
		w.f64(vertex.inTangent.y);
		w.f64(vertex.outTangent.x);
		w.f64(vertex.outTangent.y);
		w.f64(vertex.splineWeight);
		w.u8(static_cast<std::uint8_t>(vertex.style));
	}
	return out;
}

Error PathData::decode(std::span<const std::uint8_t> bytes, PathData& out)
{
	out = PathData{};
	const auto malformed = []() { return Error::make(ErrorCode::Codec, "The bytes are not a path"); };
	ByteReader r(bytes);
	std::uint32_t version = 0;
	std::uint8_t closed = 0;
	std::uint32_t count = 0;
	if (!r.magic(kMagic) || !r.u32(version)) {
		return malformed();
	}
	if (version != kVersion) {
		return Error::make(ErrorCode::Codec, "The path has a version this build does not read").with("version", std::to_string(version));
	}
	if (!r.u8(closed) || closed > 1 || !r.u32(count) || count > kMaximumVertices) {
		return malformed();
	}
	PathData decoded;
	decoded.closed_ = closed == 1;
	// A vertex is 57 bytes, so a count the input cannot hold fails on its first missing read
	// and never reserves for it.
	for (std::uint32_t i = 0; i < count; ++i) {
		PathVertex vertex;
		std::uint8_t style = 0;
		if (!r.f64(vertex.location.x) || !r.f64(vertex.location.y) || !r.f64(vertex.inTangent.x) || !r.f64(vertex.inTangent.y) || !r.f64(vertex.outTangent.x) || !r.f64(vertex.outTangent.y)
		    || !r.f64(vertex.splineWeight) || !r.u8(style) || style > static_cast<std::uint8_t>(PathStyle::Rectangle)) {
			return malformed();
		}
		vertex.style = static_cast<PathStyle>(style);
		decoded.vertices_.push_back(vertex);
	}
	out = std::move(decoded);
	return Error::none();
}

OFXGRIP_NAMESPACE_END

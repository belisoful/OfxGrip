// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Curve.hpp
	@brief Cubic curves for a curves control: control points, lookup tables, and a keyed set
	       that interpolates between keyframes.

	Introduced in OfxGrip 0.5.0. A curve carries sorted control points, an x-axis domain, and a
	role that fixes its neutral shape. The lookup-table builders evaluate a Fritsch-Carlson
	monotone cubic spline, so a curve never overshoots its points. The arithmetic follows
	FxGrip's `FxGripCurveLUT` step for step: the same points give the same table in both
	frameworks to within the last bit of a float.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The x-axis domain of a curve. The numbers are stable and match FxGrip's. */
enum class CurveDomain : std::uint8_t {
	Linear = 0,    /*!< x clamps to [0, 1], and the end values hold outside the points. */
	Circular = 1,  /*!< x is a circle of period 1 (a hue selector). The seam at 0 and 1 is smooth. */
};

/*! How a filter consumes a curve, which fixes its neutral shape. The numbers are stable and
    match FxGrip's. */
enum class CurveRole : std::uint8_t {
	Remap = 0,           /*!< Neutral on the diagonal. */
	Shift = 1,           /*!< Neutral flat at 0.5: no shift. */
	MultiplierHalf = 2,  /*!< Neutral flat at 0.5, and the kernel applies 2×. */
	MultiplierOne = 3,   /*!< Neutral flat at 1. */
};

/*! One curve point as the float pair a kernel's curve input takes. */
struct CurvePoint {
	float x{0};
	float y{0};
	constexpr bool operator==(const CurvePoint&) const noexcept = default;
};

/*! Fills a lookup table from control points with monotone cubic interpolation. Entry `k` of
    `n` is the curve at `k / (n - 1)`. The points are sorted by x, and a duplicate x is
    dropped (the first one stays). Outside the point range the end values hold. Fewer than
    two points give an identity ramp. Two or more points with one distinct x give a
    constant. */
void buildCurveLut(std::span<const CurvePoint> points, std::span<float> lut);

/*! The same over a circular domain of period 1: x folds into [0, 1), the interval after the
    last point wraps to the first, and every slope uses both neighbors. The first and last
    entries are equal. */
void buildCurveLutPeriodic(std::span<const CurvePoint> points, std::span<float> lut);

/*! One immutable curve: sorted control points with a domain and a role. */
class CurveData {
public:
	/*! The neutral remap curve in the linear domain: the diagonal. */
	CurveData();
	/*! A curve from control points. y clamps to [0, 1]. x clamps to [0, 1] in the linear
	    domain and folds into [0, 1) in the circular domain, where 1 stays 1. The points sort
	    by x, and a duplicate x is dropped (the first one stays). A point that is not finite
	    is dropped. */
	CurveData(std::span<const Point2D> points, CurveRole role, CurveDomain domain);
	/*! The role's neutral curve: the diagonal for a linear remap, no points for a circular
	    remap (the builder's identity ramp), and a flat line at the neutral value otherwise. */
	static CurveData identity(CurveRole role, CurveDomain domain);

	CurveDomain domain() const noexcept { return domain_; }
	CurveRole role() const noexcept { return role_; }
	const std::vector<Point2D>& points() const noexcept { return points_; }
	/*! Whether the points equal the role's neutral shape. */
	bool isIdentity() const;
	/*! The points as float pairs, the layout a kernel's curve input takes. */
	std::vector<CurvePoint> curvePoints() const;
	/*! Evaluates the curve into a lookup table with the domain's builder. */
	void buildLut(std::span<float> lut) const;
	/*! The curve at one x, by the same spline. */
	double evaluate(double x) const;

	/*! The curve blended toward `right`. Curves with the same point count, role, and domain
	    blend point by point. Any other pair blends the two evaluated curves on a 33-sample
	    grid, so a point added between two keyframes cannot drop the curve. The result takes
	    this curve's role and domain. */
	CurveData interpolated(const CurveData& right, double weight) const;

	bool operator==(const CurveData&) const = default;

private:
	CurveDomain domain_{CurveDomain::Linear};
	CurveRole role_{CurveRole::Remap};
	std::vector<Point2D> points_;
};

/*! A filter's curves, keyed by channel name (`luma`, `red`, `hueVsHue`, …). An absent key
    means the channel's neutral curve, so only edited curves are stored. */
class CurveSet {
public:
	/*! The stored curve, or null when the channel is neutral. */
	const CurveData* curve(std::string_view key) const noexcept;
	/*! Stores a curve. An identity curve removes the key. */
	void setCurve(std::string_view key, const CurveData& curve);
	/*! Removes a key. */
	void removeCurve(std::string_view key);
	/*! The keys with stored curves, sorted. */
	std::vector<std::string> keys() const;
	bool empty() const noexcept { return curves_.empty(); }
	std::size_t size() const noexcept { return curves_.size(); }

	/*! The set blended toward `right`, for a value between two keyframes. A curve stored on
	    one side only blends toward its role's neutral curve. */
	CurveSet interpolated(const CurveSet& right, double weight) const;

	/*! The set as bytes: the same bytes on every platform. */
	Bytes encode() const;
	/*! Reads a set `encode` wrote. Bytes that are not a curve set leave `out` empty and
	    return an error. */
	static Error decode(std::span<const std::uint8_t> bytes, CurveSet& out);

	bool operator==(const CurveSet&) const = default;

private:
	std::map<std::string, CurveData, std::less<>> curves_;
};

OFXGRIP_NAMESPACE_END

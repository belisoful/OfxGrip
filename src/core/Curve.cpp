// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Curve.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>
#include <array>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kCurveSetMagic = "OGCS";
constexpr std::uint32_t kCurveSetVersion = 1;
// The grid a mismatched pair of curves blends on. Odd, so x = 0.5 is a sample.
constexpr std::size_t kBlendSamples = 33;
constexpr std::uint32_t kMaximumCurves = 4096;
constexpr std::uint32_t kMaximumPoints = 65536;

void identityRamp(std::span<float> lut) noexcept
{
	const std::size_t n = lut.size();
	for (std::size_t i = 0; i < n; ++i) {
		lut[i] = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
	}
}

/*! Copies the points, folds x into [0, 1) when asked, sorts by x, and drops a duplicate x. */
std::vector<CurvePoint> prepared(std::span<const CurvePoint> points, bool fold)
{
	std::vector<CurvePoint> sorted(points.begin(), points.end());
	if (fold) {
		for (CurvePoint& point : sorted) {
			point.x -= std::floor(point.x);
		}
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
	std::vector<CurvePoint> unique;
	unique.reserve(sorted.size());
	for (const CurvePoint& point : sorted) {
		if (unique.empty() || point.x > unique.back().x + 1e-9f) {
			unique.push_back(point);
		}
	}
	return unique;
}

double hermite(double t, double y0, double y1, double h, double slope0, double slope1) noexcept
{
	const double t2 = t * t;
	const double t3 = t2 * t;
	const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
	const double h10 = t3 - 2.0 * t2 + t;
	const double h01 = -2.0 * t3 + 3.0 * t2;
	const double h11 = t3 - t2;
	return h00 * y0 + h10 * h * slope0 + h01 * y1 + h11 * h * slope1;
}

/*! The Fritsch-Carlson slope at a point between two intervals. */
double monotoneSlope(double hBefore, double deltaBefore, double hAfter, double deltaAfter) noexcept
{
	if (deltaBefore * deltaAfter <= 0.0) {
		return 0.0;
	}
	const double w1 = 2.0 * hAfter + hBefore;
	const double w2 = hAfter + 2.0 * hBefore;
	return (w1 + w2) / (w1 / deltaBefore + w2 / deltaAfter);
}

double neutralValue(CurveRole role) noexcept
{
	switch (role) {
	case CurveRole::Shift:
	case CurveRole::MultiplierHalf: return 0.5;
	case CurveRole::MultiplierOne: return 1.0;
	case CurveRole::Remap: break;
	}
	return 0.0;
}

/*! A curve ready to evaluate: the sorted points with their interval widths and slopes. */
struct Spline {
	enum class Shape { Identity, Constant, Cubic };
	Shape shape{Shape::Identity};
	bool periodic{false};
	std::vector<CurvePoint> pts;
	std::vector<double> h;
	std::vector<double> slope;

	Spline(std::span<const CurvePoint> points, bool wraps) : periodic(wraps)
	{
		if (points.size() < 2) {
			return;
		}
		pts = prepared(points, periodic);
		const std::size_t m = pts.size();
		if (m < 2) {
			shape = Shape::Constant;
			return;
		}
		shape = Shape::Cubic;
		// A periodic curve has m intervals: the last one crosses the seam back to point 0.
		const std::size_t intervals = periodic ? m : m - 1;
		h.resize(intervals);
		std::vector<double> delta(intervals);
		slope.resize(m);
		for (std::size_t i = 0; i < intervals; ++i) {
			const std::size_t j = (i + 1) % m;
			// The differences are taken in float, as FxGrip's builder takes them.
			double dx = static_cast<double>(pts[j].x - pts[i].x);
			if (periodic && i == m - 1) {
				dx += 1.0;
			}
			h[i] = dx;
			delta[i] = static_cast<double>(pts[j].y - pts[i].y) / h[i];
		}
		if (periodic) {
			for (std::size_t i = 0; i < m; ++i) {
				const std::size_t p = (i + m - 1) % m;
				slope[i] = monotoneSlope(h[p], delta[p], h[i], delta[i]);
			}
			return;
		}
		slope[0] = delta[0];
		slope[m - 1] = delta[m - 2];
		for (std::size_t i = 1; i + 1 < m; ++i) {
			slope[i] = monotoneSlope(h[i - 1], delta[i - 1], h[i], delta[i]);
		}
	}

	double at(double x) const noexcept
	{
		if (shape == Shape::Identity) {
			return x;
		}
		if (shape == Shape::Constant) {
			return pts[0].y;
		}
		const std::size_t m = pts.size();
		if (periodic) {
			x -= std::floor(x);
			std::size_t i = 0;
			double t = 0.0;
			if (x < static_cast<double>(pts[0].x) || x >= static_cast<double>(pts[m - 1].x)) {
				i = m - 1;
				const double wrapped = x >= static_cast<double>(pts[m - 1].x) ? x : x + 1.0;
				t = (wrapped - static_cast<double>(pts[m - 1].x)) / h[i];
			} else {
				while (i + 1 < m && x >= static_cast<double>(pts[i + 1].x)) {
					++i;
				}
				t = (x - static_cast<double>(pts[i].x)) / h[i];
			}
			const std::size_t j = (i + 1) % m;
			return hermite(t, pts[i].y, pts[j].y, h[i], slope[i], slope[j]);
		}
		if (x <= static_cast<double>(pts[0].x)) {
			return pts[0].y;
		}
		if (x >= static_cast<double>(pts[m - 1].x)) {
			return pts[m - 1].y;
		}
		std::size_t i = 0;
		while (i + 1 < m && x > static_cast<double>(pts[i + 1].x)) {
			++i;
		}
		const double t = (x - static_cast<double>(pts[i].x)) / h[i];
		return hermite(t, pts[i].y, pts[i + 1].y, h[i], slope[i], slope[i + 1]);
	}

	void fill(std::span<float> lut) const noexcept
	{
		const std::size_t n = lut.size();
		if (shape == Shape::Identity) {
			identityRamp(lut);
			return;
		}
		for (std::size_t k = 0; k < n; ++k) {
			lut[k] = static_cast<float>(at(n > 1 ? static_cast<double>(k) / static_cast<double>(n - 1) : 0.0));
		}
	}
};

}  // namespace

void buildCurveLut(std::span<const CurvePoint> points, std::span<float> lut)
{
	Spline(points, false).fill(lut);
}

void buildCurveLutPeriodic(std::span<const CurvePoint> points, std::span<float> lut)
{
	Spline(points, true).fill(lut);
}

CurveData::CurveData() : points_{{0.0, 0.0}, {1.0, 1.0}} {}

CurveData::CurveData(std::span<const Point2D> points, CurveRole role, CurveDomain domain) : domain_(domain), role_(role)
{
	std::vector<Point2D> sanitized;
	sanitized.reserve(points.size());
	for (const Point2D& point : points) {
		if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
			continue;
		}
		double x = point.x;
		if (domain == CurveDomain::Circular) {
			// x = 1 stays: it closes the loop. Folded onto 0 it would collapse a flat two-point
			// neutral to one point, which the builder reads as no curve.
			if (x != 1.0) {
				x -= std::floor(x);
			}
		} else {
			x = std::clamp(x, 0.0, 1.0);
		}
		sanitized.push_back({x, std::clamp(point.y, 0.0, 1.0)});
	}
	std::stable_sort(sanitized.begin(), sanitized.end(), [](const Point2D& a, const Point2D& b) { return a.x < b.x; });
	points_.reserve(sanitized.size());
	for (const Point2D& point : sanitized) {
		if (points_.empty() || point.x > points_.back().x + 1e-9) {
			points_.push_back(point);
		}
	}
}

CurveData CurveData::identity(CurveRole role, CurveDomain domain)
{
	if (role == CurveRole::Remap) {
		if (domain == CurveDomain::Circular) {
			// The diagonal cannot close on the circle. The builder's identity ramp is the neutral.
			return CurveData({}, role, domain);
		}
		const std::array<Point2D, 2> diagonal{Point2D{0.0, 0.0}, Point2D{1.0, 1.0}};
		return CurveData(diagonal, role, domain);
	}
	const double y = neutralValue(role);
	const std::array<Point2D, 2> flat{Point2D{0.0, y}, Point2D{1.0, y}};
	return CurveData(flat, role, domain);
}

bool CurveData::isIdentity() const
{
	const CurveData neutral = identity(role_, domain_);
	if (points_.size() != neutral.points_.size()) {
		return false;
	}
	for (std::size_t i = 0; i < points_.size(); ++i) {
		if (std::abs(points_[i].x - neutral.points_[i].x) > 1e-9 || std::abs(points_[i].y - neutral.points_[i].y) > 1e-9) {
			return false;
		}
	}
	return true;
}

std::vector<CurvePoint> CurveData::curvePoints() const
{
	std::vector<CurvePoint> out;
	out.reserve(points_.size());
	for (const Point2D& point : points_) {
		out.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
	}
	return out;
}

void CurveData::buildLut(std::span<float> lut) const
{
	const std::vector<CurvePoint> points = curvePoints();
	Spline(points, domain_ == CurveDomain::Circular).fill(lut);
}

double CurveData::evaluate(double x) const
{
	const std::vector<CurvePoint> points = curvePoints();
	const bool periodic = domain_ == CurveDomain::Circular;
	return Spline(points, periodic).at(periodic ? x : std::clamp(x, 0.0, 1.0));
}

CurveData CurveData::interpolated(const CurveData& right, double weight) const
{
	if (points_.size() == right.points_.size() && role_ == right.role_ && domain_ == right.domain_) {
		std::vector<Point2D> blended;
		blended.reserve(points_.size());
		for (std::size_t i = 0; i < points_.size(); ++i) {
			blended.push_back({(1.0 - weight) * points_[i].x + weight * right.points_[i].x, (1.0 - weight) * points_[i].y + weight * right.points_[i].y});
		}
		return CurveData(blended, role_, domain_);
	}
	std::array<float, kBlendSamples> leftLut{};
	std::array<float, kBlendSamples> rightLut{};
	buildLut(leftLut);
	right.buildLut(rightLut);
	std::array<Point2D, kBlendSamples> samples{};
	for (std::size_t i = 0; i < kBlendSamples; ++i) {
		samples[i] = {static_cast<double>(i) / static_cast<double>(kBlendSamples - 1), (1.0 - weight) * static_cast<double>(leftLut[i]) + weight * static_cast<double>(rightLut[i])};
	}
	return CurveData(samples, role_, domain_);
}

const CurveData* CurveSet::curve(std::string_view key) const noexcept
{
	const auto it = curves_.find(key);
	return it != curves_.end() ? &it->second : nullptr;
}

void CurveSet::setCurve(std::string_view key, const CurveData& curve)
{
	if (curve.isIdentity()) {
		removeCurve(key);
		return;
	}
	curves_.insert_or_assign(std::string(key), curve);
}

void CurveSet::removeCurve(std::string_view key)
{
	const auto it = curves_.find(key);
	if (it != curves_.end()) {
		curves_.erase(it);
	}
}

std::vector<std::string> CurveSet::keys() const
{
	std::vector<std::string> out;
	out.reserve(curves_.size());
	for (const auto& [key, curve] : curves_) {
		out.push_back(key);
	}
	return out;
}

CurveSet CurveSet::interpolated(const CurveSet& right, double weight) const
{
	CurveSet out;
	for (const auto& [key, leftCurve] : curves_) {
		const CurveData* rightCurve = right.curve(key);
		out.setCurve(key, leftCurve.interpolated(rightCurve != nullptr ? *rightCurve : CurveData::identity(leftCurve.role(), leftCurve.domain()), weight));
	}
	for (const auto& [key, rightCurve] : right.curves_) {
		if (curves_.find(key) == curves_.end()) {
			out.setCurve(key, CurveData::identity(rightCurve.role(), rightCurve.domain()).interpolated(rightCurve, weight));
		}
	}
	return out;
}

Bytes CurveSet::encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kCurveSetMagic);
	w.u32(kCurveSetVersion);
	w.u32(static_cast<std::uint32_t>(curves_.size()));
	for (const auto& [key, curve] : curves_) {
		w.str(key);
		w.u8(static_cast<std::uint8_t>(curve.domain()));
		w.u8(static_cast<std::uint8_t>(curve.role()));
		w.u32(static_cast<std::uint32_t>(curve.points().size()));
		for (const Point2D& point : curve.points()) {
			w.f64(point.x);
			w.f64(point.y);
		}
	}
	return out;
}

Error CurveSet::decode(std::span<const std::uint8_t> bytes, CurveSet& out)
{
	out = CurveSet{};
	const auto malformed = []() { return Error::make(ErrorCode::Codec, "The bytes are not a curve set"); };
	ByteReader r(bytes);
	if (!r.magic(kCurveSetMagic)) {
		return malformed();
	}
	std::uint32_t version = 0;
	std::uint32_t count = 0;
	if (!r.u32(version) || !r.u32(count)) {
		return malformed();
	}
	if (version != kCurveSetVersion) {
		return Error::make(ErrorCode::Codec, "The curve set has a version this build does not read").with("version", std::to_string(version));
	}
	if (count > kMaximumCurves) {
		return malformed();
	}
	CurveSet decoded;
	for (std::uint32_t c = 0; c < count; ++c) {
		std::string key;
		std::uint8_t domain = 0;
		std::uint8_t role = 0;
		std::uint32_t pointCount = 0;
		if (!r.str(key) || !r.u8(domain) || !r.u8(role) || !r.u32(pointCount)) {
			return malformed();
		}
		if (domain > static_cast<std::uint8_t>(CurveDomain::Circular) || role > static_cast<std::uint8_t>(CurveRole::MultiplierOne) || pointCount > kMaximumPoints) {
			return malformed();
		}
		std::vector<Point2D> points(pointCount);
		for (Point2D& point : points) {
			if (!r.f64(point.x) || !r.f64(point.y)) {
				return malformed();
			}
		}
		decoded.setCurve(key, CurveData(points, static_cast<CurveRole>(role), static_cast<CurveDomain>(domain)));
	}
	out = std::move(decoded);
	return Error::none();
}

OFXGRIP_NAMESPACE_END

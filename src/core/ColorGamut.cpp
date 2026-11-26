// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/ColorGamut.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr double kD65X = 0.3127;
constexpr double kD65Y = 0.3290;
constexpr double kAcesWhiteX = 0.32168;
constexpr double kAcesWhiteY = 0.33767;

/*! The CIE XYZ of a chromaticity at unit luminance. */
ColorVector xyzFromChromaticity(double x, double y) noexcept
{
	return {x / y, 1.0, (1.0 - x - y) / y};
}

ColorMatrix columns(const ColorVector& a, const ColorVector& b, const ColorVector& c) noexcept
{
	return ColorMatrix{{a[0], b[0], c[0], a[1], b[1], c[1], a[2], b[2], c[2]}};
}

ColorMatrix diagonal(const ColorVector& d) noexcept
{
	return ColorMatrix{{d[0], 0, 0, 0, d[1], 0, 0, 0, d[2]}};
}

constexpr ColorMatrix kBradford{{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296}};

// SMPTE ST 2084.
constexpr double kPqM1 = 2610.0 / 16384.0;
constexpr double kPqM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kPqC1 = 3424.0 / 4096.0;
constexpr double kPqC2 = 2413.0 / 4096.0 * 32.0;
constexpr double kPqC3 = 2392.0 / 4096.0 * 32.0;

// ITU-R BT.2100 HLG.
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892;
constexpr double kHlgC = 0.55991073;

/*! Applies a curve defined for non-negative input to a signed input, mirrored about zero. */
template <typename F>
double mirrored(double value, F curve) noexcept
{
	return value < 0.0 ? -curve(-value) : curve(value);
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
	});
}

struct PrimariesName {
	std::string_view name;
	ColorPrimaries primaries;
};

struct TransferName {
	std::string_view name;
	ColorTransfer transfer;
};

struct SpaceName {
	std::string_view name;
	ColorSpace space;
};

// The first name of each value is the one `toString` returns.
constexpr PrimariesName kPrimariesNames[] = {
    {"rec709", ColorPrimaries::Rec709},   {"srgb", ColorPrimaries::Rec709},     {"bt709", ColorPrimaries::Rec709},
    {"rec2020", ColorPrimaries::Rec2020}, {"bt2020", ColorPrimaries::Rec2020},  {"rec2100", ColorPrimaries::Rec2020},
    {"p3d65", ColorPrimaries::P3D65},     {"displayp3", ColorPrimaries::P3D65}, {"ap0", ColorPrimaries::AcesAp0},
    {"aces2065-1", ColorPrimaries::AcesAp0}, {"ap1", ColorPrimaries::AcesAp1},  {"acescg", ColorPrimaries::AcesAp1},
};

constexpr TransferName kTransferNames[] = {
    {"linear", ColorTransfer::Linear},    {"srgb", ColorTransfer::Srgb},        {"rec709", ColorTransfer::Rec709},
    {"gamma1.8", ColorTransfer::Gamma18}, {"gamma2.2", ColorTransfer::Gamma22}, {"gamma2.4", ColorTransfer::Gamma24},
    {"rec1886", ColorTransfer::Gamma24},  {"gamma2.6", ColorTransfer::Gamma26}, {"pq", ColorTransfer::Pq},
    {"st2084", ColorTransfer::Pq},        {"hlg", ColorTransfer::Hlg},
};

// The OpenFX 1.5 native colourspaces whose gamut and transfer this file implements.
constexpr SpaceName kSpaceNames[] = {
    {"srgb_display", {ColorPrimaries::Rec709, ColorTransfer::Srgb}},
    {"srgb_tx", {ColorPrimaries::Rec709, ColorTransfer::Srgb}},
    {"displayp3_display", {ColorPrimaries::P3D65, ColorTransfer::Srgb}},
    {"srgb_encoded_p3d65_tx", {ColorPrimaries::P3D65, ColorTransfer::Srgb}},
    {"srgb_encoded_ap1_tx", {ColorPrimaries::AcesAp1, ColorTransfer::Srgb}},
    {"rec1886_rec709_display", {ColorPrimaries::Rec709, ColorTransfer::Gamma24}},
    {"rec1886_rec2020_display", {ColorPrimaries::Rec2020, ColorTransfer::Gamma24}},
    {"rec2100_hlg_display", {ColorPrimaries::Rec2020, ColorTransfer::Hlg}},
    {"rec2100_pq_display", {ColorPrimaries::Rec2020, ColorTransfer::Pq}},
    {"st2084_p3d65_display", {ColorPrimaries::P3D65, ColorTransfer::Pq}},
    {"p3d65_display", {ColorPrimaries::P3D65, ColorTransfer::Gamma26}},
    {"g18_rec709_tx", {ColorPrimaries::Rec709, ColorTransfer::Gamma18}},
    {"g22_rec709_tx", {ColorPrimaries::Rec709, ColorTransfer::Gamma22}},
    {"g24_rec709_tx", {ColorPrimaries::Rec709, ColorTransfer::Gamma24}},
    {"g22_ap1_tx", {ColorPrimaries::AcesAp1, ColorTransfer::Gamma22}},
    {"camera_rec709", {ColorPrimaries::Rec709, ColorTransfer::Rec709}},
    {"lin_rec709_srgb", {ColorPrimaries::Rec709, ColorTransfer::Linear}},
    {"lin_rec2020", {ColorPrimaries::Rec2020, ColorTransfer::Linear}},
    {"lin_p3d65", {ColorPrimaries::P3D65, ColorTransfer::Linear}},
    {"ACES2065-1", {ColorPrimaries::AcesAp0, ColorTransfer::Linear}},
    {"ACEScg", {ColorPrimaries::AcesAp1, ColorTransfer::Linear}},
};

}  // namespace

ColorMatrix ColorMatrix::operator*(const ColorMatrix& right) const noexcept
{
	ColorMatrix out;
	for (std::size_t row = 0; row < 3; ++row) {
		for (std::size_t column = 0; column < 3; ++column) {
			double sum = 0.0;
			for (std::size_t k = 0; k < 3; ++k) {
				sum += at(row, k) * right.at(k, column);
			}
			out.m[row * 3 + column] = sum;
		}
	}
	return out;
}

ColorVector ColorMatrix::apply(const ColorVector& v) const noexcept
{
	return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

ColorMatrix ColorMatrix::inverse() const noexcept
{
	const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
	const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (determinant == 0.0 || !std::isfinite(determinant)) {
		return identity();
	}
	const double s = 1.0 / determinant;
	return ColorMatrix{{(e * i - f * h) * s, (c * h - b * i) * s, (b * f - c * e) * s, (f * g - d * i) * s, (a * i - c * g) * s, (c * d - a * f) * s, (d * h - e * g) * s, (b * g - a * h) * s, (a * e - b * d) * s}};
}

std::array<float, 9> ColorMatrix::columnMajorFloats() const noexcept
{
	std::array<float, 9> out{};
	for (std::size_t column = 0; column < 3; ++column) {
		for (std::size_t row = 0; row < 3; ++row) {
			out[column * 3 + row] = static_cast<float>(at(row, column));
		}
	}
	return out;
}

Chromaticities chromaticitiesFor(ColorPrimaries primaries) noexcept
{
	switch (primaries) {
	case ColorPrimaries::Rec2020: return {0.708, 0.292, 0.170, 0.797, 0.131, 0.046, kD65X, kD65Y};
	case ColorPrimaries::P3D65: return {0.680, 0.320, 0.265, 0.690, 0.150, 0.060, kD65X, kD65Y};
	case ColorPrimaries::AcesAp0: return {0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.0770, kAcesWhiteX, kAcesWhiteY};
	case ColorPrimaries::AcesAp1: return {0.713, 0.293, 0.165, 0.830, 0.128, 0.044, kAcesWhiteX, kAcesWhiteY};
	case ColorPrimaries::Rec709: break;
	}
	return {0.640, 0.330, 0.300, 0.600, 0.150, 0.060, kD65X, kD65Y};
}

ColorMatrix rgbToXyzMatrix(const Chromaticities& c) noexcept
{
	const ColorVector r = xyzFromChromaticity(c.xr, c.yr);
	const ColorVector g = xyzFromChromaticity(c.xg, c.yg);
	const ColorVector b = xyzFromChromaticity(c.xb, c.yb);
	const ColorVector white = xyzFromChromaticity(c.xw, c.yw);
	// The scale that makes equal RGB land on the white point.
	const ColorVector scale = columns(r, g, b).inverse().apply(white);
	return columns({r[0] * scale[0], r[1] * scale[0], r[2] * scale[0]}, {g[0] * scale[1], g[1] * scale[1], g[2] * scale[1]}, {b[0] * scale[2], b[1] * scale[2], b[2] * scale[2]});
}

ColorMatrix rgbToXyzMatrix(ColorPrimaries primaries) noexcept
{
	return rgbToXyzMatrix(chromaticitiesFor(primaries));
}

ColorMatrix xyzToRgbMatrix(ColorPrimaries primaries) noexcept
{
	return rgbToXyzMatrix(primaries).inverse();
}

ColorVector luminanceWeights(ColorPrimaries primaries) noexcept
{
	const ColorMatrix matrix = rgbToXyzMatrix(primaries);
	return {matrix.at(1, 0), matrix.at(1, 1), matrix.at(1, 2)};
}

ColorMatrix chromaticAdaptationMatrix(double fromX, double fromY, double toX, double toY) noexcept
{
	if (fromX == toX && fromY == toY) {
		return ColorMatrix::identity();
	}
	const ColorVector source = kBradford.apply(xyzFromChromaticity(fromX, fromY));
	const ColorVector target = kBradford.apply(xyzFromChromaticity(toX, toY));
	return kBradford.inverse() * diagonal({target[0] / source[0], target[1] / source[1], target[2] / source[2]}) * kBradford;
}

ColorMatrix gamutConversionMatrix(ColorPrimaries from, ColorPrimaries to) noexcept
{
	if (from == to) {
		return ColorMatrix::identity();
	}
	const Chromaticities source = chromaticitiesFor(from);
	const Chromaticities target = chromaticitiesFor(to);
	return xyzToRgbMatrix(to) * chromaticAdaptationMatrix(source.xw, source.yw, target.xw, target.yw) * rgbToXyzMatrix(from);
}

double toLinear(ColorTransfer transfer, double encoded) noexcept
{
	switch (transfer) {
	case ColorTransfer::Linear: return encoded;
	case ColorTransfer::Srgb: return mirrored(encoded, [](double e) { return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4); });
	case ColorTransfer::Rec709: return mirrored(encoded, [](double e) { return e < 0.081 ? e / 4.5 : std::pow((e + 0.099) / 1.099, 1.0 / 0.45); });
	case ColorTransfer::Gamma18: return mirrored(encoded, [](double e) { return std::pow(e, 1.8); });
	case ColorTransfer::Gamma22: return mirrored(encoded, [](double e) { return std::pow(e, 2.2); });
	case ColorTransfer::Gamma24: return mirrored(encoded, [](double e) { return std::pow(e, 2.4); });
	case ColorTransfer::Gamma26: return mirrored(encoded, [](double e) { return std::pow(e, 2.6); });
	case ColorTransfer::Pq: {
		const double p = std::pow(std::clamp(encoded, 0.0, 1.0), 1.0 / kPqM2);
		return std::pow(std::max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1);
	}
	case ColorTransfer::Hlg: {
		const double e = std::clamp(encoded, 0.0, 1.0);
		return e <= 0.5 ? e * e / 3.0 : (std::exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
	}
	}
	return encoded;
}

double fromLinear(ColorTransfer transfer, double linear) noexcept
{
	switch (transfer) {
	case ColorTransfer::Linear: return linear;
	case ColorTransfer::Srgb: return mirrored(linear, [](double l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055; });
	case ColorTransfer::Rec709: return mirrored(linear, [](double l) { return l < 0.018 ? l * 4.5 : 1.099 * std::pow(l, 0.45) - 0.099; });
	case ColorTransfer::Gamma18: return mirrored(linear, [](double l) { return std::pow(l, 1.0 / 1.8); });
	case ColorTransfer::Gamma22: return mirrored(linear, [](double l) { return std::pow(l, 1.0 / 2.2); });
	case ColorTransfer::Gamma24: return mirrored(linear, [](double l) { return std::pow(l, 1.0 / 2.4); });
	case ColorTransfer::Gamma26: return mirrored(linear, [](double l) { return std::pow(l, 1.0 / 2.6); });
	case ColorTransfer::Pq: {
		const double p = std::pow(std::clamp(linear, 0.0, 1.0), kPqM1);
		return std::pow((kPqC1 + kPqC2 * p) / (1.0 + kPqC3 * p), kPqM2);
	}
	case ColorTransfer::Hlg: {
		const double l = std::clamp(linear, 0.0, 1.0);
		return l <= 1.0 / 12.0 ? std::sqrt(3.0 * l) : kHlgA * std::log(12.0 * l - kHlgB) + kHlgC;
	}
	}
	return linear;
}

std::string_view toString(ColorPrimaries primaries) noexcept
{
	for (const PrimariesName& entry : kPrimariesNames) {
		if (entry.primaries == primaries) {
			return entry.name;
		}
	}
	return "rec709";
}

std::string_view toString(ColorTransfer transfer) noexcept
{
	for (const TransferName& entry : kTransferNames) {
		if (entry.transfer == transfer) {
			return entry.name;
		}
	}
	return "linear";
}

std::optional<ColorPrimaries> colorPrimariesNamed(std::string_view name) noexcept
{
	for (const PrimariesName& entry : kPrimariesNames) {
		if (equalsIgnoringCase(entry.name, name)) {
			return entry.primaries;
		}
	}
	return std::nullopt;
}

std::optional<ColorTransfer> colorTransferNamed(std::string_view name) noexcept
{
	for (const TransferName& entry : kTransferNames) {
		if (equalsIgnoringCase(entry.name, name)) {
			return entry.transfer;
		}
	}
	return std::nullopt;
}

std::optional<ColorSpace> colorSpaceNamed(std::string_view ofxName) noexcept
{
	for (const SpaceName& entry : kSpaceNames) {
		if (entry.name == ofxName) {
			return entry.space;
		}
	}
	return std::nullopt;
}

OFXGRIP_NAMESPACE_END

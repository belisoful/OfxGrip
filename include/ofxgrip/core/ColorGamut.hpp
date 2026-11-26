// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  ColorGamut.hpp
	@brief Color primaries, transfer functions, and the matrices between gamuts.

	Introduced in OfxGrip 0.5.0. A host names an image's colourspace and supplies no math for
	it. An effect that needs a luminance, a gamut conversion, or linear light derives it here.
	Every matrix comes from a gamut's primary and white chromaticities, so the luminance row
	agrees with the published weights. A conversion between gamuts with different white points
	adapts with the Bradford transform.

	A `ColorMatrix` is row-major and transforms a column vector: `matrix.apply(rgb)`.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! The primaries of an RGB gamut. */
enum class ColorPrimaries : std::uint8_t {
	Rec709 = 0,  /*!< ITU-R BT.709 and sRGB, D65. */
	Rec2020,     /*!< ITU-R BT.2020 and BT.2100, D65. */
	P3D65,       /*!< DCI-P3 primaries with a D65 white (Display P3). */
	AcesAp0,     /*!< ACES2065-1, the ACES white near D60. */
	AcesAp1,     /*!< ACEScg, the ACES white near D60. */
};

/*! The transfer function between a stored component and linear light. */
enum class ColorTransfer : std::uint8_t {
	Linear = 0,
	Srgb,     /*!< IEC 61966-2-1, the piecewise curve. */
	Rec709,   /*!< The ITU-R BT.709 camera curve. */
	Gamma18,  /*!< A pure power of 1.8. */
	Gamma22,  /*!< A pure power of 2.2. */
	Gamma24,  /*!< A pure power of 2.4, the ITU-R BT.1886 display curve with a zero black. */
	Gamma26,  /*!< A pure power of 2.6, the DCI display curve. */
	Pq,       /*!< SMPTE ST 2084. Linear 1 is 10,000 cd/m². */
	Hlg,      /*!< The ITU-R BT.2100 HLG camera curve. Linear 1 is the scene peak. */
};

/*! CIE xy chromaticities of three primaries and a white point. */
struct Chromaticities {
	double xr{0}, yr{0};
	double xg{0}, yg{0};
	double xb{0}, yb{0};
	double xw{0}, yw{0};
	constexpr bool operator==(const Chromaticities&) const noexcept = default;
};

/*! A color as three components: RGB or CIE XYZ. */
using ColorVector = std::array<double, 3>;

/*! A 3×3 matrix in row-major order. */
struct ColorMatrix {
	std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

	/*! The identity. */
	static constexpr ColorMatrix identity() noexcept { return ColorMatrix{}; }
	/*! The component at a row and a column. */
	constexpr double at(std::size_t row, std::size_t column) const noexcept { return m[row * 3 + column]; }
	/*! The product `this × right`: `right` applies first. */
	ColorMatrix operator*(const ColorMatrix& right) const noexcept;
	/*! The matrix applied to a column vector. */
	ColorVector apply(const ColorVector& v) const noexcept;
	/*! The inverse. A singular matrix returns the identity. */
	ColorMatrix inverse() const noexcept;
	/*! The nine components as floats in column-major order, the layout a shader uniform takes. */
	std::array<float, 9> columnMajorFloats() const noexcept;
	constexpr bool operator==(const ColorMatrix&) const noexcept = default;
};

/*! The chromaticities of a gamut. */
Chromaticities chromaticitiesFor(ColorPrimaries primaries) noexcept;

/*! The RGB-to-CIE-XYZ matrix of a gamut, with the gamut's own white at Y = 1. */
ColorMatrix rgbToXyzMatrix(ColorPrimaries primaries) noexcept;
/*! The same for any chromaticities. */
ColorMatrix rgbToXyzMatrix(const Chromaticities& chromaticities) noexcept;
/*! The CIE-XYZ-to-RGB matrix of a gamut. */
ColorMatrix xyzToRgbMatrix(ColorPrimaries primaries) noexcept;

/*! The luminance weights of a gamut: the Y row of its RGB-to-XYZ matrix. */
ColorVector luminanceWeights(ColorPrimaries primaries) noexcept;

/*! The matrix that converts linear RGB in `from` to linear RGB in `to`. Equal primaries give
    the identity. Gamuts with different white points adapt with the Bradford transform. */
ColorMatrix gamutConversionMatrix(ColorPrimaries from, ColorPrimaries to) noexcept;

/*! The Bradford matrix that adapts CIE XYZ from one white point to another. */
ColorMatrix chromaticAdaptationMatrix(double fromX, double fromY, double toX, double toY) noexcept;

/*! A stored component to linear light. A negative input mirrors the curve about zero for the
    power and piecewise curves. PQ and HLG clamp the input to [0, 1]. */
double toLinear(ColorTransfer transfer, double encoded) noexcept;
/*! Linear light to a stored component. The inverse of `toLinear`. */
double fromLinear(ColorTransfer transfer, double linear) noexcept;

/*! The name of a gamut: `rec709`, `rec2020`, `p3d65`, `ap0`, `ap1`. */
std::string_view toString(ColorPrimaries primaries) noexcept;
/*! The name of a transfer: `linear`, `srgb`, `rec709`, `gamma1.8`, `gamma2.2`, `gamma2.4`,
    `gamma2.6`, `pq`, `hlg`. */
std::string_view toString(ColorTransfer transfer) noexcept;
/*! The gamut a name states. Case is ignored. */
std::optional<ColorPrimaries> colorPrimariesNamed(std::string_view name) noexcept;
/*! The transfer a name states. Case is ignored. */
std::optional<ColorTransfer> colorTransferNamed(std::string_view name) noexcept;

/*! A colourspace as a gamut and a transfer. */
struct ColorSpace {
	ColorPrimaries primaries{ColorPrimaries::Rec709};
	ColorTransfer transfer{ColorTransfer::Linear};
	constexpr bool operator==(const ColorSpace&) const noexcept = default;
};

/*! The gamut and transfer of an OpenFX native colourspace name (`srgb_display`,
    `lin_rec709_srgb`, `ACEScg`, `rec2100_pq_display`, …). Returns nothing for a name this
    table does not hold: a camera log space, a generic space such as `ofx_scene_linear`, a
    role, or raw data. */
std::optional<ColorSpace> colorSpaceNamed(std::string_view ofxName) noexcept;

OFXGRIP_NAMESPACE_END

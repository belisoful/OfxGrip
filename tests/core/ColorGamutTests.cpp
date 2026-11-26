// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The gamut matrices against the published ones, the transfer functions against their
// standards' reference points, and the colourspace names.
#include <ofxgrip/core/ColorGamut.hpp>

#include <gtest/gtest.h>

#include <cmath>

using namespace ofxgrip;

namespace {

void expectMatrixNear(const ColorMatrix& actual, const std::array<double, 9>& expected, double tolerance)
{
	for (std::size_t i = 0; i < 9; ++i) {
		EXPECT_NEAR(actual.m[i], expected[i], tolerance) << "component " << i;
	}
}

constexpr ColorPrimaries kAllPrimaries[] = {ColorPrimaries::Rec709, ColorPrimaries::Rec2020, ColorPrimaries::P3D65, ColorPrimaries::AcesAp0, ColorPrimaries::AcesAp1};
constexpr ColorTransfer kAllTransfers[] = {ColorTransfer::Linear, ColorTransfer::Srgb,    ColorTransfer::Rec709, ColorTransfer::Gamma18, ColorTransfer::Gamma22,
                                           ColorTransfer::Gamma24, ColorTransfer::Gamma26, ColorTransfer::Pq,     ColorTransfer::Hlg};

}  // namespace

TEST(ColorGamut, MatrixArithmetic)
{
	const ColorMatrix scale{{2, 0, 0, 0, 3, 0, 0, 0, 4}};
	const ColorMatrix shear{{1, 1, 0, 0, 1, 0, 0, 0, 1}};
	EXPECT_EQ(ColorMatrix::identity() * scale, scale);
	const ColorVector v = (scale * shear).apply({1, 1, 1});
	EXPECT_DOUBLE_EQ(v[0], 4.0);
	EXPECT_DOUBLE_EQ(v[1], 3.0);
	EXPECT_DOUBLE_EQ(v[2], 4.0);
	expectMatrixNear(scale.inverse(), {0.5, 0, 0, 0, 1.0 / 3.0, 0, 0, 0, 0.25}, 1e-15);
	expectMatrixNear(shear * shear.inverse(), ColorMatrix::identity().m, 1e-15);
	// A singular matrix has no inverse.
	EXPECT_EQ((ColorMatrix{{1, 2, 3, 2, 4, 6, 0, 0, 1}}).inverse(), ColorMatrix::identity());
	const std::array<float, 9> packed = shear.columnMajorFloats();
	EXPECT_FLOAT_EQ(packed[3], 1.0f);  // column 1, row 0
	EXPECT_FLOAT_EQ(packed[1], 0.0f);  // column 0, row 1
	EXPECT_DOUBLE_EQ(shear.at(0, 1), 1.0);
}

TEST(ColorGamut, Rec709MatrixMatchesThePublishedOne)
{
	// The SMPTE RP 177 derivation from the BT.709 chromaticities and the D65 white (0.3127, 0.3290).
	expectMatrixNear(rgbToXyzMatrix(ColorPrimaries::Rec709), {0.4123908, 0.3575843, 0.1804808, 0.2126390, 0.7151687, 0.0721923, 0.0193308, 0.1191948, 0.9505322}, 1e-6);
	expectMatrixNear(xyzToRgbMatrix(ColorPrimaries::Rec709), {3.2409699, -1.5373832, -0.4986108, -0.9692436, 1.8759675, 0.0415551, 0.0556301, -0.2039770, 1.0569715}, 1e-6);
}

TEST(ColorGamut, LuminanceWeightsMatchThePublishedOnes)
{
	const ColorVector rec709 = luminanceWeights(ColorPrimaries::Rec709);
	EXPECT_NEAR(rec709[0], 0.2126, 1e-4);
	EXPECT_NEAR(rec709[1], 0.7152, 1e-4);
	EXPECT_NEAR(rec709[2], 0.0722, 1e-4);
	const ColorVector rec2020 = luminanceWeights(ColorPrimaries::Rec2020);
	EXPECT_NEAR(rec2020[0], 0.2627, 1e-4);
	EXPECT_NEAR(rec2020[1], 0.6780, 1e-4);
	EXPECT_NEAR(rec2020[2], 0.0593, 1e-4);
	const ColorVector ap1 = luminanceWeights(ColorPrimaries::AcesAp1);
	EXPECT_NEAR(ap1[0], 0.2722287, 1e-6);
	EXPECT_NEAR(ap1[1], 0.6740818, 1e-6);
	EXPECT_NEAR(ap1[2], 0.0536895, 1e-6);
	for (ColorPrimaries primaries : kAllPrimaries) {
		const ColorVector w = luminanceWeights(primaries);
		EXPECT_NEAR(w[0] + w[1] + w[2], 1.0, 1e-12) << toString(primaries);
	}
}

TEST(ColorGamut, EveryGamutMapsWhiteToItsWhitePoint)
{
	for (ColorPrimaries primaries : kAllPrimaries) {
		const Chromaticities c = chromaticitiesFor(primaries);
		const ColorVector white = rgbToXyzMatrix(primaries).apply({1, 1, 1});
		EXPECT_NEAR(white[0], c.xw / c.yw, 1e-12) << toString(primaries);
		EXPECT_NEAR(white[1], 1.0, 1e-12) << toString(primaries);
		EXPECT_NEAR(white[2], (1.0 - c.xw - c.yw) / c.yw, 1e-12) << toString(primaries);
		expectMatrixNear(xyzToRgbMatrix(primaries) * rgbToXyzMatrix(primaries), ColorMatrix::identity().m, 1e-12);
	}
}

TEST(ColorGamut, GamutConversions)
{
	EXPECT_EQ(gamutConversionMatrix(ColorPrimaries::Rec2020, ColorPrimaries::Rec2020), ColorMatrix::identity());
	// ITU-R BT.2087: Rec.709 to Rec.2020.
	expectMatrixNear(gamutConversionMatrix(ColorPrimaries::Rec709, ColorPrimaries::Rec2020), {0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956}, 1e-4);
	// The ACES AP0 to AP1 matrix (the two share a white point).
	expectMatrixNear(gamutConversionMatrix(ColorPrimaries::AcesAp0, ColorPrimaries::AcesAp1),
	                 {1.4514393161, -0.2365107469, -0.2149285693, -0.0765537734, 1.1762296998, -0.0996759264, 0.0083161484, -0.0060324498, 0.9977163014}, 1e-6);
	// Rec.709 to ACEScg, Bradford-adapted from D65 to the ACES white.
	expectMatrixNear(gamutConversionMatrix(ColorPrimaries::Rec709, ColorPrimaries::AcesAp1), {0.6131, 0.3395, 0.0474, 0.0702, 0.9164, 0.0135, 0.0206, 0.1096, 0.8698}, 2e-4);
	for (ColorPrimaries from : kAllPrimaries) {
		for (ColorPrimaries to : kAllPrimaries) {
			// White stays white, and the round trip is the identity.
			const ColorVector white = gamutConversionMatrix(from, to).apply({1, 1, 1});
			EXPECT_NEAR(white[0], 1.0, 1e-9) << toString(from) << " to " << toString(to);
			EXPECT_NEAR(white[1], 1.0, 1e-9);
			EXPECT_NEAR(white[2], 1.0, 1e-9);
			expectMatrixNear(gamutConversionMatrix(to, from) * gamutConversionMatrix(from, to), ColorMatrix::identity().m, 1e-9);
		}
	}
	EXPECT_EQ(chromaticAdaptationMatrix(0.3127, 0.3290, 0.3127, 0.3290), ColorMatrix::identity());
	// An adaptation carries the source white to the target white: D65 to D50 here.
	const ColorVector adapted = chromaticAdaptationMatrix(0.3127, 0.3290, 0.3457, 0.3585).apply({0.3127 / 0.3290, 1.0, (1.0 - 0.3127 - 0.3290) / 0.3290});
	EXPECT_NEAR(adapted[0], 0.3457 / 0.3585, 1e-12);
	EXPECT_NEAR(adapted[1], 1.0, 1e-12);
	EXPECT_NEAR(adapted[2], (1.0 - 0.3457 - 0.3585) / 0.3585, 1e-12);
}

TEST(ColorGamut, TransferReferencePoints)
{
	EXPECT_NEAR(toLinear(ColorTransfer::Srgb, 0.5), 0.2140411, 1e-6);
	EXPECT_NEAR(toLinear(ColorTransfer::Srgb, 0.04045), 0.04045 / 12.92, 1e-12);
	EXPECT_NEAR(fromLinear(ColorTransfer::Srgb, 0.18), 0.4613561, 1e-6);
	EXPECT_NEAR(fromLinear(ColorTransfer::Rec709, 0.18), 0.4090077, 1e-6);
	EXPECT_NEAR(fromLinear(ColorTransfer::Rec709, 0.01), 0.045, 1e-12);
	EXPECT_NEAR(toLinear(ColorTransfer::Gamma22, 0.5), std::pow(0.5, 2.2), 1e-15);
	EXPECT_NEAR(toLinear(ColorTransfer::Gamma18, 0.5), std::pow(0.5, 1.8), 1e-15);
	EXPECT_NEAR(toLinear(ColorTransfer::Gamma24, 0.5), std::pow(0.5, 2.4), 1e-15);
	EXPECT_NEAR(toLinear(ColorTransfer::Gamma26, 0.5), std::pow(0.5, 2.6), 1e-15);
	// ST 2084: 100 cd/m² encodes near 0.508, and code 1 is the 10,000 cd/m² peak.
	EXPECT_NEAR(fromLinear(ColorTransfer::Pq, 100.0 / 10000.0), 0.5080784, 1e-6);
	EXPECT_NEAR(toLinear(ColorTransfer::Pq, 1.0), 1.0, 1e-12);
	EXPECT_DOUBLE_EQ(toLinear(ColorTransfer::Pq, 0.0), 0.0);
	EXPECT_DOUBLE_EQ(toLinear(ColorTransfer::Pq, -0.5), 0.0);
	// BT.2100 HLG: the curve's two segments meet at 1/12, which encodes to 0.5.
	EXPECT_NEAR(fromLinear(ColorTransfer::Hlg, 1.0 / 12.0), 0.5, 1e-12);
	EXPECT_NEAR(fromLinear(ColorTransfer::Hlg, 1.0), 1.0, 1e-7);
	EXPECT_NEAR(toLinear(ColorTransfer::Hlg, 0.5), 1.0 / 12.0, 1e-12);
	EXPECT_DOUBLE_EQ(toLinear(ColorTransfer::Hlg, 2.0), toLinear(ColorTransfer::Hlg, 1.0));
	// A negative component mirrors the curve.
	EXPECT_DOUBLE_EQ(toLinear(ColorTransfer::Srgb, -0.5), -toLinear(ColorTransfer::Srgb, 0.5));
	EXPECT_DOUBLE_EQ(fromLinear(ColorTransfer::Gamma24, -0.25), -fromLinear(ColorTransfer::Gamma24, 0.25));
	EXPECT_DOUBLE_EQ(toLinear(ColorTransfer::Linear, -3.5), -3.5);
	EXPECT_DOUBLE_EQ(fromLinear(ColorTransfer::Linear, 7.25), 7.25);
}

TEST(ColorGamut, EveryTransferRoundTrips)
{
	for (ColorTransfer transfer : kAllTransfers) {
		for (int step = 0; step <= 100; ++step) {
			const double encoded = static_cast<double>(step) / 100.0;
			// ST 2084 encodes zero light as 7.3e-7, and the HLG constants are rounded.
			EXPECT_NEAR(fromLinear(transfer, toLinear(transfer, encoded)), encoded, 1e-6) << toString(transfer) << " at " << encoded;
		}
		EXPECT_NEAR(toLinear(transfer, 0.0), 0.0, 1e-12) << toString(transfer);
		EXPECT_NEAR(toLinear(transfer, 1.0), 1.0, 1e-7) << toString(transfer);
	}
}

TEST(ColorGamut, Names)
{
	for (ColorPrimaries primaries : kAllPrimaries) {
		const std::optional<ColorPrimaries> parsed = colorPrimariesNamed(toString(primaries));
		ASSERT_TRUE(parsed.has_value()) << toString(primaries);
		EXPECT_EQ(*parsed, primaries);
	}
	for (ColorTransfer transfer : kAllTransfers) {
		const std::optional<ColorTransfer> parsed = colorTransferNamed(toString(transfer));
		ASSERT_TRUE(parsed.has_value()) << toString(transfer);
		EXPECT_EQ(*parsed, transfer);
	}
	EXPECT_EQ(toString(ColorPrimaries::P3D65), "p3d65");
	EXPECT_EQ(toString(ColorTransfer::Gamma24), "gamma2.4");
	EXPECT_EQ(colorPrimariesNamed("Rec2020"), ColorPrimaries::Rec2020);
	EXPECT_EQ(colorPrimariesNamed("DisplayP3"), ColorPrimaries::P3D65);
	EXPECT_EQ(colorPrimariesNamed("ACEScg"), ColorPrimaries::AcesAp1);
	EXPECT_EQ(colorTransferNamed("ST2084"), ColorTransfer::Pq);
	EXPECT_EQ(colorTransferNamed("rec1886"), ColorTransfer::Gamma24);
	EXPECT_FALSE(colorPrimariesNamed("host").has_value());
	EXPECT_FALSE(colorTransferNamed("gamma").has_value());
	EXPECT_FALSE(colorTransferNamed("").has_value());
}

TEST(ColorGamut, OpenFxColourspaceNames)
{
	EXPECT_EQ(colorSpaceNamed("srgb_display"), (ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Srgb}));
	EXPECT_EQ(colorSpaceNamed("lin_rec709_srgb"), (ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Linear}));
	EXPECT_EQ(colorSpaceNamed("ACEScg"), (ColorSpace{ColorPrimaries::AcesAp1, ColorTransfer::Linear}));
	EXPECT_EQ(colorSpaceNamed("ACES2065-1"), (ColorSpace{ColorPrimaries::AcesAp0, ColorTransfer::Linear}));
	EXPECT_EQ(colorSpaceNamed("rec2100_pq_display"), (ColorSpace{ColorPrimaries::Rec2020, ColorTransfer::Pq}));
	EXPECT_EQ(colorSpaceNamed("rec2100_hlg_display"), (ColorSpace{ColorPrimaries::Rec2020, ColorTransfer::Hlg}));
	EXPECT_EQ(colorSpaceNamed("p3d65_display"), (ColorSpace{ColorPrimaries::P3D65, ColorTransfer::Gamma26}));
	EXPECT_EQ(colorSpaceNamed("camera_rec709"), (ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Rec709}));
	// The names are OpenFX's own and keep their case.
	EXPECT_FALSE(colorSpaceNamed("acescg").has_value());
	EXPECT_FALSE(colorSpaceNamed("ofx_scene_linear").has_value());
	EXPECT_FALSE(colorSpaceNamed("arri_logc4").has_value());
	EXPECT_FALSE(colorSpaceNamed("").has_value());
}

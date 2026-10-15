// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The digests against the published vectors, and the watermark's glyphs, placement, mask, and
// compositing over CPU frames.
#include <ofxgrip/core/Digest.hpp>
#include <ofxgrip/core/Watermark.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

ImageBuffer grey(int width, int height, BitDepth depth, Components components = Components::RGBA)
{
	ImageDesc desc;
	desc.bounds = {0, 0, width, height};
	desc.regionOfDefinition = desc.bounds;
	desc.depth = depth;
	desc.components = components;
	ImageBuffer out(desc);
	const ImageView v = out.view();
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			for (std::size_t c = 0; c < componentCount(components); ++c) {
				writeComponent(v.pixel(x, y), depth, c, c == 3 ? 1.0f : 0.5f);
			}
		}
	}
	return out;
}

}  // namespace

TEST(Digest, Sha256AndHmacMatchThePublishedVectors)
{
	EXPECT_EQ(hexDigest(sha256("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	EXPECT_EQ(hexDigest(sha256("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_EQ(hexDigest(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000, 'a');
	EXPECT_EQ(hexDigest(sha256(million)), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	// RFC 4231 test case 2.
	EXPECT_EQ(hexDigest(hmacSha256("Jefe", "what do ya want for nothing?")), "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	// RFC 4231 test case 1 and a key longer than a block (test case 6).
	const std::string key1(20, '\x0b');
	EXPECT_EQ(hexDigest(hmacSha256(key1, "Hi There")), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	const std::string key6(131, '\xaa');
	EXPECT_EQ(hexDigest(hmacSha256(key6, "Test Using Larger Than Block-Size Key - Hash Key First")), "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
	Sha256Digest a = sha256("x");
	Sha256Digest b = sha256("y");
	EXPECT_TRUE(digestsEqual(a, a));
	EXPECT_FALSE(digestsEqual(a, b));
	Sha256Digest parsed{};
	EXPECT_TRUE(parseHexDigest(hexDigest(a), parsed));
	EXPECT_TRUE(digestsEqual(parsed, a));
	EXPECT_TRUE(parseHexDigest("ABCDEF" + hexDigest(a).substr(6), parsed));
	EXPECT_FALSE(parseHexDigest("abc", parsed));
	EXPECT_FALSE(parseHexDigest(std::string(64, 'g'), parsed));
}

TEST(Watermark, ConfigurationReadsAndWritesTheDictionaryForm)
{
	WatermarkConfiguration c = WatermarkConfiguration::unlicensed("Demo");
	EXPECT_EQ(c.style, WatermarkStyle::Single);
	EXPECT_EQ(c.fontSize, 99.0);
	WatermarkConfiguration t = WatermarkConfiguration::trial("Demo Trial");
	EXPECT_EQ(t.style, WatermarkStyle::Corner);
	EXPECT_EQ(t.corner, WatermarkCorner::BottomRight);
	EXPECT_EQ(t.opacity, 0.5);
	std::map<std::string, Value> dictionary;
	dictionary["text"] = Value("Hi");
	dictionary["fontSize"] = Value(20.0);
	dictionary["color"] = Value(RGB{1, 0, 0});
	dictionary["angleDegrees"] = Value(45.0);
	dictionary["opacity"] = Value(0.25);
	dictionary["style"] = Value("diagonal");
	dictionary["corner"] = Value("topLeft");
	dictionary["inset"] = Value(5.0);
	dictionary["tileSpacing"] = Value(Point2D{100, 50});
	dictionary["ignored"] = Value(1);
	ASSERT_TRUE(c.apply(dictionary).ok());
	EXPECT_EQ(c.text, "Hi");
	EXPECT_EQ(c.color, (RGBA{1, 0, 0, 1}));
	EXPECT_EQ(c.style, WatermarkStyle::DiagonalTiled);
	EXPECT_EQ(c.corner, WatermarkCorner::TopLeft);
	EXPECT_EQ(c.tileSpacing, (Point2D{100, 50}));
	const std::map<std::string, Value> back = c.toDictionary();
	EXPECT_EQ(back.at("style"), Value("diagonal"));
	EXPECT_EQ(back.at("corner"), Value("topLeft"));
	WatermarkConfiguration again;
	ASSERT_TRUE(again.apply(back).ok());
	EXPECT_EQ(again, c);
	dictionary["color"] = Value(RGBA{0, 1, 0, 0.5});
	ASSERT_TRUE(c.apply(dictionary).ok());
	EXPECT_EQ(c.color.a, 0.5);
	dictionary["tileSpacing"] = Value(70.0);
	ASSERT_TRUE(c.apply(dictionary).ok());
	EXPECT_EQ(c.tileSpacing, (Point2D{70, 70}));
	EXPECT_EQ(c.apply({{"color", Value("red")}}).code(), ErrorCode::Value);
	EXPECT_EQ(c.apply({{"style", Value("spiral")}}).code(), ErrorCode::Value);
	EXPECT_EQ(c.apply({{"corner", Value("middle")}}).code(), ErrorCode::Value);
	WatermarkStyle style{};
	EXPECT_TRUE(parseWatermarkStyle("diagonalTiled", style));
	EXPECT_EQ(style, WatermarkStyle::DiagonalTiled);
	EXPECT_EQ(toString(WatermarkStyle::Banner), "banner");
	EXPECT_EQ(toString(WatermarkCorner::TopRight), "topRight");
}

TEST(Watermark, GlyphsAreDefinedForTheTextSet)
{
	const std::uint8_t* a = watermarkGlyph('a');
	EXPECT_EQ(a[0], 0x0E);
	EXPECT_EQ(watermarkGlyph('A'), a);
	EXPECT_EQ(watermarkGlyph('~')[0], 0);  // unknown: blank
	EXPECT_EQ(watermarkGlyph(' ')[3], 0);
	EXPECT_EQ(watermarkGlyph('1')[6], 0x0E);
	EXPECT_EQ(watermarkGlyph('-')[3], 0x1F);
}

TEST(Watermark, CoverageFollowsTheTextAndStyles)
{
	// A single "I" at the center of a 70×70 frame at 1080p scale: font 99 → 99 × 70 / 1080 px.
	WatermarkConfiguration c = WatermarkConfiguration::unlicensed("I");
	c.angleDegrees = 0;
	c.fontSize = 108;  // 7 px at this frame height: one pixel per cell
	Watermark mark(c);
	const Point2D frame{70, 70};
	// The glyph is 5 cells wide centered at x = 35: columns 32.5 .. 37.5; rows 31.5 .. 38.5.
	EXPECT_GT(mark.coverage(35.0, 35.0, frame), 0.0f);    // the stem
	EXPECT_EQ(mark.coverage(35.0, 50.0, frame), 0.0f);    // above
	EXPECT_EQ(mark.coverage(10.0, 35.0, frame), 0.0f);    // left
	EXPECT_GT(mark.coverage(33.5, 38.0, frame), 0.0f);    // the top bar
	EXPECT_EQ(mark.coverage(33.5, 35.0, frame), 0.0f);    // beside the stem
	const std::vector<float> mask = mark.mask({30, 30, 40, 40}, frame);
	ASSERT_EQ(mask.size(), 100u);
	float sum = 0.0f;
	for (float v : mask) {
		sum += v;
	}
	EXPECT_GT(sum, 5.0f);
	EXPECT_LT(sum, 20.0f);
	EXPECT_EQ(Watermark(WatermarkConfiguration::unlicensed("")).coverage(35, 35, frame), 0.0f);
	EXPECT_TRUE(Watermark(WatermarkConfiguration::unlicensed("")).mask({0, 0, 2, 2}, frame).size() == 4u);
	// The corner style sits at the inset; the banner at the bottom center; the tiling repeats.
	WatermarkConfiguration corner = WatermarkConfiguration::trial("I");
	corner.fontSize = 108;
	corner.inset = 0;
	corner.corner = WatermarkCorner::BottomLeft;
	Watermark cornerMark(corner);
	EXPECT_GT(cornerMark.coverage(2.5, 3.5, frame), 0.0f);
	EXPECT_EQ(cornerMark.coverage(35.0, 35.0, frame), 0.0f);
	corner.corner = WatermarkCorner::TopRight;
	EXPECT_GT(Watermark(corner).coverage(70 - 2.5, 70 - 3.5, frame), 0.0f);
	corner.corner = WatermarkCorner::TopLeft;
	EXPECT_GT(Watermark(corner).coverage(2.5, 70 - 3.5, frame), 0.0f);
	corner.corner = WatermarkCorner::BottomRight;
	EXPECT_GT(Watermark(corner).coverage(70 - 2.5, 3.5, frame), 0.0f);
	WatermarkConfiguration banner = c;
	banner.style = WatermarkStyle::Banner;
	banner.inset = 0;
	EXPECT_GT(Watermark(banner).coverage(35.0, 3.5, frame), 0.0f);
	WatermarkConfiguration tiled = c;
	tiled.style = WatermarkStyle::DiagonalTiled;
	tiled.tileSpacing = {20, 20};
	tiled.angleDegrees = 0;
	Watermark tiledMark(tiled);
	EXPECT_GT(tiledMark.coverage(35.0, 35.0, frame), 0.0f);
	bool repeated = false;
	for (int y = 0; y < 70 && !repeated; ++y) {
		for (int x = 0; x < 70 && !repeated; ++x) {
			repeated = (std::abs(x - 35) > 15 || std::abs(y - 35) > 15) && tiledMark.coverage(x + 0.5, y + 0.5, frame) > 0.0f;
		}
	}
	EXPECT_TRUE(repeated);
	// A rotated single mark still covers its center and is bounded.
	WatermarkConfiguration rotated = c;
	rotated.angleDegrees = 90;
	Watermark rotatedMark(rotated);
	EXPECT_GT(rotatedMark.coverage(35.0, 35.0, frame), 0.0f);
	EXPECT_EQ(rotatedMark.coverage(50.0, 35.0, frame), 0.0f);
}

TEST(Watermark, CompositesOverEveryDepth)
{
	WatermarkConfiguration c = WatermarkConfiguration::unlicensed("I");
	c.angleDegrees = 0;
	c.fontSize = 216;  // two pixels per cell: the pixel at (35, 35) lies inside the stem
	c.color = {1, 0, 0, 1};
	c.opacity = 1.0;
	Watermark mark(c);
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		ImageBuffer frame = grey(70, 70, depth);
		mark.composite(frame.view(), {}, {70, 70});
		const ImageView v = frame.view();
		EXPECT_NEAR(readComponent(v.pixel(35, 35), depth, 0), 1.0f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(35, 35), depth, 1), 0.0f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(35, 35), depth, 3), 1.0f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(10, 10), depth, 0), 0.5f, 0.01f) << toString(depth);
	}
	// A window limits the composite; an alpha-only frame and an empty mark are left alone.
	ImageBuffer frame = grey(70, 70, BitDepth::Float);
	mark.composite(frame.view(), {0, 0, 20, 20}, {70, 70});
	EXPECT_NEAR(readComponent(frame.view().pixel(35, 35), BitDepth::Float, 0), 0.5f, 0.01f);
	ImageBuffer alpha = grey(70, 70, BitDepth::Float, Components::Alpha);
	mark.composite(alpha.view(), {}, {70, 70});
	EXPECT_NEAR(readComponent(alpha.view().pixel(35, 35), BitDepth::Float, 0), 0.5f, 0.01f);
	ImageBuffer rgb = grey(70, 70, BitDepth::Float, Components::RGB);
	mark.composite(rgb.view(), {}, {70, 70});
	EXPECT_NEAR(readComponent(rgb.view().pixel(35, 35), BitDepth::Float, 0), 1.0f, 0.01f);
	Watermark(WatermarkConfiguration::unlicensed("")).composite(frame.view(), {}, {70, 70});
	Watermark(c).composite(ImageView{}, {}, {70, 70});
	c.opacity = 0.5;
	Watermark half(c);
	ImageBuffer blended = grey(70, 70, BitDepth::Float);
	half.composite(blended.view(), {}, {70, 70});
	EXPECT_NEAR(readComponent(blended.view().pixel(35, 35), BitDepth::Float, 0), 0.75f, 0.02f);
}

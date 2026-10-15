// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Codec.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Codec, PrimitivesRoundTripLittleEndian)
{
	Bytes out;
	ByteWriter w(out);
	w.raw("OG");
	w.u8(7);
	w.u16(0x1234);
	w.u32(0xDEADBEEFu);
	w.i32(-5);
	w.f64(1.5);
	w.str("hi");
	w.bytes(Bytes{9, 8});
	ASSERT_EQ(out.size(), 2u + 1 + 2 + 4 + 4 + 8 + (4 + 2) + (4 + 2));
	EXPECT_EQ(out[3], 0x34);
	EXPECT_EQ(out[4], 0x12);
	EXPECT_EQ(out[5], 0xEF);

	ByteReader r(out);
	std::uint8_t a = 0;
	std::uint16_t b = 0;
	std::uint32_t c = 0;
	std::int32_t d = 0;
	double e = 0;
	std::string s;
	Bytes bytes;
	EXPECT_TRUE(r.magic("OG"));
	EXPECT_TRUE(r.u8(a) && r.u16(b) && r.u32(c) && r.i32(d) && r.f64(e) && r.str(s) && r.bytes(bytes));
	EXPECT_EQ(a, 7);
	EXPECT_EQ(b, 0x1234);
	EXPECT_EQ(c, 0xDEADBEEFu);
	EXPECT_EQ(d, -5);
	EXPECT_EQ(e, 1.5);
	EXPECT_EQ(s, "hi");
	EXPECT_EQ(bytes, (Bytes{9, 8}));
	EXPECT_TRUE(r.done());
	EXPECT_EQ(r.position(), out.size());
}

TEST(Codec, ReadsFailAtTheEndInsteadOfOverrunning)
{
	Bytes out;
	ByteWriter w(out);
	w.u16(1);
	ByteReader r(out);
	std::uint32_t v = 0;
	EXPECT_FALSE(r.u32(v));
	ByteReader r2(out);
	std::string s;
	EXPECT_FALSE(r2.str(s));
	ByteReader r3(out);
	EXPECT_FALSE(r3.magic("OGSN"));
	Bytes lengthOnly;
	ByteWriter w2(lengthOnly);
	w2.u32(100);
	ByteReader r4(lengthOnly);
	Bytes b;
	EXPECT_FALSE(r4.bytes(b));
	const Bytes xy{'X', 'Y'};
	ByteReader r5(xy);
	EXPECT_FALSE(r5.magic("XZ"));
	EXPECT_EQ(r5.position(), 0u);
}

TEST(Codec, EveryValueKindRoundTrips)
{
	const std::vector<Value> values{Value(), Value(true), Value(-3), Value(2.25), Value(Point2D{1, 2}), Value(Point3D{1, 2, 3}), Value(RGB{0.1, 0.2, 0.3}), Value(RGBA{0.1, 0.2, 0.3, 0.4}), Value("text"), Value(Bytes{1, 2, 3})};
	for (const Value& v : values) {
		Bytes out;
		ByteWriter w(out);
		writeValue(w, v);
		ByteReader r(out);
		Value back;
		ASSERT_TRUE(readValue(r, back)) << v.toString();
		EXPECT_EQ(back, v) << v.toString();
		EXPECT_TRUE(r.done());
		for (std::size_t cut = 1; cut < out.size(); ++cut) {
			ByteReader partial(std::span<const std::uint8_t>(out.data(), cut));
			Value none;
			EXPECT_FALSE(readValue(partial, none)) << v.toString() << " cut at " << cut;
		}
	}
	const Bytes unknownKind{42};
	ByteReader unknown(unknownKind);
	Value none;
	EXPECT_FALSE(readValue(unknown, none));
	const Bytes nothing;
	ByteReader empty(nothing);
	EXPECT_FALSE(readValue(empty, none));
}

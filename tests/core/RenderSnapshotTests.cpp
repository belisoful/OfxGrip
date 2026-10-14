// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/RenderSnapshot.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

RenderSnapshot sample()
{
	RenderSnapshotBuilder b;
	RenderInfo info;
	info.time = 12.5;
	info.frameRate = 29.97;
	info.renderScale = {0.5, 0.5};
	info.field = Field::Lower;
	info.draft = true;
	info.renderWindow = RectI{1, 2, 300, 400};
	info.projectSize = {1920, 1080};
	info.pixelAspect = 1.5;
	info.colourspace = "lin_rec709";
	info.resolvePage = "Fusion";
	b.setInfo(info);
	b.set(100, Value(2.5));
	b.set(101, Value(7));
	b.set(102, Value(true));
	b.set(103, Value(Point2D{1, 2}));
	b.set(104, Value(Point3D{1, 2, 3}));
	b.set(105, Value(RGB{0.1, 0.2, 0.3}));
	b.set(106, Value(RGBA{0.1, 0.2, 0.3, 0.4}));
	b.set(107, Value("text"));
	b.set(108, Value(Bytes{9, 8, 7}));
	b.set(109, Value());
	b.setExtra("meta", Bytes{1, 2, 3, 4});
	b.setLicense(LicenseState::Trial);
	return b.build();
}

}  // namespace

TEST(RenderSnapshot, TypedGettersWithFallbacks)
{
	const RenderSnapshot s = sample();
	EXPECT_DOUBLE_EQ(s.number(100), 2.5);
	EXPECT_EQ(s.integer(101), 7);
	EXPECT_TRUE(s.boolean(102));
	EXPECT_EQ(s.point(103).y, 2.0);
	EXPECT_EQ(s.point3D(104).z, 3.0);
	EXPECT_EQ(s.rgb(105).b, 0.3);
	EXPECT_EQ(s.rgba(106).a, 0.4);
	EXPECT_EQ(s.rgb(106).g, 0.2);
	EXPECT_EQ(s.rgba(105).a, 1.0);
	EXPECT_EQ(s.string(107), "text");
	EXPECT_DOUBLE_EQ(s.number(999, 4.0), 4.0);
	EXPECT_EQ(s.string(999, "fb"), "fb");
	EXPECT_EQ(s.point(100).x, 0.0);
	EXPECT_TRUE(s.has(109));
	EXPECT_FALSE(s.has(999));
	ASSERT_NE(s.extra("meta"), nullptr);
	EXPECT_EQ(s.extra("meta")->size(), 4u);
	EXPECT_EQ(s.extra("none"), nullptr);
	EXPECT_EQ(s.license(), LicenseState::Trial);
	EXPECT_EQ(s.info().resolvePage, "Fusion");
}

TEST(RenderSnapshot, CodecRoundTrip)
{
	const RenderSnapshot s = sample();
	const Bytes bytes = s.encode();
	ASSERT_GT(bytes.size(), 8u);
	EXPECT_EQ(bytes[0], 'O');
	RenderSnapshot decoded;
	Error e = RenderSnapshot::decode(bytes, decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(decoded, s);
	EXPECT_EQ(decoded.encode(), bytes);
}

TEST(RenderSnapshot, TruncatedStreamsFailCleanly)
{
	const Bytes bytes = sample().encode();
	for (std::size_t cut = 0; cut < bytes.size(); cut += 7) {
		RenderSnapshot out;
		Bytes partial(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
		EXPECT_EQ(RenderSnapshot::decode(partial, out).code(), ErrorCode::Codec) << "cut at " << cut;
	}
}

TEST(RenderSnapshot, RejectsBadMagicVersionAndTags)
{
	RenderSnapshot out;
	Bytes bytes = sample().encode();
	bytes[0] = 'X';
	EXPECT_FALSE(RenderSnapshot::decode(bytes, out).ok());
	bytes = sample().encode();
	bytes[4] = 0xFF;
	bytes[5] = 0xFF;
	EXPECT_NE(RenderSnapshot::decode(bytes, out).describe().find("newer"), std::string::npos);
	bytes = sample().encode();
	bytes[8] = 99;
	EXPECT_NE(RenderSnapshot::decode(bytes, out).describe().find("unknown tag"), std::string::npos);
	EXPECT_FALSE(RenderSnapshot::decode(Bytes{}, out).ok());
}

TEST(RenderSnapshot, EmptySnapshotRoundTrips)
{
	RenderSnapshotBuilder b;
	const RenderSnapshot s = b.build();
	RenderSnapshot out;
	ASSERT_TRUE(RenderSnapshot::decode(s.encode(), out).ok());
	EXPECT_EQ(out, s);
	EXPECT_TRUE(out.values().empty());
	EXPECT_EQ(out.license(), LicenseState::Unknown);
}

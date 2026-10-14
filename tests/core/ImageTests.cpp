// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Image.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace ofxgrip;

namespace {

ImageDesc desc(int w, int h, BitDepth depth, Components components = Components::RGBA)
{
	ImageDesc d;
	d.bounds = RectI{0, 0, w, h};
	d.regionOfDefinition = d.bounds;
	d.depth = depth;
	d.components = components;
	return d;
}

}  // namespace

TEST(Image, BufferRowsAreAlignedAndAddressable)
{
	ImageBuffer buffer(desc(5, 3, BitDepth::UInt8, Components::RGB));
	EXPECT_EQ(buffer.desc().pixelBytes(), 3u);
	EXPECT_EQ(buffer.desc().minimumRowBytes(), 15u);
	EXPECT_EQ(buffer.rowBytes(), 16u);
	EXPECT_EQ(buffer.bytes().size(), 48u);
	ImageView v = buffer.view();
	EXPECT_TRUE(v.valid());
	EXPECT_EQ(v.pixel(0, 0), v.data);
	EXPECT_EQ(v.pixel(1, 0), v.data + 3);
	EXPECT_EQ(v.pixel(0, 1), v.data + 16);
	EXPECT_EQ(v.pixel(5, 0), nullptr);
	EXPECT_EQ(v.pixel(-1, 0), nullptr);
}

TEST(Image, SubViewAndCopy)
{
	ImageBuffer a(desc(8, 8, BitDepth::Float));
	ImageBuffer b(desc(8, 8, BitDepth::Float));
	ImageView va = a.view();
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			writeComponent(va.pixel(x, y), BitDepth::Float, 0, static_cast<float>(x + 10 * y));
		}
	}
	ImageView sub = va.subView(RectI{2, 2, 6, 6});
	EXPECT_EQ(sub.desc.bounds, (RectI{2, 2, 6, 6}));
	EXPECT_EQ(sub.pixel(2, 2), va.pixel(2, 2));
	EXPECT_EQ(sub.pixel(1, 1), nullptr);
	EXPECT_TRUE(va.subView(RectI{20, 20, 30, 30}).desc.bounds.empty());
	ASSERT_TRUE(b.view().copyFrom(sub).ok());
	EXPECT_FLOAT_EQ(readComponent(b.view().pixel(3, 4), BitDepth::Float, 0), 43.0f);
	EXPECT_FLOAT_EQ(readComponent(b.view().pixel(0, 0), BitDepth::Float, 0), 0.0f);
	ImageBuffer wrong(desc(8, 8, BitDepth::UInt8));
	EXPECT_EQ(wrong.view().copyFrom(va).code(), ErrorCode::Format);
	EXPECT_FALSE(ImageView{}.copyFrom(va).ok());
	va.clear();
	EXPECT_FLOAT_EQ(readComponent(va.pixel(5, 5), BitDepth::Float, 0), 0.0f);
}

TEST(Image, ComponentConversionsRoundTrip)
{
	std::uint8_t px[16] = {0};
	writeComponent(px, BitDepth::UInt8, 0, 0.5f);
	EXPECT_EQ(px[0], 128);
	EXPECT_NEAR(readComponent(px, BitDepth::UInt8, 0), 0.502f, 0.002f);
	writeComponent(px, BitDepth::UInt8, 1, 2.0f);
	EXPECT_EQ(px[1], 255);
	writeComponent(px, BitDepth::UInt16, 0, 0.25f);
	EXPECT_NEAR(readComponent(px, BitDepth::UInt16, 0), 0.25f, 0.0001f);
	writeComponent(px, BitDepth::Float, 0, -3.5f);
	EXPECT_FLOAT_EQ(readComponent(px, BitDepth::Float, 0), -3.5f);
	for (float v : {0.0f, 1.0f, 0.5f, -2.0f, 1000.0f, 0.001f}) {
		writeComponent(px, BitDepth::Half, 0, v);
		EXPECT_NEAR(readComponent(px, BitDepth::Half, 0), v, std::abs(v) * 0.001f + 1e-6f) << v;
	}
	EXPECT_EQ(readComponent(px, BitDepth::None, 0), 0.0f);
}

TEST(Image, BandsCoverTheWindowOnce)
{
	std::vector<int> rows(37, 0);
	std::mutex m;
	Error e = processBands(
	    RectI{0, 0, 10, 37}, 8,
	    [](unsigned count, const std::function<void(unsigned)>& fn) {
		    std::vector<std::thread> threads;
		    for (unsigned i = 0; i < count; ++i) {
			    threads.emplace_back([&fn, i]() { fn(i); });
		    }
		    for (std::thread& t : threads) {
			    t.join();
		    }
	    },
	    []() { return false; },
	    [&](const RectI& band) {
		    std::lock_guard<std::mutex> lock(m);
		    for (int y = band.y1; y < band.y2; ++y) {
			    rows[static_cast<std::size_t>(y)]++;
		    }
	    });
	EXPECT_TRUE(e.ok());
	for (int count : rows) {
		EXPECT_EQ(count, 1);
	}
}

TEST(Image, BandsReportCancellation)
{
	int calls = 0;
	Error e = processBands(
	    RectI{0, 0, 4, 4}, 4, [](unsigned count, const std::function<void(unsigned)>& fn) {
		    for (unsigned i = 0; i < count; ++i) {
			    fn(i);
		    }
	    },
	    [&]() { return calls >= 2; }, [&](const RectI&) { ++calls; });
	EXPECT_TRUE(e.isCancellation());
	EXPECT_EQ(calls, 2);
	EXPECT_TRUE(processBands(RectI{}, 4, nullptr, nullptr, nullptr).ok());
}

TEST(Image, GpuApiNames)
{
	EXPECT_EQ(toString(GpuApi::None), "cpu");
	EXPECT_EQ(toString(GpuApi::Metal), "metal");
	EXPECT_EQ(toString(GpuApi::D3D12), "d3d12");
	GpuImage g;
	EXPECT_EQ(g.api, GpuApi::None);
	EXPECT_FALSE(g.needsIntermediateCopy);
}

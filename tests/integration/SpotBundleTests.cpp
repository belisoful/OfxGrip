// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The Spot bundle: a kernel effect whose parameters come from its Slang metadata block, rendered
// on the CPU on every platform, on Metal on macOS, and on the Vulkan accelerator where a device
// opens, with the same pixels.
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#endif

#if defined(OFXGRIP_HAS_VULKAN)
#include <ofxgrip/gpu/VulkanBackend.hpp>

#include "../TestEnvironment.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#endif

#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

class SpotBundle : public ::testing::Test {
protected:
	void SetUp() override
	{
		Error e = bundle_.open(OFXGRIP_SPOT_BUNDLE);
		ASSERT_TRUE(e.ok()) << e.describe();
		ASSERT_EQ(bundle_.pluginCount(), 1);
		plugin_ = bundle_.plugin(0);
		ASSERT_NE(plugin_, nullptr);
	}
	void TearDown() override
	{
		instance_.reset();
		if (host_) {
			host_->unload();
		}
		host_.reset();
	}
	void start(HostOptions options = HostOptions::spec(), int width = 32, int height = 16)
	{
		host_ = std::make_unique<MiniHost>(options);
		bundle_.setHost(host_->ofxHost());
		ASSERT_EQ(host_->load(plugin_, OFXGRIP_SPOT_BUNDLE), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = width;
		project.height = height;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	static float at(const ImageBuffer& image, int x, int y, std::size_t c) { return readComponent(image.view().pixel(x, y), image.desc().depth, c); }
	Bundle bundle_;
	OfxPlugin* plugin_{nullptr};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(SpotBundle, DescribesTheKernelsParametersAndPaths)
{
	start();
	EXPECT_STREQ(plugin_->pluginIdentifier, "com.belisoful.ofxgrip.spot");
	PropSet* props = host_->descriptorProps();
	EXPECT_EQ(props->getString(kOfxPropLabel), "OfxGrip Spot");
	EXPECT_EQ(props->getString(kOfxImageEffectPropCPURenderSupported), "true");
#if defined(OFXGRIP_HAS_METAL)
	EXPECT_EQ(props->getString(kOfxImageEffectPropMetalRenderSupported), "true");
#endif
	EXPECT_EQ(props->getString(kOfxImageEffectPropCudaRenderSupported), "false");
	EXPECT_EQ(props->getString(kOfxImageEffectPropOpenCLRenderSupported), "false");
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	ASSERT_NE(params->find("about"), nullptr);
	Param* center = params->find("center");
	ASSERT_NE(center, nullptr);
	EXPECT_EQ(center->type(), kOfxParamTypeDouble2D);
	EXPECT_EQ(center->props().getDouble(kOfxParamPropDefault, 0.0, 0), 960.0);
	Param* radius = params->find("radius");
	ASSERT_NE(radius, nullptr);
	EXPECT_EQ(radius->props().getDouble(kOfxParamPropDefault), 300.0);
	EXPECT_EQ(radius->props().getDouble(kOfxParamPropDisplayMax), 2000.0);
	EXPECT_EQ(params->find("tint")->type(), kOfxParamTypeRGB);
	EXPECT_EQ(params->find("tint")->props().getString(kOfxParamPropParent), "Look");
	EXPECT_EQ(params->find("INVERT")->type(), kOfxParamTypeBoolean);
	EXPECT_EQ(host_->contextClips(kOfxImageEffectContextFilter), (std::vector<std::string>{"Source", "Output"}));
}

TEST_F(SpotBundle, RendersTheSpotlightOnTheCpu)
{
	start();
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance_->setParam("center", {8.0, 8.0});
	instance_->setParam("radius", {4.0});
	instance_->setParam("softness", {0.0});
	instance_->setParam("outsideGain", {0.0});
	for (BitDepth depth : {BitDepth::Float, BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half}) {
		instance_->setInput("Source", makeRampImage(32, 16, depth));
		RenderRequest request;
		request.depth = depth;
		request.tileSize = 7;
		request.threads = 3;
		RenderResult result = instance_->render(request);
		ASSERT_TRUE(result.ok()) << toString(depth);
		EXPECT_EQ(result.gpu, GpuApi::None);
		// Inside the circle the ramp passes; outside it is black; alpha stays 1.
		EXPECT_NEAR(at(result.image, 8, 8, 0), 8.5f / 31.0f, 0.03f) << toString(depth);
		EXPECT_NEAR(at(result.image, 30, 8, 0), 0.0f, 0.01f) << toString(depth);
		EXPECT_NEAR(at(result.image, 30, 8, 3), 1.0f, 0.01f) << toString(depth);
	}
	// Invert swaps the inside and the outside; the specialization constant builds another pipeline.
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance_->setParam("INVERT", {1.0});
	RenderRequest inverted;
	RenderResult result = instance_->render(inverted);
	ASSERT_TRUE(result.ok());
	EXPECT_NEAR(at(result.image, 8, 8, 0), 0.0f, 0.01f);
	EXPECT_NEAR(at(result.image, 30, 8, 0), 30.5f / 31.0f, 0.03f);
	// Render scale halves the pixel-unit parameters with the frame.
	instance_->setParam("INVERT", {0.0});
	RenderRequest half;
	half.renderScale = {0.5, 0.5};
	RenderResult scaled = instance_->render(half);
	ASSERT_TRUE(scaled.ok());
	EXPECT_EQ(scaled.image.desc().bounds.width(), 16);
	// The mini host hands the full-size source, so the scaled frame samples it at 4.5 / 32.
	EXPECT_NEAR(at(scaled.image, 4, 4, 0), 4.0f / 31.0f, 0.03f);
	EXPECT_NEAR(at(scaled.image, 15, 4, 0), 0.0f, 0.01f);
	// Without a source the kernel reads transparent black.
	instance_->disconnect("Source");
	RenderResult none = instance_->render(inverted);
	ASSERT_TRUE(none.ok());
	EXPECT_EQ(at(none.image, 8, 8, 3), 0.0f);
}

#if defined(OFXGRIP_HAS_METAL)

TEST_F(SpotBundle, MetalRenderMatchesTheCpuRender)
{
	if (!metal::MetalContext::available()) {
		GTEST_SKIP() << "No Metal device";
	}
	HostOptions options = HostOptions::spec();
	options.metalRenderSupported = true;
	start(options);
	instance_->setParam("center", {10.0, 6.0});
	instance_->setParam("radius", {5.0});
	instance_->setParam("softness", {6.0});
	instance_->setParam("outsideGain", {0.3});
	instance_->setParam("tint", {1.0, 0.5, 0.25});
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		instance_->setInput("Source", makeRampImage(32, 16, depth));
		RenderRequest cpu;
		cpu.depth = depth;
		RenderResult a = instance_->render(cpu);
		RenderRequest gpu = cpu;
		gpu.gpu = GpuApi::Metal;
		RenderResult b = instance_->render(gpu);
		ASSERT_TRUE(a.ok() && b.ok()) << toString(depth);
		ASSERT_EQ(b.gpu, GpuApi::Metal);
		for (int y = 0; y < 16; ++y) {
			for (int x = 0; x < 32; ++x) {
				for (std::size_t c = 0; c < 4; ++c) {
					ASSERT_NEAR(at(a.image, x, y, c), at(b.image, x, y, c), 0.02f) << toString(depth) << " at " << x << "," << y << " channel " << c;
				}
			}
		}
	}
	// Tiles on Metal frames render through the intermediate destination and match too.
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	RenderRequest whole;
	RenderResult reference = instance_->render(whole);
	RenderRequest tiled;
	tiled.gpu = GpuApi::Metal;
	tiled.tileSize = 9;
	RenderResult tiles = instance_->render(tiled);
	ASSERT_TRUE(reference.ok() && tiles.ok());
	ASSERT_EQ(tiles.gpu, GpuApi::Metal);
	EXPECT_EQ(tiles.tileCount, 8);
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 32; ++x) {
			ASSERT_NEAR(at(reference.image, x, y, 0), at(tiles.image, x, y, 0), 0.002f) << x << "," << y;
		}
	}
}

#endif

#if defined(OFXGRIP_HAS_VULKAN)

namespace {

std::string spotLog()
{
	const char* directory = std::getenv("OFXGRIP_LOG_DIR");
	if (directory == nullptr) {
		return {};
	}
	std::ifstream in(std::filesystem::path(directory) / "com.belisoful.ofxgrip.spot.log", std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

}  // namespace

TEST_F(SpotBundle, VulkanAcceleratorMatchesTheCpuRender)
{
	if (!vulkan::VulkanContext::available()) {
		GTEST_SKIP() << "No Vulkan device: " << vulkan::VulkanContext::unavailableReason();
	}
	if (std::getenv("OFXGRIP_LOG_DIR") == nullptr) {
		GTEST_SKIP() << "The test reads the plug-in's log from OFXGRIP_LOG_DIR";
	}
	// The accelerator is chosen at the load action, so each pass loads the bundle afresh.
	auto renderEveryDepth = [this](const char* accelerator) {
		ofxgrip::testing::setTestEnv("OFXGRIP_KERNEL_ACCELERATOR", accelerator);
		start();
		ofxgrip::testing::unsetTestEnv("OFXGRIP_KERNEL_ACCELERATOR");
		instance_->setParam("center", {10.0, 6.0});
		instance_->setParam("radius", {5.0});
		instance_->setParam("softness", {6.0});
		instance_->setParam("outsideGain", {0.3});
		instance_->setParam("tint", {1.0, 0.5, 0.25});
		std::vector<ImageBuffer> frames;
		for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
			instance_->setInput("Source", makeRampImage(32, 16, depth));
			RenderRequest request;
			request.depth = depth;
			RenderResult whole = instance_->render(request);
			EXPECT_TRUE(whole.ok()) << toString(depth);
			EXPECT_EQ(whole.gpu, GpuApi::None);
			frames.push_back(std::move(whole.image));
			request.tileSize = 7;
			request.threads = 3;
			RenderResult tiled = instance_->render(request);
			EXPECT_TRUE(tiled.ok()) << toString(depth);
			frames.push_back(std::move(tiled.image));
		}
		instance_->setParam("INVERT", {1.0});
		RenderResult inverted = instance_->render(RenderRequest{});
		EXPECT_TRUE(inverted.ok());
		frames.push_back(std::move(inverted.image));
		instance_.reset();
		host_->unload();
		host_.reset();
		return frames;
	};
	const std::vector<ImageBuffer> cpu = renderEveryDepth("none");
	const std::size_t logged = spotLog().size();
	const std::vector<ImageBuffer> accelerated = renderEveryDepth("vulkan");
	ASSERT_EQ(cpu.size(), accelerated.size());
	for (std::size_t i = 0; i < cpu.size(); ++i) {
		ASSERT_EQ(cpu[i].desc(), accelerated[i].desc()) << "frame " << i;
		const RectI& b = cpu[i].desc().bounds;
		for (int y = b.y1; y < b.y2; ++y) {
			for (int x = b.x1; x < b.x2; ++x) {
				for (std::size_t c = 0; c < 4; ++c) {
					ASSERT_NEAR(at(cpu[i], x, y, c), at(accelerated[i], x, y, c), 0.02f) << "frame " << i << " at " << x << "," << y << " channel " << c;
				}
			}
		}
	}
	// A failed accelerated dispatch falls back and says so; the load says which backend runs.
	const std::string log = spotLog().substr(logged);
	EXPECT_NE(log.find("Kernels render CPU frames on vulkan"), std::string::npos) << log;
	EXPECT_EQ(log.find("unavailable"), std::string::npos) << log;
	EXPECT_EQ(log.find("falls back to the CPU backend"), std::string::npos) << log;
}

TEST_F(SpotBundle, AnUnknownAcceleratorNameIsReportedAndIgnored)
{
	if (std::getenv("OFXGRIP_LOG_DIR") == nullptr) {
		GTEST_SKIP() << "The test reads the plug-in's log from OFXGRIP_LOG_DIR";
	}
	const std::size_t logged = spotLog().size();
	ofxgrip::testing::setTestEnv("OFXGRIP_KERNEL_ACCELERATOR", "abacus");
	start();
	ofxgrip::testing::unsetTestEnv("OFXGRIP_KERNEL_ACCELERATOR");
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	EXPECT_TRUE(instance_->render(RenderRequest{}).ok());
	EXPECT_NE(spotLog().substr(logged).find("names no accelerator: abacus"), std::string::npos);
}

#endif

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The inference effect through the adapter and the mini host: the passthrough default, a
// backend's output in every destination depth, the per-frame cache and its signature, a backend
// that is not ready, a result of another size, an abort, and the failures.
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/InferenceEffect.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxImageEffect.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

enum : ParameterId { kStrength = 1 };

/*! Scales the colors by `strength`. A test steers its readiness, its failures, and its pace. */
class Dimmer final : public InferenceBackend {
public:
	bool isReady() const noexcept override { return ready.load(); }
	std::string identifier() const override { return name; }
	Error run(const InferenceRequest& request, InferenceResult& result) override
	{
		++runs;
		while (hold.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (fails) {
			return Error::make(ErrorCode::Render, "The model failed");
		}
		if (silent) {
			return Error::none();
		}
		const InferenceImage source = request.inputs.at("image").image;
		const float strength = static_cast<float>(request.parameters.at("strength").toDouble(1.0));
		ImageDesc desc = source->desc();
		if (halfSize) {
			desc.bounds = {0, 0, desc.bounds.width() / 2, desc.bounds.height() / 2};
			desc.regionOfDefinition = desc.bounds;
		}
		desc.depth = BitDepth::Float;
		desc.components = Components::RGBA;
		auto out = std::make_shared<ImageBuffer>(desc);
		const ImageView from = source->view();
		const ImageView to = out->view();
		const int step = halfSize ? 2 : 1;
		for (int y = desc.bounds.y1; y < desc.bounds.y2; ++y) {
			for (int x = desc.bounds.x1; x < desc.bounds.x2; ++x) {
				const std::uint8_t* pixel = from.pixel(x * step, y * step);
				for (std::size_t c = 0; c < 4; ++c) {
					const float value = readComponent(pixel, source->desc().depth, c);
					writeComponent(to.pixel(x, y), BitDepth::Float, c, c == 3 ? value : value * strength);
				}
			}
		}
		result.outputs["image"] = InferenceItem(InferenceImage(std::move(out)));
		return Error::none();
	}
	std::string name{"dimmer"};
	std::atomic<bool> ready{true};
	std::atomic<bool> hold{false};
	std::atomic<int> runs{0};
	bool fails{false};
	bool silent{false};
	bool halfSize{false};
};

class DimEffect final : public InferenceEffect {
public:
	std::map<std::string, Value> inferenceParameters(const RenderSnapshot& snapshot) const override { return {{"strength", Value(snapshot.number(kStrength, 1.0))}}; }
};

std::shared_ptr<InferenceBackend> gBackend;
DimEffect* gEffect = nullptr;

class DimFactory final : public EffectFactory {
public:
	DimFactory()
	{
		manifest_.identity.ofxId = "com.belisoful.ofxgrip.inferencetest";
		manifest_.identity.uuid = "66666666-6666-4666-8666-666666666666";
		manifest_.identity.name = "Inference Test";
		manifest_.identity.grouping = "OfxGrip Tests";
		manifest_.version = Version{1, 0, 0};
		manifest_.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
		manifest_.capabilities.supportsTiles = false;
		manifest_.parameters.push_back(ParameterSpec::floatParam(kStrength, "strength", "Strength", 0.5, 0, 1));
	}
	const EffectManifest& manifest() const override { return manifest_; }
	std::unique_ptr<Effect> create() override
	{
		auto effect = std::make_unique<DimEffect>();
		if (gBackend) {
			effect->setInferenceBackend(gBackend);
		}
		gEffect = effect.get();
		return effect;
	}

private:
	EffectManifest manifest_;
};

class InferenceEffectTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		PluginRegistry::instance().clear();
		gBackend.reset();
		gEffect = nullptr;
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
		gBackend.reset();
		gEffect = nullptr;
	}
	void start(std::shared_ptr<Dimmer> backend)
	{
		backend_ = backend;
		gBackend = backend;
		const int index = PluginRegistry::instance().registerFactory(std::make_unique<DimFactory>());
		ASSERT_GE(index, 0);
		host_ = std::make_unique<MiniHost>(HostOptions::spec());
		ASSERT_EQ(host_->load(PluginRegistry::instance().plugin(index), "/tmp/inference.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 32;
		project.height = 16;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
		ASSERT_NE(gEffect, nullptr);
		instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	}
	static float at(const ImageBuffer& image, int x, int y, std::size_t c) { return readComponent(image.view().pixel(x, y), image.desc().depth, c); }
	std::shared_ptr<Dimmer> backend_;
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(InferenceEffectTest, ThePassthroughRendersTheSourceUnchanged)
{
	start(nullptr);
	EXPECT_EQ(gEffect->inferenceBackend()->identifier(), "passthrough");
	EXPECT_EQ(gEffect->inputImageName(), "image");
	EXPECT_EQ(gEffect->outputImageName(), "image");
	EXPECT_TRUE(gEffect->cacheEnabled());
	const std::shared_ptr<ImageBuffer> ramp = makeRampImage(32, 16, BitDepth::Float);
	const ImageBuffer& source = *ramp;
	RenderResult result = instance_->render(RenderRequest{});
	ASSERT_TRUE(result.ok());
	for (int x : {0, 9, 31}) {
		EXPECT_FLOAT_EQ(at(result.image, x, 7, 0), at(source, x, 7, 0)) << x;
	}
	// Without a source the frame is transparent black.
	instance_->disconnect("Source");
	RenderResult none = instance_->render(RenderRequest{});
	ASSERT_TRUE(none.ok());
	EXPECT_EQ(at(none.image, 9, 7, 3), 0.0f);
}

TEST_F(InferenceEffectTest, RendersTheBackendsOutputInEveryDepthAndCachesTheFrame)
{
	auto dimmer = std::make_shared<Dimmer>();
	start(dimmer);
	const std::shared_ptr<ImageBuffer> ramp = makeRampImage(32, 16, BitDepth::Float);
	const ImageBuffer& source = *ramp;
	const float bright = at(source, 20, 7, 0);
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		RenderRequest request;
		request.depth = depth;
		RenderResult result = instance_->render(request);
		ASSERT_TRUE(result.ok()) << toString(depth);
		EXPECT_NEAR(at(result.image, 20, 7, 0), bright * 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(at(result.image, 20, 7, 3), 1.0f, 0.01f) << toString(depth);
	}
	// One run served the four depths: the frame and its signature did not change.
	EXPECT_EQ(dimmer->runs.load(), 1);
	EXPECT_EQ(gEffect->inferenceCache().frameCount(), 1u);
	// Another frame runs again; the first frame is still held.
	RenderRequest later;
	later.time = 3;
	ASSERT_TRUE(instance_->render(later).ok());
	EXPECT_EQ(dimmer->runs.load(), 2);
	ASSERT_TRUE(instance_->render(RenderRequest{}).ok());
	EXPECT_EQ(dimmer->runs.load(), 2);
	// A parameter changes the signature: the cache starts over.
	instance_->setParam("strength", {0.25});
	RenderResult dimmerStill = instance_->render(RenderRequest{});
	ASSERT_TRUE(dimmerStill.ok());
	EXPECT_NEAR(at(dimmerStill.image, 20, 7, 0), bright * 0.25f, 0.01f);
	EXPECT_EQ(dimmer->runs.load(), 3);
	EXPECT_EQ(gEffect->inferenceCache().frameCount(), 1u);
	// So does another backend.
	auto other = std::make_shared<Dimmer>();
	other->name = "dimmer2";
	gEffect->setInferenceBackend(other);
	ASSERT_TRUE(instance_->render(RenderRequest{}).ok());
	EXPECT_EQ(other->runs.load(), 1);
	// With the cache off every render runs the model.
	gEffect->setCacheEnabled(false);
	ASSERT_TRUE(instance_->render(RenderRequest{}).ok());
	ASSERT_TRUE(instance_->render(RenderRequest{}).ok());
	EXPECT_EQ(other->runs.load(), 3);
	// A null backend restores the passthrough.
	gEffect->setInferenceBackend(nullptr);
	EXPECT_EQ(gEffect->inferenceBackend()->identifier(), "passthrough");
}

TEST_F(InferenceEffectTest, AnUnreadyBackendRendersTheSourceAndAResultOfAnotherSizeIsResampled)
{
	auto dimmer = std::make_shared<Dimmer>();
	dimmer->ready = false;
	start(dimmer);
	const std::shared_ptr<ImageBuffer> ramp = makeRampImage(32, 16, BitDepth::Float);
	const ImageBuffer& source = *ramp;
	RenderResult waiting = instance_->render(RenderRequest{});
	ASSERT_TRUE(waiting.ok());
	EXPECT_FLOAT_EQ(at(waiting.image, 20, 7, 0), at(source, 20, 7, 0));
	EXPECT_EQ(dimmer->runs.load(), 0);
	dimmer->ready = true;
	dimmer->halfSize = true;
	RenderResult result = instance_->render(RenderRequest{});
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.image.desc().bounds.width(), 32);
	EXPECT_NEAR(at(result.image, 20, 7, 0), at(source, 20, 7, 0) * 0.5f, 0.03f);
	EXPECT_NEAR(at(result.image, 20, 7, 3), 1.0f, 0.01f);
}

TEST_F(InferenceEffectTest, ReportsAFailedRunAndAResultWithNoImage)
{
	auto dimmer = std::make_shared<Dimmer>();
	dimmer->fails = true;
	start(dimmer);
	EXPECT_FALSE(instance_->render(RenderRequest{}).ok());
	EXPECT_EQ(gEffect->inferenceCache().frameCount(), 0u);
	dimmer->fails = false;
	dimmer->silent = true;
	EXPECT_FALSE(instance_->render(RenderRequest{}).ok());
	dimmer->silent = false;
	EXPECT_TRUE(instance_->render(RenderRequest{}).ok());
}

TEST_F(InferenceEffectTest, AnAbortedRenderReturnsAndTheRunStillFillsTheCache)
{
	auto dimmer = std::make_shared<Dimmer>();
	dimmer->hold = true;
	start(dimmer);
	std::thread aborter([&]() {
		while (dimmer->runs.load() == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		instance_->abortFlag = true;
	});
	// The render returns while the model is still held: the adapter answers an aborted render
	// with success, as OpenFX asks, and the host discards the frame.
	(void)instance_->render(RenderRequest{});
	aborter.join();
	EXPECT_TRUE(dimmer->hold.load());
	EXPECT_EQ(gEffect->inferenceCache().frameCount(), 0u);
	instance_->abortFlag = false;
	dimmer->hold = false;
	// The run finishes on the runner's thread and keeps its frame.
	for (int i = 0; i < 2000 && gEffect->inferenceCache().frameCount() == 0; ++i) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	EXPECT_EQ(gEffect->inferenceCache().frameCount(), 1u);
	RenderResult served = instance_->render(RenderRequest{});
	ASSERT_TRUE(served.ok());
	EXPECT_EQ(dimmer->runs.load(), 1);
}

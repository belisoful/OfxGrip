// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The inference seam: the passthrough backend, the per-frame cache, the runner that keeps a
// backend off the calling thread, and the image conversion a result is written with.
#include <ofxgrip/core/Inference.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace ofxgrip;

namespace {

InferenceImage image(int width, int height, BitDepth depth = BitDepth::Float, Components components = Components::RGBA, float seed = 0.0f)
{
	ImageDesc desc;
	desc.bounds = {0, 0, width, height};
	desc.regionOfDefinition = desc.bounds;
	desc.depth = depth;
	desc.components = components;
	auto buffer = std::make_shared<ImageBuffer>(desc);
	const ImageView v = buffer->view();
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			for (std::size_t c = 0; c < componentCount(components); ++c) {
				writeComponent(v.pixel(x, y), depth, c, seed + static_cast<float>(x) / static_cast<float>(width) + static_cast<float>(c) * 0.1f);
			}
		}
	}
	return buffer;
}

float at(const ImageBuffer& buffer, int x, int y, std::size_t c)
{
	return readComponent(buffer.view().pixel(x, y), buffer.desc().depth, c);
}

/*! A backend a test steers: it can wait, fail, throw, and count its runs. */
class Steered final : public InferenceBackend {
public:
	bool isReady() const noexcept override { return true; }
	std::string identifier() const override { return "steered"; }
	Error run(const InferenceRequest& request, InferenceResult& result) override
	{
		++runs;
		runThread = std::this_thread::get_id();
		while (hold.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (throws) {
			throw std::runtime_error("model exploded");
		}
		if (fails) {
			return Error::make(ErrorCode::Render, "The model failed");
		}
		result.outputs["image"] = request.inputs.at("image");
		result.outputs["count"] = InferenceItem(Value(runs.load()));
		return Error::none();
	}
	std::atomic<int> runs{0};
	std::atomic<bool> hold{false};
	bool fails{false};
	bool throws{false};
	std::thread::id runThread;
};

InferenceRequest requestWith(InferenceImage picture)
{
	InferenceRequest request;
	request.inputs["image"] = InferenceItem(std::move(picture));
	request.parameters["strength"] = Value(0.5);
	return request;
}

}  // namespace

TEST(Inference, PassthroughReturnsItsInputImage)
{
	PassthroughInferenceBackend backend;
	EXPECT_TRUE(backend.isReady());
	EXPECT_EQ(backend.identifier(), "passthrough");
	EXPECT_TRUE(backend.prepare().ok());
	const InferenceImage picture = image(4, 2);
	InferenceResult result;
	ASSERT_TRUE(backend.run(requestWith(picture), result).ok());
	EXPECT_EQ(result.image("image"), picture);
	EXPECT_EQ(result.image("missing"), nullptr);
	// A value under the image's name, and no input at all, are refused.
	InferenceRequest wrong;
	wrong.inputs["image"] = InferenceItem(Value("a prompt"));
	EXPECT_FALSE(wrong.inputs["image"].isImage());
	EXPECT_EQ(backend.run(wrong, result).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(backend.run(InferenceRequest{}, result).code(), ErrorCode::InvalidParameter);
	// The port names are the effect's to choose.
	PassthroughInferenceBackend renamed("source", "matte");
	InferenceRequest named;
	named.inputs["source"] = InferenceItem(picture);
	InferenceResult matte;
	ASSERT_TRUE(renamed.run(named, matte).ok());
	EXPECT_EQ(matte.image("matte"), picture);
}

TEST(InferenceCache, KeepsFramesUnderOneSignatureWithinItsLimit)
{
	const InferenceImage a = image(8, 8);  // 1024 bytes
	InferenceCache cache(3 * a->bytes().size());
	EXPECT_EQ(cache.byteLimit(), 3072u);
	EXPECT_EQ(cache.find(1, "s"), nullptr);
	cache.store(1, "s", a);
	cache.store(2, "s", image(8, 8, BitDepth::Float, Components::RGBA, 1.0f));
	cache.store(3, "s", image(8, 8));
	cache.store(4, "s", nullptr);
	EXPECT_EQ(cache.frameCount(), 3u);
	EXPECT_EQ(cache.byteCount(), 3072u);
	EXPECT_EQ(cache.find(1, "s"), a);
	// Frame 1 was just used, so the fourth frame pushes frame 2 out.
	cache.store(4, "s", image(8, 8));
	EXPECT_EQ(cache.frameCount(), 3u);
	EXPECT_EQ(cache.find(2, "s"), nullptr);
	EXPECT_EQ(cache.find(1, "s"), a);
	// Storing a frame again replaces it without counting it twice.
	const InferenceImage replaced = image(8, 8);
	cache.store(1, "s", replaced);
	EXPECT_EQ(cache.find(1, "s"), replaced);
	EXPECT_EQ(cache.byteCount(), 3072u);
	// Another signature finds nothing, and a store under it clears the rest.
	EXPECT_EQ(cache.find(1, "t"), nullptr);
	cache.store(9, "t", a);
	EXPECT_EQ(cache.frameCount(), 1u);
	EXPECT_EQ(cache.find(1, "s"), nullptr);
	EXPECT_EQ(cache.find(9, "t"), a);
	// An image larger than the limit is not kept, and takes the frame's old entry with it.
	cache.store(9, "t", image(64, 64));
	EXPECT_EQ(cache.frameCount(), 0u);
	EXPECT_EQ(cache.byteCount(), 0u);
	cache.store(5, "t", a);
	cache.clear();
	EXPECT_EQ(cache.frameCount(), 0u);
	EXPECT_EQ(cache.find(5, "t"), nullptr);
}

TEST(InferenceRunner, RunsOffTheCallingThreadAndReturnsTheResult)
{
	InferenceRunner runner;
	auto backend = std::make_shared<Steered>();
	const InferenceImage picture = image(4, 4);
	InferenceResult result;
	int completions = 0;
	const Error e = runner.run(backend, requestWith(picture), result, {}, [&](const Error& error, const InferenceResult& finished) {
		EXPECT_TRUE(error.ok());
		EXPECT_EQ(finished.image("image"), picture);
		++completions;
	});
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(result.image("image"), picture);
	EXPECT_EQ(result.outputs.at("count").value.toInt(), 1);
	EXPECT_EQ(completions, 1);
	EXPECT_NE(backend->runThread, std::this_thread::get_id());
	EXPECT_EQ(runner.pendingCount(), 0u);
	// A failure and a throw come back as errors; the result stays as it was.
	backend->fails = true;
	InferenceResult untouched;
	EXPECT_EQ(runner.run(backend, requestWith(picture), untouched).code(), ErrorCode::Render);
	EXPECT_TRUE(untouched.outputs.empty());
	backend->fails = false;
	backend->throws = true;
	const Error thrown = runner.run(backend, requestWith(picture), untouched);
	EXPECT_EQ(thrown.code(), ErrorCode::Render);
	EXPECT_NE(thrown.describe().find("model exploded"), std::string::npos);
	EXPECT_EQ(runner.run(nullptr, requestWith(picture), untouched).code(), ErrorCode::InvalidParameter);
}

TEST(InferenceRunner, AnAbortReturnsAtOnceAndTheRunStillCompletes)
{
	auto backend = std::make_shared<Steered>();
	std::atomic<int> completions{0};
	auto runner = std::make_unique<InferenceRunner>();
	backend->hold = true;
	InferenceResult result;
	std::atomic<bool> abort{false};
	std::thread canceller([&]() {
		while (backend->runs.load() == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		abort = true;
	});
	const Error e = runner->run(backend, requestWith(image(4, 4)), result, [&]() { return abort.load(); }, [&](const Error& error, const InferenceResult&) {
		EXPECT_TRUE(error.ok());
		++completions;
	});
	canceller.join();
	EXPECT_TRUE(e.isCancellation());
	EXPECT_TRUE(result.outputs.empty());
	EXPECT_EQ(completions.load(), 0);
	EXPECT_EQ(runner->pendingCount(), 1u);

	// A second run queues behind the held one. Destroying the runner finishes the run in
	// progress, whose completion still fires, and cancels the queued one.
	Error queued;
	std::thread second([&, waiting = runner.get()]() {
		InferenceResult dropped;
		queued = waiting->run(backend, requestWith(image(4, 4)), dropped);
	});
	while (runner->pendingCount() < 2) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::thread releaser([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		backend->hold = false;
	});
	runner.reset();
	second.join();
	releaser.join();
	EXPECT_TRUE(queued.isCancellation());
	EXPECT_EQ(completions.load(), 1);
	EXPECT_EQ(backend->runs.load(), 1);
}

TEST(Inference, ConvertsDepthLayoutAndSize)
{
	const InferenceImage source = image(8, 4);
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		ImageDesc desc = source->desc();
		desc.depth = depth;
		ImageBuffer out(desc);
		ASSERT_TRUE(convertInferenceImage(source->view(), out.view(), {}).ok());
		EXPECT_NEAR(at(out, 4, 2, 0), 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(at(out, 4, 2, 1), 0.6f, 0.01f) << toString(depth);
	}
	// A window limits what is written.
	ImageBuffer windowed(source->desc());
	ASSERT_TRUE(convertInferenceImage(source->view(), windowed.view(), {2, 1, 4, 3}).ok());
	EXPECT_FLOAT_EQ(at(windowed, 2, 1, 0), 0.25f);
	EXPECT_FLOAT_EQ(at(windowed, 4, 1, 0), 0.0f);
	EXPECT_FLOAT_EQ(at(windowed, 2, 0, 0), 0.0f);
	// A one-component destination takes the alpha; an RGB source reads with alpha 1; a
	// one-component source fills every channel.
	ImageDesc alphaDesc = source->desc();
	alphaDesc.components = Components::Alpha;
	ImageBuffer alpha(alphaDesc);
	ASSERT_TRUE(convertInferenceImage(source->view(), alpha.view(), {}).ok());
	EXPECT_NEAR(at(alpha, 4, 2, 0), 0.8f, 0.001f);
	const InferenceImage rgb = image(8, 4, BitDepth::Float, Components::RGB);
	ImageBuffer fromRgb(source->desc());
	ASSERT_TRUE(convertInferenceImage(rgb->view(), fromRgb.view(), {}).ok());
	EXPECT_FLOAT_EQ(at(fromRgb, 4, 2, 3), 1.0f);
	const InferenceImage matte = image(8, 4, BitDepth::Float, Components::Alpha, 0.25f);
	ImageBuffer fromAlpha(source->desc());
	ASSERT_TRUE(convertInferenceImage(matte->view(), fromAlpha.view(), {}).ok());
	EXPECT_FLOAT_EQ(at(fromAlpha, 4, 2, 0), 0.75f);
	EXPECT_FLOAT_EQ(at(fromAlpha, 4, 2, 3), 0.75f);
	// A result of another size is resampled onto the destination's bounds.
	ImageDesc bigDesc = source->desc();
	bigDesc.bounds = {10, 20, 26, 28};
	ImageBuffer big(bigDesc);
	ASSERT_TRUE(convertInferenceImage(source->view(), big.view(), {}).ok());
	EXPECT_NEAR(at(big, 18, 24, 0), 0.47f, 0.05f);   // the middle of the ramp
	EXPECT_NEAR(at(big, 10, 20, 0), 0.0f, 0.02f);    // the edge texel holds
	EXPECT_NEAR(at(big, 25, 27, 0), 0.875f, 0.02f);
	ImageDesc smallDesc = source->desc();
	smallDesc.bounds = {0, 0, 4, 2};
	ImageBuffer small(smallDesc);
	ASSERT_TRUE(convertInferenceImage(source->view(), small.view(), {}).ok());
	EXPECT_NEAR(at(small, 2, 1, 0), 0.5625f, 0.01f);
	// Two images are needed.
	EXPECT_EQ(convertInferenceImage(ImageView{}, small.view(), {}).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(convertInferenceImage(source->view(), ImageView{}, {}).code(), ErrorCode::InvalidParameter);
}

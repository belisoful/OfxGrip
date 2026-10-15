// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The kernel engine over the two fixture kernels: the CPU backend on every platform, the Metal
// backend on macOS, and the conformance between them: the same dispatch produces the same pixels.
#include <ofxgrip/gpu/CpuKernelBackend.hpp>
#include <ofxgrip/gpu/KernelCpu.hpp>
#include <ofxgrip/gpu/KernelEngine.hpp>
#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/core/KernelTile.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#include <ofxgrip/gpu/MetalKernelBackend.hpp>
#endif

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <thread>

OFXGRIP_DECLARE_KERNEL(GripBlur);
OFXGRIP_DECLARE_KERNEL(GripInspect);

using namespace ofxgrip;

namespace {

/*! A fake threading service that really runs in parallel. */
class Threads final : public ThreadingService {
public:
	unsigned cpuCount() override { return 3; }
	Error parallelFor(unsigned count, const std::function<void(unsigned, unsigned)>& work) override
	{
		std::vector<std::thread> pool;
		for (unsigned i = 1; i < count; ++i) {
			pool.emplace_back([&work, i, count]() { work(i, count); });
		}
		work(0, count);
		for (std::thread& t : pool) {
			t.join();
		}
		return Error::none();
	}
	bool isSpawnedThread() override { return false; }
};

/*! A kernel resolved for one API, with its layout and plan. */
struct Prepared {
	KernelRecord record;
	KernelCatalog catalog;
	KernelMetadata metadata;
	KernelLayout layout;
	KernelBindingPlan plan;
	const KernelImplementation* implementation{nullptr};

	Prepared(const EmbeddedKernel& kernel, GpuApi api) : record(kernelRecordFor(kernel))
	{
		Error e = KernelMetadata::scan(record.slangSource, metadata);
		EXPECT_TRUE(e.ok()) << e.describe();
		catalog.add(record);
		implementation = catalog.resolve(record.name, api, "", "");
		if (implementation == nullptr) {
			return;
		}
		e = KernelLayout::parse(implementation->layoutJson, layout);
		EXPECT_TRUE(e.ok()) << e.describe();
		e = KernelBindingPlan::resolve(layout, metadata, plan);
		EXPECT_TRUE(e.ok()) << e.describe();
	}

	KernelDispatch dispatch(const KernelValueMap& values, double pixelScale = 1.0) const
	{
		KernelDispatch d;
		d.layout = &layout;
		d.implementation = implementation;
		d.plan = &plan;
		d.cacheKey = "ofxgrip.tests." + record.name;
		d.uniforms = packKernelUniforms(layout, plan, values, pixelScale);
		d.constants = packKernelConstants(plan, values, pixelScale);
		d.pixelScale = {pixelScale, pixelScale};
		return d;
	}
};

ImageBuffer image(const RectI& bounds, BitDepth depth, Components components = Components::RGBA)
{
	ImageDesc desc;
	desc.bounds = bounds;
	desc.regionOfDefinition = bounds;
	desc.depth = depth;
	desc.components = components;
	return ImageBuffer(desc);
}

/*! A ramp: r = x / width, g = y / height, b = 0.25, a = 1. */
ImageBuffer ramp(const RectI& bounds, BitDepth depth, Components components = Components::RGBA)
{
	ImageBuffer out = image(bounds, depth, components);
	const ImageView v = out.view();
	for (int y = bounds.y1; y < bounds.y2; ++y) {
		for (int x = bounds.x1; x < bounds.x2; ++x) {
			const float values[4] = {static_cast<float>(x - bounds.x1) / static_cast<float>(bounds.width()), static_cast<float>(y - bounds.y1) / static_cast<float>(bounds.height()), 0.25f, 1.0f};
			for (std::size_t c = 0; c < componentCount(components); ++c) {
				writeComponent(v.pixel(x, y), depth, c, components == Components::Alpha ? 0.75f : values[c]);
			}
		}
	}
	return out;
}

KernelImage cpuImage(ImageBuffer& buffer, bool connected = true)
{
	KernelImage k;
	k.connected = connected;
	k.cpu = buffer.view();
	return k;
}

float at(const ImageBuffer& buffer, int x, int y, std::size_t c)
{
	return readComponent(buffer.view().pixel(x, y), buffer.desc().depth, c);
}

// GripInspect's parameter IDs (its metadata block).
enum : ParameterId { kRadius = 1, kGain = 2, kCenter = 3, kInvert = 4, kCount = 5, kLevel = 6, kSmall = 7, kByte = 8, kMode = 9, kTaps = 10, kFill = 11, kRamp = 12, kMask = 13 };

KernelValueMap inspectValues(int mode)
{
	KernelValueMap values;
	values[kRadius] = Value(3.0);
	values[kGain] = Value(0.5);
	values[kCenter] = Value(Point2D{0.25, 0.75});
	values[kInvert] = Value(true);
	values[kCount] = Value(7);
	values[kLevel] = Value(0.5);
	values[kSmall] = Value(-3);
	values[kByte] = Value(200);
	values[kMode] = Value(mode);
	values[kTaps] = Value(2);
	values[kFill] = Value(0.125);
	return values;
}

}  // namespace

TEST(KernelEngine, RegistersTheCpuBackendAndRefusesIncompleteDispatches)
{
	KernelEngine& engine = KernelEngine::shared();
	ASSERT_NE(engine.backend(GpuApi::None), nullptr);
	EXPECT_TRUE(engine.backend(GpuApi::None)->available());
	EXPECT_EQ(engine.backend(GpuApi::Cuda), nullptr);
	const std::vector<GpuApi> apis = engine.availableApis();
	EXPECT_NE(std::find(apis.begin(), apis.end(), GpuApi::None), apis.end());
	KernelDispatch empty;
	EXPECT_EQ(engine.run(empty).code(), ErrorCode::InvalidParameter);
	engine.registerBackend(nullptr);
	Prepared blur(OFXGRIP_KERNEL(GripBlur), GpuApi::None);
	ASSERT_NE(blur.implementation, nullptr);
	KernelDispatch d = blur.dispatch({});
	d.destination.gpu.api = GpuApi::Cuda;
	EXPECT_EQ(engine.run(d).code(), ErrorCode::MissingHostFeature);
	// A Metal implementation handed to the CPU destination is refused.
	Prepared metalBlur(OFXGRIP_KERNEL(GripBlur), GpuApi::Metal);
	ASSERT_NE(metalBlur.implementation, nullptr);
	KernelDispatch wrong = metalBlur.dispatch({});
	ImageBuffer out = image({0, 0, 4, 4}, BitDepth::Float);
	wrong.destination = cpuImage(out);
	EXPECT_EQ(engine.run(wrong).code(), ErrorCode::InvalidParameter);
	// The record carries both builds, the Slang source, and the entry name.
	EXPECT_EQ(blur.record.implementations.size(), 2u);
	EXPECT_EQ(blur.record.implementations[0].api, GpuApi::Metal);
	EXPECT_EQ(blur.record.implementations[1].api, GpuApi::None);
	EXPECT_EQ(blur.record.implementations[1].entryPoint, "fxGripKernel");
	EXPECT_NE(blur.record.slangSource.find("FxGripShader"), std::string::npos);
}

TEST(KernelEngine, CpuBackendBindsValuesConstantsAndTextures)
{
	Prepared inspect(OFXGRIP_KERNEL(GripInspect), GpuApi::None);
	ASSERT_NE(inspect.implementation, nullptr);
	EXPECT_EQ(inspect.layout.form(), KernelBindingForm::Block);
	ImageBuffer first = ramp({0, 0, 8, 4}, BitDepth::Float);
	ImageBuffer second = ramp({0, 0, 8, 4}, BitDepth::UInt8);
	ImageBuffer mask = ramp({2, 1, 6, 3}, BitDepth::Half, Components::Alpha);
	ImageBuffer rampImage = ramp({0, 0, 8, 4}, BitDepth::UInt16);
	Threads threads;
	auto render = [&](int mode, const RectI& window, ImageBuffer& out, double pixelScale = 1.0) {
		KernelDispatch d = inspect.dispatch(inspectValues(mode), pixelScale);
		d.window = window;
		d.destination = cpuImage(out);
		d.sources = {cpuImage(first), cpuImage(second)};
		d.keyedImages[kRamp] = cpuImage(rampImage);
		d.keyedImages[kMask] = cpuImage(mask);
		d.threading = &threads;
		Error e = KernelEngine::shared().run(d);
		ASSERT_TRUE(e.ok()) << e.describe();
	};
	ImageBuffer out = image({0, 0, 8, 4}, BitDepth::Float);
	// Default: radius (pixelUnits × 2), gain, TAPS (constant, pixelUnits × 2), 1.
	render(0, {}, out, 2.0);
	EXPECT_FLOAT_EQ(at(out, 0, 0, 0), 6.0f);
	EXPECT_FLOAT_EQ(at(out, 7, 3, 1), 0.5f);
	EXPECT_FLOAT_EQ(at(out, 3, 2, 2), 4.0f);
	EXPECT_FLOAT_EQ(at(out, 3, 2, 3), 1.0f);
	// Mode 1: the image pixel of each texel (y up) and the pixel scale.
	render(1, {}, out, 0.5);
	EXPECT_FLOAT_EQ(at(out, 0, 0, 0), 0.5f);
	EXPECT_FLOAT_EQ(at(out, 0, 0, 1), 0.5f);
	EXPECT_FLOAT_EQ(at(out, 7, 3, 0), 7.5f);
	EXPECT_FLOAT_EQ(at(out, 7, 3, 1), 3.5f);
	EXPECT_FLOAT_EQ(at(out, 7, 3, 2), 0.5f);
	// Mode 2: texture coordinates of textures 0 and 1 (same bounds as the destination).
	render(2, {}, out);
	EXPECT_NEAR(at(out, 0, 0, 0), 0.5f / 8.0f, 1e-5f);
	EXPECT_NEAR(at(out, 0, 0, 1), 0.5f / 4.0f, 1e-5f);
	EXPECT_NEAR(at(out, 7, 3, 2), 7.5f / 8.0f, 1e-5f);
	EXPECT_NEAR(at(out, 7, 3, 3), 3.5f / 4.0f, 1e-5f);
	// Mode 3: first − second at the pixel centers: both ramps agree within 8-bit precision.
	render(3, {}, out);
	EXPECT_NEAR(at(out, 5, 2, 0), 0.0f, 0.01f);
	EXPECT_NEAR(at(out, 5, 2, 1), 0.0f, 0.01f);
	// Mode 4: center, invert, count.
	render(4, {}, out);
	EXPECT_FLOAT_EQ(at(out, 1, 1, 0), 0.25f);
	EXPECT_FLOAT_EQ(at(out, 1, 1, 1), 0.75f);
	EXPECT_FLOAT_EQ(at(out, 1, 1, 2), 1.0f);
	EXPECT_FLOAT_EQ(at(out, 1, 1, 3), 7.0f);
	// Mode 5: half, int16, uint8, and the unbound member (0).
	render(5, {}, out);
	EXPECT_FLOAT_EQ(at(out, 2, 2, 0), 0.5f);
	EXPECT_FLOAT_EQ(at(out, 2, 2, 1), -3.0f);
	EXPECT_FLOAT_EQ(at(out, 2, 2, 2), 200.0f);
	EXPECT_FLOAT_EQ(at(out, 2, 2, 3), 0.0f);
	// Mode 6: the mask (texture 3, bounds 2..6 × 1..3) sampled through its own map at a pixel
	// inside it, the ramp texel (2, 0) blue, and the texture count (5).
	render(6, {}, out);
	EXPECT_NEAR(at(out, 4, 2, 0), 0.75f, 0.01f);  // alpha image: r = a
	EXPECT_NEAR(at(out, 4, 2, 1), 0.0f, 0.01f);
	EXPECT_NEAR(at(out, 4, 2, 2), 0.25f, 0.01f);
	EXPECT_FLOAT_EQ(at(out, 4, 2, 3), 5.0f);
	// Mode 7: the first image's red sampled at its own texel center, and alpha.
	render(7, {}, out);
	EXPECT_NEAR(at(out, 4, 1, 0), 0.5f, 0.02f);
	EXPECT_FLOAT_EQ(at(out, 4, 1, 3), 1.0f);
	// Mode 8: the float constant.
	render(8, {}, out);
	EXPECT_FLOAT_EQ(at(out, 0, 3, 0), 0.125f);
	// A window renders only its pixels, into an 8-bit destination, with the mask's own bounds.
	ImageBuffer small = image({0, 0, 8, 4}, BitDepth::UInt8);
	small.view().clear();
	render(8, {2, 1, 5, 3}, small);
	EXPECT_NEAR(at(small, 2, 1, 0), 0.125f, 0.01f);
	EXPECT_FLOAT_EQ(at(small, 0, 0, 0), 0.0f);
	EXPECT_FLOAT_EQ(at(small, 5, 1, 0), 0.0f);
	// A disconnected keyed image samples transparent black; a missing positional source too.
	KernelDispatch d = inspect.dispatch(inspectValues(6));
	d.destination = cpuImage(out);
	d.sources = {cpuImage(first)};
	ASSERT_TRUE(KernelEngine::shared().run(d).ok());
	EXPECT_FLOAT_EQ(at(out, 4, 2, 0), 0.0f);
	EXPECT_FLOAT_EQ(at(out, 4, 2, 2), 0.0f);
	EXPECT_FLOAT_EQ(at(out, 4, 2, 3), 5.0f);
	// An alpha-only destination takes the texel's alpha.
	ImageBuffer alpha = image({0, 0, 8, 4}, BitDepth::Float, Components::Alpha);
	render(4, {}, alpha);
	EXPECT_FLOAT_EQ(at(alpha, 0, 0, 0), 7.0f);
	// An empty window is a no-op; abort cancels.
	KernelDispatch none = inspect.dispatch(inspectValues(0));
	none.destination = cpuImage(out);
	none.window = {0, 0, 0, 0};
	EXPECT_TRUE(KernelEngine::shared().run(none).ok());
	KernelDispatch cancelled = inspect.dispatch(inspectValues(0));
	cancelled.destination = cpuImage(out);
	cancelled.abort = []() { return true; };
	EXPECT_TRUE(KernelEngine::shared().run(cancelled).isCancellation());
}

TEST(KernelEngine, CpuBackendRefusesWhatItCannotRun)
{
	Prepared inspect(OFXGRIP_KERNEL(GripInspect), GpuApi::None);
	CpuKernelBackend backend;
	KernelDispatch d = inspect.dispatch(inspectValues(0));
	EXPECT_EQ(backend.dispatch(d).code(), ErrorCode::InvalidParameter);  // no destination view
	ImageBuffer out = image({0, 0, 4, 4}, BitDepth::Float);
	d.destination = cpuImage(out);
	KernelImplementation noEntry = *inspect.implementation;
	noEntry.cpuEntry = nullptr;
	d.implementation = &noEntry;
	EXPECT_EQ(backend.dispatch(d).code(), ErrorCode::InvalidParameter);
	d.implementation = inspect.implementation;
	Prepared metalInspect(OFXGRIP_KERNEL(GripInspect), GpuApi::Metal);
	d.layout = &metalInspect.layout;
	EXPECT_EQ(backend.dispatch(d).code(), ErrorCode::InvalidParameter);  // a slot layout
	d.layout = &inspect.layout;
	KernelImage gpuSource;
	gpuSource.connected = true;
	gpuSource.gpu.api = GpuApi::Metal;
	d.sources = {gpuSource};
	EXPECT_EQ(backend.dispatch(d).code(), ErrorCode::InvalidParameter);  // a GPU source
}

TEST(KernelEngine, CpuBlurMatchesAHandComputedPixel)
{
	Prepared blur(OFXGRIP_KERNEL(GripBlur), GpuApi::None);
	ASSERT_NE(blur.implementation, nullptr);
	ImageBuffer source = ramp({0, 0, 16, 2}, BitDepth::Float);
	ImageBuffer matte = ramp({0, 0, 16, 2}, BitDepth::Float);
	ImageBuffer out = image({0, 0, 16, 2}, BitDepth::Float);
	KernelValueMap values;
	values[1] = Value(0.0);  // radius 0: no spread
	values[2] = Value(1);    // TAPS
	values[3] = Value(false);
	values[11] = Value(RGB{1.0, 0.5, 1.0});
	KernelDispatch d = blur.dispatch(values);
	d.destination = cpuImage(out);
	d.sources = {cpuImage(source)};
	d.keyedImages[12] = cpuImage(matte);
	ASSERT_TRUE(KernelEngine::shared().run(d).ok());
	// r = x / 16 at the pixel center, tinted by 1, times the matte's alpha (1).
	EXPECT_NEAR(at(out, 8, 0, 0), 0.5f, 0.05f);
	EXPECT_NEAR(at(out, 8, 0, 1), 0.0f, 0.05f);  // y ramp at row 0 is ~0, tint 0.5
	EXPECT_NEAR(at(out, 8, 0, 2), 0.25f, 0.02f);
	EXPECT_FLOAT_EQ(at(out, 8, 0, 3), 1.0f);
	values[3] = Value(true);  // INVERT: rgb = a - rgb
	d = blur.dispatch(values);
	d.destination = cpuImage(out);
	d.sources = {cpuImage(source)};
	d.keyedImages[12] = cpuImage(matte);
	ASSERT_TRUE(KernelEngine::shared().run(d).ok());
	EXPECT_NEAR(at(out, 8, 0, 2), 0.75f, 0.02f);
}

#if defined(OFXGRIP_HAS_METAL)

TEST(KernelEngine, MetalBackendMatchesTheCpuBackend)
{
	std::shared_ptr<metal::MetalContext> context = metal::MetalContext::systemDefault();
	if (!context) {
		GTEST_SKIP() << "No Metal device";
	}
	MetalKernelBackend::registerWithEngine();
	MetalKernelBackend::registerWithEngine();
	ASSERT_NE(KernelEngine::shared().backend(GpuApi::Metal), nullptr);
	EXPECT_TRUE(KernelEngine::shared().backend(GpuApi::Metal)->available());
	Prepared cpu(OFXGRIP_KERNEL(GripInspect), GpuApi::None);
	Prepared gpu(OFXGRIP_KERNEL(GripInspect), GpuApi::Metal);
	ASSERT_NE(gpu.implementation, nullptr);
	EXPECT_EQ(gpu.layout.form(), KernelBindingForm::Slots);
	const RectI bounds{0, 0, 24, 6};
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt8, BitDepth::UInt16}) {
		ImageBuffer first = ramp(bounds, depth);
		ImageBuffer second = ramp(bounds, BitDepth::Float);
		ImageBuffer mask = ramp({4, 1, 20, 5}, BitDepth::Float, Components::Alpha);
		ImageBuffer rampImage = ramp(bounds, depth);
		for (int mode : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
			// CPU.
			ImageBuffer cpuOut = image(bounds, depth);
			KernelDispatch c = cpu.dispatch(inspectValues(mode), 0.5);
			c.destination = cpuImage(cpuOut);
			c.sources = {cpuImage(first), cpuImage(second)};
			c.keyedImages[kRamp] = cpuImage(rampImage);
			c.keyedImages[kMask] = cpuImage(mask);
			ASSERT_TRUE(KernelEngine::shared().run(c).ok());
			// Metal, over buffers the host would hand in.
			auto upload = [&](ImageBuffer& buffer) {
				metal::MetalBuffer b = context->newBuffer(buffer.bytes().size(), buffer.bytes().data());
				KernelImage k;
				k.connected = true;
				k.gpu.api = GpuApi::Metal;
				k.gpu.queue = context->queue();
				k.gpu.handle = b.handle();
				k.gpu.desc = buffer.desc();
				k.gpu.rowBytes = static_cast<std::ptrdiff_t>(buffer.rowBytes());
				return std::make_pair(k, b);
			};
			ImageBuffer gpuOut = image(bounds, depth);
			auto [outImage, outBuffer] = upload(gpuOut);
			auto [firstImage, firstBuffer] = upload(first);
			auto [secondImage, secondBuffer] = upload(second);
			auto [maskImage, maskBuffer] = upload(mask);
			auto [rampImageGpu, rampBuffer] = upload(rampImage);
			if (mode == 3) {
				firstImage.gpu.needsIntermediateCopy = true;  // Q-RESOLVE-031 path
			}
			KernelDispatch g = gpu.dispatch(inspectValues(mode), 0.5);
			g.destination = outImage;
			g.sources = {firstImage, secondImage};
			g.keyedImages[kRamp] = rampImageGpu;
			g.keyedImages[kMask] = maskImage;
			Error e = KernelEngine::shared().run(g);
			ASSERT_TRUE(e.ok()) << e.describe() << " mode " << mode << " depth " << toString(depth);
			ASSERT_TRUE(context->waitIdle().ok());
			std::memcpy(gpuOut.bytes().data(), outBuffer.contents(), gpuOut.bytes().size());
			for (int y = bounds.y1; y < bounds.y2; ++y) {
				for (int x = bounds.x1; x < bounds.x2; ++x) {
					for (std::size_t ch = 0; ch < 4; ++ch) {
						const float a = at(cpuOut, x, y, ch);
						const float b = at(gpuOut, x, y, ch);
						ASSERT_NEAR(a, b, depth == BitDepth::Half ? 0.01f : 0.004f) << "mode " << mode << " depth " << toString(depth) << " at " << x << "," << y << " channel " << ch;
					}
				}
			}
		}
	}
}

TEST(KernelEngine, MetalBackendRendersAWindowAndRefusesBadFrames)
{
	std::shared_ptr<metal::MetalContext> context = metal::MetalContext::systemDefault();
	if (!context) {
		GTEST_SKIP() << "No Metal device";
	}
	MetalKernelBackend::registerWithEngine();
	Prepared gpu(OFXGRIP_KERNEL(GripInspect), GpuApi::Metal);
	const RectI bounds{0, 0, 10, 6};
	ImageBuffer out = image(bounds, BitDepth::Float);
	metal::MetalBuffer outBuffer = context->newBuffer(out.bytes().size());
	KernelImage destination;
	destination.connected = true;
	destination.gpu.api = GpuApi::Metal;
	destination.gpu.queue = context->queue();
	destination.gpu.handle = outBuffer.handle();
	destination.gpu.desc = out.desc();
	destination.gpu.rowBytes = static_cast<std::ptrdiff_t>(out.rowBytes());
	KernelDispatch d = gpu.dispatch(inspectValues(8));
	d.destination = destination;
	d.window = {3, 2, 7, 5};
	ASSERT_TRUE(KernelEngine::shared().run(d).ok());
	ASSERT_TRUE(context->waitIdle().ok());
	std::memcpy(out.bytes().data(), outBuffer.contents(), out.bytes().size());
	EXPECT_FLOAT_EQ(at(out, 3, 2, 0), 0.125f);
	EXPECT_FLOAT_EQ(at(out, 6, 4, 0), 0.125f);
	EXPECT_FLOAT_EQ(at(out, 2, 2, 0), 0.0f);
	EXPECT_FLOAT_EQ(at(out, 7, 2, 0), 0.0f);
	EXPECT_FLOAT_EQ(at(out, 3, 5, 0), 0.0f);
	// Unaligned rows take the blit path: a 6-byte-wide 8-bit frame with 7-byte rows.
	ImageDesc odd;
	odd.bounds = {0, 0, 1, 3};
	odd.regionOfDefinition = odd.bounds;
	odd.depth = BitDepth::UInt8;
	odd.components = Components::Alpha;
	std::vector<std::uint8_t> oddBytes(7 * 3, 0);
	metal::MetalBuffer oddBuffer = context->newBuffer(oddBytes.size(), oddBytes.data());
	KernelImage oddImage;
	oddImage.connected = true;
	oddImage.gpu.api = GpuApi::Metal;
	oddImage.gpu.queue = context->queue();
	oddImage.gpu.handle = oddBuffer.handle();
	oddImage.gpu.desc = odd;
	oddImage.gpu.rowBytes = 7;
	KernelDispatch unaligned = gpu.dispatch(inspectValues(8));
	unaligned.destination = oddImage;
	unaligned.sources = {oddImage, oddImage};
	ASSERT_TRUE(KernelEngine::shared().run(unaligned).ok());
	ASSERT_TRUE(context->waitIdle().ok());
	const auto* written = static_cast<const std::uint8_t*>(oddBuffer.contents());
	EXPECT_EQ(written[0], 32);   // 0.125 × 255 rounded
	EXPECT_EQ(written[7], 32);
	EXPECT_EQ(written[4], 0);    // the row's padding is untouched
	// An RGB frame has no Metal format; a CPU destination is refused; a CPU layout is refused.
	KernelImage rgb = destination;
	rgb.gpu.desc.components = Components::RGB;
	KernelDispatch bad = gpu.dispatch(inspectValues(0));
	bad.destination = rgb;
	EXPECT_EQ(KernelEngine::shared().run(bad).code(), ErrorCode::Format);
	MetalKernelBackend backend;
	KernelDispatch cpuDest = gpu.dispatch(inspectValues(0));
	cpuDest.destination = cpuImage(out);
	EXPECT_EQ(backend.dispatch(cpuDest).code(), ErrorCode::InvalidParameter);
	Prepared cpu(OFXGRIP_KERNEL(GripInspect), GpuApi::None);
	KernelDispatch blockLayout = gpu.dispatch(inspectValues(0));
	blockLayout.layout = &cpu.layout;
	blockLayout.destination = destination;
	EXPECT_EQ(backend.dispatch(blockLayout).code(), ErrorCode::InvalidParameter);
	KernelDispatch noQueue = gpu.dispatch(inspectValues(0));
	noQueue.destination = destination;
	noQueue.destination.gpu.queue = nullptr;
	EXPECT_EQ(backend.dispatch(noQueue).code(), ErrorCode::MissingHostFeature);
	KernelDispatch emptyWindow = gpu.dispatch(inspectValues(0));
	emptyWindow.destination = destination;
	emptyWindow.window = {20, 20, 30, 30};
	EXPECT_TRUE(backend.dispatch(emptyWindow).ok());
	KernelDispatch rgbSource = gpu.dispatch(inspectValues(0));
	rgbSource.destination = destination;
	rgbSource.sources = {rgb};
	EXPECT_EQ(backend.dispatch(rgbSource).code(), ErrorCode::Format);
}

TEST(MetalBackend, FunctionConstantsTexturesAndSamplers)
{
	std::shared_ptr<metal::MetalContext> context = metal::MetalContext::systemDefault();
	if (!context) {
		GTEST_SKIP() << "No Metal device";
	}
	constexpr const char* kSource = R"MSL(
#include <metal_stdlib>
using namespace metal;
constant int SCALE [[function_constant(0)]];
constant bool FLIP [[function_constant(1)]];
kernel void scale(texture2d<float, access::sample> in [[texture(0)]], texture2d<float, access::write> out [[texture(1)]], sampler s [[sampler(0)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= out.get_width() || gid.y >= out.get_height()) { return; }
    float2 uv = (float2(gid) + 0.5) / float2(out.get_width(), out.get_height());
    float4 c = in.sample(s, uv) * float(SCALE);
    out.write(FLIP ? c.bgra : c, gid);
}
)MSL";
	using namespace metal;
	MetalFunctionConstant scale;
	scale.index = 0;
	scale.type = MetalDataType::Int;
	const std::int32_t two = 2;
	std::memcpy(scale.bytes.data(), &two, sizeof two);
	MetalFunctionConstant flip;
	flip.index = 1;
	flip.type = MetalDataType::Bool;
	flip.bytes[0] = 1;
	MetalPipeline pipeline;
	Error e = context->pipeline("ofxgrip.tests.constants", kSource, "scale", {scale, flip}, pipeline);
	ASSERT_TRUE(e.ok()) << e.describe();
	MetalPipeline same;
	ASSERT_TRUE(context->pipeline("ofxgrip.tests.constants", kSource, "scale", {scale, flip}, same).ok());
	EXPECT_EQ(same.state(), pipeline.state());
	MetalPipeline other;
	flip.bytes[0] = 0;
	ASSERT_TRUE(context->pipeline("ofxgrip.tests.constants", kSource, "scale", {scale, flip}, other).ok());
	EXPECT_NE(other.state(), pipeline.state());
	// A wrong type and a missing required constant are refused.
	MetalFunctionConstant wrong = scale;
	wrong.type = MetalDataType::Float;
	EXPECT_EQ(context->pipeline("ofxgrip.tests.constants", kSource, "scale", {wrong, flip}, other).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(context->pipeline("ofxgrip.tests.constants", kSource, "scale", {scale}, other).code(), ErrorCode::InvalidParameter);

	EXPECT_EQ(metalPixelFormatFor(BitDepth::Float, Components::RGB), MetalPixelFormat::Invalid);
	EXPECT_EQ(metalPixelFormatFor(BitDepth::None, Components::RGBA), MetalPixelFormat::Invalid);
	EXPECT_EQ(metalPixelBytes(MetalPixelFormat::RGBA16Float), 8u);
	EXPECT_EQ(metalPixelBytes(MetalPixelFormat::R8Unorm), 1u);
	EXPECT_EQ(metalPixelBytes(MetalPixelFormat::Invalid), 0u);
	EXPECT_GT(context->linearTextureAlignment(MetalPixelFormat::RGBA32Float), 0u);
	EXPECT_EQ(context->linearTextureAlignment(MetalPixelFormat::Invalid), 0u);
	EXPECT_NE(context->bilinearSampler(), nullptr);
	EXPECT_EQ(context->bilinearSampler(), context->bilinearSampler());

	// A 4×2 float texture over a buffer, sampled into a private texture, and read back.
	const std::size_t rowBytes = 4 * 16;
	std::vector<float> pixels(4 * 2 * 4);
	for (std::size_t i = 0; i < pixels.size(); ++i) {
		pixels[i] = static_cast<float>(i % 4) * 0.1f;
	}
	MetalBuffer in = context->newBuffer(pixels.size() * sizeof(float), pixels.data());
	MetalTexture input;
	ASSERT_TRUE(context->textureOverBuffer(in.handle(), 0, rowBytes, 4, 2, MetalPixelFormat::RGBA32Float, false, input).ok());
	EXPECT_EQ(input.width(), 4u);
	EXPECT_EQ(input.height(), 2u);
	EXPECT_EQ(input.format(), MetalPixelFormat::RGBA32Float);
	EXPECT_NE(input.handle(), nullptr);
	MetalTexture output;
	ASSERT_TRUE(context->newTexture(4, 2, MetalPixelFormat::RGBA32Float, true, output).ok());
	MetalDispatch dispatch;
	dispatch.pipeline = pipeline;
	dispatch.width = 4;
	dispatch.height = 2;
	dispatch.textures = {{input.handle(), 0}, {output.handle(), 1}};
	dispatch.bilinearSamplers = {0};
	dispatch.threadsPerThreadgroup[0] = 8;
	dispatch.threadsPerThreadgroup[1] = 8;
	dispatch.threadsPerThreadgroup[2] = 1;
	MetalBuffer out = context->newBuffer(pixels.size() * sizeof(float));
	MetalCommandBuffer command = context->commandBuffer();
	ASSERT_TRUE(command.dispatch(dispatch).ok());
	ASSERT_TRUE(command.copyTextureToBuffer(output, out.handle(), 0, rowBytes).ok());
	command.retain(input);
	command.retain(MetalTexture{});
	ASSERT_TRUE(command.commitAndWait().ok());
	const auto* result = static_cast<const float*>(out.contents());
	// (r, g, b, a) = (0, 0.1, 0.2, 0.3) × 2, then bgra: (0.4, 0.2, 0, 0.6).
	EXPECT_NEAR(result[0], 0.4f, 1e-5f);
	EXPECT_NEAR(result[1], 0.2f, 1e-5f);
	EXPECT_NEAR(result[2], 0.0f, 1e-5f);
	EXPECT_NEAR(result[3], 0.6f, 1e-5f);
	// A blit from a buffer into a private texture and errors.
	MetalTexture scratch;
	ASSERT_TRUE(context->newTexture(4, 2, MetalPixelFormat::RGBA32Float, false, scratch).ok());
	MetalCommandBuffer second = context->commandBuffer();
	ASSERT_TRUE(second.copyBufferToTexture(in.handle(), 0, rowBytes, scratch).ok());
	EXPECT_EQ(second.copyBufferToTexture(nullptr, 0, rowBytes, scratch).code(), ErrorCode::BadHandle);
	EXPECT_EQ(second.copyBufferToTexture(in.handle(), 0, rowBytes * 8, scratch).code(), ErrorCode::BadIndex);
	EXPECT_EQ(second.copyTextureToBuffer(scratch, nullptr, 0, rowBytes).code(), ErrorCode::BadHandle);
	EXPECT_EQ(second.copyTextureToBuffer(scratch, out.handle(), 0, rowBytes * 8).code(), ErrorCode::BadIndex);
	MetalDispatch big = dispatch;
	big.threadsPerThreadgroup[0] = 4096;
	EXPECT_EQ(second.dispatch(big).code(), ErrorCode::Render);
	MetalDispatch nullTexture = dispatch;
	nullTexture.textures[0].handle = nullptr;
	EXPECT_EQ(second.dispatch(nullTexture).code(), ErrorCode::BadHandle);
	ASSERT_TRUE(second.commitAndWait().ok());
	EXPECT_EQ(second.copyBufferToTexture(in.handle(), 0, rowBytes, scratch).code(), ErrorCode::WrongPhase);
	EXPECT_EQ(second.copyTextureToBuffer(scratch, out.handle(), 0, rowBytes).code(), ErrorCode::WrongPhase);
	MetalCommandBuffer invalid;
	EXPECT_EQ(invalid.copyBufferToTexture(in.handle(), 0, rowBytes, scratch).code(), ErrorCode::BadHandle);
	EXPECT_EQ(invalid.copyTextureToBuffer(scratch, out.handle(), 0, rowBytes).code(), ErrorCode::BadHandle);
	MetalTexture none;
	EXPECT_FALSE(none.valid());
	EXPECT_EQ(none.handle(), nullptr);
	EXPECT_EQ(none.width(), 0u);
	EXPECT_EQ(none.format(), MetalPixelFormat::Invalid);
	EXPECT_EQ(context->textureOverBuffer(nullptr, 0, rowBytes, 4, 2, MetalPixelFormat::RGBA32Float, false, none).code(), ErrorCode::BadHandle);
	EXPECT_EQ(context->textureOverBuffer(in.handle(), 0, rowBytes, 0, 2, MetalPixelFormat::RGBA32Float, false, none).code(), ErrorCode::Format);
	EXPECT_EQ(context->textureOverBuffer(in.handle(), 0, 17, 4, 2, MetalPixelFormat::RGBA32Float, false, none).code(), ErrorCode::Format);
	EXPECT_EQ(context->textureOverBuffer(in.handle(), 0, rowBytes, 4, 200, MetalPixelFormat::RGBA32Float, false, none).code(), ErrorCode::BadIndex);
	EXPECT_EQ(context->newTexture(0, 0, MetalPixelFormat::RGBA32Float, false, none).code(), ErrorCode::Format);
}

#endif

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The colourspace conversion kernel against the ColorGamut math, and the engine's conversion
// around a kernel on the CPU backend, on the Metal backend where a device exists, and on the CUDA
// backend where an NVIDIA device and the conversion kernel's PTX exist.
#include <ofxgrip/gpu/KernelColor.hpp>

#include <ofxgrip/gpu/CpuKernelBackend.hpp>
#include <ofxgrip/gpu/CudaDriver.hpp>
#include <ofxgrip/gpu/CudaKernelBackend.hpp>
#include <ofxgrip/gpu/KernelEngine.hpp>

#include "CudaDeviceContext.hpp"

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#include <ofxgrip/gpu/MetalKernelBackend.hpp>
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace ofxgrip;

namespace {

const ColorSpace kSrgb{ColorPrimaries::Rec709, ColorTransfer::Srgb};
const ColorSpace kLinear709{ColorPrimaries::Rec709, ColorTransfer::Linear};
const ColorSpace kLinear2020{ColorPrimaries::Rec2020, ColorTransfer::Linear};

ImageBuffer image(const RectI& bounds, BitDepth depth = BitDepth::Float, Components components = Components::RGBA, Premultiplication premultiplication = Premultiplication::Premultiplied)
{
	ImageDesc desc;
	desc.bounds = bounds;
	desc.regionOfDefinition = bounds;
	desc.depth = depth;
	desc.components = components;
	desc.premultiplication = premultiplication;
	return ImageBuffer(desc);
}

/*! Unpremultiplied colors that vary per pixel, with an alpha ramp from 0. Stored with the alpha
    product when the buffer is premultiplied. */
void fill(ImageBuffer& buffer, float low, float high)
{
	const ImageView v = buffer.view();
	const RectI b = buffer.desc().bounds;
	const bool premultiplied = buffer.desc().premultiplication == Premultiplication::Premultiplied;
	const std::size_t channels = componentCount(buffer.desc().components);
	for (int y = b.y1; y < b.y2; ++y) {
		for (int x = b.x1; x < b.x2; ++x) {
			const float u = static_cast<float>(x - b.x1) / static_cast<float>(b.width() - 1);
			const float w = static_cast<float>(y - b.y1) / static_cast<float>(b.height() - 1);
			const float alpha = channels == 4 ? w : 1.0f;
			const float rgb[3] = {low + (high - low) * u, low + (high - low) * (1.0f - u), low + (high - low) * 0.5f * (u + w)};
			for (std::size_t c = 0; c < channels; ++c) {
				const float value = c == 3 ? alpha : (premultiplied ? rgb[c] * alpha : rgb[c]);
				writeComponent(v.pixel(x, y), buffer.desc().depth, c, channels == 1 ? 0.75f : value);
			}
		}
	}
}

float at(const ImageBuffer& buffer, int x, int y, std::size_t c)
{
	return readComponent(buffer.view().pixel(x, y), buffer.desc().depth, c);
}

KernelImage cpuImage(ImageBuffer& buffer)
{
	KernelImage k;
	k.connected = true;
	k.cpu = buffer.view();
	return k;
}

/*! The conversion kernel as an ordinary kernel: its build for an API and a dispatch with values. */
KernelDispatch convertDispatch(GpuApi api, const ColorConversion& conversion, bool premultiplied)
{
	const KernelColorBuild* build = kernelColorBuild(api);
	KernelDispatch d;
	if (build == nullptr) {
		return d;
	}
	const KernelValueMap values = colorConvertValues(conversion, premultiplied);
	d.layout = &build->layout;
	d.implementation = build->implementation;
	d.plan = &build->plan;
	d.cacheKey = "ofxgrip.tests.colorconvert";
	d.uniforms = packKernelUniforms(build->layout, build->plan, values, 1.0);
	d.constants = packKernelConstants(build->plan, values, 1.0);
	return d;
}

/*! What one stored pixel becomes under a conversion: the reference the kernel must match. */
std::array<double, 4> expected(const ColorConversion& conversion, bool premultiplied, const std::array<double, 4>& stored)
{
	const bool curved = conversion.decode != ColorTransfer::Linear || conversion.encode != ColorTransfer::Linear;
	const double alpha = stored[3];
	const bool divide = curved && premultiplied && alpha > 0.0;
	ColorVector rgb{stored[0], stored[1], stored[2]};
	if (divide) {
		rgb = {rgb[0] / alpha, rgb[1] / alpha, rgb[2] / alpha};
	}
	const ColorVector linear = conversion.matrix.apply({toLinear(conversion.decode, rgb[0]), toLinear(conversion.decode, rgb[1]), toLinear(conversion.decode, rgb[2])});
	rgb = {fromLinear(conversion.encode, linear[0]), fromLinear(conversion.encode, linear[1]), fromLinear(conversion.encode, linear[2])};
	if (divide) {
		rgb = {rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha};
	}
	return {rgb[0], rgb[1], rgb[2], alpha};
}

/*! A kernel that keeps only the red component in its own space: the conversion kernel with a
    projection matrix. Its result depends on the space it runs in. */
ColorConversion redOnly()
{
	ColorConversion c;
	c.matrix = ColorMatrix{{1, 0, 0, 0, 0, 0, 0, 0, 0}};
	return c;
}

/*! The reference for a red-only kernel between two conversions, on unpremultiplied colors. */
std::array<double, 4> aroundRedOnly(const KernelColorPass& pass, bool premultiplied, const std::array<double, 4>& stored)
{
	const std::array<double, 4> inKernel = expected(pass.toKernel, premultiplied, stored);
	const std::array<double, 4> kept{inKernel[0], 0.0, 0.0, inKernel[3]};
	return expected(pass.toHost, premultiplied, kept);
}

KernelColorPass passBetween(const ColorSpace& host, const ColorSpace& kernel)
{
	KernelColorPass pass;
	pass.toKernel = colorConversion(host, kernel);
	pass.toHost = colorConversion(kernel, host);
	return pass;
}

}  // namespace

TEST(KernelColor, TheConversionKernelHasABuildPerBackend)
{
	const KernelRecord& record = colorConvertKernelRecord();
	EXPECT_EQ(record.name, kColorConvertKernelName);
	const KernelColorBuild* cpu = kernelColorBuild(GpuApi::None);
	ASSERT_NE(cpu, nullptr);
	EXPECT_NE(cpu->implementation->cpuEntry, nullptr);
	EXPECT_EQ(cpu->layout.form(), KernelBindingForm::Block);
	EXPECT_EQ(cpu->plan.members.size(), 6u);
	const KernelColorBuild* metal = kernelColorBuild(GpuApi::Metal);
	ASSERT_NE(metal, nullptr);
	EXPECT_EQ(metal->layout.form(), KernelBindingForm::Slots);
	EXPECT_NE(kernelColorBuild(GpuApi::Vulkan), nullptr);
	EXPECT_EQ(kernelColorBuild(GpuApi::OpenCL), nullptr);
	EXPECT_EQ(kernelColorBuild(GpuApi::OpenGL), nullptr);
	// The CUDA build exists once an NVIDIA toolchain has compiled the PTX.
	if (const KernelColorBuild* cuda = kernelColorBuild(GpuApi::Cuda)) {
		EXPECT_EQ(cuda->layout.form(), KernelBindingForm::Block);
	}
	EXPECT_TRUE(KernelEngine::shared().convertsColorOn(GpuApi::None));
	EXPECT_FALSE(KernelEngine::shared().convertsColorOn(GpuApi::OpenGL));
}

TEST(KernelColor, TheValuesCarryTheMatrixRowsAndTheTransfers)
{
	ColorConversion conversion = colorConversion(ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Pq}, ColorSpace{ColorPrimaries::AcesAp1, ColorTransfer::Hlg});
	const KernelValueMap values = colorConvertValues(conversion, true);
	ASSERT_EQ(values.size(), 6u);
	EXPECT_EQ(values.at(1).as<RGB>(), (RGB{conversion.matrix.at(0, 0), conversion.matrix.at(0, 1), conversion.matrix.at(0, 2)}));
	EXPECT_EQ(values.at(3).as<RGB>(), (RGB{conversion.matrix.at(2, 0), conversion.matrix.at(2, 1), conversion.matrix.at(2, 2)}));
	EXPECT_EQ(values.at(4).toInt(), static_cast<int>(ColorTransfer::Pq));
	EXPECT_EQ(values.at(5).toInt(), static_cast<int>(ColorTransfer::Hlg));
	EXPECT_EQ(values.at(6).toInt(), 1);
	EXPECT_EQ(colorConvertValues(conversion, false).at(6).toInt(), 0);
}

TEST(KernelColor, TheCpuKernelMatchesTheColorGamutMath)
{
	const RectI bounds{3, -2, 20, 7};
	const ColorTransfer transfers[] = {ColorTransfer::Linear, ColorTransfer::Srgb,    ColorTransfer::Rec709, ColorTransfer::Gamma18, ColorTransfer::Gamma22,
	                                   ColorTransfer::Gamma24, ColorTransfer::Gamma26, ColorTransfer::Pq,     ColorTransfer::Hlg};
	for (Premultiplication premultiplication : {Premultiplication::Premultiplied, Premultiplication::Unpremultiplied}) {
		const bool premultiplied = premultiplication == Premultiplication::Premultiplied;
		for (ColorTransfer decode : transfers) {
			for (ColorTransfer encode : transfers) {
				ColorConversion conversion;
				conversion.decode = decode;
				conversion.encode = encode;
				conversion.matrix = decode == encode ? ColorMatrix::identity() : gamutConversionMatrix(ColorPrimaries::Rec709, ColorPrimaries::P3D65);
				// PQ and HLG clamp to [0, 1]. The other curves mirror about zero and pass 1.
				const bool clamps = decode == ColorTransfer::Pq || decode == ColorTransfer::Hlg || encode == ColorTransfer::Pq || encode == ColorTransfer::Hlg;
				ImageBuffer source = image(bounds, BitDepth::Float, Components::RGBA, premultiplication);
				fill(source, clamps ? 0.0f : -0.25f, clamps ? 1.0f : 1.5f);
				ImageBuffer out = image(bounds, BitDepth::Float, Components::RGBA, premultiplication);
				KernelDispatch d = convertDispatch(GpuApi::None, conversion, premultiplied);
				d.destination = cpuImage(out);
				d.sources = {cpuImage(source)};
				const Error e = KernelEngine::shared().run(d);
				ASSERT_TRUE(e.ok()) << e.describe();
				for (int y = bounds.y1; y < bounds.y2; ++y) {
					for (int x = bounds.x1; x < bounds.x2; ++x) {
						const std::array<double, 4> want = expected(conversion, premultiplied, {at(source, x, y, 0), at(source, x, y, 1), at(source, x, y, 2), at(source, x, y, 3)});
						for (std::size_t c = 0; c < 4; ++c) {
							ASSERT_NEAR(at(out, x, y, c), want[c], 2e-4 * std::max(1.0, std::fabs(want[c])))
							    << toString(decode) << " to " << toString(encode) << (premultiplied ? " premultiplied" : " straight") << " at " << x << "," << y << " channel " << c;
						}
					}
				}
			}
		}
	}
}

TEST(KernelEngine, AColorPassConvertsAroundTheKernelOnTheCpu)
{
	const RectI bounds{0, 0, 16, 10};
	const RectI window{2, 1, 13, 9};
	struct Case {
		ColorSpace host;
		ColorSpace kernel;
	};
	const Case cases[] = {{kSrgb, kLinear709}, {kLinear709, kLinear2020}, {kSrgb, kLinear2020}, {ColorSpace{ColorPrimaries::Rec2020, ColorTransfer::Pq}, ColorSpace{ColorPrimaries::AcesAp1, ColorTransfer::Linear}}};
	for (const Case& each : cases) {
		for (Premultiplication premultiplication : {Premultiplication::Premultiplied, Premultiplication::Unpremultiplied}) {
			const bool premultiplied = premultiplication == Premultiplication::Premultiplied;
			ImageBuffer source = image(bounds, BitDepth::Float, Components::RGBA, premultiplication);
			fill(source, 0.0f, 1.0f);
			ImageBuffer out = image(bounds, BitDepth::Float, Components::RGBA, premultiplication);
			KernelDispatch d = convertDispatch(GpuApi::None, redOnly(), premultiplied);
			d.destination = cpuImage(out);
			d.sources = {cpuImage(source)};
			d.window = window;
			d.color = passBetween(each.host, each.kernel);
			ASSERT_TRUE(d.color.active());
			const Error e = KernelEngine::shared().run(d);
			ASSERT_TRUE(e.ok()) << e.describe();
			for (int y = bounds.y1; y < bounds.y2; ++y) {
				for (int x = bounds.x1; x < bounds.x2; ++x) {
					const bool inside = window.contains(x, y);
					const std::array<double, 4> want = inside ? aroundRedOnly(d.color, premultiplied, {at(source, x, y, 0), at(source, x, y, 1), at(source, x, y, 2), at(source, x, y, 3)}) : std::array<double, 4>{0, 0, 0, 0};
					for (std::size_t c = 0; c < 4; ++c) {
						ASSERT_NEAR(at(out, x, y, c), want[c], 5e-4) << toString(each.host.transfer) << " host at " << x << "," << y << " channel " << c;
					}
				}
			}
		}
	}
}

TEST(KernelEngine, AColorPassLeavesAnAlphaSourceAndAnIdentityAlone)
{
	const RectI bounds{0, 0, 8, 6};
	// An alpha-only source is not a color image. The red-only kernel reads its one component
	// as red, and only the result converts.
	ImageBuffer mask = image(bounds, BitDepth::Float, Components::Alpha);
	fill(mask, 0.0f, 1.0f);
	ImageBuffer out = image(bounds, BitDepth::Float, Components::RGBA, Premultiplication::Unpremultiplied);
	KernelDispatch d = convertDispatch(GpuApi::None, redOnly(), false);
	d.destination = cpuImage(out);
	d.sources = {cpuImage(mask)};
	d.color = passBetween(kSrgb, kLinear709);
	ASSERT_TRUE(KernelEngine::shared().run(d).ok());
	EXPECT_NEAR(at(out, 3, 2, 0), fromLinear(ColorTransfer::Srgb, 0.75), 1e-4);
	EXPECT_NEAR(at(out, 3, 2, 1), 0.0, 1e-6);

	// An identity pass is the plain dispatch, on a backend that makes no scratch image too.
	KernelColorPass same = passBetween(kSrgb, kSrgb);
	EXPECT_FALSE(same.active());

	// An 8-bit frame converts through float intermediates.
	ImageBuffer bytes = image(bounds, BitDepth::UInt8, Components::RGBA, Premultiplication::Unpremultiplied);
	fill(bytes, 0.0f, 1.0f);
	ImageBuffer bytesOut = image(bounds, BitDepth::UInt8, Components::RGBA, Premultiplication::Unpremultiplied);
	KernelDispatch b = convertDispatch(GpuApi::None, ColorConversion{}, false);
	b.destination = cpuImage(bytesOut);
	b.sources = {cpuImage(bytes)};
	b.color = passBetween(kSrgb, kLinear709);
	ASSERT_TRUE(KernelEngine::shared().run(b).ok());
	for (int x = bounds.x1; x < bounds.x2; ++x) {
		for (std::size_t c = 0; c < 4; ++c) {
			EXPECT_NEAR(at(bytesOut, x, 4, c), at(bytes, x, 4, c), 1.0f / 255.0f) << x << " channel " << c;
		}
	}
}

TEST(KernelEngine, AColorPassFailsWhereNoConversionRuns)
{
	// A frame of an API with no backend and no conversion build.
	class GlBackend final : public KernelBackend {
	public:
		GpuApi api() const noexcept override { return GpuApi::OpenGL; }
		bool available() const noexcept override { return true; }
		Error dispatch(const KernelDispatch&) override { return Error::none(); }
	};
	GlBackend backend;
	KernelImage like;
	KernelImage scratch;
	EXPECT_EQ(backend.makeScratch(like, {0, 0, 4, 4}, scratch).code(), ErrorCode::Unsupported);
	CpuKernelBackend cpu;
	EXPECT_EQ(cpu.makeScratch(like, RectI{}, scratch).code(), ErrorCode::InvalidParameter);
	ASSERT_TRUE(cpu.makeScratch(like, {1, 2, 5, 4}, scratch).ok());
	EXPECT_TRUE(scratch.connected);
	EXPECT_NE(scratch.owner, nullptr);
	EXPECT_EQ(scratch.bounds(), (RectI{1, 2, 5, 4}));
	EXPECT_EQ(scratch.desc().depth, BitDepth::Float);
	EXPECT_EQ(scratch.desc().components, Components::RGBA);
}

#if defined(OFXGRIP_HAS_METAL)

TEST(KernelEngine, AColorPassOnMetalMatchesTheCpu)
{
	std::shared_ptr<metal::MetalContext> context = metal::MetalContext::systemDefault();
	if (!context) {
		GTEST_SKIP() << "No Metal device";
	}
	MetalKernelBackend::registerWithEngine();
	ASSERT_TRUE(KernelEngine::shared().convertsColorOn(GpuApi::Metal));
	const RectI bounds{0, 0, 40, 12};
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
	for (BitDepth depth : {BitDepth::Float, BitDepth::Half, BitDepth::UInt16}) {
		for (const RectI& window : {bounds, RectI{5, 2, 33, 11}}) {
			for (bool intermediateCopy : {false, true}) {
				ImageBuffer source = image(bounds, depth);
				fill(source, 0.0f, 1.0f);
				const KernelColorPass pass = passBetween(ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Gamma24}, kLinear2020);

				ImageBuffer cpuOut = image(bounds, depth);
				KernelDispatch c = convertDispatch(GpuApi::None, redOnly(), true);
				c.destination = cpuImage(cpuOut);
				c.sources = {cpuImage(source)};
				c.window = window;
				c.color = pass;
				ASSERT_TRUE(KernelEngine::shared().run(c).ok());

				ImageBuffer gpuOut = image(bounds, depth);
				auto [outImage, outBuffer] = upload(gpuOut);
				auto [sourceImage, sourceBuffer] = upload(source);
				sourceImage.gpu.needsIntermediateCopy = intermediateCopy;
				KernelDispatch g = convertDispatch(GpuApi::Metal, redOnly(), true);
				g.destination = outImage;
				g.sources = {sourceImage};
				g.window = window;
				g.color = pass;
				const Error e = KernelEngine::shared().run(g);
				ASSERT_TRUE(e.ok()) << e.describe();
				ASSERT_TRUE(context->waitIdle().ok());
				std::memcpy(gpuOut.bytes().data(), outBuffer.contents(), gpuOut.bytes().size());
				for (int y = bounds.y1; y < bounds.y2; ++y) {
					for (int x = bounds.x1; x < bounds.x2; ++x) {
						for (std::size_t ch = 0; ch < 4; ++ch) {
							ASSERT_NEAR(at(cpuOut, x, y, ch), at(gpuOut, x, y, ch), depth == BitDepth::Half ? 0.01f : 0.004f) << toString(depth) << " at " << x << "," << y << " channel " << ch;
						}
					}
				}
			}
		}
	}
	// The scratch buffers return to the pool once the queue is idle.
	ASSERT_TRUE(context->waitIdle().ok());
	EXPECT_GT(context->pooledBytes(), 0u);
}

#endif

// Runs on a real device. It skips on a machine without an NVIDIA driver, and until an NVIDIA
// toolchain has compiled the conversion kernel's PTX.
TEST(KernelEngine, AColorPassOnCudaMatchesTheCpuOnADevice)
{
	const cuda::CudaDriver* driver = cuda::CudaDriver::system();
	if (driver == nullptr) {
		GTEST_SKIP() << "No CUDA driver is installed";
	}
	if (kernelColorBuild(GpuApi::Cuda) == nullptr) {
		GTEST_SKIP() << "GripColorConvert has no PTX build";
	}
	ofxgrip::testing::CudaDeviceContext context;
	if (!context.current()) {
		GTEST_SKIP() << context.unavailableReason();
	}
	CudaKernelBackend::registerWithEngine();
	if (!KernelEngine::shared().convertsColorOn(GpuApi::Cuda)) {
		GTEST_SKIP() << "The driver is older than the toolkit that built the conversion kernel's PTX";
	}
	const RectI bounds{0, 0, 40, 12};
	const std::size_t rowBytes = static_cast<std::size_t>(bounds.width()) * 4 * sizeof(float);
	const std::size_t bytes = rowBytes * static_cast<std::size_t>(bounds.height());
	auto frame = [&](cuda::CudaDevicePointer pointer) {
		KernelImage k;
		k.connected = true;
		k.gpu.api = GpuApi::Cuda;
		k.gpu.handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(pointer));
		k.gpu.desc = image(bounds).desc();
		k.gpu.rowBytes = static_cast<std::ptrdiff_t>(rowBytes);
		return k;
	};
	for (const RectI& window : {bounds, RectI{5, 2, 33, 11}}) {
		ImageBuffer source = image(bounds);
		fill(source, 0.0f, 1.0f);
		const KernelColorPass pass = passBetween(ColorSpace{ColorPrimaries::Rec709, ColorTransfer::Gamma24}, kLinear2020);

		ImageBuffer cpuOut = image(bounds);
		KernelDispatch c = convertDispatch(GpuApi::None, redOnly(), true);
		c.destination = cpuImage(cpuOut);
		c.sources = {cpuImage(source)};
		c.window = window;
		c.color = pass;
		ASSERT_TRUE(KernelEngine::shared().run(c).ok());

		cuda::CudaDevicePointer in = 0;
		cuda::CudaDevicePointer out = 0;
		ASSERT_EQ(driver->memAlloc(&in, bytes), cuda::kCudaSuccess);
		ASSERT_EQ(driver->memAlloc(&out, bytes), cuda::kCudaSuccess);
		// ImageBuffer rows are 16-byte multiples, so a float RGBA buffer has no row padding.
		ASSERT_EQ(source.rowBytes(), rowBytes);
		ASSERT_EQ(driver->memcpyHtoD(in, source.bytes().data(), bytes), cuda::kCudaSuccess);
		const std::vector<std::uint8_t> zeros(bytes, 0);
		ASSERT_EQ(driver->memcpyHtoD(out, zeros.data(), bytes), cuda::kCudaSuccess);
		{
			KernelDispatch g = convertDispatch(GpuApi::Cuda, redOnly(), true);
			g.destination = frame(out);
			g.sources = {frame(in)};
			g.window = window;
			g.color = pass;
			const Error e = KernelEngine::shared().run(g);
			ASSERT_TRUE(e.ok()) << e.describe();
		}
		ImageBuffer gpuOut = image(bounds);
		cuda::CudaMemcpy2D copy;
		copy.sourceType = cuda::kCudaMemoryDevice;
		copy.sourceDevice = out;
		copy.sourcePitch = rowBytes;
		copy.destinationType = cuda::kCudaMemoryHost;
		copy.destinationHost = gpuOut.bytes().data();
		copy.destinationPitch = gpuOut.rowBytes();
		copy.widthBytes = rowBytes;
		copy.height = static_cast<std::size_t>(bounds.height());
		ASSERT_EQ(driver->memcpy2D(&copy), cuda::kCudaSuccess);
		driver->memFree(in);
		driver->memFree(out);
		for (int y = bounds.y1; y < bounds.y2; ++y) {
			for (int x = bounds.x1; x < bounds.x2; ++x) {
				for (std::size_t ch = 0; ch < 4; ++ch) {
					ASSERT_NEAR(at(cpuOut, x, y, ch), at(gpuOut, x, y, ch), 0.004f) << "at " << x << "," << y << " channel " << ch;
				}
			}
		}
	}
	KernelEngine::shared().releaseDeviceState();
}

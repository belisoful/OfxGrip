// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The Vulkan device: the loader failure paths on every machine, and, where a device opens, the
// job's validation, the pipeline cache, specialization constants, and concurrent jobs. The
// kernels are the fixture kernels' SPIR-V builds.
#include <ofxgrip/gpu/KernelCpu.hpp>
#include <ofxgrip/gpu/VulkanBackend.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelTile.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <thread>

OFXGRIP_DECLARE_KERNEL(GripBlur);
OFXGRIP_DECLARE_KERNEL(GripInspect);

using namespace ofxgrip;
using namespace ofxgrip::vulkan;

namespace {

const KernelImplementation* spirvOf(const KernelRecord& record)
{
	for (const KernelImplementation& implementation : record.implementations) {
		if (implementation.api == GpuApi::Vulkan) {
			return &implementation;
		}
	}
	return nullptr;
}

/*! The inspect kernel in fill mode over a destination of one size: every component of every
    texel becomes `fill`. The constants and the tile are bound by hand from the SPIR-V layout. */
struct FillJob {
	KernelRecord record{kernelRecordFor(OFXGRIP_KERNEL(GripInspect))};
	KernelLayout layout;
	GripTile tile;
	Bytes uniforms;
	std::vector<float> destination;
	VulkanJob job;

	void constant(const char* name, const void* bytes, std::size_t size)
	{
		const KernelConstant* declared = layout.constantNamed(name);
		ASSERT_NE(declared, nullptr) << name;
		VulkanConstant value;
		value.id = static_cast<std::uint32_t>(declared->slotIndex);
		std::memcpy(value.bytes.data(), bytes, size);
		value.length = size;
		job.constants.push_back(value);
	}

	FillJob(unsigned width, unsigned height, float fill, std::int32_t taps = 2)
	{
		const KernelImplementation* implementation = spirvOf(record);
		EXPECT_NE(implementation, nullptr);
		const Error e = KernelLayout::parse(implementation->layoutJson, layout);
		EXPECT_TRUE(e.ok()) << e.describe();
		const RectI window{0, 0, static_cast<int>(width), static_cast<int>(height)};
		std::vector<std::optional<RectI>> bounds(layout.textureCount());
		bounds[layout.destination().mapIndex] = window;
		tile = makeGripTile(window, {1, 1}, bounds, true);
		uniforms.assign(layout.uniformsLength(), 0);
		destination.assign(static_cast<std::size_t>(width) * height * 4, -1.0f);
		job.cacheKey = "ofxgrip.tests.fill";
		job.spirv = implementation->source;
		job.buffers.push_back({static_cast<std::uint32_t>(layout.uniformsIndex()), uniforms.data(), uniforms.size()});
		job.buffers.push_back({static_cast<std::uint32_t>(layout.tileIndex()), &tile, sizeof tile});
		for (const KernelTexture& texture : layout.textures()) {
			if (texture.access == KernelTextureAccess::Read) {
				job.sources.push_back({static_cast<std::uint32_t>(texture.slotIndex), nullptr, 0, 0});
			}
		}
		for (const KernelSampler& sampler : layout.samplers()) {
			job.samplers.push_back(static_cast<std::uint32_t>(sampler.slotIndex));
		}
		const std::int32_t mode = 8;
		constant("MODE", &mode, sizeof mode);
		constant("TAPS", &taps, sizeof taps);
		constant("FILL", &fill, sizeof fill);
		job.destinationBinding = static_cast<std::uint32_t>(layout.destination().slotIndex);
		job.width = width;
		job.height = height;
		job.destination = destination.data();
		job.groupSize = layout.threadGroupSize();
	}
};

}  // namespace

TEST(VulkanBackend, ReadsTheComputeEntryPointOfAModule)
{
	const KernelRecord record = kernelRecordFor(OFXGRIP_KERNEL(GripBlur));
	const KernelImplementation* implementation = spirvOf(record);
	ASSERT_NE(implementation, nullptr);
	EXPECT_TRUE(implementation->universal);
	EXPECT_EQ(spirvComputeEntryPoint(implementation->source), "main");
	EXPECT_EQ(spirvComputeEntryPoint("not a module"), "");
	EXPECT_EQ(spirvComputeEntryPoint(std::string(64, '\0')), "");
	// A header with no entry point, and an instruction that runs past the end.
	std::string header = implementation->source.substr(0, 20);
	EXPECT_EQ(spirvComputeEntryPoint(header), "");
	const std::uint32_t overrun = (100u << 16) | 15u;
	header.append(reinterpret_cast<const char*>(&overrun), sizeof overrun);
	EXPECT_EQ(spirvComputeEntryPoint(header), "");
}

TEST(VulkanBackend, OpenReportsAMissingOrForeignLibrary)
{
	std::shared_ptr<VulkanContext> context;
	Error e = VulkanContext::open("/nonexistent/ofxgrip/libvulkan-missing.so", context);
	EXPECT_EQ(e.code(), ErrorCode::MissingHostFeature);
	EXPECT_EQ(context, nullptr);
#if defined(__APPLE__)
	const char* foreign = "/usr/lib/libz.dylib";
#elif defined(_WIN32)
	const char* foreign = "kernel32.dll";
#else
	const char* foreign = "libm.so.6";
#endif
	e = VulkanContext::open(foreign, context);
	EXPECT_EQ(e.code(), ErrorCode::MissingHostFeature);
	EXPECT_NE(e.describe().find("not a Vulkan loader"), std::string::npos) << e.describe();
	EXPECT_EQ(context, nullptr);
}

TEST(VulkanBackend, SharedDeviceReportsWhatItOpened)
{
	std::shared_ptr<VulkanContext> context = VulkanContext::shared();
	EXPECT_EQ(context != nullptr, VulkanContext::available());
	if (!context) {
		EXPECT_FALSE(VulkanContext::unavailableReason().empty());
		GTEST_SKIP() << "No Vulkan device: " << VulkanContext::unavailableReason();
	}
	EXPECT_TRUE(VulkanContext::unavailableReason().empty());
	EXPECT_FALSE(context->info().name.empty());
	EXPECT_GT(context->info().apiVersion, 0u);
	EXPECT_EQ(VulkanContext::shared(), context);
	RecordProperty("device", context->info().name);
}

TEST(VulkanBackend, RunsAJobAndCachesItsPipeline)
{
	std::shared_ptr<VulkanContext> context = VulkanContext::shared();
	if (!context) {
		GTEST_SKIP() << "No Vulkan device";
	}
	// A device of its own: the cache counts below see no other test's pipelines.
	std::shared_ptr<VulkanContext> own;
	ASSERT_TRUE(VulkanContext::open(std::string(), own).ok());
	EXPECT_EQ(own->cachedPipelineCount(), 0u);
	FillJob first(19, 5, 0.375f);
	Error e = own->run(first.job);
	ASSERT_TRUE(e.ok()) << e.describe();
	for (std::size_t i = 0; i < first.destination.size(); i += 4) {
		ASSERT_FLOAT_EQ(first.destination[i], 0.375f) << "texel " << i / 4;
		ASSERT_FLOAT_EQ(first.destination[i + 3], 0.375f);
	}
	EXPECT_EQ(own->cachedPipelineCount(), 1u);
	FillJob again(4, 4, 0.375f);
	ASSERT_TRUE(own->run(again.job).ok());
	EXPECT_FLOAT_EQ(again.destination[0], 0.375f);
	EXPECT_EQ(own->cachedPipelineCount(), 1u);
	// Another constant value is another pipeline.
	FillJob specialized(4, 4, 0.375f, 5);
	ASSERT_TRUE(own->run(specialized.job).ok());
	EXPECT_EQ(own->cachedPipelineCount(), 2u);
}

TEST(VulkanBackend, RejectsJobsItCannotRun)
{
	std::shared_ptr<VulkanContext> context = VulkanContext::shared();
	if (!context) {
		GTEST_SKIP() << "No Vulkan device";
	}
	FillJob noDestination(4, 4, 0.5f);
	noDestination.job.destination = nullptr;
	EXPECT_EQ(context->run(noDestination.job).code(), ErrorCode::InvalidParameter);
	FillJob empty(4, 4, 0.5f);
	empty.job.width = 0;
	EXPECT_EQ(context->run(empty.job).code(), ErrorCode::InvalidParameter);
	FillJob noGroup(4, 4, 0.5f);
	noGroup.job.groupSize = {0, 8, 1};
	EXPECT_EQ(context->run(noGroup.job).code(), ErrorCode::InvalidParameter);
	FillJob notSpirv(4, 4, 0.5f);
	notSpirv.job.spirv = "this is not a SPIR-V module at all";
	EXPECT_EQ(context->run(notSpirv.job).code(), ErrorCode::InvalidParameter);
	FillJob noEntry(4, 4, 0.5f);
	const std::string header(noEntry.job.spirv.substr(0, 20));
	noEntry.job.spirv = header;
	EXPECT_EQ(context->run(noEntry.job).code(), ErrorCode::InvalidParameter);
	FillJob huge(4, 4, 0.5f);
	huge.job.width = 1u << 30;
	EXPECT_EQ(context->run(huge.job).code(), ErrorCode::Unsupported);
	FillJob hugeSource(4, 4, 0.5f);
	const float texel[4] = {0, 0, 0, 0};
	hugeSource.job.sources[0].rgba = texel;
	hugeSource.job.sources[0].width = 1u << 30;
	hugeSource.job.sources[0].height = 1;
	EXPECT_EQ(context->run(hugeSource.job).code(), ErrorCode::Unsupported);
}

TEST(VulkanBackend, ConcurrentJobsKeepTheirOwnPixels)
{
	std::shared_ptr<VulkanContext> context = VulkanContext::shared();
	if (!context) {
		GTEST_SKIP() << "No Vulkan device";
	}
	constexpr int kThreads = 4;
	constexpr int kRounds = 6;
	std::vector<std::thread> pool;
	std::vector<int> failures(kThreads, 0);
	for (int t = 0; t < kThreads; ++t) {
		pool.emplace_back([&, t]() {
			for (int round = 0; round < kRounds; ++round) {
				const float fill = static_cast<float>(t + 1) / 8.0f;
				FillJob fillJob(17 + static_cast<unsigned>(t), 9, fill);
				if (!context->run(fillJob.job).ok()) {
					++failures[static_cast<std::size_t>(t)];
					continue;
				}
				for (std::size_t i = 0; i < fillJob.destination.size(); i += 4) {
					if (fillJob.destination[i] != fill) {
						++failures[static_cast<std::size_t>(t)];
						break;
					}
				}
			}
		});
	}
	for (std::thread& thread : pool) {
		thread.join();
	}
	for (int t = 0; t < kThreads; ++t) {
		EXPECT_EQ(failures[static_cast<std::size_t>(t)], 0) << "thread " << t;
	}
}

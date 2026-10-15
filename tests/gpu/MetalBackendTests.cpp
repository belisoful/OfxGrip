// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The Metal backend on the system default device. Every test skips on a machine without one.
#include <ofxgrip/gpu/MetalBackend.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ofxgrip;
using namespace ofxgrip::metal;

namespace {

constexpr const char* kShader = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Args { uint width; uint height; uint offset; };
kernel void fillIndex(device uint* out [[buffer(0)]], constant Args& args [[buffer(1)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= args.width || gid.y >= args.height) {
        return;
    }
    out[gid.y * args.width + gid.x] = gid.y * args.width + gid.x + args.offset;
}
kernel void addOne(device const uint* in [[buffer(0)]], device uint* out [[buffer(1)]], constant Args& args [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= args.width || gid.y >= args.height) {
        return;
    }
    const uint i = gid.y * args.width + gid.x;
    out[i] = in[i] + 1;
}
)MSL";

struct Args {
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t offset;
};

std::shared_ptr<MetalContext> contextOrSkip()
{
	std::shared_ptr<MetalContext> context = MetalContext::systemDefault();
	return context;
}

#define OFXGRIP_METAL_OR_SKIP()                               \
	std::shared_ptr<MetalContext> context = contextOrSkip(); \
	if (!context) {                                           \
		GTEST_SKIP() << "No Metal device";                    \
	}

}  // namespace

TEST(MetalBackend, SystemDefaultContextIsSharedAndDescribed)
{
	OFXGRIP_METAL_OR_SKIP();
	EXPECT_TRUE(MetalContext::available());
	EXPECT_EQ(MetalContext::systemDefault(), context);
	EXPECT_NE(context->queue(), nullptr);
	EXPECT_NE(context->device(), nullptr);
	EXPECT_FALSE(context->deviceName().empty());
	EXPECT_EQ(MetalContext::forQueue(context->queue()), context);
	EXPECT_EQ(MetalContext::forQueue(nullptr), nullptr);
}

TEST(MetalBackend, CompilesCachesAndDispatchesAKernel)
{
	OFXGRIP_METAL_OR_SKIP();
	MetalPipeline pipeline;
	EXPECT_FALSE(pipeline.valid());
	EXPECT_EQ(pipeline.state(), nullptr);
	EXPECT_EQ(pipeline.maxThreadsPerThreadgroup(), 0u);
	EXPECT_EQ(pipeline.threadExecutionWidth(), 0u);
	EXPECT_TRUE(pipeline.function().empty());
	const std::size_t before = context->pipelineCount();
	Error e = context->pipeline("ofxgrip.tests", kShader, "fillIndex", pipeline);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(pipeline.valid());
	EXPECT_NE(pipeline.state(), nullptr);
	EXPECT_GT(pipeline.maxThreadsPerThreadgroup(), 0u);
	EXPECT_GT(pipeline.threadExecutionWidth(), 0u);
	EXPECT_EQ(pipeline.function(), "fillIndex");
	MetalPipeline again;
	ASSERT_TRUE(context->pipeline("ofxgrip.tests", kShader, "fillIndex", again).ok());
	EXPECT_EQ(again.state(), pipeline.state());
	EXPECT_EQ(context->pipelineCount(), before + 1);

	const Args args{70, 33, 5};
	MetalBuffer out = context->newBuffer(args.width * args.height * sizeof(std::uint32_t));
	ASSERT_TRUE(out.valid());
	EXPECT_GE(out.length(), args.width * args.height * sizeof(std::uint32_t));
	MetalDispatch dispatch;
	dispatch.pipeline = pipeline;
	dispatch.width = args.width;
	dispatch.height = args.height;
	dispatch.buffers = {{out.handle(), 0, 0}};
	dispatch.bytes = {{&args, sizeof args, 1}};
	MetalCommandBuffer command = context->commandBuffer();
	ASSERT_TRUE(command.valid());
	EXPECT_FALSE(command.committed());
	ASSERT_TRUE(command.dispatch(dispatch).ok());
	ASSERT_TRUE(command.commitAndWait().ok());
	EXPECT_TRUE(command.committed());
	const auto* values = static_cast<const std::uint32_t*>(out.contents());
	for (std::uint32_t i = 0; i < args.width * args.height; ++i) {
		ASSERT_EQ(values[i], i + args.offset) << i;
	}
}

TEST(MetalBackend, CopiesFillsAndChainsKernels)
{
	OFXGRIP_METAL_OR_SKIP();
	const Args args{16, 4, 0};
	std::vector<std::uint32_t> initial(args.width * args.height);
	for (std::uint32_t i = 0; i < initial.size(); ++i) {
		initial[i] = i * 3;
	}
	MetalBuffer in = context->newBuffer(initial.size() * sizeof(std::uint32_t), initial.data());
	MetalBuffer out = context->newBuffer(initial.size() * sizeof(std::uint32_t));
	MetalBuffer copy = context->newBuffer(initial.size() * sizeof(std::uint32_t));
	ASSERT_TRUE(in.valid() && out.valid() && copy.valid());
	EXPECT_EQ(std::memcmp(in.contents(), initial.data(), initial.size() * sizeof(std::uint32_t)), 0);
	MetalPipeline addOne;
	ASSERT_TRUE(context->pipeline("ofxgrip.tests", kShader, "addOne", addOne).ok());
	MetalDispatch dispatch;
	dispatch.pipeline = addOne;
	dispatch.width = args.width;
	dispatch.height = args.height;
	dispatch.buffers = {{in.handle(), 0, 0}, {out.handle(), 0, 1}};
	dispatch.bytes = {{&args, sizeof args, 2}};
	MetalCommandBuffer command = context->commandBuffer();
	ASSERT_TRUE(command.dispatch(dispatch).ok());
	ASSERT_TRUE(command.copy(out.handle(), 0, copy.handle(), 0, initial.size() * sizeof(std::uint32_t)).ok());
	ASSERT_TRUE(command.fill(out.handle(), 0, 4, 0xFF).ok());
	ASSERT_TRUE(command.copy(out.handle(), 0, copy.handle(), 0, 0).ok());
	ASSERT_TRUE(command.fill(out.handle(), 0, 0, 1).ok());
	ASSERT_TRUE(command.commit().ok());
	ASSERT_TRUE(context->waitIdle().ok());
	const auto* copied = static_cast<const std::uint32_t*>(copy.contents());
	for (std::uint32_t i = 0; i < initial.size(); ++i) {
		ASSERT_EQ(copied[i], initial[i] + 1) << i;
	}
	EXPECT_EQ(static_cast<const std::uint32_t*>(out.contents())[0], 0xFFFFFFFFu);
	EXPECT_EQ(static_cast<const std::uint32_t*>(out.contents())[1], initial[1] + 1);
}

TEST(MetalBackend, ScratchBuffersReturnToThePoolAfterCompletion)
{
	OFXGRIP_METAL_OR_SKIP();
	context->drainPool();
	EXPECT_EQ(context->pooledBytes(), 0u);
	{
		MetalBuffer scratch = context->scratch(1024);
		ASSERT_TRUE(scratch.valid());
		EXPECT_GE(scratch.length(), 1024u);
		EXPECT_EQ(context->pooledBytes(), 0u);
	}
	EXPECT_GE(context->pooledBytes(), 1024u);
	{
		MetalBuffer reused = context->scratch(512);
		EXPECT_GE(reused.length(), 1024u);  // the smallest buffer that fits is reused
		EXPECT_EQ(context->pooledBytes(), 0u);
		MetalBuffer bigger = context->scratch(4096);
		EXPECT_GE(bigger.length(), 4096u);
	}
	EXPECT_GE(context->pooledBytes(), 1024u + 4096u);

	// A buffer retained by a command buffer returns only after the GPU finished with it.
	context->drainPool();
	MetalBuffer held = context->scratch(256);
	void* handle = held.handle();
	MetalCommandBuffer command = context->commandBuffer();
	ASSERT_TRUE(command.fill(handle, 0, 256, 7).ok());
	command.retain(std::move(held));
	command.retain(MetalBuffer{});
	ASSERT_TRUE(command.commit().ok());
	ASSERT_TRUE(context->waitIdle().ok());
	EXPECT_GE(context->pooledBytes(), 256u);
	MetalBuffer back = context->scratch(256);
	EXPECT_EQ(back.handle(), handle);
	EXPECT_EQ(static_cast<const std::uint8_t*>(back.contents())[255], 7);
	context->drainPool();
}

TEST(MetalBackend, ReportsErrors)
{
	OFXGRIP_METAL_OR_SKIP();
	MetalPipeline pipeline;
	EXPECT_EQ(context->pipeline("ofxgrip.tests", kShader, "nonesuch", pipeline).code(), ErrorCode::NotFound);
	EXPECT_EQ(context->pipeline("ofxgrip.broken", "kernel void broken( { }", "broken", pipeline).code(), ErrorCode::Render);
	ASSERT_TRUE(context->pipeline("ofxgrip.tests", kShader, "fillIndex", pipeline).ok());

	MetalCommandBuffer invalid;
	EXPECT_FALSE(invalid.valid());
	EXPECT_FALSE(invalid.committed());
	MetalDispatch dispatch;
	EXPECT_EQ(invalid.dispatch(dispatch).code(), ErrorCode::BadHandle);
	EXPECT_EQ(invalid.copy(nullptr, 0, nullptr, 0, 0).code(), ErrorCode::BadHandle);
	EXPECT_EQ(invalid.fill(nullptr, 0, 0, 0).code(), ErrorCode::BadHandle);
	EXPECT_EQ(invalid.commit().code(), ErrorCode::BadHandle);
	EXPECT_EQ(invalid.commitAndWait().code(), ErrorCode::BadHandle);
	invalid.retain(MetalBuffer{});

	MetalBuffer buffer = context->newBuffer(64);
	MetalCommandBuffer command = context->commandBuffer();
	EXPECT_EQ(command.dispatch(dispatch).code(), ErrorCode::BadHandle);  // no pipeline
	dispatch.pipeline = pipeline;
	dispatch.width = 0;
	EXPECT_TRUE(command.dispatch(dispatch).ok());  // an empty grid is a no-op
	dispatch.width = 4;
	dispatch.buffers = {{nullptr, 0, 0}};
	EXPECT_EQ(command.dispatch(dispatch).code(), ErrorCode::BadHandle);
	EXPECT_EQ(command.copy(buffer.handle(), 0, nullptr, 0, 8).code(), ErrorCode::BadHandle);
	EXPECT_EQ(command.copy(buffer.handle(), 0, buffer.handle(), 32, 64).code(), ErrorCode::BadIndex);
	EXPECT_EQ(command.fill(nullptr, 0, 8, 0).code(), ErrorCode::BadHandle);
	EXPECT_EQ(command.fill(buffer.handle(), 60, 8, 0).code(), ErrorCode::BadIndex);
	ASSERT_TRUE(command.commit().ok());
	EXPECT_EQ(command.commit().code(), ErrorCode::WrongPhase);
	EXPECT_EQ(command.dispatch(dispatch).code(), ErrorCode::WrongPhase);
	EXPECT_EQ(command.copy(buffer.handle(), 0, buffer.handle(), 0, 8).code(), ErrorCode::WrongPhase);
	EXPECT_EQ(command.fill(buffer.handle(), 0, 8, 0).code(), ErrorCode::WrongPhase);
	EXPECT_TRUE(context->waitIdle().ok());

	MetalBuffer none;
	EXPECT_FALSE(none.valid());
	EXPECT_EQ(none.handle(), nullptr);
	EXPECT_EQ(none.length(), 0u);
	EXPECT_EQ(none.contents(), nullptr);
	MetalBuffer tiny = context->newBuffer(0);
	EXPECT_TRUE(tiny.valid());
	MetalCommandBuffer moved = std::move(command);
	EXPECT_TRUE(moved.committed());
	MetalCommandBuffer assigned;
	assigned = std::move(moved);
	EXPECT_TRUE(assigned.committed());
}

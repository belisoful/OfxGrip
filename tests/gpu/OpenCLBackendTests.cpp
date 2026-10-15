// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The OpenCL backend on the first GPU device. Every test skips on a machine without one.
#include <ofxgrip/gpu/OpenCLBackend.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ofxgrip;
using namespace ofxgrip::opencl;

namespace {

constexpr const char* kSource = R"CL(
typedef struct { uint width; uint height; uint offset; } Args;
__kernel void fillIndex(__global uint* out, Args args)
{
    const uint x = get_global_id(0);
    const uint y = get_global_id(1);
    if (x >= args.width || y >= args.height) { return; }
    out[y * args.width + x] = y * args.width + x + args.offset;
}
__kernel void addOne(__global const uint* in, __global uint* out, Args args)
{
    const uint x = get_global_id(0);
    const uint y = get_global_id(1);
    if (x >= args.width || y >= args.height) { return; }
    const uint i = y * args.width + x;
    out[i] = in[i] + 1;
}
)CL";

struct Args {
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t offset;
};

#define OFXGRIP_OPENCL_OR_SKIP()                                        \
	std::shared_ptr<OpenCLContext> context = OpenCLContext::systemDefault(); \
	if (!context) {                                                      \
		GTEST_SKIP() << "No OpenCL device";                              \
	}

}  // namespace

TEST(OpenCLBackend, SystemDefaultContextIsSharedAndDescribed)
{
	OFXGRIP_OPENCL_OR_SKIP();
	EXPECT_TRUE(OpenCLContext::available());
	EXPECT_EQ(OpenCLContext::systemDefault(), context);
	EXPECT_NE(context->queue(), nullptr);
	EXPECT_NE(context->context(), nullptr);
	EXPECT_NE(context->device(), nullptr);
	EXPECT_FALSE(context->deviceName().empty());
	EXPECT_EQ(OpenCLContext::forQueue(context->queue()), context);
	EXPECT_EQ(OpenCLContext::forQueue(nullptr), nullptr);
}

TEST(OpenCLBackend, CompilesCachesAndDispatchesAKernel)
{
	OFXGRIP_OPENCL_OR_SKIP();
	OpenCLKernel kernel;
	EXPECT_FALSE(kernel.valid());
	EXPECT_EQ(kernel.handle(), nullptr);
	EXPECT_TRUE(kernel.name().empty());
	const std::size_t before = context->programCount();
	Error e = context->kernel("ofxgrip.tests", kSource, "fillIndex", kernel);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(kernel.valid());
	EXPECT_NE(kernel.handle(), nullptr);
	EXPECT_EQ(kernel.name(), "fillIndex");
	OpenCLKernel again;
	ASSERT_TRUE(context->kernel("ofxgrip.tests", kSource, "fillIndex", again).ok());
	EXPECT_NE(again.handle(), kernel.handle());  // a kernel object per call
	EXPECT_EQ(context->programCount(), before + 1);

	const Args args{70, 33, 5};
	OpenCLBuffer out = context->newBuffer(args.width * args.height * sizeof(std::uint32_t));
	ASSERT_TRUE(out.valid());
	EXPECT_GE(out.length(), args.width * args.height * sizeof(std::uint32_t));
	OpenCLDispatch dispatch;
	dispatch.kernel = kernel;
	dispatch.globalSize[0] = args.width;
	dispatch.globalSize[1] = args.height;
	dispatch.buffers = {{out.handle(), 0}};
	dispatch.bytes = {{&args, sizeof args, 1}};
	ASSERT_TRUE(context->enqueue(dispatch).ok());
	ASSERT_TRUE(context->finish().ok());
	std::vector<std::uint32_t> values(args.width * args.height);
	ASSERT_TRUE(context->read(out.handle(), 0, values.size() * sizeof(std::uint32_t), values.data()).ok());
	for (std::uint32_t i = 0; i < values.size(); ++i) {
		ASSERT_EQ(values[i], i + args.offset) << i;
	}
	// A fixed workgroup rounds the grid up; the kernel bounds-checks.
	dispatch.localSize[0] = 16;
	dispatch.localSize[1] = 16;
	ASSERT_TRUE(context->enqueue(dispatch).ok());
	ASSERT_TRUE(context->finish().ok());
}

TEST(OpenCLBackend, CopiesFillsAndChainsKernels)
{
	OFXGRIP_OPENCL_OR_SKIP();
	const Args args{16, 4, 0};
	std::vector<std::uint32_t> initial(args.width * args.height);
	for (std::uint32_t i = 0; i < initial.size(); ++i) {
		initial[i] = i * 3;
	}
	const std::size_t bytes = initial.size() * sizeof(std::uint32_t);
	OpenCLBuffer in = context->newBuffer(bytes, initial.data());
	OpenCLBuffer out = context->newBuffer(bytes);
	OpenCLBuffer copy = context->newBuffer(bytes);
	ASSERT_TRUE(in.valid() && out.valid() && copy.valid());
	OpenCLKernel addOne;
	ASSERT_TRUE(context->kernel("ofxgrip.tests", kSource, "addOne", addOne).ok());
	OpenCLDispatch dispatch;
	dispatch.kernel = addOne;
	dispatch.globalSize[0] = args.width;
	dispatch.globalSize[1] = args.height;
	dispatch.buffers = {{in.handle(), 0}, {out.handle(), 1}};
	dispatch.bytes = {{&args, sizeof args, 2}};
	ASSERT_TRUE(context->enqueue(dispatch).ok());
	ASSERT_TRUE(context->copy(out.handle(), 0, copy.handle(), 0, bytes).ok());
	ASSERT_TRUE(context->fill(out.handle(), 0, 4, 0xFF).ok());
	ASSERT_TRUE(context->copy(out.handle(), 0, copy.handle(), 0, 0).ok());
	ASSERT_TRUE(context->fill(out.handle(), 0, 0, 1).ok());
	ASSERT_TRUE(context->finish().ok());
	std::vector<std::uint32_t> copied(initial.size());
	ASSERT_TRUE(context->read(copy.handle(), 0, bytes, copied.data()).ok());
	for (std::uint32_t i = 0; i < initial.size(); ++i) {
		ASSERT_EQ(copied[i], initial[i] + 1) << i;
	}
	std::vector<std::uint32_t> filled(initial.size());
	ASSERT_TRUE(context->read(out.handle(), 0, bytes, filled.data()).ok());
	EXPECT_EQ(filled[0], 0xFFFFFFFFu);
	EXPECT_EQ(filled[1], initial[1] + 1);
	const std::uint32_t seven = 7;
	ASSERT_TRUE(context->write(out.handle(), 4, sizeof seven, &seven).ok());
	ASSERT_TRUE(context->read(out.handle(), 4, sizeof seven, filled.data()).ok());
	EXPECT_EQ(filled[0], 7u);
}

TEST(OpenCLBackend, ScratchBuffersReturnToThePool)
{
	OFXGRIP_OPENCL_OR_SKIP();
	context->drainPool();
	EXPECT_EQ(context->pooledBytes(), 0u);
	{
		OpenCLBuffer scratch = context->scratch(1024);
		ASSERT_TRUE(scratch.valid());
		EXPECT_GE(scratch.length(), 1024u);
		EXPECT_EQ(context->pooledBytes(), 0u);
	}
	EXPECT_GE(context->pooledBytes(), 1024u);
	{
		OpenCLBuffer reused = context->scratch(512);
		EXPECT_GE(reused.length(), 1024u);
		EXPECT_EQ(context->pooledBytes(), 0u);
		OpenCLBuffer bigger = context->scratch(4096);
		EXPECT_GE(bigger.length(), 4096u);
	}
	EXPECT_GE(context->pooledBytes(), 1024u + 4096u);
	context->drainPool();
	EXPECT_EQ(context->pooledBytes(), 0u);
}

TEST(OpenCLBackend, ReportsErrors)
{
	OFXGRIP_OPENCL_OR_SKIP();
	OpenCLKernel kernel;
	EXPECT_EQ(context->kernel("ofxgrip.tests", kSource, "nonesuch", kernel).code(), ErrorCode::NotFound);
	EXPECT_EQ(context->kernel("ofxgrip.broken", "__kernel void broken( { }", "broken", kernel).code(), ErrorCode::Render);
	ASSERT_TRUE(context->kernel("ofxgrip.tests", kSource, "fillIndex", kernel).ok());
	OpenCLDispatch dispatch;
	EXPECT_EQ(context->enqueue(dispatch).code(), ErrorCode::BadHandle);  // no kernel
	dispatch.kernel = kernel;
	dispatch.globalSize[0] = 0;
	EXPECT_TRUE(context->enqueue(dispatch).ok());  // an empty grid is a no-op
	dispatch.globalSize[0] = 4;
	dispatch.buffers = {{nullptr, 0}};
	EXPECT_EQ(context->enqueue(dispatch).code(), ErrorCode::BadHandle);
	OpenCLBuffer buffer = context->newBuffer(64);
	dispatch.buffers = {{buffer.handle(), 0}};
	dispatch.bytes = {{nullptr, 3, 7}};  // a bad argument index
	EXPECT_EQ(context->enqueue(dispatch).code(), ErrorCode::Render);
	EXPECT_EQ(context->copy(buffer.handle(), 0, nullptr, 0, 8).code(), ErrorCode::BadHandle);
	EXPECT_EQ(context->copy(buffer.handle(), 0, buffer.handle(), 32, 64).code(), ErrorCode::Render);
	EXPECT_EQ(context->fill(nullptr, 0, 8, 0).code(), ErrorCode::BadHandle);
	EXPECT_EQ(context->fill(buffer.handle(), 60, 8, 0).code(), ErrorCode::Render);
	std::uint8_t byte = 0;
	EXPECT_EQ(context->read(nullptr, 0, 1, &byte).code(), ErrorCode::BadHandle);
	EXPECT_TRUE(context->read(buffer.handle(), 0, 0, &byte).ok());
	EXPECT_EQ(context->read(buffer.handle(), 100, 8, &byte).code(), ErrorCode::Render);
	EXPECT_EQ(context->write(nullptr, 0, 1, &byte).code(), ErrorCode::BadHandle);
	EXPECT_TRUE(context->write(buffer.handle(), 0, 0, &byte).ok());
	EXPECT_EQ(context->write(buffer.handle(), 100, 8, &byte).code(), ErrorCode::Render);
	OpenCLBuffer none;
	EXPECT_FALSE(none.valid());
	EXPECT_EQ(none.handle(), nullptr);
	EXPECT_EQ(none.length(), 0u);
	OpenCLBuffer tiny = context->newBuffer(0);
	EXPECT_TRUE(tiny.valid());
	EXPECT_TRUE(context->finish().ok());
}

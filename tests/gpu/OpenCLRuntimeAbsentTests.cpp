// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The OpenCL backend and the Gain bundle in a process that cannot open the OpenCL library.
// `main` names a library that does not exist before anything opens the runtime, so this
// program stands for a machine without OpenCL.
#include <ofxgrip/gpu/OpenCLBackend.hpp>
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#include "../TestEnvironment.hpp"

#include "ofxGPURender.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using namespace ofxgrip;
using namespace ofxgrip::minihost;
using namespace ofxgrip::opencl;

namespace {

constexpr const char* kMissingLibrary = "/nonexistent/ofxgrip/libOpenCL.so.1";

}  // namespace

TEST(OpenCLRuntimeAbsent, TheBackendReportsItselfUnavailable)
{
	EXPECT_FALSE(OpenCLContext::runtimeAvailable());
	EXPECT_NE(OpenCLContext::runtimeUnavailableReason().find(kMissingLibrary), std::string::npos);
	EXPECT_FALSE(OpenCLContext::available());
	EXPECT_EQ(OpenCLContext::systemDefault(), nullptr);
	int notAQueue = 0;
	EXPECT_EQ(OpenCLContext::forQueue(&notAQueue), nullptr);
	EXPECT_EQ(OpenCLContext::probeRuntime({}).code(), ErrorCode::MissingHostFeature);
	OpenCLContext::releaseAll();
	EXPECT_EQ(OpenCLContext::systemDefault(), nullptr);
}

TEST(OpenCLRuntimeAbsent, AnOpenCLHostGetsCpuRendersFromThePlugIn)
{
	Bundle bundle;
	Error e = bundle.open(OFXGRIP_GAIN_BUNDLE);
	ASSERT_TRUE(e.ok()) << e.describe();
	ASSERT_EQ(bundle.pluginCount(), 1);
	HostOptions options = HostOptions::spec();
	options.openCLRenderSupported = true;
	MiniHost host(options);
	bundle.setHost(host.ofxHost());
	ASSERT_EQ(host.load(bundle.plugin(0), OFXGRIP_GAIN_BUNDLE), kOfxStatOK);
	ASSERT_EQ(host.describe(), kOfxStatOK);
	EXPECT_EQ(host.descriptorProps()->getString(kOfxImageEffectPropOpenCLRenderSupported), "false");

	Project project;
	project.width = 32;
	project.height = 16;
	std::unique_ptr<Instance> instance = host.createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(instance, nullptr);
	instance->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance->setParam("gain", {0.5});
	RenderRequest request;
	request.gpu = GpuApi::OpenCL;
	RenderResult result = instance->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.gpu, GpuApi::None);
	EXPECT_NEAR(readComponent(result.image.view().pixel(31, 0), BitDepth::Float, 0), 0.5f, 0.01f);
	instance.reset();
	host.unload();
}

int main(int argc, char** argv)
{
	ofxgrip::testing::useBuildTreeFolders(OFXGRIP_TEST_ROOT);
	ofxgrip::testing::setTestEnv("OFXGRIP_OPENCL_LIBRARY", kMissingLibrary);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

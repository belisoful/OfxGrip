// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Loads the Rust sample bundle (rust/examples/gain-rs, built by cargo and packaged by CMake) and
// drives it through the mini host: the Rust effect runs the same adapter as the C++ plug-ins.
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

class RustGainBundle : public ::testing::Test {
protected:
	void SetUp() override
	{
		Error e = bundle_.open(OFXGRIP_GAINRS_BUNDLE);
		ASSERT_TRUE(e.ok()) << e.describe();
		ASSERT_EQ(bundle_.pluginCount(), 1);
		plugin_ = bundle_.plugin(0);
		ASSERT_NE(plugin_, nullptr);
		host_ = std::make_unique<MiniHost>(HostOptions::resolve21());
		bundle_.setHost(host_->ofxHost());
		ASSERT_EQ(host_->load(plugin_, OFXGRIP_GAINRS_BUNDLE), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 16;
		project.height = 8;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	void TearDown() override
	{
		instance_.reset();
		if (host_) {
			host_->unload();
		}
		host_.reset();
	}
	Bundle bundle_;
	OfxPlugin* plugin_{nullptr};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(RustGainBundle, DescribesTheManifestBuiltInRust)
{
	EXPECT_STREQ(plugin_->pluginIdentifier, "com.belisoful.ofxgrip.gainrs");
	EXPECT_EQ(host_->descriptorProps()->getString(kOfxPropLabel), "OfxGrip Gain (Rust)");
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	ASSERT_NE(params->find("gain"), nullptr);
	EXPECT_EQ(params->find("gain")->props().getDouble(kOfxParamPropDisplayMax), 4.0);
	EXPECT_EQ(params->find("gainR")->props().getString(kOfxParamPropParent), "channels");
	EXPECT_EQ(params->find("channels")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(params->find("clamp")->props().getStrings(kOfxParamPropChoiceOption).size(), 2u);
}

TEST_F(RustGainBundle, VisibilityRulesAndClicksRunThroughTheFramework)
{
	instance_->setParam("perChannel", {1.0});
	EXPECT_EQ(instance_->instanceChanged("perChannel"), kOfxStatOK);
	EXPECT_EQ(instance_->param("channels")->props().getInt(kOfxParamPropSecret, -1), 0);
	EXPECT_EQ(instance_->instanceChanged("about"), kOfxStatOK);
	ASSERT_EQ(instance_->messages().size(), 1u);
	EXPECT_NE(instance_->messages()[0].find("Rust"), std::string::npos);
}

TEST_F(RustGainBundle, RendersLikeTheCppEffect)
{
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		instance_->setInput("Source", makeRampImage(16, 8, depth));
		instance_->setParam("gain", {0.5});
		RenderRequest request;
		request.depth = depth;
		request.tileSize = 5;
		request.threads = 3;
		RenderResult result = instance_->render(request);
		ASSERT_TRUE(result.ok()) << toString(depth);
		EXPECT_FALSE(result.wasIdentity);
		const ImageView v = result.image.view();
		EXPECT_NEAR(readComponent(v.pixel(15, 0), depth, 0), 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(0, 7), depth, 1), 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(0, 0), depth, 3), 1.0f, 0.01f) << toString(depth);
	}
	instance_->setParam("gain", {1.0});
	RenderRequest unity;
	RenderResult identity = instance_->render(unity);
	ASSERT_TRUE(identity.ok());
	// Resolve renders despite identity (Q-RESOLVE-027), so the pixels pass through either way.
	EXPECT_NEAR(readComponent(identity.image.view().pixel(15, 0), BitDepth::Float, 0), 1.0f, 0.01f);
}

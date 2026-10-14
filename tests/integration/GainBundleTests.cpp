// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Loads the real OfxGripGain and OfxGripHostDump bundles the build produced and drives them
// through the mini host, as a host would.
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

class GainBundle : public ::testing::Test {
protected:
	void SetUp() override
	{
		Error e = bundle_.open(OFXGRIP_GAIN_BUNDLE);
		ASSERT_TRUE(e.ok()) << e.describe();
		ASSERT_EQ(bundle_.pluginCount(), 1);
		plugin_ = bundle_.plugin(0);
		ASSERT_NE(plugin_, nullptr);
	}
	void start(HostOptions options = HostOptions::spec())
	{
		host_ = std::make_unique<MiniHost>(options);
		bundle_.setHost(host_->ofxHost());
		ASSERT_EQ(host_->load(plugin_, OFXGRIP_GAIN_BUNDLE), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 32;
		project.height = 16;
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

TEST_F(GainBundle, ExportsThePluginRecord)
{
	EXPECT_STREQ(plugin_->pluginIdentifier, "com.belisoful.ofxgrip.gain");
	EXPECT_STREQ(plugin_->pluginApi, kOfxImageEffectPluginApi);
	EXPECT_EQ(plugin_->pluginVersionMajor, 1u);
}

TEST_F(GainBundle, DescribesParametersAndContexts)
{
	start();
	EXPECT_EQ(host_->descriptorProps()->getString(kOfxPropLabel), "OfxGrip Gain");
	EXPECT_EQ(host_->descriptorProps()->getString(kOfxImageEffectPluginPropGrouping), "OfxGrip");
	EXPECT_EQ(host_->describedContexts().size(), 2u);
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	ASSERT_NE(params->find("gain"), nullptr);
	ASSERT_NE(params->find("gainR"), nullptr);
	EXPECT_EQ(params->find("gainR")->props().getString(kOfxParamPropParent), "channels");
	EXPECT_EQ(params->find("channels")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(params->find("clamp")->props().getStrings(kOfxParamPropChoiceOption).size(), 2u);
	ASSERT_NE(params->find("ofxgrip_state"), nullptr);
	const std::vector<std::string> actions = host_->actionLog();
	EXPECT_NE(std::find(actions.begin(), actions.end(), std::string(kOfxActionLoad)), actions.end());
	EXPECT_NE(std::find(actions.begin(), actions.end(), std::string(kOfxActionDescribe)), actions.end());
	EXPECT_NE(std::find(actions.begin(), actions.end(), std::string(kOfxActionCreateInstance)), actions.end());
}

TEST_F(GainBundle, PerChannelToggleRevealsTheGroup)
{
	start();
	EXPECT_EQ(instance_->param("channels")->props().getInt(kOfxParamPropSecret, -1), 1);
	instance_->setParam("perChannel", {1.0});
	EXPECT_EQ(instance_->instanceChanged("perChannel"), kOfxStatOK);
	EXPECT_EQ(instance_->param("channels")->props().getInt(kOfxParamPropSecret, -1), 0);
	instance_->setParam("perChannel", {0.0});
	EXPECT_EQ(instance_->instanceChanged("perChannel"), kOfxStatOK);
	EXPECT_EQ(instance_->param("channels")->props().getInt(kOfxParamPropSecret, -1), 1);
}

TEST_F(GainBundle, RendersGainAcrossDepthsTilesAndThreads)
{
	start();
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		instance_->setInput("Source", makeRampImage(32, 16, depth));
		instance_->setParam("gain", {0.5});
		RenderRequest whole;
		whole.depth = depth;
		RenderResult a = instance_->render(whole);
		ASSERT_TRUE(a.ok()) << toString(depth);
		EXPECT_FALSE(a.wasIdentity);
		RenderRequest tiled = whole;
		tiled.tileSize = 7;
		tiled.threads = 4;
		RenderResult b = instance_->render(tiled);
		ASSERT_TRUE(b.ok()) << toString(depth);
		EXPECT_EQ(b.tileCount, 15);
		EXPECT_EQ(a.image.bytes(), b.image.bytes()) << toString(depth);
		const ImageView v = a.image.view();
		EXPECT_NEAR(readComponent(v.pixel(31, 0), depth, 0), 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(0, 15), depth, 1), 0.5f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(0, 0), depth, 3), 1.0f, 0.01f) << toString(depth);
	}
}

TEST_F(GainBundle, UnityGainIsIdentity)
{
	start();
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance_->setParam("gain", {1.0});
	RenderRequest request;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_TRUE(result.wasIdentity);
	EXPECT_EQ(result.identityClip, "Source");
	instance_->setParam("clamp", {1.0});
	result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_FALSE(result.wasIdentity);
}

TEST_F(GainBundle, PerChannelGainsAndClamp)
{
	start();
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance_->setParam("perChannel", {1.0});
	instance_->setParam("gainR", {3.0});
	instance_->setParam("gainG", {0.0});
	instance_->setParam("clamp", {1.0});
	RenderRequest request;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	const ImageView v = result.image.view();
	EXPECT_NEAR(readComponent(v.pixel(31, 15), BitDepth::Float, 0), 1.0f, 1e-6f);
	EXPECT_NEAR(readComponent(v.pixel(31, 15), BitDepth::Float, 1), 0.0f, 1e-6f);
	instance_->setParam("clamp", {0.0});
	result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_NEAR(readComponent(result.image.view().pixel(31, 15), BitDepth::Float, 0), 3.0f, 1e-6f);
}

TEST_F(GainBundle, ResolveProfileRendersAndAboutButtonMessages)
{
	start(HostOptions::resolve21());
	instance_->setInput("Source", makeRampImage(32, 16, BitDepth::Float));
	instance_->setParam("gain", {2.0});
	RenderRequest request;
	request.threads = 1;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_NEAR(readComponent(result.image.view().pixel(31, 0), BitDepth::Float, 0), 2.0f, 1e-6f);
	EXPECT_EQ(instance_->instanceChanged("about"), kOfxStatOK);
	ASSERT_FALSE(instance_->messages().empty());
	EXPECT_NE(instance_->messages().back().find("OfxGrip Gain"), std::string::npos);
	EXPECT_EQ(instance_->syncPrivateData(), kOfxStatOK);
}

TEST_F(GainBundle, RenderScaleHalvesTheFrame)
{
	start();
	instance_->setInput("Source", makeRampImage(16, 8, BitDepth::Float));
	instance_->setParam("gain", {2.0});
	RenderRequest request;
	request.renderScale = {0.5, 0.5};
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.image.desc().bounds, (RectI{0, 0, 16, 8}));
	EXPECT_NEAR(readComponent(result.image.view().pixel(15, 7), BitDepth::Float, 0), 2.0f, 1e-6f);
}

TEST(HostDumpBundle, LoadsDescribesAndPassesThrough)
{
	Bundle bundle;
	Error e = bundle.open(OFXGRIP_HOSTDUMP_BUNDLE);
	ASSERT_TRUE(e.ok()) << e.describe();
	ASSERT_EQ(bundle.pluginCount(), 1);
	OfxPlugin* plugin = bundle.plugin(0);
	ASSERT_NE(plugin, nullptr);
	EXPECT_STREQ(plugin->pluginIdentifier, "com.belisoful.ofxgrip.hostdump");
	MiniHost host(HostOptions::resolve21());
	bundle.setHost(host.ofxHost());
	ASSERT_EQ(host.load(plugin, OFXGRIP_HOSTDUMP_BUNDLE), kOfxStatOK);
	ASSERT_EQ(host.describe(), kOfxStatOK);
	EXPECT_EQ(host.describedContexts().size(), 3u);
	const ParamSet* params = host.contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	EXPECT_NE(params->find("probeDouble"), nullptr);
	EXPECT_NE(params->find("probeStatus"), nullptr);
	Project project;
	project.width = 8;
	project.height = 4;
	std::unique_ptr<Instance> instance = host.createInstance(kOfxImageEffectContextFilter, project, "Color");
	ASSERT_NE(instance, nullptr);
	instance->setInput("Source", makeRampImage(8, 4, BitDepth::UInt8));
	EXPECT_EQ(instance->instanceChanged("probeButton"), kOfxStatOK);
	EXPECT_EQ(stringValue(*instance, "probeStatus"), "dumped; see the OfxGrip log");
	RenderRequest request;
	request.depth = BitDepth::UInt8;
	RenderResult result = instance->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(result.image.bytes(), makeRampImage(8, 4, BitDepth::UInt8)->bytes());
	std::unique_ptr<Instance> generator = host.createInstance(kOfxImageEffectContextGenerator, project);
	ASSERT_NE(generator, nullptr);
	RenderResult gen = generator->render(request);
	ASSERT_TRUE(gen.ok());
	EXPECT_EQ(readComponent(gen.image.view().pixel(0, 0), BitDepth::UInt8, 0), 0.0f);
	generator.reset();
	instance.reset();
	EXPECT_EQ(host.unload(), kOfxStatOK);
}

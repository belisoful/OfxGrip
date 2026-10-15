// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Licensing through the adapter and the mini host: the framework parameters the manifest's
// declaration adds, the status toggle, the watermark on CPU, Metal, and OpenCL frames, the debug
// override rows, and the manifest's JSON form.
#include <ofxgrip/core/LicensingProviders.hpp>
#include <ofxgrip/core/ManifestJson.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/minihost/ParamImpl.hpp>
#include <ofxgrip/ofx/Descriptor.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#endif
#if defined(OFXGRIP_HAS_OPENCL)
#include <ofxgrip/gpu/OpenCLBackend.hpp>
#endif

#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

enum : ParameterId { kLevel = 1 };

EffectManifest licensedManifest(const char* status, bool debugMenu)
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.licensedtest";
	m.identity.uuid = "55555555-5555-4555-8555-555555555555";
	m.identity.name = "Licensed Test";
	m.identity.grouping = "OfxGrip Tests";
	m.version = Version{2, 0, 0};
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
	m.capabilities.supportsTiles = true;
#if defined(OFXGRIP_HAS_METAL)
	m.capabilities.metalRender = true;
#endif
#if defined(OFXGRIP_HAS_OPENCL)
	m.capabilities.openCLRender = true;
#endif
	m.parameters.push_back(ParameterSpec::floatParam(kLevel, "level", "Level", 0.5, 0, 1));
	m.licensing.provider = "static";
	m.licensing.settings["status"] = Value(status);
	m.licensing.licensedVersion = "*";
	if (debugMenu) {
		m.properties["debugMenu"] = Value(true);
	}
	return m;
}

/*! Fills the output with the level, on the CPU or by leaving a GPU frame as the host filled it. */
class LevelEffect final : public Effect {
public:
	Error render(RenderContext& context) override
	{
		if (context.outputIsGpu()) {
			return Error::none();
		}
		const float level = static_cast<float>(context.snapshot.number(kLevel, 0.5));
		const ImageView& out = context.output;
		const std::size_t components = componentCount(out.desc.components);
		for (int y = context.args.renderWindow.y1; y < context.args.renderWindow.y2; ++y) {
			for (int x = context.args.renderWindow.x1; x < context.args.renderWindow.x2; ++x) {
				std::uint8_t* pixel = out.pixel(x, y);
				if (pixel == nullptr) {
					continue;
				}
				for (std::size_t c = 0; c < components; ++c) {
					writeComponent(pixel, out.desc.depth, c, c == 3 ? 1.0f : level);
				}
			}
		}
		return Error::none();
	}
};

class LicensingEffect : public ::testing::Test {
protected:
	void SetUp() override { PluginRegistry::instance().clear(); }
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
	}
	void start(const char* status, HostOptions options = HostOptions::spec(), bool debugMenu = true)
	{
		index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<LevelEffect>>(licensedManifest(status, debugMenu)));
		ASSERT_GE(index_, 0);
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(host_->load(PluginRegistry::instance().plugin(index_), "/tmp/licensed.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 96;
		project.height = 48;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	/*! Pixels whose first colour component is not the level. */
	static int markedPixels(const ImageBuffer& image, BitDepth depth, float level = 0.5f)
	{
		int n = 0;
		const ImageView v = image.view();
		for (int y = v.desc.bounds.y1; y < v.desc.bounds.y2; ++y) {
			for (int x = v.desc.bounds.x1; x < v.desc.bounds.x2; ++x) {
				n += std::abs(readComponent(v.pixel(x, y), depth, 0) - level) > 0.02f ? 1 : 0;
			}
		}
		return n;
	}
	static void expectClose(const ImageBuffer& a, const ImageBuffer& b, BitDepth depth, float tolerance)
	{
		const ImageView va = a.view();
		const ImageView vb = b.view();
		ASSERT_EQ(va.desc.bounds, vb.desc.bounds);
		float worst = 0.0f;
		for (int y = va.desc.bounds.y1; y < va.desc.bounds.y2; ++y) {
			for (int x = va.desc.bounds.x1; x < va.desc.bounds.x2; ++x) {
				for (std::size_t c = 0; c < 4; ++c) {
					worst = std::max(worst, std::abs(readComponent(va.pixel(x, y), depth, c) - readComponent(vb.pixel(x, y), depth, c)));
				}
			}
		}
		EXPECT_LE(worst, tolerance) << toString(depth);
	}
	static std::vector<std::string> options(Param* param) { return param == nullptr ? std::vector<std::string>{} : param->props().getStrings(kOfxParamPropChoiceOption); }
	static double current(Param* param)
	{
		const std::vector<double> value = param == nullptr ? std::vector<double>{} : param->numericValueAt(0);
		return value.empty() ? 0.0 : value[0];
	}
	void pickDebugRow(std::string_view label)
	{
		Param* debug = instance_->param(kDebugMenuParameterKey);
		ASSERT_NE(debug, nullptr);
		const std::vector<std::string> rows = options(debug);
		int index = -1;
		for (std::size_t i = 0; i < rows.size(); ++i) {
			index = rows[i].find(label) != std::string::npos ? static_cast<int>(i) : index;
		}
		ASSERT_GE(index, 0) << label;
		instance_->setParam(kDebugMenuParameterKey, {static_cast<double>(index)});
		ASSERT_EQ(instance_->instanceChanged(kDebugMenuParameterKey), kOfxStatOK);
	}
	bool licensedToggle() { return current(instance_->param(kLicenseStatusParameterKey)) != 0.0; }
	int index_{-1};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(LicensingEffect, DeclaresTheParametersAndMirrorsTheStatus)
{
	start("licensed");
	const std::vector<std::string> names = instance_->paramNames();
	EXPECT_NE(std::find(names.begin(), names.end(), kLicenseStatusParameterKey), names.end());
	EXPECT_NE(std::find(names.begin(), names.end(), kLicensingMenuParameterKey), names.end());
	EXPECT_NE(std::find(names.begin(), names.end(), kDebugMenuParameterKey), names.end());
	EXPECT_EQ(options(instance_->param(kLicensingMenuParameterKey)).size(), 7u);
	EXPECT_EQ(options(instance_->param(kDebugMenuParameterKey)).size(), 7u);
	EXPECT_TRUE(licensedToggle());
	auto* effect = dynamic_cast<LevelEffect*>(static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData)));
	ASSERT_NE(effect, nullptr);
	ASSERT_NE(effect->licensingExtension(), nullptr);
	EXPECT_EQ(effect->licensingExtension()->status(), LicenseStatus::Licensed);
	EXPECT_EQ(effect->licensingExtension()->productId(), "55555555-5555-4555-8555-555555555555");
	EXPECT_EQ(effect->licensingExtension()->provider()->name(), "static");
	// The factory shares one provider across instances.
	Project project;
	std::unique_ptr<Instance> second = host_->createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(second, nullptr);
	auto* other = dynamic_cast<LevelEffect*>(static_cast<Effect*>(second->props().getPointer(kOfxPropInstanceData)));
	ASSERT_NE(other, nullptr);
	EXPECT_EQ(other->licensingExtension()->provider(), effect->licensingExtension()->provider());
	// The licensing menu's refresh row reports the status and snaps back.
	instance_->setParam(kLicensingMenuParameterKey, {static_cast<double>(options(instance_->param(kLicensingMenuParameterKey)).size() - 1)});
	ASSERT_EQ(instance_->instanceChanged(kLicensingMenuParameterKey), kOfxStatOK);
	EXPECT_EQ(current(instance_->param(kLicensingMenuParameterKey)), 0.0);
	bool reported = false;
	for (const std::string& message : instance_->messages()) {
		reported = reported || message.find("License status") != std::string::npos;
	}
	EXPECT_TRUE(reported);
	// A render under a license carries no mark and the snapshot says Licensed.
	RenderRequest request;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(markedPixels(result.image, BitDepth::Float), 0);
}

TEST_F(LicensingEffect, UnlicensedRendersCarryTheMarkOnEveryDepthAndTile)
{
	start("unlicensed");
	EXPECT_FALSE(licensedToggle());
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		RenderRequest request;
		request.depth = depth;
		RenderResult whole = instance_->render(request);
		ASSERT_TRUE(whole.ok()) << toString(depth);
		const int marked = markedPixels(whole.image, depth);
		EXPECT_GT(marked, 20) << toString(depth);
		EXPECT_LT(marked, 96 * 48 / 2) << toString(depth);
		// Alpha stays.
		EXPECT_NEAR(readComponent(whole.image.view().pixel(48, 24), depth, 3), 1.0f, 0.01f);
		// Tiles of the same frame line up with the whole render.
		RenderRequest tiled = request;
		tiled.tileSize = 17;
		tiled.threads = 3;
		RenderResult tiles = instance_->render(tiled);
		ASSERT_TRUE(tiles.ok()) << toString(depth);
		EXPECT_GT(tiles.tileCount, 1);
		expectClose(whole.image, tiles.image, depth, 0.0f);
	}
	// The debug rows force a status and clear it again.
	pickDebugRow("force Licensed");
	EXPECT_TRUE(licensedToggle());
	RenderRequest request;
	RenderResult clean = instance_->render(request);
	ASSERT_TRUE(clean.ok());
	EXPECT_EQ(markedPixels(clean.image, BitDepth::Float), 0);
	EXPECT_EQ(current(instance_->param(kDebugMenuParameterKey)), 0.0);
	pickDebugRow("force Trial");
	EXPECT_TRUE(licensedToggle());
	RenderResult trial = instance_->render(request);
	ASSERT_TRUE(trial.ok());
	EXPECT_EQ(markedPixels(trial.image, BitDepth::Float), 0);  // no trial mark by default
	pickDebugRow("force Unlicensed");
	EXPECT_FALSE(licensedToggle());
	pickDebugRow("clear override");
	EXPECT_FALSE(licensedToggle());
	RenderResult again = instance_->render(request);
	ASSERT_TRUE(again.ok());
	EXPECT_GT(markedPixels(again.image, BitDepth::Float), 20);
	// An edit to the toggle itself is reverted.
	instance_->setParam(kLicenseStatusParameterKey, {1.0});
	ASSERT_EQ(instance_->instanceChanged(kLicenseStatusParameterKey), kOfxStatOK);
	EXPECT_FALSE(licensedToggle());
}

TEST_F(LicensingEffect, ReleaseBuildsOfferNoDebugRows)
{
	start("unlicensed", HostOptions::spec(), false);
	EXPECT_EQ(instance_->param(kDebugMenuParameterKey), nullptr);
	EXPECT_FALSE(licensedToggle());
	auto* effect = dynamic_cast<LevelEffect*>(static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData)));
	ASSERT_NE(effect, nullptr);
	ASSERT_NE(effect->licensingExtension(), nullptr);
	EXPECT_FALSE(effect->licensingExtension()->isLicensed());
}

#if defined(OFXGRIP_HAS_METAL)
TEST_F(LicensingEffect, MetalFramesCarryTheSameMarkAsCpuFrames)
{
	if (!metal::MetalContext::available()) {
		GTEST_SKIP() << "No Metal device";
	}
	HostOptions options = HostOptions::spec();
	options.metalRenderSupported = true;
	start("unlicensed", options);
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		RenderRequest cpu;
		cpu.depth = depth;
		RenderResult a = instance_->render(cpu);
		ASSERT_TRUE(a.ok()) << toString(depth);
		EXPECT_GT(markedPixels(a.image, depth), 20);
		RenderRequest gpu = cpu;
		gpu.gpu = GpuApi::Metal;
		gpu.tileSize = 23;
		RenderResult b = instance_->render(gpu);
		ASSERT_TRUE(b.ok()) << toString(depth);
		ASSERT_EQ(b.gpu, GpuApi::Metal);
		// The host fills a GPU output with zeros, so the unmarked pixels differ; the marked
		// pixels agree once the level is blended in on both sides.
		const ImageView va = a.image.view();
		const ImageView vb = b.image.view();
		int agreed = 0;
		for (int y = 0; y < 48; ++y) {
			for (int x = 0; x < 96; ++x) {
				const float cpuRed = readComponent(va.pixel(x, y), depth, 0);
				const float gpuRed = readComponent(vb.pixel(x, y), depth, 0);
				if (std::abs(cpuRed - 0.5f) <= 0.02f) {
					EXPECT_NEAR(gpuRed, 0.0f, 0.02f) << toString(depth) << " at " << x << "," << y;
					continue;
				}
				// cpu = 0.5 (1 − alpha) + alpha; gpu = alpha. Solve for alpha from the CPU value.
				const float alpha = (cpuRed - 0.5f) / 0.5f;
				EXPECT_NEAR(gpuRed, alpha, 0.03f) << toString(depth) << " at " << x << "," << y;
				++agreed;
			}
		}
		EXPECT_GT(agreed, 20) << toString(depth);
	}
}
#endif

#if defined(OFXGRIP_HAS_OPENCL)
TEST_F(LicensingEffect, OpenCLFramesCarryTheMark)
{
	if (!opencl::OpenCLContext::available()) {
		GTEST_SKIP() << "No OpenCL device";
	}
	HostOptions options = HostOptions::spec();
	options.openCLRenderSupported = true;
	start("unlicensed", options);
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		RenderRequest gpu;
		gpu.depth = depth;
		gpu.gpu = GpuApi::OpenCL;
		RenderResult b = instance_->render(gpu);
		ASSERT_TRUE(b.ok()) << toString(depth);
		ASSERT_EQ(b.gpu, GpuApi::OpenCL);
		EXPECT_GT(markedPixels(b.image, depth, 0.0f), 20) << toString(depth);
		EXPECT_NEAR(readComponent(b.image.view().pixel(48, 24), depth, 3), 0.0f, 0.01f);  // alpha untouched
	}
}
#endif

TEST(LicensingManifest, JsonRoundTripsTheDeclaration)
{
	EffectManifest m = licensedManifest("trial", true);
	m.licensing.productId = "P";
	m.licensing.productVersion = "2.0.0";
	m.licensing.releaseDate = "2026-10-15";
	m.licensing.watermarkTrial = true;
	WatermarkConfiguration trial = WatermarkConfiguration::trial("Try me");
	trial.opacity = 0.25;
	m.licensing.trialWatermark = trial;
	WatermarkConfiguration unlicensed = WatermarkConfiguration::unlicensed("Buy me");
	unlicensed.style = WatermarkStyle::DiagonalTiled;
	unlicensed.color = {1, 0, 0, 1};
	m.licensing.unlicensedWatermark = unlicensed;
	m.licensing.showBuyButton = false;
	ASSERT_TRUE(m.validate().ok());
	const std::string json = writeManifestJson(m);
	EXPECT_NE(json.find("\"licensing\""), std::string::npos);
	EffectManifest back;
	ASSERT_TRUE(parseManifestJson(json, back).ok()) << parseManifestJson(json, back).describe();
	EXPECT_EQ(back.licensing, m.licensing);
	EXPECT_EQ(back.licensing.trialWatermark->text, "Try me");
	EXPECT_EQ(back.licensing.unlicensedWatermark->style, WatermarkStyle::DiagonalTiled);
	// Validation catches bad versions and opacities; the reader catches bad shapes.
	m.licensing.productVersion = "two";
	EXPECT_EQ(m.validate().code(), ErrorCode::InvalidManifest);
	m.licensing.productVersion.clear();
	m.licensing.licensedVersion = "x";
	EXPECT_EQ(m.validate().code(), ErrorCode::InvalidManifest);
	m.licensing.licensedVersion = "*";
	m.licensing.trialWatermark->opacity = -1;
	EXPECT_EQ(m.validate().code(), ErrorCode::InvalidManifest);
	m.licensing.trialWatermark->opacity = 1;
	m.licensing.unlicensedWatermark->opacity = -1;
	EXPECT_EQ(m.validate().code(), ErrorCode::InvalidManifest);
	EXPECT_FALSE(parseManifestJson(R"({"schemaVersion":1,"effect":{"ofxId":"a","name":"A"},"licensing":[]})", back).ok());
	EXPECT_FALSE(parseManifestJson(R"({"schemaVersion":1,"effect":{"ofxId":"a","name":"A"},"licensing":{"provider":"static","trialWatermark":3}})", back).ok());
	EXPECT_FALSE(parseManifestJson(R"({"schemaVersion":1,"effect":{"ofxId":"a","name":"A"},"licensing":{"provider":"static","trialWatermark":{"style":"spiral"}}})", back).ok());
	EXPECT_FALSE(parseManifestJson(R"({"schemaVersion":1,"effect":{"ofxId":"a","name":"A"},"licensing":{"provider":"static","settings":[]}})", back).ok());
	ASSERT_TRUE(parseManifestJson(R"({"schemaVersion":1,"effect":{"ofxId":"a","name":"A"},"licensing":{"provider":"static","settings":{"status":"trial"}}})", back).ok());
	EXPECT_EQ(back.licensing.settings.at("status"), Value("trial"));
	// A manifest without licensing writes no block and loads no extension.
	EffectManifest plain = licensedManifest("licensed", false);
	plain.licensing = LicensingSpec{};
	EXPECT_EQ(writeManifestJson(plain).find("\"licensing\""), std::string::npos);
	EXPECT_TRUE(frameworkParameters(plain).size() == 1u);
	EXPECT_EQ(frameworkParameters(licensedManifest("licensed", false)).size(), 3u);
}

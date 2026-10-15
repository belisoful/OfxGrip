// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Phase 2 through the adapter and the mini host: control lowering, the presets control, the
// About and Debug menus, the analysis pass, describe-time localization, and the trackers.
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/Descriptor.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxParam.h"

#include "../TestEnvironment.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace ofxgrip;
using namespace ofxgrip::minihost;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kSection = 500, kBanner = 501, kDivider = 502, kStatus = 503, kProgress = 504, kSwitch = 505, kPresets = 506, kAnalyze = 507, kAmount = 508, kCapsule = 509, kHidden = 510, kMode = 511 };

std::filesystem::path testBundle()
{
	static std::filesystem::path dir;
	if (!dir.empty()) {
		return dir;
	}
	dir = std::filesystem::temp_directory_path() / "ofxgrip-phase2" / "Phase2.ofx.bundle";
	std::error_code ec;
	std::filesystem::remove_all(dir.parent_path(), ec);
	std::filesystem::create_directories(dir / "Contents" / "Resources" / "de.lproj", ec);
	std::ofstream(dir / "Contents" / "Resources" / "de.lproj" / "Localizable.strings") << "\"Amount\" = \"Menge\";\n\"Soft\" = \"Weich\";\n\"Status\" = \"Zustand\";\n";
	std::filesystem::create_directories(dir / "Contents" / "Resources" / "Presets" / "look", ec);
	Preset bundled;
	bundled.name = "Bundled";
	bundled.tag = "look";
	bundled.pluginUuid = "22222222-2222-4222-8222-222222222222";
	bundled.values[kAmount] = Value(0.25);
	bundled.save(dir / "Contents" / "Resources" / "Presets" / "look" / "Bundled.fxpreset");
	return dir;
}

EffectManifest phase2Manifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.phase2";
	m.identity.uuid = "22222222-2222-4222-8222-222222222222";
	m.identity.name = "Phase Two";
	m.identity.grouping = "OfxGrip Tests";
	m.identity.author = "Belisoful";
	m.version = Version{1, 0, 0};
	m.properties["debugMenu"] = Value(true);
	m.properties["internationalize"] = Value(true);
	m.properties["trackInstances"] = Value(true);
	m.properties["regression"] = Value(true);
	ParameterSpec section;
	section.id = kSection;
	section.key = "section";
	section.type = ParameterType::Section;
	section.label = "Look";
	m.parameters.push_back(section);
	ParameterSpec banner;
	banner.id = kBanner;
	banner.key = "banner";
	banner.type = ParameterType::Banner;
	banner.label = "Banner";
	banner.meta["title"] = Value("Rendering");
	m.parameters.push_back(banner);
	ParameterSpec divider;
	divider.id = kDivider;
	divider.key = "divider";
	divider.type = ParameterType::Divider;
	m.parameters.push_back(divider);
	ParameterSpec status;
	status.id = kStatus;
	status.key = "status";
	status.type = ParameterType::Status;
	status.label = "Status";
	status.defaultValue = Value("Ready");
	m.parameters.push_back(status);
	ParameterSpec capsule;
	capsule.id = kCapsule;
	capsule.key = "capsule";
	capsule.type = ParameterType::Capsule;
	capsule.label = "Badge";
	m.parameters.push_back(capsule);
	ParameterSpec progress;
	progress.id = kProgress;
	progress.key = "progress";
	progress.type = ParameterType::Progress;
	progress.label = "Progress";
	progress.defaultValue = Value(0.0);
	m.parameters.push_back(progress);
	ParameterSpec sw;
	sw.id = kSwitch;
	sw.key = "switch";
	sw.type = ParameterType::Switch;
	sw.label = "Switch";
	sw.defaultValue = Value(true);
	m.parameters.push_back(sw);
	m.parameters.push_back(ParameterSpec::floatParam(kAmount, "amount", "Amount", 0.5, 0, 1).withTags({"look"}).withHint("How much."));
	m.parameters.push_back(ParameterSpec::menuParam(kMode, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}, 0).withTags({"look"}).withHint("Which."));
	m.parameters.push_back(ParameterSpec::floatParam(kHidden, "hidden", "Hidden", 0.5, 0, 1).withFlags({}, AppFlags(AppFlag::InDebugMode)).withHint("Debug only."));
	ParameterSpec presets = ParameterSpec::menuParam(kPresets, "presets", "Presets", {}, 0);
	presets.type = ParameterType::Presets;
	presets.tags = {"look"};
	m.parameters.push_back(presets);
	ParameterSpec analyze;
	analyze.id = kAnalyze;
	analyze.key = "analyze";
	analyze.type = ParameterType::Analyzer;
	analyze.label = "Analyze";
	m.parameters.push_back(analyze);
	ManifestPreset warm;
	warm.name = "Warm";
	warm.tag = "look";
	warm.values["amount"] = Value(0.9);
	warm.values["mode"] = Value("hard");
	m.presets.push_back(warm);
	AboutEntry link;
	link.label = "Website";
	link.url = "https://example.invalid/ofxgrip";
	m.about.entries.push_back(link);
	AboutEntry dialog;
	dialog.kind = AboutEntry::Kind::Dialog;
	dialog.label = "Credits";
	dialog.dialogText = "Made with OfxGrip";
	m.about.entries.push_back(dialog);
	return m;
}

class Phase2Effect final : public Effect {
public:
	static inline int analyzed = 0;
	Error onAnalyzeFrame(const ImageView& frame, Time, long frameIndex, Bytes& record, bool& store) override
	{
		++analyzed;
		if (frameIndex == 2) {
			store = false;  // one frame stores nothing
			return Error::none();
		}
		// The record is the mean of the first component over the frame, as a byte.
		double sum = 0;
		int count = 0;
		for (int y = frame.desc.bounds.y1; y < frame.desc.bounds.y2; ++y) {
			for (int x = frame.desc.bounds.x1; x < frame.desc.bounds.x2; ++x) {
				sum += readComponent(frame.pixel(x, y), frame.desc.depth, 0);
				++count;
			}
		}
		record = Bytes{static_cast<std::uint8_t>(sum / count * 255.0)};
		store = true;
		return Error::none();
	}
	Error render(RenderContext&) override { return Error::none(); }
};

class Phase2 : public ::testing::Test {
protected:
	void SetUp() override
	{
		Phase2Effect::analyzed = 0;
		PluginRegistry::instance().clear();
		index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<Phase2Effect>>(phase2Manifest()));
		ASSERT_GE(index_, 0);
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
	}
	void start(HostOptions options = HostOptions::spec(), const std::string& bundle = testBundle().string())
	{
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(host_->load(PluginRegistry::instance().plugin(index_), bundle), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 8;
		project.height = 4;
		project.lastFrame = 3;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	Effect* effect() { return static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData)); }
	int index_{-1};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(Phase2, LowersTheControlVocabularyToStandardParameters)
{
	unsetTestEnv("OFXGRIP_LANGUAGE");
	setTestEnv("LANG", "C");
	start();
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	Param* section = params->find("section");
	ASSERT_NE(section, nullptr);
	EXPECT_EQ(section->type(), kOfxParamTypeGroup);
	EXPECT_EQ(section->props().getInt(kOfxParamPropGroupOpen, -1), 1);
	Param* banner = params->find("banner");
	ASSERT_NE(banner, nullptr);
	EXPECT_EQ(banner->type(), kOfxParamTypeString);
	EXPECT_EQ(banner->props().getString(kOfxParamPropStringMode), kOfxParamStringIsLabel);
	EXPECT_EQ(banner->props().getString(kOfxParamPropDefault), "Rendering");
	EXPECT_EQ(banner->props().getInt(kOfxParamPropEnabled, -1), 0);
	Param* divider = params->find("divider");
	ASSERT_NE(divider, nullptr);
	EXPECT_EQ(divider->props().getString(kOfxParamPropStringMode), kOfxParamStringIsLabel);
	EXPECT_FALSE(divider->props().getString(kOfxParamPropDefault).empty());
	Param* status = params->find("status");
	ASSERT_NE(status, nullptr);
	EXPECT_EQ(status->props().getString(kOfxParamPropStringMode), kOfxParamStringIsSingleLine);
	EXPECT_EQ(status->props().getInt(kOfxParamPropEnabled, -1), 0);
	EXPECT_EQ(status->props().getString(kOfxParamPropDefault), "Ready");
	EXPECT_EQ(params->find("capsule")->props().getInt(kOfxParamPropEnabled, -1), 0);
	Param* progress = params->find("progress");
	ASSERT_NE(progress, nullptr);
	EXPECT_EQ(progress->type(), kOfxParamTypeDouble);
	EXPECT_EQ(progress->props().getInt(kOfxParamPropEnabled, -1), 0);
	EXPECT_EQ(params->find("switch")->type(), kOfxParamTypeBoolean);
	EXPECT_EQ(params->find("hidden")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(params->find("analyze")->type(), kOfxParamTypePushButton);
	Param* presets = params->find("presets");
	ASSERT_NE(presets, nullptr);
	EXPECT_EQ(presets->type(), kOfxParamTypeChoice);
	const std::vector<std::string> options = presets->props().getStrings(kOfxParamPropChoiceOption);
	// Default, sep, Warm (manifest), Bundled (bundle), sep, Reveal, Save
	ASSERT_EQ(options.size(), 7u);
	EXPECT_EQ(options[2], "Warm");
	EXPECT_EQ(options[3], "Bundled");
	EXPECT_EQ(options[5], "Reveal User Presets...");
	Param* about = params->find(kAboutMenuParameterKey);
	ASSERT_NE(about, nullptr);
	EXPECT_EQ(about->props().getStrings(kOfxParamPropChoiceOption).size(), 3u);
	Param* debug = params->find(kDebugMenuParameterKey);
	ASSERT_NE(debug, nullptr);
	EXPECT_EQ(debug->props().getStrings(kOfxParamPropChoiceOption).size(), 3u);
	// On a host whose label strings do not persist, banners lower to disabled single-line fields.
	instance_.reset();
	host_.reset();
	HostOptions nuke = HostOptions::nuke();
	start(nuke);
	EXPECT_EQ(host_->contextParams(kOfxImageEffectContextFilter)->find("banner")->props().getString(kOfxParamPropStringMode), kOfxParamStringIsSingleLine);
}

TEST_F(Phase2, PresetsControlAppliesManifestAndBundlePresets)
{
	setTestEnv("LANG", "C");
	start();
	Effect& e = *effect();
	ASSERT_NE(e.presetsExtension(), nullptr);
	EXPECT_EQ(e.presets().presetsForTag("look").size(), 2u);
	instance_->setParam("presets", {2.0});
	ASSERT_EQ(instance_->instanceChanged("presets"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "amount")[0], 0.9);
	EXPECT_EQ(numericValue(*instance_, "mode")[0], 1.0);
	EXPECT_EQ(e.presetsExtension()->selectedName(kPresets), "Warm");
	instance_->setParam("presets", {3.0});
	ASSERT_EQ(instance_->instanceChanged("presets"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "amount")[0], 0.25);
	EXPECT_EQ(e.presetsExtension()->selectedName(kPresets), "Bundled");
	// Saving writes into the test support folder and lists the new preset.
	instance_->setParam("amount", {0.6});
	instance_->setParam("presets", {6.0});
	ASSERT_EQ(instance_->instanceChanged("presets"), kOfxStatOK);
	const std::vector<Preset> user = e.presets().userPresets("look");
	ASSERT_EQ(user.size(), 1u);
	EXPECT_EQ(user[0].values.at(kAmount), Value(0.6));
	EXPECT_EQ(user[0].pluginAuthor, "Belisoful");
	// Save snaps the menu back to the last real selection.
	EXPECT_EQ(numericValue(*instance_, "presets")[0], 3.0);
	EXPECT_EQ(instance_->param("presets")->props().getStrings(kOfxParamPropChoiceOption).size(), 9u);
	std::error_code ec;
	std::filesystem::remove(user[0].path, ec);
	// Capture through the public service carries the preset identity.
	const Preset captured = e.presets().capture("Now", "look");
	EXPECT_EQ(captured.pluginUuid, "22222222-2222-4222-8222-222222222222");
	EXPECT_EQ(captured.values.at(kAmount), Value(0.6));
	EXPECT_EQ(captured.values.count(kPresets), 0u);
}

TEST_F(Phase2, MenusAndTrackersRunThroughTheHost)
{
	setTestEnv("LANG", "C");
	start();
	Effect& e = *effect();
	ASSERT_NE(e.aboutExtension(), nullptr);
	ASSERT_NE(e.debugExtension(), nullptr);
	ASSERT_NE(e.instanceTracker(), nullptr);
	ASSERT_NE(e.regressionExtension(), nullptr);
	EXPECT_EQ(e.instanceTracker()->siblingCount(), 1u);
	EXPECT_FALSE(e.regressionExtension()->reports().empty());  // the controls lack hints
	// The dialog row shows a message through the host and snaps back.
	instance_->setParam(kAboutMenuParameterKey, {2.0});
	ASSERT_EQ(instance_->instanceChanged(kAboutMenuParameterKey), kOfxStatOK);
	ASSERT_EQ(instance_->messages().size(), 1u);
	EXPECT_NE(instance_->messages()[0].find("Made with OfxGrip"), std::string::npos);
	EXPECT_EQ(numericValue(*instance_, kAboutMenuParameterKey)[0], 0.0);
	// The debug menu reveals the debug-only parameter and hides it again.
	EXPECT_EQ(instance_->param("hidden")->props().getInt(kOfxParamPropSecret, -1), 1);
	instance_->setParam(kDebugMenuParameterKey, {1.0});
	ASSERT_EQ(instance_->instanceChanged(kDebugMenuParameterKey), kOfxStatOK);
	EXPECT_EQ(instance_->param("hidden")->props().getInt(kOfxParamPropSecret, -1), 0);
	EXPECT_TRUE(e.debugExtension()->debugMode());
	instance_->setParam(kDebugMenuParameterKey, {2.0});
	ASSERT_EQ(instance_->instanceChanged(kDebugMenuParameterKey), kOfxStatOK);
	EXPECT_EQ(instance_->param("hidden")->props().getInt(kOfxParamPropSecret, -1), 1);
	// A second instance is a sibling; destroying it leaves one.
	Project project;
	std::unique_ptr<Instance> second = host_->createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(e.instanceTracker()->siblingCount(), 2u);
	second.reset();
	EXPECT_EQ(e.instanceTracker()->siblingCount(), 1u);
}

TEST_F(Phase2, DebugMenuIsAbsentWhenTheFactoryForbidsIt)
{
	class LockedFactory final : public EffectFactory {
	public:
		explicit LockedFactory(EffectManifest m) : manifest_(std::move(m)) {}
		const EffectManifest& manifest() const override { return manifest_; }
		std::unique_ptr<Effect> create() override { return std::make_unique<Phase2Effect>(); }
		bool allowsDebugFeatures() const override { return false; }

	private:
		EffectManifest manifest_;
	};
	PluginRegistry::instance().clear();
	index_ = PluginRegistry::instance().registerFactory(std::make_unique<LockedFactory>(phase2Manifest()));
	ASSERT_GE(index_, 0);
	setTestEnv("LANG", "C");
	start();
	EXPECT_EQ(host_->contextParams(kOfxImageEffectContextFilter)->find(kDebugMenuParameterKey), nullptr);
	EXPECT_EQ(effect()->debugExtension(), nullptr);
	EXPECT_NE(effect()->aboutExtension(), nullptr);
}

TEST_F(Phase2, AnalysisPassStoresRecordsPerFrame)
{
	setTestEnv("LANG", "C");
	start();
	Effect& e = *effect();
	ASSERT_NE(e.analysisExtension(), nullptr);
	ASSERT_NE(e.analysis(), nullptr);
	EXPECT_FALSE(e.analysisRecordAt(0).has_value());
	instance_->setInput("Source", makeRampImage(8, 4, BitDepth::Float));
	ASSERT_EQ(instance_->instanceChanged("analyze"), kOfxStatOK);
	EXPECT_EQ(Phase2Effect::analyzed, 4);
	EXPECT_EQ(e.analysis()->size(), 3u);
	ASSERT_TRUE(e.analysisRecordAt(0).has_value());
	ASSERT_TRUE(e.analysisRecordAt(2).has_value());  // frame 2 stored nothing: frame 1's record serves
	EXPECT_EQ(*e.analysisRecordAt(2), *e.analysisRecordAt(1));
	EXPECT_GT((*e.analysisRecordAt(3))[0], 0);
	ASSERT_FALSE(instance_->progressUpdates().empty());
	EXPECT_DOUBLE_EQ(instance_->progressUpdates().back(), 1.0);
	// The store travels in the state parameter.
	ASSERT_EQ(instance_->syncPrivateData(), kOfxStatOK);
	Bytes blob;
	ASSERT_TRUE(e.services().persistence().load(AnalysisExtension::kPersistenceKey, blob).ok());
	FrameData stored;
	ASSERT_TRUE(FrameData::decode(blob, {}, stored).ok());
	EXPECT_EQ(stored.size(), 3u);
	// A request over a range, backward, with a step, and a missing clip.
	AnalysisRequest request;
	request.first = 1;
	request.last = 3;
	request.step = 2;
	request.backward = true;
	AnalysisResult result = e.runAnalysis(request);
	EXPECT_TRUE(result.error.ok()) << result.error.describe();
	EXPECT_EQ(result.framesAnalyzed, 2);
	EXPECT_EQ(e.analysis()->frames(), (std::vector<long>{1, 3}));
	request.clip = "Nope";
	EXPECT_EQ(e.runAnalysis(request).error.code(), ErrorCode::NotFound);
	request.clip = "Source";
	request.first = 5;
	request.last = 2;
	request.step = 0;
	EXPECT_EQ(e.runAnalysis(request).error.code(), ErrorCode::Value);
	// A disconnected source stops the pass with a host-call error.
	instance_->disconnect("Source");
	AnalysisRequest whole;
	EXPECT_EQ(e.runAnalysis(whole).error.code(), ErrorCode::HostCall);
	// An abort request stops it cleanly.
	instance_->setInput("Source", makeRampImage(8, 4, BitDepth::Float));
	instance_->abortFlag = true;
	AnalysisResult aborted = e.runAnalysis(whole);
	EXPECT_TRUE(aborted.aborted);
	EXPECT_TRUE(aborted.error.ok());
	instance_->abortFlag = false;
}

TEST_F(Phase2, DescribeTimeLocalizationUsesTheBundleTable)
{
	setTestEnv("OFXGRIP_LANGUAGE", "de");
	start();
	unsetTestEnv("OFXGRIP_LANGUAGE");
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	EXPECT_EQ(params->find("amount")->props().getString(kOfxPropLabel), "Menge");
	EXPECT_EQ(params->find("mode")->props().getStrings(kOfxParamPropChoiceOption)[0], "Weich");
	EXPECT_EQ(params->find("status")->props().getString(kOfxPropLabel), "Zustand");
	Effect& e = *effect();
	ASSERT_NE(e.i18nExtension(), nullptr);
	ASSERT_NE(e.stringTable(), nullptr);
	// The runtime label read delocalizes, so effect code sees the key.
	EXPECT_EQ(e["amount"]->label(), "Amount");
	// An effect without the table in its bundle loads no extension.
	instance_.reset();
	host_.reset();
	setTestEnv("OFXGRIP_LANGUAGE", "de");
	start(HostOptions::spec(), "/tmp/no-such.ofx.bundle");
	unsetTestEnv("OFXGRIP_LANGUAGE");
	EXPECT_EQ(effect()->i18nExtension(), nullptr);
	EXPECT_EQ(host_->contextParams(kOfxImageEffectContextFilter)->find("amount")->props().getString(kOfxPropLabel), "Amount");
}

TEST(Phase2Analysis, RefusesWithoutAnAnalysisStore)
{
	class Plain final : public Effect {
	public:
		Error render(RenderContext&) override { return Error::none(); }
	};
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.plain2";
	m.identity.name = "Plain";
	PluginRegistry::instance().clear();
	const int index = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<Plain>>(m));
	ASSERT_GE(index, 0);
	{
		// The host outlives the instance and dies before the registry is cleared.
		MiniHost host;
		ASSERT_EQ(host.load(PluginRegistry::instance().plugin(index), "/tmp/plain2.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host.describe(), kOfxStatOK);
		Project project;
		std::unique_ptr<Instance> instance = host.createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance, nullptr);
		Effect* effect = static_cast<Effect*>(instance->props().getPointer(kOfxPropInstanceData));
		EXPECT_EQ(effect->analysis(), nullptr);
		EXPECT_EQ(effect->runAnalysis().error.code(), ErrorCode::Unsupported);
		EXPECT_FALSE(effect->analysisRecordAt(0).has_value());
		EXPECT_EQ(effect->stringTable(), nullptr);
		instance.reset();
		host.unload();
	}
	PluginRegistry::instance().clear();
}

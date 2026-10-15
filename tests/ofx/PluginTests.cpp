// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// In-process adapter tests: a test effect registered with the PluginRegistry is driven through
// the mini host, so every action, service, and quirk path runs without a bundle on disk.
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/Descriptor.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

enum : ParameterId { kGain = 100, kEnabled = 101, kMode = 102, kLabel = 103, kCenter = 104, kColor = 105, kButton = 106, kGroup = 200, kInner = 201, kNoState = 300 };

EffectManifest testManifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.test";
	m.identity.uuid = "TEST-UUID";
	m.identity.name = "Test <Effect>";
	m.identity.grouping = "OfxGrip Tests";
	m.identity.description = "A test effect";
	m.identity.kind = EffectKind::Filter;
	m.identity.extraContexts = {EffectKind::General};
	m.version = Version{1, 2, 0};
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1.0, 0.0, 10.0).withDisplayRange(0, 2));
	m.parameters.push_back(ParameterSpec::boolParam(kEnabled, "enabled", "Enabled", true));
	m.parameters.push_back(ParameterSpec::menuParam(kMode, "mode", "Mode", {{"a", "A"}, {"b", "B"}}, 1));
	m.parameters.push_back(ParameterSpec::stringParam(kLabel, "label", "Label", "hello"));
	m.parameters.push_back(ParameterSpec::pointParam(kCenter, "center", "Center", {3, 4}));
	m.parameters.push_back(ParameterSpec::rgbaParam(kColor, "color", "Color", {1, 0.5, 0.25, 1}));
	m.parameters.push_back(ParameterSpec::buttonParam(kButton, "button", "Button"));
	m.parameters.push_back(ParameterSpec::groupParam(kGroup, "group", "Group", {ParameterSpec::intParam(kInner, "inner", "Inner", 5, 0, 10)}).withFlags(HostFlags(HostFlag::Collapsed)));
	ParameterSpec unranged;
	unranged.id = kNoState;
	unranged.key = "unranged";
	unranged.type = ParameterType::Float;
	unranged.label = "Unranged";
	unranged.defaultValue = 0.5;
	unranged.appFlags = AppFlags(AppFlag::NoState);
	m.parameters.push_back(unranged);
	return m;
}

struct Trace {
	std::atomic<int> creates{0};
	std::atomic<int> changes{0};
	std::atomic<int> clicks{0};
	std::atomic<int> renders{0};
	std::atomic<int> snapshots{0};
	std::vector<EventId> events;
	std::mutex mutex;
	std::string lastPage;
	std::string lastChangedName;
	double lastGainAtRender{-1};
	std::vector<unsigned> threadIndices;
	bool sawDisconnectedSource{false};
	void reset()
	{
		creates = 0;
		changes = 0;
		clicks = 0;
		renders = 0;
		snapshots = 0;
		events.clear();
		lastPage.clear();
		lastChangedName.clear();
		lastGainAtRender = -1;
		threadIndices.clear();
		sawDisconnectedSource = false;
	}
};

Trace& trace()
{
	static Trace t;
	return t;
}

class Recorder final : public Extension {
public:
	std::string name() const override { return "Recorder"; }
	std::vector<EventId> interests() const override
	{
		return {EventId::CreateInstance, EventId::FinishSetup, EventId::AddedToDocument, EventId::InstanceChanged, EventId::ParameterClicked, EventId::Flush, EventId::BuildSnapshot, EventId::Render, EventId::DestroyInstance, EventId::ApiSetValuePre, EventId::ApiGetValue};
	}
	void handle(Event& e) override
	{
		std::lock_guard<std::mutex> lock(trace().mutex);
		trace().events.push_back(e.id);
		if (e.id == EventId::BuildSnapshot) {
			static_cast<SnapshotEvent&>(e).builder.setExtra("recorder", Bytes{42});
		}
		if (e.id == EventId::ApiSetValuePre) {
			auto& api = static_cast<ApiEvent&>(e);
			if (api.parameter == kLabel && api.value.is<std::string>() && api.value.as<std::string>() == "veto") {
				e.error = Error::make(ErrorCode::Value, "vetoed");
			}
		}
	}
};

class TestEffect final : public Effect {
public:
	void loadExtensions(ExtensionSystem& system) override { system.load(std::make_unique<Recorder>()); }
	Error onCreate() override
	{
		trace().creates++;
		std::lock_guard<std::mutex> lock(trace().mutex);
		trace().lastPage = ofxServices().resolvePage();
		return Error::none();
	}
	Error onInstanceChanged(const ChangeEvent& change) override
	{
		trace().changes++;
		std::lock_guard<std::mutex> lock(trace().mutex);
		trace().lastChangedName = change.name;
		return Error::none();
	}
	Error onParameterClicked(ParameterId id) override
	{
		if (id == kButton) {
			trace().clicks++;
			services().persistence().store("clicks", Bytes{static_cast<std::uint8_t>(trace().clicks.load())});
			services().feedback().message(MessageKind::Message, "clicked", "button clicked");
		}
		return Error::none();
	}
	Error onBuildSnapshot(RenderSnapshotBuilder& builder, const RenderInfo&) override
	{
		trace().snapshots++;
		builder.setExtra("effect", Bytes{7});
		return Error::none();
	}
	Error onIsIdentity(const IsIdentityArgs&, const RenderSnapshot& snapshot, std::string& clip, Time&, bool& identity) override
	{
		identity = !snapshot.boolean(kEnabled, true);
		clip = "Source";
		return Error::none();
	}
	Error render(RenderContext& context) override
	{
		trace().renders++;
		{
			std::lock_guard<std::mutex> lock(trace().mutex);
			trace().lastGainAtRender = context.snapshot.number(kGain);
			if (const InputImage* src = context.source(); src != nullptr && !src->connected) {
				trace().sawDisconnectedSource = true;
			}
		}
		const float gain = static_cast<float>(context.snapshot.number(kGain, 1.0));
		const InputImage* source = context.source();
		const ImageView& out = context.output;
		const std::size_t n = componentCount(out.desc.components);
		return context.forEachBand([&](const RectI& band) {
			for (int y = band.y1; y < band.y2; ++y) {
				for (int x = band.x1; x < band.x2; ++x) {
					std::uint8_t* dst = out.pixel(x, y);
					const std::uint8_t* src = source != nullptr && source->connected ? source->view.pixel(x, y) : nullptr;
					for (std::size_t c = 0; c < n; ++c) {
						const float v = src != nullptr ? readComponent(src, source->view.desc.depth, c) * gain : 0.25f;
						writeComponent(dst, out.desc.depth, c, v);
					}
				}
			}
		});
	}
};

class Fixture : public ::testing::Test {
protected:
	void SetUp() override
	{
		trace().reset();
		PluginRegistry::instance().clear();
		index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<TestEffect>>(testManifest()));
		ASSERT_GE(index_, 0);
		plugin_ = PluginRegistry::instance().plugin(index_);
		ASSERT_NE(plugin_, nullptr);
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
	}
	void start(HostOptions options = HostOptions::spec(), const std::string& page = {})
	{
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(host_->load(plugin_, "/tmp/test.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 16;
		project.height = 8;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project, page);
		ASSERT_NE(instance_, nullptr);
	}
	int index_{-1};
	OfxPlugin* plugin_{nullptr};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(Fixture, RegistryFillsThePluginRecord)
{
	EXPECT_STREQ(plugin_->pluginApi, kOfxImageEffectPluginApi);
	EXPECT_STREQ(plugin_->pluginIdentifier, "com.belisoful.ofxgrip.test");
	EXPECT_EQ(plugin_->pluginVersionMajor, 1u);
	EXPECT_EQ(plugin_->pluginVersionMinor, 2u);
	EXPECT_NE(plugin_->mainEntry, nullptr);
	EXPECT_NE(plugin_->setHost, nullptr);
	EXPECT_EQ(PluginRegistry::instance().count(), 1);
	EXPECT_EQ(PluginRegistry::instance().plugin(5), nullptr);
	EXPECT_EQ(PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<TestEffect>>(testManifest())), -1);
	EXPECT_EQ(PluginRegistry::instance().registerFactory(nullptr), -1);
}

TEST_F(Fixture, DescribeLowersTheManifest)
{
	start();
	PropSet* props = host_->descriptorProps();
	ASSERT_NE(props, nullptr);
	EXPECT_EQ(props->getString(kOfxPropLabel), "Test  Effect ");
	EXPECT_EQ(props->getString(kOfxImageEffectPluginPropGrouping), "OfxGrip Tests");
	EXPECT_EQ(props->getStrings(kOfxImageEffectPropSupportedContexts).size(), 2u);
	EXPECT_EQ(props->getStrings(kOfxImageEffectPropSupportedPixelDepths).size(), 4u);
	EXPECT_EQ(props->getString(kOfxImageEffectPluginRenderThreadSafety), kOfxImageEffectRenderFullySafe);
	const std::vector<std::string> contexts = host_->describedContexts();
	ASSERT_EQ(contexts.size(), 2u);
	const ParamSet* params = host_->contextParams(kOfxImageEffectContextFilter);
	ASSERT_NE(params, nullptr);
	EXPECT_EQ(params->params().size(), 11u);  // the manifest's ten plus the hidden state parameter
	Param* gain = params->find("gain");
	ASSERT_NE(gain, nullptr);
	EXPECT_EQ(gain->type(), kOfxParamTypeDouble);
	EXPECT_EQ(gain->props().getDouble(kOfxParamPropMin), 0.0);
	EXPECT_EQ(gain->props().getDouble(kOfxParamPropMax), 10.0);
	EXPECT_EQ(gain->props().getDouble(kOfxParamPropDisplayMax), 2.0);
	EXPECT_EQ(gain->props().getInt(kOfxParamPropAnimates, -1), 1);
	Param* unranged = params->find("unranged");
	ASSERT_NE(unranged, nullptr);
	EXPECT_LT(unranged->props().getDouble(kOfxParamPropMin), -1000.0);
	EXPECT_EQ(unranged->props().getDouble(kOfxParamPropDisplayMin, -9), 0.0);
	EXPECT_EQ(unranged->props().getDouble(kOfxParamPropDisplayMax, -9), 1.0);
	Param* inner = params->find("inner");
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(inner->props().getString(kOfxParamPropParent), "group");
	EXPECT_EQ(params->find("group")->props().getInt(kOfxParamPropGroupOpen, -1), 0);
	EXPECT_EQ(params->find("enabled")->props().getInt(kOfxParamPropAnimates, -1), 1);
	EXPECT_EQ(params->find("label")->props().getInt(kOfxParamPropAnimates, -1), 0);
	EXPECT_EQ(params->find("mode")->props().getStrings(kOfxParamPropChoiceOption).size(), 2u);
	Param* state = params->find(kInstanceStateParameterKey);
	ASSERT_NE(state, nullptr);
	EXPECT_EQ(state->props().getInt(kOfxParamPropSecret, -1), 1);
	const std::vector<std::string> clips = host_->contextClips(kOfxImageEffectContextFilter);
	ASSERT_EQ(clips.size(), 2u);
	EXPECT_EQ(clips[0], "Source");
	EXPECT_EQ(clips[1], "Output");
}

TEST_F(Fixture, CreateInstanceRunsTheLifecycleEvents)
{
	start(HostOptions::spec(), "Fusion");
	EXPECT_EQ(trace().creates.load(), 1);
	EXPECT_EQ(trace().lastPage, "Fusion");
	std::lock_guard<std::mutex> lock(trace().mutex);
	const std::vector<EventId>& events = trace().events;
	ASSERT_GE(events.size(), 4u);
	EXPECT_EQ(events[0], EventId::CreateInstance);
	EXPECT_EQ(events[1], EventId::FinishSetup);
	EXPECT_EQ(events[2], EventId::AddedToDocument);
	EXPECT_EQ(events[3], EventId::Flush);
}

TEST_F(Fixture, InstanceChangedReachesTheEffectAndClicksButtons)
{
	start();
	instance_->setParam("gain", {2.0});
	EXPECT_EQ(instance_->instanceChanged("gain"), kOfxStatOK);
	EXPECT_EQ(trace().changes.load(), 1);
	EXPECT_EQ(trace().lastChangedName, "gain");
	EXPECT_EQ(instance_->instanceChanged("button"), kOfxStatOK);
	EXPECT_EQ(trace().clicks.load(), 1);
	ASSERT_EQ(instance_->messages().size(), 1u);
	EXPECT_NE(instance_->messages()[0].find("button clicked"), std::string::npos);
	// Q-RESOLVE-035: an unknown name is ignored.
	EXPECT_EQ(instance_->instanceChanged("ResolveUseAlphaForTrackCompositing"), kOfxStatOK);
	EXPECT_EQ(trace().changes.load(), 2);
	EXPECT_EQ(instance_->clipChanged("Source"), kOfxStatOK);
	EXPECT_EQ(trace().changes.load(), 3);
}

TEST_F(Fixture, PersistenceSurvivesSyncPrivateData)
{
	start();
	instance_->instanceChanged("button");
	EXPECT_EQ(instance_->syncPrivateData(), kOfxStatOK);
	const std::string stored = stringValue(*instance_, kInstanceStateParameterKey);
	EXPECT_EQ(stored.rfind("OGST1:", 0), 0u);
	EXPECT_GT(stored.size(), 6u);
	// A new instance created from the same host parameter store reads the blob back.
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	Bytes clicks;
	ASSERT_TRUE(effect->services().persistence().load("clicks", clicks).ok());
	ASSERT_EQ(clicks.size(), 1u);
	EXPECT_EQ(clicks[0], 1);
	// The framework keeps its version record and meta store beside the effect's own blob.
	const std::vector<std::string> keys = effect->services().persistence().keys();
	EXPECT_NE(std::find(keys.begin(), keys.end(), "clicks"), keys.end());
	EXPECT_NE(std::find(keys.begin(), keys.end(), std::string(VersioningExtension::kPersistenceKey)), keys.end());
	EXPECT_NE(std::find(keys.begin(), keys.end(), std::string(MetaExtension::kPersistenceKey)), keys.end());
}

TEST_F(Fixture, SnapshotCarriesTimedValuesAndExtras)
{
	start();
	instance_->setParam("gain", {0.0}, 0, true);
	instance_->setParam("gain", {4.0}, 10, true);
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	RenderInfo info;
	info.time = 5.0;
	const RenderSnapshot snapshot = effect->buildSnapshot(info);
	EXPECT_DOUBLE_EQ(snapshot.number(kGain), 2.0);
	EXPECT_TRUE(snapshot.boolean(kEnabled));
	EXPECT_EQ(snapshot.integer(kMode), 1);
	EXPECT_EQ(snapshot.string(kLabel), "hello");
	EXPECT_EQ(snapshot.point(kCenter).y, 4.0);
	EXPECT_EQ(snapshot.rgba(kColor).b, 0.25);
	EXPECT_EQ(snapshot.integer(kInner), 5);
	EXPECT_FALSE(snapshot.has(kButton));
	EXPECT_FALSE(snapshot.has(kNoState));
	EXPECT_FALSE(snapshot.has(kParameterIdInstanceState));
	ASSERT_NE(snapshot.extra("effect"), nullptr);
	ASSERT_NE(snapshot.extra("recorder"), nullptr);
	EXPECT_EQ((*snapshot.extra("recorder"))[0], 42);
	EXPECT_EQ(trace().snapshots.load(), 1);
}

TEST_F(Fixture, RenderMultipliesTheSourceAndTilesAgree)
{
	start();
	instance_->setInput("Source", makeRampImage(16, 8, BitDepth::Float));
	instance_->setParam("gain", {2.0});
	RenderRequest whole;
	whole.depth = BitDepth::Float;
	RenderResult a = instance_->render(whole);
	ASSERT_TRUE(a.ok());
	EXPECT_FALSE(a.wasIdentity);
	EXPECT_EQ(a.tileCount, 1);
	RenderRequest tiled = whole;
	tiled.tileSize = 5;
	tiled.threads = 4;
	RenderResult b = instance_->render(tiled);
	ASSERT_TRUE(b.ok());
	EXPECT_EQ(b.tileCount, 8);
	EXPECT_EQ(a.image.bytes(), b.image.bytes());
	const ImageView v = a.image.view();
	EXPECT_NEAR(readComponent(v.pixel(15, 0), BitDepth::Float, 0), 2.0f, 1e-6f);
	EXPECT_NEAR(readComponent(v.pixel(0, 7), BitDepth::Float, 1), 2.0f, 1e-6f);
	EXPECT_NEAR(readComponent(v.pixel(0, 0), BitDepth::Float, 2), 1.0f, 1e-6f);
	EXPECT_DOUBLE_EQ(trace().lastGainAtRender, 2.0);
	EXPECT_GE(trace().renders.load(), 9);
}

TEST_F(Fixture, RenderHandlesEveryDepth)
{
	start();
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		instance_->setInput("Source", makeRampImage(16, 8, depth));
		RenderRequest request;
		request.depth = depth;
		RenderResult result = instance_->render(request);
		ASSERT_TRUE(result.ok()) << toString(depth);
		const ImageView v = result.image.view();
		EXPECT_NEAR(readComponent(v.pixel(15, 7), depth, 0), 1.0f, 0.01f) << toString(depth);
		EXPECT_NEAR(readComponent(v.pixel(8, 0), depth, 2), 0.5f, 0.01f) << toString(depth);
	}
}

TEST_F(Fixture, IdentityIsReportedFromTheSnapshot)
{
	start();
	instance_->setInput("Source", makeRampImage(16, 8, BitDepth::Float));
	instance_->setParam("enabled", {0.0});
	RenderRequest request;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_TRUE(result.wasIdentity);
	EXPECT_EQ(result.identityClip, "Source");
	EXPECT_EQ(trace().renders.load(), 0);
}

TEST_F(Fixture, DisconnectedSourceIsSeenAsDisconnected)
{
	start();
	instance_->disconnect("Source");
	RenderRequest request;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_TRUE(trace().sawDisconnectedSource);
	EXPECT_NEAR(readComponent(result.image.view().pixel(0, 0), BitDepth::Float, 0), 0.25f, 1e-6f);
}

TEST_F(Fixture, ResolveProfileStillRendersThroughTheOwnThreadPool)
{
	start(HostOptions::resolve21());
	// Q-RESOLVE-002: the clip claims connected but returns no image.
	instance_->disconnect("Source");
	RenderRequest request;
	request.depth = BitDepth::Float;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_TRUE(trace().sawDisconnectedSource);
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	EXPECT_TRUE(effect->services().profile().has(Quirk::ThreadSuiteDummy));
	EXPECT_TRUE(effect->services().profile().has(Quirk::MaskAlwaysConnected));
	EXPECT_EQ(effect->services().profile().edition, HostEdition::Free);
	// Q-RESOLVE-007: the broken V2 suite falls back to a plain message.
	EXPECT_TRUE(effect->services().feedback().setPersistentMessage(MessageKind::Warning, "w", "warn").ok());
	EXPECT_TRUE(instance_->persistentMessage().empty());
	EXPECT_FALSE(instance_->messages().empty());
	// Q-RESOLVE-003: parallel work runs on the plug-in's pool, so every band is covered.
	instance_->setInput("Source", makeRampImage(16, 8, BitDepth::Float));
	instance_->setParam("gain", {1.0});
	RenderResult full = instance_->render(request);
	ASSERT_TRUE(full.ok());
	EXPECT_NEAR(readComponent(full.image.view().pixel(15, 7), BitDepth::Float, 1), 1.0f, 1e-6f);
	EXPECT_NEAR(readComponent(full.image.view().pixel(0, 0), BitDepth::Float, 1), 0.0f, 1e-6f);
}

TEST_F(Fixture, SpecHostUsesPersistentMessages)
{
	start();
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	EXPECT_TRUE(effect->services().feedback().setPersistentMessage(MessageKind::Error, "e", "persist").ok());
	EXPECT_NE(instance_->persistentMessage().find("persist"), std::string::npos);
	EXPECT_TRUE(effect->services().feedback().clearPersistentMessage().ok());
	EXPECT_TRUE(instance_->persistentMessage().empty());
	EXPECT_TRUE(effect->services().feedback().progressStart("work").ok());
	EXPECT_TRUE(effect->services().feedback().progressUpdate(0.5).ok());
	EXPECT_TRUE(effect->services().feedback().progressEnd().ok());
	EXPECT_EQ(instance_->progressUpdates().size(), 3u);
	EXPECT_EQ(effect->services().time().frameRate(), 24.0);
	EXPECT_EQ(effect->services().time().projectSize().x, 16.0);
	EXPECT_EQ(effect->services().threading().cpuCount() >= 1u, true);
}

TEST_F(Fixture, ParameterServiceWritesOnlyInWritablePhases)
{
	start();
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	ParameterService& params = effect->services().parameters();
	EXPECT_FALSE(params.canWrite());
	EXPECT_EQ(params.setCurrent(kGain, Value(3.0)).code(), ErrorCode::WrongPhase);
	{
		EditScope scope(effect->services(), "test edit");
		EXPECT_TRUE(params.canWrite());
		EXPECT_TRUE(params.setCurrent(kGain, Value(3.0)).ok());
		EXPECT_TRUE(params.set(kInner, 2.0, Value(9)).ok());
		EXPECT_TRUE(params.setCurrent(kCenter, Value(Point2D{7, 8})).ok());
		EXPECT_TRUE(params.setCurrent(kColor, Value(RGBA{0, 0, 1, 1})).ok());
		EXPECT_TRUE(params.setCurrent(kEnabled, Value(false)).ok());
		EXPECT_EQ(params.setCurrent(kLabel, Value("veto")).code(), ErrorCode::Value);
		EXPECT_TRUE(params.setCurrent(kLabel, Value("fine")).ok());
		EXPECT_EQ(params.setCurrent(kButton, Value(1)).code(), ErrorCode::Unsupported);
		EXPECT_EQ(params.setCurrent(9999, Value(1)).code(), ErrorCode::NotFound);
	}
	EXPECT_FALSE(params.canWrite());
	EXPECT_EQ(numericValue(*instance_, "gain")[0], 3.0);
	EXPECT_EQ(numericValue(*instance_, "inner", 2.0)[0], 9.0);
	EXPECT_EQ(numericValue(*instance_, "center")[1], 8.0);
	EXPECT_EQ(numericValue(*instance_, "color")[2], 1.0);
	EXPECT_EQ(numericValue(*instance_, "enabled")[0], 0.0);
	EXPECT_EQ(stringValue(*instance_, "label"), "fine");
	Value v;
	EXPECT_TRUE(params.getCurrent(kLabel, v).ok());
	EXPECT_EQ(v.as<std::string>(), "fine");
	EXPECT_EQ(params.idForKey("gain"), kGain);
	EXPECT_EQ(params.idForKey("nope"), kParameterIdNone);
	EXPECT_NE(params.spec(kGain), nullptr);
	int keys = -1;
	EXPECT_TRUE(params.keyframeCount(kInner, keys).ok());
	EXPECT_EQ(keys, 1);
	Time t = -1;
	EXPECT_TRUE(params.keyframeTime(kInner, 0, t).ok());
	EXPECT_EQ(t, 2.0);
}

TEST_F(Fixture, FlagsLabelsAndOptionsReachTheHost)
{
	start();
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	ParameterService& params = effect->services().parameters();
	HostFlags flags;
	ASSERT_TRUE(params.getFlags(kGroup, flags).ok());
	EXPECT_FALSE(flags.has(HostFlag::Hidden));
	flags.set(HostFlag::Hidden).set(HostFlag::Disabled);
	ASSERT_TRUE(params.setFlags(kGroup, flags).ok());
	EXPECT_EQ(instance_->param("group")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(instance_->param("group")->props().getInt(kOfxParamPropEnabled, -1), 0);
	ASSERT_TRUE(params.getFlags(kGroup, flags).ok());
	EXPECT_TRUE(flags.has(HostFlag::Hidden));
	EXPECT_TRUE(params.setLabel(kGain, "Loudness").ok());
	std::string label;
	EXPECT_TRUE(params.getLabel(kGain, label).ok());
	EXPECT_EQ(label, "Loudness");
	EXPECT_TRUE(params.setHint(kGain, "hint").ok());
	EXPECT_TRUE(params.setOptions(kMode, {{"x", "X"}, {"y", "Y"}, {"z", "Z"}}).ok());
	EXPECT_EQ(instance_->param("mode")->props().getStrings(kOfxParamPropChoiceOption).size(), 3u);
	EXPECT_TRUE(params.setRange(kGain, {-5}, {5}).ok());
	EXPECT_EQ(instance_->param("gain")->props().getDouble(kOfxParamPropMin), -5.0);
	EXPECT_TRUE(params.setDisplayRange(kGain, {-1}, {1}).ok());
	EXPECT_EQ(instance_->param("gain")->props().getDouble(kOfxParamPropDisplayMax), 1.0);
}

TEST_F(Fixture, OptionsAreRefusedOnHostsWithoutDynamicChoices)
{
	start(HostOptions::resolve21());
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	EXPECT_EQ(effect->services().parameters().setOptions(kMode, {{"x", "X"}}).code(), ErrorCode::Unsupported);
}

TEST_F(Fixture, UnknownActionsReplyDefault)
{
	start();
	minihost::PropSet in;
	EXPECT_EQ(plugin_->mainEntry("OfxActionFromTheFuture", instance_->effectHandle(), in.handle(), nullptr), kOfxStatReplyDefault);
	EXPECT_EQ(plugin_->mainEntry(kOfxImageEffectActionGetTimeDomain, instance_->effectHandle(), in.handle(), nullptr), kOfxStatReplyDefault);
	EXPECT_EQ(plugin_->mainEntry(kOfxImageEffectActionGetFramesNeeded, instance_->effectHandle(), in.handle(), nullptr), kOfxStatReplyDefault);
	EXPECT_EQ(plugin_->mainEntry(kOfxImageEffectActionRender, nullptr, in.handle(), nullptr), kOfxStatErrBadHandle);
	EXPECT_EQ(instance_->purgeCaches(), kOfxStatOK);
	EXPECT_EQ(instance_->beginInstanceEdit(), kOfxStatOK);
	EXPECT_EQ(instance_->endInstanceEdit(), kOfxStatOK);
}

TEST_F(Fixture, DestroyInstanceUnloadsExtensions)
{
	start();
	instance_.reset();
	std::lock_guard<std::mutex> lock(trace().mutex);
	EXPECT_EQ(trace().events.back(), EventId::DestroyInstance);
	EXPECT_EQ(host_->unload(), kOfxStatOK);
}

TEST_F(Fixture, RegionOfDefinitionDefaultsWhenUnhandled)
{
	start();
	instance_->setInput("Source", makeRampImage(16, 8, BitDepth::Float));
	RectD rod;
	ASSERT_EQ(instance_->regionOfDefinition(0, {1, 1}, rod), kOfxStatOK);
	EXPECT_EQ(rod.x2, 16.0);
	EXPECT_EQ(rod.y2, 8.0);
}

// ---- an effect that answers every optional action, so the handled paths run -------------------

namespace {

class AnsweringEffect final : public Effect {
public:
	Error onRegionOfDefinition(const RegionArgs& args, const RenderSnapshot&, RectD& rod, bool& handled) override
	{
		rod = RectD{0, 0, 100 * args.renderScale.x, 50 * args.renderScale.y};
		handled = true;
		return Error::none();
	}
	Error onRegionsOfInterest(const RegionArgs& args, const RenderSnapshot&, std::map<std::string, RectD>& regions, bool& handled) override
	{
		regions["Source"] = RectD{args.regionOfInterest.x1 - 1, args.regionOfInterest.y1 - 1, args.regionOfInterest.x2 + 1, args.regionOfInterest.y2 + 1};
		handled = true;
		return Error::none();
	}
	Error onClipPreferences(ClipPreferences& prefs, bool& handled) override
	{
		prefs.components["Output"] = Components::RGBA;
		prefs.depths["Output"] = BitDepth::Float;
		prefs.pixelAspects["Output"] = 1.0;
		prefs.frameRate = 25.0;
		prefs.fieldOrder = Field::None;
		prefs.premultiplication = Premultiplication::Premultiplied;
		prefs.continuousSamples = true;
		prefs.frameVarying = true;
		handled = true;
		return Error::none();
	}
	Error onBeginSequenceRender(const SequenceRenderArgs& args) override
	{
		sequenceFrames = static_cast<int>(args.lastFrame - args.firstFrame + 1);
		return Error::none();
	}
	Error onEndSequenceRender(const SequenceRenderArgs&) override
	{
		sequenceEnded = true;
		return Error::none();
	}
	Error render(RenderContext& context) override
	{
		if (context.snapshot.integer(kMode) == 0) {
			return Error::make(ErrorCode::Render, "mode zero fails on purpose");
		}
		if (context.snapshot.integer(kInner) == 7) {
			return Error::cancelled();
		}
		context.output.clear();
		return Error::none();
	}
	int sequenceFrames{0};
	bool sequenceEnded{false};
};

}  // namespace

TEST(AnsweringEffect, HandledGeometryClipPreferencesAndRenderOutcomes)
{
	PluginRegistry::instance().clear();
	const int index = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<AnsweringEffect>>(testManifest()));
	ASSERT_GE(index, 0);
	OfxPlugin* plugin = PluginRegistry::instance().plugin(index);
	MiniHost host(HostOptions::spec());
	ASSERT_EQ(host.load(plugin), kOfxStatOK);
	ASSERT_EQ(host.describe(), kOfxStatOK);
	Project project;
	project.width = 10;
	project.height = 5;
	std::unique_ptr<Instance> instance = host.createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(instance, nullptr);
	instance->setInput("Source", makeRampImage(10, 5, BitDepth::Float));

	RectD rod;
	ASSERT_EQ(instance->regionOfDefinition(0, {0.5, 0.5}, rod), kOfxStatOK);
	EXPECT_EQ(rod.x2, 50.0);
	EXPECT_EQ(rod.y2, 25.0);

	minihost::PropSet in;
	in.setDouble(kOfxPropTime, 0.0);
	in.setDoubles(kOfxImageEffectPropRenderScale, {1.0, 1.0});
	in.setDoubles(kOfxImageEffectPropRegionOfInterest, {2, 2, 4, 4});
	minihost::PropSet out;
	EXPECT_EQ(plugin->mainEntry(kOfxImageEffectActionGetRegionsOfInterest, instance->effectHandle(), in.handle(), out.handle()), kOfxStatOK);
	EXPECT_EQ(out.getDoubles("OfxImageClipPropRoI_Source"), (std::vector<double>{1, 1, 5, 5}));

	minihost::PropSet prefs;
	EXPECT_EQ(plugin->mainEntry(kOfxImageEffectActionGetClipPreferences, instance->effectHandle(), nullptr, prefs.handle()), kOfxStatOK);
	EXPECT_EQ(prefs.getString("OfxImageClipPropComponents_Output"), kOfxImageComponentRGBA);
	EXPECT_EQ(prefs.getString("OfxImageClipPropDepth_Output"), kOfxBitDepthFloat);
	EXPECT_EQ(prefs.getDouble(kOfxImageEffectPropFrameRate), 25.0);
	EXPECT_EQ(prefs.getString(kOfxImageEffectPropPreMultiplication), kOfxImagePreMultiplied);
	EXPECT_EQ(prefs.getInt(kOfxImageClipPropContinuousSamples), 1);
	EXPECT_EQ(prefs.getInt(kOfxImageEffectFrameVarying), 1);

	RenderRequest request;
	request.depth = BitDepth::Float;
	instance->setParam("mode", {0.0});
	RenderResult failed = instance->render(request);
	EXPECT_FALSE(failed.ok());
	EXPECT_EQ(failed.tileStatuses[0], kOfxStatFailed);
	instance->setParam("mode", {1.0});
	instance->setParam("inner", {7.0});
	RenderResult cancelled = instance->render(request);
	EXPECT_TRUE(cancelled.ok());
	instance->setParam("inner", {1.0});
	RenderResult fine = instance->render(request);
	EXPECT_TRUE(fine.ok());
	auto* effect = static_cast<AnsweringEffect*>(instance->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	EXPECT_EQ(effect->sequenceFrames, 1);
	EXPECT_TRUE(effect->sequenceEnded);
	request.depth = BitDepth::Half;
	EXPECT_TRUE(instance->render(request).ok());
	instance.reset();
	host.unload();
	PluginRegistry::instance().clear();
}

TEST_F(Fixture, UnsupportedDepthIsRefusedAtRender)
{
	// A manifest that declares only float, rendered at 8 bits: the adapter refuses before the
	// effect runs (Q-OFX-ANY-003). The fixture's own slot is released first, because clearing the
	// registry frees the plug-in the fixture's instance would otherwise destroy through.
	instance_.reset();
	host_.reset();
	PluginRegistry::instance().clear();
	EffectManifest narrow = testManifest();
	narrow.capabilities.depths = {BitDepth::Float};
	const int index = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<TestEffect>>(narrow));
	ASSERT_GE(index, 0);
	OfxPlugin* plugin = PluginRegistry::instance().plugin(index);
	MiniHost host(HostOptions::spec());
	ASSERT_EQ(host.load(plugin), kOfxStatOK);
	ASSERT_EQ(host.describe(), kOfxStatOK);
	Project project;
	std::unique_ptr<Instance> instance = host.createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(instance, nullptr);
	instance->setInput("Source", makeRampImage(64, 64, BitDepth::UInt8));
	RenderRequest request;
	request.depth = BitDepth::UInt8;
	RenderResult result = instance->render(request);
	EXPECT_FALSE(result.ok());
	EXPECT_EQ(result.tileStatuses[0], kOfxStatErrFormat);
	instance.reset();
	host.unload();
}

TEST_F(Fixture, ServicesEdgeCases)
{
	start();
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	HostServices& s = effect->services();
	EXPECT_EQ(s.identity().pluginId, "com.belisoful.ofxgrip.test");
	EXPECT_EQ(s.identity().pluginUuid, "TEST-UUID");
	EXPECT_NE(s.identity().instanceId.find("#"), std::string::npos);
	EXPECT_EQ(s.identity().bundlePath.string(), "/tmp/test.ofx.bundle");
	EXPECT_EQ(s.phase(), Phase::None);
	EXPECT_TRUE(s.feedback().message(MessageKind::Question, "q", "yes?").ok());
	EXPECT_TRUE(s.feedback().message(MessageKind::Log, "l", "log").ok());
	EXPECT_TRUE(s.feedback().message(MessageKind::Warning, "w", "warn").ok());
	EXPECT_FALSE(s.feedback().abortRequested());
	instance_->abortFlag = true;
	EXPECT_TRUE(s.feedback().abortRequested());
	instance_->abortFlag = false;
	EXPECT_EQ(s.time().currentTime(), 0.0);
	instance_->setCurrentTime(3.0);
	EXPECT_EQ(s.time().currentTime(), 3.0);
	EXPECT_EQ(s.time().timeBounds().second, 0.0);
	EXPECT_EQ(s.time().projectOffset().x, 0.0);
	EXPECT_EQ(s.time().projectPixelAspect(), 1.0);
	EXPECT_FALSE(s.paths().cacheDirectory().empty());
	EXPECT_FALSE(s.paths().userSupportDirectory().empty());
	EXPECT_FALSE(s.paths().logDirectory().empty());
	EXPECT_FALSE(s.threading().isSpawnedThread());
	int count = 0;
	std::mutex m;
	EXPECT_TRUE(s.threading().parallelFor(5, [&](unsigned, unsigned total) {
		std::lock_guard<std::mutex> lock(m);
		count += static_cast<int>(total);
	}).ok());
	EXPECT_EQ(count, 25);
	EXPECT_TRUE(s.threading().parallelFor(0, [](unsigned, unsigned) {}).ok());
	Bytes none;
	EXPECT_EQ(s.persistence().load("missing", none).code(), ErrorCode::NotFound);
	EXPECT_TRUE(s.persistence().store("k", Bytes{1}).ok());
	EXPECT_TRUE(s.persistence().remove("k").ok());
	const std::vector<std::string> keys = s.persistence().keys();
	EXPECT_EQ(std::find(keys.begin(), keys.end(), "k"), keys.end());
	s.log().debug("debug line");
	s.log().warning("warning line");
	s.log().error("error line");
	EXPECT_EQ(s.profile().family, HostFamily::OfxGripMiniHost);
	EXPECT_EQ(toString(Phase::Render), "Render");
	EXPECT_TRUE(phaseAllowsParameterWrites(Phase::EditScope));
	EXPECT_FALSE(phaseAllowsParameterWrites(Phase::Render));
	class Detached final : public Effect {
	public:
		Error render(RenderContext&) override { return Error::none(); }
	};
	Detached detached;
	EXPECT_FALSE(detached.attached());
	EXPECT_THROW(detached.services(), OfxGripException);
	EXPECT_TRUE(detached.manifest().parameters.empty());
}

TEST_F(Fixture, StateParameterIsRestoredOnANewInstance)
{
	start();
	instance_->instanceChanged("button");
	ASSERT_EQ(instance_->syncPrivateData(), kOfxStatOK);
	const std::string stored = stringValue(*instance_, kInstanceStateParameterKey);
	// A second instance whose host store already holds the blob reads it at creation.
	Project project;
	std::unique_ptr<Instance> second = host_->createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(second, nullptr);
	second->setStringParam(kInstanceStateParameterKey, stored);
	// Re-create so bind() runs against the stored value: destroy and create again.
	second.reset();
	std::unique_ptr<Instance> third = host_->createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(third, nullptr);
	third->setStringParam(kInstanceStateParameterKey, "OGST1:not base64!!");
	Effect* effect = static_cast<Effect*>(third->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(effect, nullptr);
	// The unreadable blob was discarded: only the framework's fresh records remain.
	const std::vector<std::string> keys = effect->services().persistence().keys();
	EXPECT_EQ(std::find(keys.begin(), keys.end(), "clicks"), keys.end());
	EXPECT_NE(std::find(keys.begin(), keys.end(), std::string(VersioningExtension::kPersistenceKey)), keys.end());
}

// ---- phase 1: the parameter model and the framework extensions through the adapter -----------

namespace {

enum : ParameterId { kRigMode = 400, kRigSoftness = 401, kRigEdge = 402, kRigBypass = 403, kRigOld = 404, kRigNew = 405 };

EffectManifest riggedManifest()
{
	EffectManifest m = testManifest();
	m.identity.ofxId = "com.belisoful.ofxgrip.rigged";
	m.version = Version{3, 0, 0};
	m.deprecatedMajors = {1, 2};
	m.schemaVersion = 2;
	TargetPreset soft;
	soft.values["rigSoftness"] = Value(0.75);
	soft.labels["rigSoftness"] = "Feather";
	soft.tags["rigEdge"] = {"soft-only"};
	TargetPreset hard;
	hard.labels["rigSoftness"] = "Softness";
	m.parameters.push_back(ParameterSpec::menuParam(kRigMode, "rigMode", "Rig Mode", {{"soft", "Soft"}, {"hard", "Hard"}}, 1).withTargetPreset("soft", soft).withTargetPreset("hard", hard));
	m.parameters.push_back(ParameterSpec::floatParam(kRigSoftness, "rigSoftness", "Softness", 0.5, 0, 1).withVisibleWhen("rigMode == 'soft'").withTags({"rig"}).withMeta("unit", Value("px")));
	m.parameters.push_back(ParameterSpec::floatParam(kRigEdge, "rigEdge", "Edge", 0.5, 0, 1).withEnabledWhen("!rigBypass"));
	m.parameters.push_back(ParameterSpec::boolParam(kRigBypass, "rigBypass", "Bypass", false));
	m.parameters.push_back(ParameterSpec::floatParam(kRigOld, "rigOld", "Old", 0.25, 0, 1).withFlags(HostFlags(HostFlag::Hidden), AppFlags(AppFlag::NoState)));
	m.parameters.push_back(ParameterSpec::floatParam(kRigNew, "rigNew", "New", 0.0, 0, 1));
	Migration rename;
	rename.fromSchemaVersion = 1;
	rename.renames["rigOld"] = "rigNew";
	m.migrations.push_back(rename);
	return m;
}

class RiggedEffect final : public Effect {
public:
	static inline std::vector<std::string> migrations;
	Error onMigrate(unsigned from, unsigned to, Version fromVersion) override
	{
		migrations.push_back(std::to_string(from) + "->" + std::to_string(to) + "@" + fromVersion.toString());
		return Error::none();
	}
	Error render(RenderContext&) override { return Error::none(); }
};

class Rigged : public ::testing::Test {
protected:
	void SetUp() override
	{
		RiggedEffect::migrations.clear();
		PluginRegistry::instance().clear();
		index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<RiggedEffect>>(riggedManifest()));
		ASSERT_GE(index_, 0);
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
	}
	void start(int slot, HostOptions options = HostOptions::spec())
	{
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(host_->load(PluginRegistry::instance().plugin(slot), "/tmp/rigged.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	Effect* effect() { return static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData)); }
	int index_{-1};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(Rigged, DeprecatedMajorsTakeTheirOwnSlots)
{
	EXPECT_EQ(PluginRegistry::instance().count(), 3);
	EXPECT_EQ(PluginRegistry::instance().plugin(0)->pluginVersionMajor, 3u);
	EXPECT_EQ(PluginRegistry::instance().plugin(1)->pluginVersionMajor, 1u);
	EXPECT_EQ(PluginRegistry::instance().plugin(2)->pluginVersionMajor, 2u);
	EXPECT_STREQ(PluginRegistry::instance().plugin(2)->pluginIdentifier, "com.belisoful.ofxgrip.rigged");
	// The same identifier cannot register twice, whatever its majors.
	EXPECT_EQ(PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<RiggedEffect>>(riggedManifest())), -1);
	start(2);
	EXPECT_EQ(effect()->services().identity().pluginVersion.major, 2u);
	EXPECT_TRUE(effect()->manifest().deprecatedMajors.empty());
}

TEST_F(Rigged, ParameterObjectsReadAndWriteThroughTheHost)
{
	start(0);
	Effect& e = *effect();
	ASSERT_NE(e.param("gain"), nullptr);
	EXPECT_EQ(e[kGain], e.param("gain"));
	EXPECT_EQ(e["gain"], e.param(kGain));
	EXPECT_EQ(e.param("nonesuch"), nullptr);
	EXPECT_DOUBLE_EQ(e["gain"]->number(0), 1.0);
	EXPECT_EQ(e["mode"]->currentOptionKey(), "b");
	EXPECT_EQ(e["group"]->children().size(), 1u);
	EXPECT_EQ(e["inner"]->parent(), e["group"]);
	EXPECT_EQ(e.parameters().size(), e.services().parameters().ids().size());
	EXPECT_NE(e.parameters().find(kParameterIdInstanceState), nullptr);
	EXPECT_TRUE(e["rigSoftness"]->hasTag("rig"));
	EXPECT_EQ(e["rigSoftness"]->meta("unit")->as<std::string>(), "px");
	ASSERT_NE(e.meta(), nullptr);
	EXPECT_EQ(e.meta()->parametersWithTag("rig"), std::vector<ParameterId>{kRigSoftness});
	// Writes outside a callback need an edit scope.
	EXPECT_EQ(e["gain"]->setCurrent(Value(2.0)).code(), ErrorCode::WrongPhase);
	{
		EditScope scope = e.edit("test");
		EXPECT_TRUE(scope.open());
		EXPECT_TRUE(e["gain"]->setCurrent(Value(2.0)).ok());
		EXPECT_TRUE(e["rigSoftness"]->addTag("user").ok());
	}
	EXPECT_EQ(numericValue(*instance_, "gain")[0], 2.0);
	EXPECT_TRUE(e.meta()->hasTag(kRigSoftness, "user"));
	EXPECT_FALSE(e.meta()->dirty());  // the scope's flush saved it
	EXPECT_NE(e.metaExtension(), nullptr);
	EXPECT_NE(e.visibilityExtension(), nullptr);
	EXPECT_NE(e.versioningExtension(), nullptr);
}

TEST_F(Rigged, TargetPresetsAndRulesApplyThroughInstanceChanged)
{
	start(0);
	Effect& e = *effect();
	// On creation: mode is hard, so softness is hidden; bypass is off, so edge is enabled.
	EXPECT_EQ(instance_->param("rigSoftness")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(instance_->param("rigEdge")->props().getInt(kOfxParamPropEnabled, -1), 1);
	instance_->setParam("rigMode", {0.0});
	ASSERT_EQ(instance_->instanceChanged("rigMode"), kOfxStatOK);
	EXPECT_EQ(instance_->param("rigSoftness")->props().getInt(kOfxParamPropSecret, -1), 0);
	EXPECT_EQ(numericValue(*instance_, "rigSoftness")[0], 0.75);
	EXPECT_EQ(instance_->param("rigSoftness")->props().getString(kOfxPropLabel), "Feather");
	EXPECT_TRUE(e.meta()->hasTag(kRigEdge, "soft-only"));
	instance_->setParam("rigBypass", {1.0});
	ASSERT_EQ(instance_->instanceChanged("rigBypass"), kOfxStatOK);
	EXPECT_EQ(instance_->param("rigEdge")->props().getInt(kOfxParamPropEnabled, -1), 0);
	instance_->setParam("rigMode", {1.0});
	ASSERT_EQ(instance_->instanceChanged("rigMode"), kOfxStatOK);
	EXPECT_EQ(instance_->param("rigSoftness")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(instance_->param("rigSoftness")->props().getString(kOfxPropLabel), "Softness");
	// Sync re-applies the rules after the host restores values behind the plug-in's back.
	instance_->setParam("rigBypass", {0.0});
	ASSERT_EQ(instance_->syncPrivateData(), kOfxStatOK);
	EXPECT_EQ(instance_->param("rigEdge")->props().getInt(kOfxParamPropEnabled, -1), 1);
	// The meta store travels in the state parameter.
	const std::string stored = stringValue(*instance_, kInstanceStateParameterKey);
	EXPECT_EQ(stored.rfind("OGST1:", 0), 0u);
	Bytes blob;
	ASSERT_TRUE(e.services().persistence().load(MetaExtension::kPersistenceKey, blob).ok());
	MetaManager decoded;
	ASSERT_TRUE(MetaManager::decode(blob, decoded).ok());
	EXPECT_TRUE(decoded.hasTag(kRigEdge, "soft-only"));
}

TEST_F(Rigged, VersionIsRecordedAndMigrationsRunOnOlderDocuments)
{
	start(0);
	Effect& e = *effect();
	ASSERT_NE(e.versioningExtension(), nullptr);
	EXPECT_FALSE(e.versioningExtension()->stored().has_value());
	EXPECT_TRUE(RiggedEffect::migrations.empty());
	ASSERT_EQ(instance_->syncPrivateData(), kOfxStatOK);
	Bytes bytes;
	ASSERT_TRUE(e.services().persistence().load(VersioningExtension::kPersistenceKey, bytes).ok());
	StoredVersion v;
	ASSERT_TRUE(VersioningExtension::decode(bytes, v).ok());
	EXPECT_EQ(v.schemaVersion, 2u);
	EXPECT_EQ(v.pluginVersion, (Version{3, 0, 0}));

	// A document from schema 1 (plug-in 1.0.0): the rename copies rigOld into rigNew.
	instance_.reset();
	Project project;
	std::unique_ptr<Instance> older = host_->createInstance(kOfxImageEffectContextFilter, project);
	Effect* olderEffect = static_cast<Effect*>(older->props().getPointer(kOfxPropInstanceData));
	ASSERT_NE(olderEffect, nullptr);
	ASSERT_TRUE(olderEffect->services().persistence().store(VersioningExtension::kPersistenceKey, VersioningExtension::encode(StoredVersion{1, Version{1, 0, 0}})).ok());
	ASSERT_EQ(older->syncPrivateData(), kOfxStatOK);
	const std::string legacyState = stringValue(*older, kInstanceStateParameterKey);
	older.reset();
	// The host restores the saved parameters before it creates the instance.
	instance_ = host_->createInstance(kOfxImageEffectContextFilter, project, {}, [&legacyState](ParamSet& params) {
		params.find(kInstanceStateParameterKey)->setStringValue(legacyState);
		params.find("rigOld")->setNumericValue({0.25});
	});
	ASSERT_NE(instance_, nullptr);
	Effect& migrated = *effect();
	ASSERT_TRUE(migrated.versioningExtension()->stored().has_value());
	EXPECT_EQ(migrated.versioningExtension()->stored()->schemaVersion, 1u);
	EXPECT_TRUE(migrated.versioningExtension()->migrated());
	EXPECT_EQ(RiggedEffect::migrations, std::vector<std::string>{"1->2@1.0.0"});
	EXPECT_EQ(numericValue(*instance_, "rigNew")[0], 0.25);
}

TEST_F(Rigged, ManifestPropertiesDisableTheFrameworkExtensions)
{
	PluginRegistry::instance().clear();
	EffectManifest m = riggedManifest();
	m.identity.ofxId = "com.belisoful.ofxgrip.plain";
	m.deprecatedMajors.clear();
	m.properties["manageMeta"] = Value(false);
	m.properties["manageVisibility"] = Value(false);
	index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<RiggedEffect>>(m));
	ASSERT_GE(index_, 0);
	start(0);
	Effect& e = *effect();
	EXPECT_EQ(e.meta(), nullptr);
	EXPECT_EQ(e.metaExtension(), nullptr);
	EXPECT_EQ(e.visibilityExtension(), nullptr);
	EXPECT_NE(e.versioningExtension(), nullptr);
	EXPECT_EQ(e["rigSoftness"]->tags(), std::vector<std::string>{"rig"});
	EXPECT_EQ(e["rigSoftness"]->addTag("x").code(), ErrorCode::Unsupported);
	// Without the extensions, neither rules nor presets run.
	EXPECT_EQ(instance_->param("rigSoftness")->props().getInt(kOfxParamPropSecret, -1), 0);
	instance_->setParam("rigMode", {0.0});
	ASSERT_EQ(instance_->instanceChanged("rigMode"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "rigSoftness")[0], 0.5);
}

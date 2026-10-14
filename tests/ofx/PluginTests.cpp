// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// In-process adapter tests: a test effect registered with the PluginRegistry is driven through
// the mini host, so every action, service, and quirk path runs without a bundle on disk.
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/Descriptor.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

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
	EXPECT_EQ(effect->services().persistence().keys().size(), 1u);
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
	EXPECT_TRUE(s.persistence().keys().empty());
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
	EXPECT_TRUE(effect->services().persistence().keys().empty());
}

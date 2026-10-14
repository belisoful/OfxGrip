// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Effect.hpp
	@brief The effect base a plug-in subclasses, the render context it renders into, and the
	       factory that creates instances.

	Introduced in OfxGrip 0.1.0. `Effect` is OfxGrip's `FxGripTileableEffect`: it owns the
	extension system, the event bus, and the host services, answers every OpenFX action through
	the dispatcher, and exposes override points for the author. Render receives a `RenderContext`
	holding the immutable snapshot, the action arguments, the output image, and the input images,
	and nothing else (D-002, D-003).
*/
#pragma once

#include <ofxgrip/core/Events.hpp>
#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/RenderSnapshot.hpp>
#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/OfxHostServices.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! An input clip's image for one render. `connected` is false when the clip reported
    disconnected or returned no image (Q-RESOLVE-002). */
struct InputImage {
	std::string clip;
	bool connected{false};
	ImageView view;
	GpuImage gpu;
	bool isGpu() const noexcept { return gpu.api != GpuApi::None; }
};

/*! What the render reads and writes. */
class RenderContext {
public:
	RenderContext(const RenderSnapshot& snapshotRef, const RenderArgs& argsRef, HostServices& servicesRef)
	: snapshot(snapshotRef), args(argsRef), services(servicesRef)
	{
	}

	const RenderSnapshot& snapshot;
	const RenderArgs& args;
	HostServices& services;
	ImageView output;
	GpuImage outputGpu;
	std::vector<InputImage> inputs;

	bool outputIsGpu() const noexcept { return outputGpu.api != GpuApi::None; }
	/*! The input for a clip name, or null. */
	const InputImage* input(std::string_view clip) const noexcept;
	/*! The first input, or null for a generator. */
	const InputImage* source() const noexcept { return inputs.empty() ? nullptr : &inputs.front(); }
	bool abortRequested() { return services.feedback().abortRequested(); }
	/*! Runs `work` over horizontal bands of the render window on the threading service, polling
	    abort between bands. */
	Error forEachBand(const std::function<void(const RectI& band)>& work, unsigned bandCount = 0);
};

/*! The answers to the clip-preferences action. Empty optionals leave the host default. */
struct ClipPreferences {
	std::map<std::string, Components> components;
	std::map<std::string, BitDepth> depths;
	std::map<std::string, double> pixelAspects;
	std::optional<double> frameRate;
	std::optional<Field> fieldOrder;
	std::optional<Premultiplication> premultiplication;
	bool continuousSamples{false};
	bool frameVarying{false};
};

/*! The effect base. */
class Effect : public EffectHost {
public:
	Effect();
	~Effect() override;
	Effect(const Effect&) = delete;
	Effect& operator=(const Effect&) = delete;

	HostServices& services() override;
	EventBus& bus() override { return *bus_; }
	const EffectManifest& manifest() const override;
	Effect* effectBase() override { return this; }
	ExtensionSystem& extensions() { return *extensions_; }
	OfxHostServices& ofxServices() { return *services_; }
	const OfxHostServices& ofxServices() const { return *services_; }
	bool attached() const noexcept { return services_ != nullptr; }

	/*! Override points. Each default does nothing and returns success. */
	virtual void loadExtensions(ExtensionSystem& system);
	virtual Error onCreate();
	virtual Error onInstanceChanged(const ChangeEvent& change);
	virtual Error onParameterClicked(ParameterId id);
	virtual Error onBuildSnapshot(RenderSnapshotBuilder& builder, const RenderInfo& info);
	/*! Sets `handled` to answer the region of definition; otherwise the host default applies. */
	virtual Error onRegionOfDefinition(const RegionArgs& args, const RenderSnapshot& snapshot, RectD& regionOfDefinition, bool& handled);
	/*! Sets `handled` to answer per-clip regions of interest. */
	virtual Error onRegionsOfInterest(const RegionArgs& args, const RenderSnapshot& snapshot, std::map<std::string, RectD>& regions, bool& handled);
	/*! Sets `identity` to pass a clip through. `clip` and `time` name the source. */
	virtual Error onIsIdentity(const IsIdentityArgs& args, const RenderSnapshot& snapshot, std::string& clip, Time& time, bool& identity);
	virtual Error onClipPreferences(ClipPreferences& preferences, bool& handled);
	virtual Error onBeginSequenceRender(const SequenceRenderArgs& args);
	virtual Error onEndSequenceRender(const SequenceRenderArgs& args);
	/*! The render. Required. */
	virtual Error render(RenderContext& context) = 0;

	/*! Adapter entry points. */
	void attach(std::unique_ptr<OfxHostServices> services, const CreateInstanceArgs& args);
	/*! Builds the snapshot for a render: timed reads of every state parameter, the subclass hook,
	    then the `BuildSnapshot` event. */
	RenderSnapshot buildSnapshot(const RenderInfo& info);
	/*! Posts `Flush` so extensions persist, then commits persistence. */
	Error flush();

private:
	std::unique_ptr<EventBus> bus_;
	std::unique_ptr<OfxHostServices> services_;
	std::unique_ptr<ExtensionSystem> extensions_;
	EffectManifest emptyManifest_;
};

/*! Creates instances and owns the manifest. */
class EffectFactory {
public:
	virtual ~EffectFactory() = default;
	virtual const EffectManifest& manifest() const = 0;
	virtual std::unique_ptr<Effect> create() = 0;
	/*! Edits the manifest before describe, for example to append an extension's parameters. */
	virtual void augmentManifest(EffectManifest& manifest) { (void)manifest; }
	virtual void onLoad(const HostProfile& profile, LogService& log)
	{
		(void)profile;
		(void)log;
	}
	virtual void onUnload() {}
};

/*! A factory that default-constructs an effect class. */
template <typename E>
class EffectFactoryFor final : public EffectFactory {
public:
	explicit EffectFactoryFor(EffectManifest manifest) : manifest_(std::move(manifest)) {}
	const EffectManifest& manifest() const override { return manifest_; }
	std::unique_ptr<Effect> create() override { return std::make_unique<E>(); }

private:
	EffectManifest manifest_;
};

OFXGRIP_NAMESPACE_END

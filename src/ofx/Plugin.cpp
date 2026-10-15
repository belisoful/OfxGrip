// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Plugin.hpp>

#include <ofxgrip/ofx/RenderWatermark.hpp>

#include <ofxgrip/ofx/Descriptor.hpp>
#include <ofxgrip/ofx/Interact.hpp>
#include <ofxgrip/ofx/ResolveExtensions.hpp>

#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxParam.h"

#include <array>
#include <atomic>
#include <cstring>
#include <sstream>
#include <utility>

OFXGRIP_NAMESPACE_BEGIN

namespace detail {

template <std::size_t... I>
constexpr std::array<SetHostFn, sizeof...(I)> makeSetHostTable(std::index_sequence<I...>)
{
	return {{&slotSetHost<static_cast<int>(I)>...}};
}

template <std::size_t... I>
constexpr std::array<MainEntryFn, sizeof...(I)> makeMainEntryTable(std::index_sequence<I...>)
{
	return {{&slotMainEntry<static_cast<int>(I)>...}};
}

SetHostFn setHostTrampoline(int index) noexcept
{
	static constexpr auto table = makeSetHostTable(std::make_index_sequence<kMaxPluginsPerBundle>{});
	if (index < 0 || index >= kMaxPluginsPerBundle) {
		return nullptr;
	}
	return table[static_cast<std::size_t>(index)];
}

MainEntryFn mainEntryTrampoline(int index) noexcept
{
	static constexpr auto table = makeMainEntryTable(std::make_index_sequence<kMaxPluginsPerBundle>{});
	if (index < 0 || index >= kMaxPluginsPerBundle) {
		return nullptr;
	}
	return table[static_cast<std::size_t>(index)];
}

}  // namespace detail

// ---- PluginSlot ---------------------------------------------------------------------------------

/*! One registered effect: its factory, its `OfxPlugin` record, and everything the bundle keeps
    for it across actions. */
class PluginSlot {
public:
	/*! `major` is 0 for the current version; a deprecated major gets its own slot whose manifest
	    carries that major and no deprecation list. */
	PluginSlot(int index, std::shared_ptr<EffectFactory> factory, unsigned major)
	: factory_(std::move(factory)), manifest_(factory_->manifest()), identifier_(manifest_.identity.ofxId), logger_(manifest_.identity.ofxId)
	{
		factory_->augmentManifest(manifest_);
		if (major != 0) {
			manifest_.version.major = major;
			manifest_.deprecatedMajors.clear();
		}
		std::memset(&plugin_, 0, sizeof plugin_);
		plugin_.pluginApi = kOfxImageEffectPluginApi;
		plugin_.apiVersion = kOfxImageEffectPluginApiVersion;
		plugin_.pluginIdentifier = identifier_.c_str();
		plugin_.pluginVersionMajor = manifest_.version.major;
		plugin_.pluginVersionMinor = manifest_.version.minor;
		plugin_.setHost = detail::setHostTrampoline(index);
		plugin_.mainEntry = detail::mainEntryTrampoline(index);
		interactEntry_ = detail::interactEntryTrampoline(index);
	}

	OfxPlugin* plugin() noexcept { return &plugin_; }
	const std::string& identifier() const noexcept { return identifier_; }
	unsigned major() const noexcept { return manifest_.version.major; }

	void setHost(OfxHost* host) noexcept { host_ = host; }

	OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) noexcept
	{
		// The single catch boundary: nothing propagates past here.
		try {
			return dispatch(parseAction(action), action, handle, inArgs, outArgs);
		} catch (const OfxGripException& e) {
			logger_.error(std::string("Action ") + (action ? action : "?") + " failed: " + e.error().describe());
			return errorToStatus(e.error());
		} catch (const std::exception& e) {
			logger_.error(std::string("Action ") + (action ? action : "?") + " threw: " + e.what());
			return kOfxStatFailed;
		} catch (...) {
			logger_.error(std::string("Action ") + (action ? action : "?") + " threw an unknown exception");
			return kOfxStatFailed;
		}
	}

	/*! The overlay interact's actions, with their own catch boundary. */
	OfxStatus interactEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) noexcept
	{
		try {
			return interactDispatch(parseAction(action), handle, inArgs, outArgs);
		} catch (const OfxGripException& e) {
			logger_.error(std::string("Interact action ") + (action ? action : "?") + " failed: " + e.error().describe());
			return errorToStatus(e.error());
		} catch (const std::exception& e) {
			logger_.error(std::string("Interact action ") + (action ? action : "?") + " threw: " + e.what());
			return kOfxStatFailed;
		} catch (...) {
			logger_.error(std::string("Interact action ") + (action ? action : "?") + " threw an unknown exception");
			return kOfxStatFailed;
		}
	}

private:
	/*! The property set of an interact descriptor or instance. */
	PropertySet interactProperties(const void* handle)
	{
		OfxPropertySetHandle props = nullptr;
		if (handle == nullptr || suites_.interact == nullptr || suites_.interact->interactGetPropertySet == nullptr) {
			return {};
		}
		if (suites_.interact->interactGetPropertySet(static_cast<OfxInteractHandle>(const_cast<void*>(handle)), &props) != kOfxStatOK) {
			return {};
		}
		return PropertySet(suites_.property, props);
	}

	OfxStatus interactDispatch(Action action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
	{
		PropertySet props = interactProperties(handle);
		PropertySet in(suites_.property, inArgs);
		PropertySet out(suites_.property, outArgs);
		switch (action) {
		case Action::Describe:
			if (props.valid()) {
				props.trySetInt(kOfxInteractPropHasAlpha, 0);
				props.trySetInt(kOfxInteractPropBitDepth, 8);
			}
			return kOfxStatOK;
		case Action::CreateInstance: {
			if (!props.valid()) {
				return kOfxStatErrBadHandle;
			}
			Effect* effect = effectFor(props.getPointer(kOfxPropEffectInstance).value_or(nullptr));
			if (effect == nullptr) {
				logger_.warning("An overlay interact was created for an unknown effect instance");
				return kOfxStatErrBadHandle;
			}
			auto interact = std::make_unique<OverlayInteract>(*effect, const_cast<void*>(handle));
			if (!interact->hasControl()) {
				return kOfxStatOK;
			}
			if (Error e = props.setPointer(kOfxPropInstanceData, interact.get())) {
				return errorToStatus(e);
			}
			interact.release();
			return kOfxStatOK;
		}
		case Action::DestroyInstance: {
			auto* interact = static_cast<OverlayInteract*>(props.getPointer(kOfxPropInstanceData).value_or(nullptr));
			if (interact != nullptr) {
				props.setPointer(kOfxPropInstanceData, nullptr);
				delete interact;
			}
			return kOfxStatOK;
		}
		case Action::InteractDraw:
		case Action::InteractPenMotion:
		case Action::InteractPenDown:
		case Action::InteractPenUp:
		case Action::InteractKeyDown:
		case Action::InteractKeyUp:
		case Action::InteractKeyRepeat:
		case Action::InteractGainFocus:
		case Action::InteractLoseFocus: {
			auto* interact = static_cast<OverlayInteract*>(props.getPointer(kOfxPropInstanceData).value_or(nullptr));
			if (interact == nullptr) {
				return kOfxStatReplyDefault;
			}
			return interact->dispatch(action, in, out);
		}
		default:
			return kOfxStatReplyDefault;
		}
	}

	struct ImageHolder {
		const Suites* suites{nullptr};
		OfxPropertySetHandle handle{nullptr};
		~ImageHolder()
		{
			if (handle != nullptr && suites != nullptr) {
				suites->imageEffect->clipReleaseImage(handle);
			}
		}
	};

	static OfxStatus statusOf(const Error& e) noexcept { return e.ok() || e.isCancellation() ? kOfxStatOK : errorToStatus(e); }

	Effect* effectFor(const void* handle)
	{
		auto effectHandle = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
		OfxPropertySetHandle props = nullptr;
		if (effectHandle == nullptr || suites_.imageEffect->getPropertySet(effectHandle, &props) != kOfxStatOK) {
			return nullptr;
		}
		PropertySet p(suites_.property, props);
		return static_cast<Effect*>(p.getPointer(kOfxPropInstanceData).value_or(nullptr));
	}

	OfxStatus dispatch(Action action, const char* actionName, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
	{
		PropertySet in(suites_.property, inArgs);
		PropertySet out(suites_.property, outArgs);
		switch (action) {
		case Action::Load:
			return load();
		case Action::Unload:
			return unload();
		case Action::Describe:
			return describe(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
		case Action::DescribeInContext:
			return describeInContext(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in);
		case Action::CreateInstance:
			return createInstance(static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in);
		default:
			break;
		}

		Effect* effect = effectFor(handle);
		if (effect == nullptr) {
			if (action == Action::Unknown) {
				logger_.debug(std::string("Unknown action without instance: ") + (actionName ? actionName : "?"));
				return kOfxStatReplyDefault;
			}
			logger_.warning(std::string("Action ") + actionString(action) + " arrived without an instance");
			return kOfxStatErrBadHandle;
		}
		OfxHostServices& services = effect->ofxServices();

		switch (action) {
		case Action::DestroyInstance:
			return destroyInstance(effect);
		case Action::InstanceChanged:
			return instanceChanged(*effect, in);
		case Action::BeginInstanceChanged:
		case Action::EndInstanceChanged: {
			OfxHostServices::PhaseScope scope(services, Phase::InstanceChanged);
			PlainEvent event(action == Action::BeginInstanceChanged ? EventId::BeginInstanceChanged : EventId::EndInstanceChanged);
			return statusOf(effect->bus().post(event));
		}
		case Action::SyncPrivateData: {
			OfxHostServices::PhaseScope scope(services, Phase::SyncPrivateData);
			PlainEvent event(EventId::SyncPrivateData);
			effect->bus().post(event);
			return statusOf(services.persistence().commit());
		}
		case Action::PurgeCaches: {
			PlainEvent event(EventId::PurgeCaches);
			return statusOf(effect->bus().post(event));
		}
		case Action::GetRegionOfDefinition:
			return regionOfDefinition(*effect, in, out);
		case Action::GetRegionsOfInterest:
			return regionsOfInterest(*effect, in, out);
		case Action::GetClipPreferences:
			return clipPreferences(*effect, out);
		case Action::IsIdentity:
			return isIdentity(*effect, in, out);
		case Action::BeginSequenceRender:
		case Action::EndSequenceRender: {
			OfxHostServices::PhaseScope scope(services, Phase::Render);
			const SequenceRenderArgs args = readSequenceRenderArgs(in);
			const bool begin = action == Action::BeginSequenceRender;
			Error e = begin ? effect->onBeginSequenceRender(args) : effect->onEndSequenceRender(args);
			PlainEvent event(begin ? EventId::BeginSequenceRender : EventId::EndSequenceRender);
			effect->bus().post(event);
			return statusOf(e);
		}
		case Action::Render:
			return render(*effect, in);
		case Action::GetFramesNeeded:
		case Action::GetTimeDomain:
		case Action::BeginInstanceEdit:
		case Action::EndInstanceEdit:
		case Action::GetOutputColourspace:
		case Action::OpenGLContextAttached:
		case Action::OpenGLContextDetached:
		case Action::InteractDraw:
		case Action::InteractPenMotion:
		case Action::InteractPenDown:
		case Action::InteractPenUp:
		case Action::InteractKeyDown:
		case Action::InteractKeyUp:
		case Action::InteractKeyRepeat:
		case Action::InteractGainFocus:
		case Action::InteractLoseFocus:
		case Action::Dialog:
			return kOfxStatReplyDefault;
		default:
			logger_.debug(std::string("Unhandled action: ") + (actionName ? actionName : "?"));
			return kOfxStatReplyDefault;
		}
	}

	OfxStatus load()
	{
		if (host_ == nullptr) {
			logger_.error("Load arrived before setHost");
			return kOfxStatErrMissingHostFeature;
		}
		if (Error e = suites_.fetch(host_)) {
			logger_.error("Load failed: " + e.describe());
			return kOfxStatErrMissingHostFeature;
		}
		buildProfile();
		logger_.info("Loaded " + identifier_ + " " + manifest_.version.toString() + " in host\n" + profile_.describe());
		factory_->onLoad(profile_, logger_);
		loaded_ = true;
		return kOfxStatOK;
	}

	OfxStatus unload()
	{
		if (loaded_) {
			factory_->onUnload();
		}
		loaded_ = false;
		return kOfxStatOK;
	}

	void buildProfile()
	{
		profile_ = HostProfile{};
		PropertySet hostProps(suites_.property, host_->host);
		profile_.hostName = hostProps.getString(kOfxPropName, std::string("unknown"), 0);
		profile_.hostLabel = hostProps.getString(kOfxPropLabel, std::string(), 0);
		const std::vector<int> version = hostProps.getInts(kOfxPropVersion);
		profile_.hostVersion = Version{version.size() > 0 ? static_cast<unsigned>(version[0]) : 0u, version.size() > 1 ? static_cast<unsigned>(version[1]) : 0u,
		                               version.size() > 2 ? static_cast<unsigned>(version[2]) : 0u};
		profile_.hostVersionLabel = hostProps.getString(kOfxPropVersionLabel, std::string(), 0);
		const std::vector<int> api = hostProps.getInts(kOfxPropAPIVersion);
		profile_.apiVersionMajor = api.size() > 0 ? api[0] : 1;
		profile_.apiVersionMinor = api.size() > 1 ? api[1] : 0;
		profile_.suites = suites_.availability();
		HostCapabilities& c = profile_.capabilities;
		c.supportsTiles = hostProps.getInt(kOfxImageEffectPropSupportsTiles, 1, 0) != 0;
		c.supportsMultiResolution = hostProps.getInt(kOfxImageEffectPropSupportsMultiResolution, 1, 0) != 0;
		c.supportsMultipleClipDepths = hostProps.getInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0) != 0;
		c.supportsMultipleClipPARs = hostProps.getInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0) != 0;
		c.supportsOverlays = hostProps.getInt(kOfxImageEffectPropSupportsOverlays, 1, 0) != 0;
		c.supportsCustomInteract = hostProps.getInt(kOfxParamHostPropSupportsCustomInteract, 0, 0) != 0;
		c.supportsChoiceAnimation = hostProps.getInt(kOfxParamHostPropSupportsChoiceAnimation, 0, 0) != 0;
		c.supportsBooleanAnimation = hostProps.getInt(kOfxParamHostPropSupportsBooleanAnimation, 0, 0) != 0;
		c.supportsStringAnimation = hostProps.getInt(kOfxParamHostPropSupportsStringAnimation, 0, 0) != 0;
		c.supportsCustomAnimation = hostProps.getInt(kOfxParamHostPropSupportsCustomAnimation, 0, 0) != 0;
		c.supportsStrChoice = hostProps.getInt(kOfxParamHostPropSupportsStrChoice, 0, 0) != 0;
		c.supportsStrChoiceAnimation = hostProps.getInt(kOfxParamHostPropSupportsStrChoiceAnimation, 0, 0) != 0;
		c.supportsParametricAnimation = hostProps.getInt(kOfxParamHostPropSupportsParametricAnimation, 0, 0) != 0;
		c.supportsSetableFrameRate = hostProps.getInt(kOfxImageEffectPropSetableFrameRate, 0, 0) != 0;
		c.supportsSetableFielding = hostProps.getInt(kOfxImageEffectPropSetableFielding, 0, 0) != 0;
		c.supportsRenderQualityDraft = hostProps.getInt(kOfxImageEffectPropRenderQualityDraft, 0, 0) != 0;
		c.temporalClipAccess = hostProps.getInt(kOfxImageEffectPropTemporalClipAccess, 0, 0) != 0;
		c.isBackground = hostProps.getInt(kOfxImageEffectHostPropIsBackground, 0, 0) != 0;
		c.maxParameters = hostProps.getInt(kOfxParamHostPropMaxParameters, -1, 0);
		c.maxPages = hostProps.getInt(kOfxParamHostPropMaxPages, 0, 0);
		c.metalRender = hostProps.getString(kOfxImageEffectPropMetalRenderSupported, std::string("false"), 0) == "true";
		c.cudaRender = hostProps.getString(kOfxImageEffectPropCudaRenderSupported, std::string("false"), 0) == "true";
		c.cudaStream = hostProps.getString(kOfxImageEffectPropCudaStreamSupported, std::string("false"), 0) == "true";
		c.openCLRender = hostProps.getString(kOfxImageEffectPropOpenCLRenderSupported, std::string("false"), 0) == "true";
		c.openGLRender = hostProps.getString(kOfxImageEffectPropOpenGLRenderSupported, std::string("false"), 0) == "true";
		c.nativeOrigin = hostProps.getString(kOfxImageEffectHostPropNativeOrigin, std::string(), 0);
		c.supportedContexts = hostProps.getStrings(kOfxImageEffectPropSupportedContexts);
		c.supportedComponents = hostProps.getStrings(kOfxImageEffectPropSupportedComponents);
		c.supportedPixelDepths = hostProps.getStrings(kOfxImageEffectPropSupportedPixelDepths);
		profile_.deriveIdentity();
	}

	OfxStatus describe(OfxImageEffectHandle descriptor)
	{
		if (!loaded_) {
			// Some hosts describe without a preceding load; fetch now.
			if (load() != kOfxStatOK) {
				return kOfxStatErrMissingHostFeature;
			}
		}
		if (Error e = manifest_.validate()) {
			logger_.error("Manifest is invalid: " + e.describe());
			return kOfxStatFailed;
		}
		OfxPropertySetHandle props = nullptr;
		if (suites_.imageEffect->getPropertySet(descriptor, &props) == kOfxStatOK) {
			bundlePath_ = PropertySet(suites_.property, props).getString(kOfxPluginPropFilePath, std::string(), 0);
		}
		localized_.reset();
		if (manifest_.property("internationalize", false)) {
			if (std::optional<StringTable> table = findStringTable(bundlePath_, preferredLanguages())) {
				localized_ = std::make_unique<EffectManifest>(manifest_);
				localizeManifest(*localized_, *table);
			}
		}
		LoweringReport report;
		if (Error e = lowerEffectDescriptor(describedManifest(), profile_, suites_, descriptor, report, interactEntry_)) {
			logger_.error("Describe failed: " + e.describe());
			return errorToStatus(e);
		}
		for (const std::string& name : report.rejectedProperties) {
			profile_.extensionPropertyAccepted[name] = false;
			logger_.debug("Descriptor property rejected by the host: " + name);
		}
		for (const std::string& warning : report.warnings) {
			logger_.warning(warning);
		}
		return kOfxStatOK;
	}

	OfxStatus describeInContext(OfxImageEffectHandle descriptor, const PropertySet& in)
	{
		EffectKind context = manifest_.identity.kind;
		if (!parseOfxContext(in.getString(kOfxImageEffectPropContext, std::string(), 0), context)) {
			logger_.warning("Describe-in-context with an unknown context; using the manifest kind");
		}
		LoweringReport report;
		LoweringContext lowering;
		lowering.bundlePath = bundlePath_;
		lowering.userSupportDirectory = defaultUserSupportDirectory(identifier_);
		lowering.debugAllowed = factory_->allowsDebugFeatures();
		if (Error e = lowerContextDescriptor(describedManifest(), profile_, suites_, descriptor, context, report, lowering)) {
			logger_.error("Describe-in-context failed: " + e.describe());
			return errorToStatus(e);
		}
		for (const std::string& omitted : report.omitted) {
			logger_.info("Parameter omitted: " + omitted);
		}
		for (const std::string& warning : report.warnings) {
			logger_.warning(warning);
		}
		for (const std::string& name : report.rejectedProperties) {
			profile_.extensionPropertyAccepted[name] = false;
		}
		return kOfxStatOK;
	}

	OfxStatus createInstance(OfxImageEffectHandle handle, const PropertySet& in)
	{
		std::unique_ptr<Effect> effect = factory_->create();
		if (!effect) {
			return kOfxStatErrMemory;
		}
		Identity identity;
		identity.pluginId = identifier_;
		identity.pluginUuid = manifest_.identity.uuid;
		identity.pluginVersion = manifest_.version;
		identity.instanceId = identifier_ + "#" + std::to_string(++instanceCounter_);
		identity.bundlePath = bundlePath_;
		auto services = std::make_unique<OfxHostServices>(suites_, profile_, manifest_, identity, effect->bus(), logger_, handle);
		services->setDebugFeaturesAllowed(factory_->allowsDebugFeatures());
		services->setLicensingProvider(factory_->licensingProvider());
		services->setPhase(Phase::CreateInstance);
		if (Error e = services->bind()) {
			logger_.error("Create instance failed: " + e.describe());
			return errorToStatus(e);
		}
		const CreateInstanceArgs args = readCreateInstanceArgs(in);
		effect->attach(std::move(services), args);
		OfxPropertySetHandle props = nullptr;
		if (suites_.imageEffect->getPropertySet(handle, &props) != kOfxStatOK) {
			return kOfxStatErrBadHandle;
		}
		if (Error e = PropertySet(suites_.property, props).setPointer(kOfxPropInstanceData, effect.get())) {
			logger_.error("Cannot store the instance pointer: " + e.describe());
			return errorToStatus(e);
		}
		Effect* raw = effect.release();
		raw->loadExtensions(raw->extensions());
		PlainEvent created(EventId::CreateInstance);
		raw->bus().post(created);
		Error e = raw->onCreate();
		PlainEvent finished(EventId::FinishSetup);
		raw->bus().post(finished);
		PlainEvent added(EventId::AddedToDocument);
		raw->bus().post(added);
		raw->flush();
		raw->ofxServices().setPhase(Phase::None);
		if (e) {
			logger_.error("onCreate failed: " + e.describe());
		}
		return statusOf(e);
	}

	OfxStatus destroyInstance(Effect* effect)
	{
		{
			OfxHostServices::PhaseScope scope(effect->ofxServices(), Phase::Destroy);
			PlainEvent event(EventId::DestroyInstance);
			effect->bus().post(event, true);
			effect->extensions().unloadAll();
		}
		OfxPropertySetHandle props = nullptr;
		if (suites_.imageEffect->getPropertySet(effect->ofxServices().instanceHandle(), &props) == kOfxStatOK) {
			PropertySet(suites_.property, props).setPointer(kOfxPropInstanceData, nullptr);
		}
		delete effect;
		return kOfxStatOK;
	}

	OfxStatus instanceChanged(Effect& effect, const PropertySet& in)
	{
		OfxHostServices& services = effect.ofxServices();
		OfxHostServices::PhaseScope scope(services, Phase::InstanceChanged);
		const InstanceChangedArgs args = readInstanceChangedArgs(in);
		ParameterId id = kParameterIdNone;
		const ParameterSpec* spec = nullptr;
		if (!args.isClip) {
			id = services.parameters().idForKey(args.name);
			if (id == kParameterIdNone) {
				// Q-RESOLVE-035: hosts report names the plug-in never declared.
				logger_.debug("Change for an unknown name ignored: " + args.name);
				return kOfxStatOK;
			}
			spec = services.parameters().spec(id);
		}
		ChangeEvent event(EventId::InstanceChanged, id, args.name, args.time, args.reason);
		event.renderScale = args.renderScale;
		Error e = effect.bus().post(event);
		if (!e) {
			e = effect.onInstanceChanged(event);
		}
		if (!e && spec != nullptr && (spec->type == ParameterType::PushButton || spec->type == ParameterType::Analyzer || spec->type == ParameterType::Help)) {
			if (spec->type == ParameterType::Analyzer) {
				// An analyzer control starts the pass over the source clip, as FxGrip's does.
				const AnalysisResult result = effect.runAnalysis(AnalysisRequest{});
				if (result.error) {
					logger_.warning("Analysis stopped: " + result.error.describe());
				}
			}
			ChangeEvent clicked(EventId::ParameterClicked, id, args.name, args.time, args.reason);
			e = effect.bus().post(clicked);
			if (!e) {
				e = effect.onParameterClicked(id);
			}
		}
		Error flushed = effect.flush();
		return statusOf(e ? e : flushed);
	}

	RenderInfo renderInfo(Effect& effect, Time time, Point2D renderScale, Field field, const RectI& window, bool draft, bool interactive, bool sequential)
	{
		OfxHostServices& services = effect.ofxServices();
		RenderInfo info;
		info.time = time;
		info.frameRate = services.time().frameRate();
		info.renderScale = renderScale;
		info.field = field;
		info.draft = draft;
		info.interactive = interactive;
		info.sequential = sequential;
		info.renderWindow = window;
		info.projectSize = services.time().projectSize();
		info.projectOffset = services.time().projectOffset();
		info.pixelAspect = services.time().projectPixelAspect();
		info.resolvePage = services.resolvePage();
		return info;
	}

	OfxStatus regionOfDefinition(Effect& effect, const PropertySet& in, const PropertySet& out)
	{
		OfxHostServices::PhaseScope scope(effect.ofxServices(), Phase::Geometry);
		const RegionArgs args = readRegionArgs(in, false);
		const RenderSnapshot snapshot = effect.buildSnapshot(renderInfo(effect, args.time, args.renderScale, Field::None, {}, false, false, false));
		RectD rod;
		bool handled = false;
		if (Error e = effect.onRegionOfDefinition(args, snapshot, rod, handled)) {
			return statusOf(e);
		}
		if (!handled) {
			return kOfxStatReplyDefault;
		}
		if (Error e = out.setDoubles(kOfxImageEffectPropRegionOfDefinition, {rod.x1, rod.y1, rod.x2, rod.y2})) {
			return statusOf(e);
		}
		return kOfxStatOK;
	}

	OfxStatus regionsOfInterest(Effect& effect, const PropertySet& in, const PropertySet& out)
	{
		OfxHostServices::PhaseScope scope(effect.ofxServices(), Phase::Geometry);
		const RegionArgs args = readRegionArgs(in, true);
		const RenderSnapshot snapshot = effect.buildSnapshot(renderInfo(effect, args.time, args.renderScale, Field::None, {}, false, false, false));
		std::map<std::string, RectD> regions;
		bool handled = false;
		if (Error e = effect.onRegionsOfInterest(args, snapshot, regions, handled)) {
			return statusOf(e);
		}
		if (!handled) {
			return kOfxStatReplyDefault;
		}
		for (const auto& [clip, rect] : regions) {
			const std::string name = "OfxImageClipPropRoI_" + clip;
			out.setDoubles(name.c_str(), {rect.x1, rect.y1, rect.x2, rect.y2});
		}
		return kOfxStatOK;
	}

	OfxStatus clipPreferences(Effect& effect, const PropertySet& out)
	{
		OfxHostServices::PhaseScope scope(effect.ofxServices(), Phase::ClipPreferences);
		ClipPreferences prefs;
		bool handled = false;
		if (Error e = effect.onClipPreferences(prefs, handled)) {
			return statusOf(e);
		}
		if (!handled) {
			return kOfxStatReplyDefault;
		}
		for (const auto& [clip, components] : prefs.components) {
			out.setString(("OfxImageClipPropComponents_" + clip).c_str(), ofxComponentsName(components));
		}
		for (const auto& [clip, depth] : prefs.depths) {
			out.setString(("OfxImageClipPropDepth_" + clip).c_str(), ofxBitDepthName(depth));
		}
		for (const auto& [clip, par] : prefs.pixelAspects) {
			out.setDouble(("OfxImageClipPropPAR_" + clip).c_str(), par);
		}
		if (prefs.frameRate) {
			out.setDouble(kOfxImageEffectPropFrameRate, *prefs.frameRate);
		}
		if (prefs.fieldOrder) {
			out.setString(kOfxImageClipPropFieldOrder, ofxFieldName(*prefs.fieldOrder));
		}
		if (prefs.premultiplication) {
			out.setString(kOfxImageEffectPropPreMultiplication, ofxPremultiplicationName(*prefs.premultiplication));
		}
		out.setInt(kOfxImageClipPropContinuousSamples, prefs.continuousSamples ? 1 : 0);
		out.setInt(kOfxImageEffectFrameVarying, prefs.frameVarying ? 1 : 0);
		return kOfxStatOK;
	}

	OfxStatus isIdentity(Effect& effect, const PropertySet& in, const PropertySet& out)
	{
		OfxHostServices::PhaseScope scope(effect.ofxServices(), Phase::Geometry);
		const IsIdentityArgs args = readIsIdentityArgs(in);
		const RenderSnapshot snapshot = effect.buildSnapshot(renderInfo(effect, args.time, args.renderScale, args.field, args.renderWindow, false, false, false));
		std::string clip;
		Time time = args.time;
		bool identity = false;
		if (Error e = effect.onIsIdentity(args, snapshot, clip, time, identity)) {
			return statusOf(e);
		}
		if (!identity) {
			return kOfxStatReplyDefault;
		}
		out.setString(kOfxPropName, clip.empty() ? std::string(kOfxImageEffectSimpleSourceClipName) : clip);
		out.setDouble(kOfxPropTime, time);
		return kOfxStatOK;
	}

	bool readImage(OfxPropertySetHandle imageHandle, const RenderArgs& args, const std::string& page, InputImage& outInput, ImageView& outView, GpuImage& outGpu)
	{
		PropertySet props(suites_.property, imageHandle);
		ImageDesc desc;
		const std::vector<int> bounds = props.getInts(kOfxImagePropBounds);
		const std::vector<int> rod = props.getInts(kOfxImagePropRegionOfDefinition);
		if (bounds.size() == 4) {
			desc.bounds = RectI{bounds[0], bounds[1], bounds[2], bounds[3]};
		}
		desc.regionOfDefinition = rod.size() == 4 ? RectI{rod[0], rod[1], rod[2], rod[3]} : desc.bounds;
		parseOfxBitDepth(props.getString(kOfxImageEffectPropPixelDepth, std::string(kOfxBitDepthFloat), 0), desc.depth);
		parseOfxComponents(props.getString(kOfxImageEffectPropComponents, std::string(kOfxImageComponentRGBA), 0), desc.components);
		parseOfxPremultiplication(props.getString(kOfxImageEffectPropPreMultiplication, std::string(kOfxImagePreMultiplied), 0), desc.premultiplication);
		const double par = props.getDouble(kOfxImagePropPixelAspectRatio, 1.0, 0);
		desc.pixelAspect = par > 0.0 ? par : 1.0;  // Q-OFX-ANY-017
		desc.field = args.field;                   // Q-OFX-ANY-016: never from the image.
		desc.renderScale = args.renderScale;
		void* data = props.getPointer(kOfxImagePropData).value_or(nullptr);
		const int rowBytes = props.getInt(kOfxImagePropRowBytes, 0, 0);
		if (args.gpu != GpuApi::None) {
			outGpu.api = args.gpu;
			outGpu.queue = args.gpuQueue;
			outGpu.handle = data;
			outGpu.desc = desc;
			outGpu.rowBytes = rowBytes;
			outGpu.needsIntermediateCopy = profile_.has(Quirk::FusionPageMetalNotInPlace) && page == resolve::kPageFusion;
			outView = ImageView{};
			outView.desc = desc;
		} else {
			outView.desc = desc;
			outView.data = static_cast<std::uint8_t*>(data);
			outView.rowBytes = rowBytes != 0 ? rowBytes : static_cast<std::ptrdiff_t>(desc.minimumRowBytes());
			outGpu = GpuImage{};
		}
		(void)outInput;
		return data != nullptr;
	}

	OfxStatus render(Effect& effect, const PropertySet& in)
	{
		OfxHostServices& services = effect.ofxServices();
		OfxHostServices::PhaseScope scope(services, Phase::Render);
		const RenderArgs args = readRenderArgs(in);
		const RenderSnapshot snapshot = effect.buildSnapshot(renderInfo(effect, args.time, args.renderScale, args.field, args.renderWindow, args.draft, args.interactive, args.sequentialStatus));

		OfxImageClipHandle outputClip = services.clipHandle(kOfxImageEffectOutputClipName);
		if (outputClip == nullptr) {
			logger_.error("Render without an output clip");
			return kOfxStatErrBadHandle;
		}
		ImageHolder outputHolder{&suites_, nullptr};
		if (suites_.imageEffect->clipGetImage(outputClip, args.time, nullptr, &outputHolder.handle) != kOfxStatOK || outputHolder.handle == nullptr) {
			logger_.error("The host supplied no output image");
			return kOfxStatFailed;
		}
		RenderContext context(snapshot, args, services);
		InputImage unused;
		if (!readImage(outputHolder.handle, args, services.resolvePage(), unused, context.output, context.outputGpu)) {
			logger_.error("The output image has no data pointer");
			return kOfxStatFailed;
		}
		bool depthSupported = false;
		for (BitDepth d : manifest_.capabilities.depths) {
			depthSupported = depthSupported || d == context.output.desc.depth;
		}
		if (!depthSupported) {
			// Q-OFX-ANY-003: hosts may ignore clip preferences; validate in every build.
			logger_.error("The host delivered an unsupported bit depth: " + std::string(toString(context.output.desc.depth)));
			return kOfxStatErrFormat;
		}

		std::vector<ImageHolder> inputHolders;
		inputHolders.reserve(manifest_.effectiveClips().size());
		for (const ClipSpec& clip : manifest_.effectiveClips()) {
			InputImage input;
			input.clip = clip.name;
			OfxImageClipHandle handle = services.clipHandle(clip.name);
			const bool reportsConnected = handle != nullptr && services.clipProperties(clip.name).getInt(kOfxImageClipPropConnected, 1, 0) != 0;
			if (handle != nullptr && reportsConnected) {
				inputHolders.push_back(ImageHolder{&suites_, nullptr});
				ImageHolder& holder = inputHolders.back();
				if (suites_.imageEffect->clipGetImage(handle, args.time, nullptr, &holder.handle) == kOfxStatOK && holder.handle != nullptr) {
					// Q-RESOLVE-002: a connected clip may still return no data; that means unconnected.
					input.connected = readImage(holder.handle, args, services.resolvePage(), input, input.view, input.gpu);
				}
			}
			context.inputs.push_back(std::move(input));
		}

		Error e = effect.render(context);
		if (e && !e.isCancellation()) {
			logger_.error("Render failed: " + e.describe());
			return errorToStatus(e);
		}
		if (LicensingExtension* licensing = effect.licensingExtension(); licensing != nullptr && !e) {
			if (std::optional<WatermarkConfiguration> mark = licensing->watermarkFor(snapshot.license())) {
				if (Error w = applyRenderWatermark(context, *mark)) {
					logger_.error("Watermark failed: " + w.describe());
					return errorToStatus(w);
				}
			}
		}
		PlainEvent event(EventId::Render);
		effect.bus().post(event);
		return kOfxStatOK;
	}

	/*! The manifest the host sees at describe: the authored one, or its localized copy. */
	const EffectManifest& describedManifest() const noexcept { return localized_ ? *localized_ : manifest_; }

	std::shared_ptr<EffectFactory> factory_;
	EffectManifest manifest_;
	std::unique_ptr<EffectManifest> localized_;
	std::string identifier_;
	FileLogger logger_;
	OfxPlugin plugin_{};
	OfxPluginEntryPoint* interactEntry_{nullptr};
	OfxHost* host_{nullptr};
	Suites suites_;
	HostProfile profile_;
	bool loaded_{false};
	std::string bundlePath_;
	std::atomic<unsigned> instanceCounter_{0};
};

// ---- PluginRegistry -----------------------------------------------------------------------------

PluginRegistry& PluginRegistry::instance()
{
	static PluginRegistry registry;
	return registry;
}

PluginRegistry::PluginRegistry() = default;
PluginRegistry::~PluginRegistry() = default;

int PluginRegistry::registerFactory(std::unique_ptr<EffectFactory> factory)
{
	if (!factory) {
		return -1;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	const EffectManifest& manifest = factory->manifest();
	std::vector<unsigned> majors{0};
	for (unsigned major : manifest.deprecatedMajors) {
		if (major != 0 && major < manifest.version.major) {
			majors.push_back(major);
		}
	}
	if (static_cast<int>(slots_.size() + majors.size()) > kMaxPluginsPerBundle) {
		return -1;
	}
	const std::string id = manifest.identity.ofxId;
	for (const auto& slot : slots_) {
		if (slot->identifier() == id) {
			return -1;
		}
	}
	std::shared_ptr<EffectFactory> shared(std::move(factory));
	const int index = static_cast<int>(slots_.size());
	for (unsigned major : majors) {
		slots_.push_back(std::make_unique<PluginSlot>(static_cast<int>(slots_.size()), shared, major));
	}
	return index;
}

int PluginRegistry::count() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return static_cast<int>(slots_.size());
}

OfxPlugin* PluginRegistry::plugin(int index)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (index < 0 || index >= static_cast<int>(slots_.size())) {
		return nullptr;
	}
	return slots_[static_cast<std::size_t>(index)]->plugin();
}

void PluginRegistry::setGlobalHost(const OfxHost* host)
{
	std::lock_guard<std::mutex> lock(mutex_);
	globalHost_ = host;
}

void PluginRegistry::ensureRegistered(void (*registerFunction)())
{
	bool run = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!registered_) {
			registered_ = true;
			run = true;
		}
	}
	if (run && registerFunction != nullptr) {
		registerFunction();
	}
}

void PluginRegistry::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	slots_.clear();
	registered_ = false;
	globalHost_ = nullptr;
}

void PluginRegistry::setHost(int index, OfxHost* host)
{
	PluginSlot* slot = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < static_cast<int>(slots_.size())) {
			slot = slots_[static_cast<std::size_t>(index)].get();
		}
	}
	if (slot != nullptr) {
		slot->setHost(host);
	}
}

OfxStatus PluginRegistry::interactEntry(int index, const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	PluginSlot* slot = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < static_cast<int>(slots_.size())) {
			slot = slots_[static_cast<std::size_t>(index)].get();
		}
	}
	if (slot == nullptr) {
		return kOfxStatErrBadHandle;
	}
	return slot->interactEntry(action, handle, inArgs, outArgs);
}

OfxStatus PluginRegistry::mainEntry(int index, const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	PluginSlot* slot = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= 0 && index < static_cast<int>(slots_.size())) {
			slot = slots_[static_cast<std::size_t>(index)].get();
		}
	}
	if (slot == nullptr) {
		return kOfxStatErrBadHandle;
	}
	return slot->mainEntry(action, handle, inArgs, outArgs);
}

OFXGRIP_NAMESPACE_END

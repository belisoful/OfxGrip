// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MiniHost.hpp
	@brief The headless OpenFX host: loads real plug-ins, runs the real action order, tiles,
	       varies render scale and bit depth, renders concurrently, and imitates a named host's
	       quirk profile.

	Introduced in OfxGrip 0.1.0. The mini host implements D-004. A `HostOptions` chooses what the
	host description says, which suites it vends, and which deviations it imitates
	(clips that always report connected, a stub thread suite, a broken message suite V2, images
	whose render scale always reads 1). `MiniHost` drives a plug-in through load and describe;
	`Instance` drives one instance through changes and renders.
*/
#pragma once

#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/minihost/ParamImpl.hpp>
#include <ofxgrip/minihost/PropertySetImpl.hpp>

#include "ofxCore.h"
#include "ofxImageEffect.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN
namespace minihost {

/*! What the host claims and how it misbehaves. */
struct HostOptions {
	std::string name{"com.belisoful.ofxgrip.minihost"};
	std::string label{"OfxGrip Mini Host"};
	std::vector<int> version{0, 1, 0};
	std::string versionLabel{"0.1.0"};
	int apiMajor{1};
	int apiMinor{4};
	bool supportsTiles{true};
	bool supportsMultiResolution{true};
	bool supportsMultipleClipDepths{false};
	bool supportsMultipleClipPARs{false};
	bool supportsOverlays{true};
	bool supportsCustomInteract{false};
	bool supportsChoiceAnimation{true};
	bool supportsBooleanAnimation{true};
	bool supportsStringAnimation{false};
	bool supportsCustomAnimation{false};
	bool supportsStrChoice{false};
	bool supportsParametric{false};
	int maxParameters{-1};
	int maxPages{0};
	std::vector<std::string> contexts{"OfxImageEffectContextFilter", "OfxImageEffectContextGeneral", "OfxImageEffectContextGenerator", "OfxImageEffectContextTransition"};
	std::vector<std::string> components{"OfxImageComponentRGBA", "OfxImageComponentAlpha"};
	std::vector<std::string> depths{"OfxBitDepthByte", "OfxBitDepthShort", "OfxBitDepthHalf", "OfxBitDepthFloat"};
	std::string nativeOrigin{"kOfxImageEffectHostPropNativeOriginBottomLeft"};  /*!< Empty withholds the property. */
	bool metalRenderSupported{false};          /*!< The host description's Metal flag. */
	std::set<std::string> withheldSuites;      /*!< Suite names (with `:version` for V2) the host does not vend. */
	std::set<std::string> withheldHostProps;   /*!< Host description properties the host lacks. */
	bool clipsAlwaysConnected{false};          /*!< Q-RESOLVE-002 */
	bool threadSuiteDummy{false};              /*!< Q-RESOLVE-003: `multiThread` runs the function once. */
	bool messageV2Broken{false};               /*!< Q-RESOLVE-007: V2 is vended with null persistent-message pointers. */
	bool imageScaleAlwaysOne{false};           /*!< Q-RESOLVE-010 */
	bool nonConformantInstanceType{false};     /*!< Q-RESOLVE-014 */
	bool paramCopyNoOp{false};                 /*!< Q-RESOLVE-006 */
	bool renderDespiteIdentity{false};         /*!< Q-RESOLVE-027 */
	bool noPluginHandleOnInstance{false};      /*!< Q-RESOLVE-014 */

	/*! A spec-conformant host. */
	static HostOptions spec();
	/*! What the registry records for DaVinci Resolve (free), to be refined by the live probes. */
	static HostOptions resolve21();
	/*! What the registry records for Nuke. */
	static HostOptions nuke();
	/*! What the registry records for Natron. */
	static HostOptions natron();
};

/*! The project an instance lives in. */
struct Project {
	int width{64};
	int height{64};
	double pixelAspect{1.0};
	double frameRate{24.0};
	Time firstFrame{0};
	Time lastFrame{0};
};

/*! One render. An empty window means the whole project at the render scale. A tile size of 0
    renders the window as one tile. */
struct RenderRequest {
	Time time{0};
	Point2D renderScale{1, 1};
	RectI window;
	Field field{Field::None};
	BitDepth depth{BitDepth::Float};
	Components components{Components::RGBA};
	Premultiplication premultiplication{Premultiplication::Premultiplied};
	int tileSize{0};
	unsigned threads{1};
	bool draft{false};
	bool interactive{false};
	/*! The GPU API to hand frames in. `Metal` uploads the inputs to `MTLBuffer`s, passes the
	    host's own command queue, and reads the output buffer back; it falls back to CPU frames
	    when the host options or the plug-in descriptor lack Metal, or no device exists. */
	GpuApi gpu{GpuApi::None};
};

/*! What a render produced. */
struct RenderResult {
	ImageBuffer image;
	bool wasIdentity{false};
	std::string identityClip;
	Time identityTime{0};
	int tileCount{0};
	std::vector<OfxStatus> tileStatuses;
	OfxStatus status{kOfxStatOK};
	GpuApi gpu{GpuApi::None};  /*!< The API the frames were handed in. */
	bool ok() const noexcept
	{
		if (status != kOfxStatOK) {
			return false;
		}
		for (OfxStatus s : tileStatuses) {
			if (s != kOfxStatOK) {
				return false;
			}
		}
		return true;
	}
};

class MiniHost;
class EffectObject;
class Clip;

/*! One effect instance. */
class Instance {
public:
	~Instance();
	Instance(const Instance&) = delete;
	Instance& operator=(const Instance&) = delete;

	OfxStatus instanceChanged(const std::string& paramName, Time time = 0, const char* reason = kOfxChangeUserEdited);
	OfxStatus clipChanged(const std::string& clipName, Time time = 0);
	OfxStatus syncPrivateData();
	OfxStatus purgeCaches();
	OfxStatus beginInstanceEdit();
	OfxStatus endInstanceEdit();

	Param* param(const std::string& name) const;
	std::vector<std::string> paramNames() const;
	/*! Writes a numeric value through the host's own parameter store, bypassing the plug-in. */
	void setParam(const std::string& name, const std::vector<double>& value, Time time = 0, bool keyframe = false);
	void setStringParam(const std::string& name, const std::string& value);

	/*! Connects an input clip to an image. */
	void setInput(const std::string& clip, std::shared_ptr<const ImageBuffer> image);
	void disconnect(const std::string& clip);
	bool isConnected(const std::string& clip) const;

	OfxStatus clipPreferences();
	/*! The region of definition the plug-in answers, or the default (the union of connected
	    inputs, else the project) when it replies default. */
	OfxStatus regionOfDefinition(Time time, Point2D renderScale, RectD& out);
	/*! Asks is-identity. Returns true when the plug-in answers identity. */
	bool isIdentity(Time time, Point2D renderScale, const RectI& window, std::string& clip, Time& identityTime);
	/*! Runs clip preferences, RoD, is-identity, and the sequence-render actions around tiled,
	    threaded render actions. */
	RenderResult render(const RenderRequest& request);

	PropSet& props();
	/*! The handle the plug-in sees for this instance. */
	OfxImageEffectHandle effectHandle() const noexcept;
	EffectObject& object() noexcept { return *object_; }
	MiniHost& host() noexcept { return host_; }
	const std::string& context() const noexcept { return context_; }
	const Project& project() const noexcept { return project_; }
	void setCurrentTime(Time t) noexcept { currentTime_ = t; }
	Time currentTime() const noexcept { return currentTime_; }

	/*! Messages the plug-in posted through the message suite. */
	std::vector<std::string> messages() const;
	std::string persistentMessage() const;
	std::vector<double> progressUpdates() const;
	std::atomic<bool> abortFlag{false};

private:
	friend class MiniHost;
	friend struct HostSuites;
	Instance(MiniHost& host, std::unique_ptr<EffectObject> object, std::string context, Project project);
	OfxStatus dispatch(const char* action, PropSet* inArgs, PropSet* outArgs);

	MiniHost& host_;
	std::unique_ptr<EffectObject> object_;
	std::string context_;
	Project project_;
	Time currentTime_{0};
	mutable std::mutex mutex_;
	std::vector<std::string> messages_;
	std::string persistentMessage_;
	std::vector<double> progress_;
};

/*! The host. */
class MiniHost {
public:
	explicit MiniHost(HostOptions options = HostOptions::spec());
	~MiniHost();
	MiniHost(const MiniHost&) = delete;
	MiniHost& operator=(const MiniHost&) = delete;

	const HostOptions& options() const noexcept { return options_; }
	OfxHost* ofxHost() noexcept { return &ofxHost_; }
	PropSet& hostProps() noexcept { return hostProps_; }

	/*! Calls `setHost` and the load action. */
	OfxStatus load(OfxPlugin* plugin, const std::string& bundlePath = {});
	/*! Runs describe, then describe-in-context for every context the plug-in and the host share. */
	OfxStatus describe();
	OfxStatus unload();

	OfxPlugin* plugin() const noexcept { return plugin_; }
	/*! The effect descriptor's properties after describe, or null. */
	PropSet* descriptorProps() const;
	/*! The described contexts in order. */
	std::vector<std::string> describedContexts() const;
	/*! The parameters a context described, or null. */
	const ParamSet* contextParams(const std::string& context) const;
	/*! The clips a context described, in order. */
	std::vector<std::string> contextClips(const std::string& context) const;

	/*! Creates an instance: instantiates the context's parameters, runs `restore` over them so a
	    test can imitate a host restoring a saved document, then dispatches the create action. */
	std::unique_ptr<Instance> createInstance(const std::string& context, const Project& project, const std::string& resolvePage = {}, const std::function<void(ParamSet&)>& restore = {});

	/*! Every action dispatched, with the handle kind, in order. */
	std::vector<std::string> actionLog() const;
	void logAction(const std::string& line);
	void clearActionLog();

private:
	friend class Instance;
	friend struct HostSuites;
	OfxStatus dispatch(const char* action, const void* handle, PropSet* inArgs, PropSet* outArgs);
	void fillHostProps();

	HostOptions options_;
	OfxHost ofxHost_{};
	PropSet hostProps_;
	OfxPlugin* plugin_{nullptr};
	std::unique_ptr<EffectObject> descriptor_;
	std::map<std::string, std::unique_ptr<EffectObject>> contextDescriptors_;
	std::vector<std::string> describedContexts_;
	mutable std::mutex logMutex_;
	std::vector<std::string> actionLog_;
	bool loaded_{false};
};

/*! Reads a parameter's current numeric value from an instance, for tests. */
std::vector<double> numericValue(const Instance& instance, const std::string& name, Time time = 0);
/*! Reads a parameter's current string value from an instance, for tests. */
std::string stringValue(const Instance& instance, const std::string& name, Time time = 0);
/*! Makes a test image: a horizontal red ramp, a vertical green ramp, constant blue, opaque. */
std::shared_ptr<ImageBuffer> makeRampImage(int width, int height, BitDepth depth, Components components = Components::RGBA);

}  // namespace minihost
OFXGRIP_NAMESPACE_END

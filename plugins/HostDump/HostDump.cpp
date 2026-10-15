// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  HostDump.cpp
	@brief The probe plug-in: records what a host really does, for the quirks registry.

	HostDump is a pass-through filter. At load it logs the host profile (identity, suites,
	capabilities, active quirks, and the extension properties the host rejected). On every
	instance it logs the instance properties, every action it receives with its arguments, every
	parameter change by name, and the image layout of each render. The log lands in the OfxGrip
	log directory, which on macOS is `~/Library/Logs/OfxGrip/com.belisoful.ofxgrip.hostdump.log`.
	Registry step 1 of the verification plan runs this plug-in on Resolve 21.1.1 free.
*/
#include <ofxgrip/ofx/Plugin.hpp>

#if defined(OFXGRIP_HAS_METAL)
#include <ofxgrip/gpu/MetalBackend.hpp>
#endif
#include <ofxgrip/ofx/ResolveExtensions.hpp>

#include "ofxImageEffect.h"

#include <sstream>

namespace {

using namespace ofxgrip;

enum : ParameterId {
	kProbeDouble = 100,
	kProbeDoubleRanged = 101,
	kProbeInt = 102,
	kProbeIntRanged = 103,
	kProbeBool = 104,
	kProbeChoice = 105,
	kProbeString = 106,
	kProbePoint = 107,
	kProbeColor = 108,
	kProbeButton = 109,
	kProbeGroup = 110,
	kProbeChild = 111,
	kProbeStatus = 112,
};

EffectManifest makeManifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.hostdump";
	m.identity.uuid = "B1C2D3E4-F5A6-4B7C-8D9E-0F1A2B3C4D5E";
	m.identity.name = "OfxGrip Host Dump";
	m.identity.shortName = "Host Dump";
	m.identity.grouping = "OfxGrip";
	m.identity.description = "Records the host's capabilities and behavior to the OfxGrip log. Passes the image through.";
	m.identity.kind = EffectKind::Filter;
	m.identity.extraContexts = {EffectKind::General, EffectKind::Generator};
	m.version = Version{1, 2, 0};
	m.capabilities.depths = {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float};
#if defined(OFXGRIP_HAS_METAL)
	m.capabilities.metalRender = true;
#endif

	// One of each kind, with and without ranges, so Q-RESOLVE-004 and Q-RESOLVE-015 can be read
	// back from the instance.
	ParameterSpec unranged;
	unranged.id = kProbeDouble;
	unranged.key = "probeDouble";
	unranged.type = ParameterType::Float;
	unranged.label = "Probe double (no range)";
	unranged.defaultValue = 0.5;
	m.parameters.push_back(unranged);
	m.parameters.push_back(ParameterSpec::floatParam(kProbeDoubleRanged, "probeDoubleRanged", "Probe double (ranged)", 0.5, -1000.0, 1000.0));
	ParameterSpec unrangedInt;
	unrangedInt.id = kProbeInt;
	unrangedInt.key = "probeInt";
	unrangedInt.type = ParameterType::Int;
	unrangedInt.label = "Probe int (no range)";
	unrangedInt.defaultValue = 5;
	m.parameters.push_back(unrangedInt);
	m.parameters.push_back(ParameterSpec::intParam(kProbeIntRanged, "probeIntRanged", "Probe int (ranged)", 5, -1000, 1000));
	m.parameters.push_back(ParameterSpec::boolParam(kProbeBool, "probeBool", "Probe bool", false));
	m.parameters.push_back(ParameterSpec::menuParam(kProbeChoice, "probeChoice", "Probe choice", {{"a", "Option A"}, {"b", "Option B"}, {"c", "Option C"}}, 1));
	m.parameters.push_back(ParameterSpec::stringParam(kProbeString, "probeString", "Probe string", "hello"));
	m.parameters.push_back(ParameterSpec::pointParam(kProbePoint, "probePoint", "Probe point", {0.5, 0.5}));
	m.parameters.push_back(ParameterSpec::rgbaParam(kProbeColor, "probeColor", "Probe color", {1.0, 0.5, 0.25, 1.0}));
	m.parameters.push_back(ParameterSpec::buttonParam(kProbeButton, "probeButton", "Dump instance now", "dump"));
	m.parameters.push_back(ParameterSpec::groupParam(kProbeGroup, "probeGroup", "Probe group", {ParameterSpec::floatParam(kProbeChild, "probeChild", "Probe child", 0.0, 0.0, 1.0)}));
	ParameterSpec status = ParameterSpec::stringParam(kProbeStatus, "probeStatus", "Status", "not yet dumped");
	status.hostFlags = HostFlag::Disabled | HostFlag::NotAnimatable;
	status.type = ParameterType::Status;
	m.parameters.push_back(status);
	return m;
}

class HostDumpEffect final : public Effect {
public:
	Error onCreate() override
	{
		dumpInstance("create");
		return Error::none();
	}

	Error onInstanceChanged(const ChangeEvent& change) override
	{
		std::ostringstream out;
		out << "instanceChanged name=" << change.name << " id=" << change.parameter << " time=" << change.time << " reason=" << static_cast<int>(change.reason)
		    << " scale=" << change.renderScale.x << "," << change.renderScale.y << " phase=" << toString(services().phase());
		if (change.parameter != kParameterIdNone) {
			Value value;
			if (services().parameters().get(change.parameter, change.time, value).ok()) {
				out << " value=" << value.toString();
			}
			HostFlags flags;
			if (services().parameters().getFlags(change.parameter, flags).ok()) {
				out << " hidden=" << flags.has(HostFlag::Hidden) << " disabled=" << flags.has(HostFlag::Disabled);
			}
		}
		services().log().info(out.str());
		return Error::none();
	}

	Error onParameterClicked(ParameterId id) override
	{
		if (id == kProbeButton) {
			dumpInstance("button");
			services().parameters().setCurrent(kProbeStatus, Value("dumped; see the OfxGrip log"));
		}
		return Error::none();
	}

	Error onIsIdentity(const IsIdentityArgs& args, const RenderSnapshot&, std::string&, Time&, bool& identity) override
	{
		std::ostringstream out;
		out << "isIdentity time=" << args.time << " scale=" << args.renderScale.x << "," << args.renderScale.y << " window=" << args.renderWindow.x1 << "," << args.renderWindow.y1 << ","
		    << args.renderWindow.x2 << "," << args.renderWindow.y2 << " field=" << toString(args.field);
		services().log().info(out.str());
		identity = false;
		return Error::none();
	}

	Error render(RenderContext& context) override
	{
		std::ostringstream out;
		const ImageView& o = context.output;
		out << "render time=" << context.args.time << " scale=" << context.args.renderScale.x << "," << context.args.renderScale.y << " window=" << context.args.renderWindow.x1 << ","
		    << context.args.renderWindow.y1 << "," << context.args.renderWindow.x2 << "," << context.args.renderWindow.y2 << " field=" << toString(context.args.field)
		    << " draft=" << context.args.draft << " interactive=" << context.args.interactive << " sequential=" << context.args.sequentialStatus << " gpu=" << toString(context.args.gpu)
		    << " openGL=" << context.args.openGLEnabled << " srcFrame=" << context.args.resolveSourceFrame << " page=" << context.snapshot.info().resolvePage
		    << " | output bounds=" << o.desc.bounds.x1 << "," << o.desc.bounds.y1 << "," << o.desc.bounds.x2 << "," << o.desc.bounds.y2 << " rod=" << o.desc.regionOfDefinition.x1 << ","
		    << o.desc.regionOfDefinition.y1 << "," << o.desc.regionOfDefinition.x2 << "," << o.desc.regionOfDefinition.y2 << " depth=" << toString(o.desc.depth)
		    << " components=" << toString(o.desc.components) << " par=" << o.desc.pixelAspect << " rowBytes=" << o.rowBytes << " premult=" << static_cast<int>(o.desc.premultiplication);
		for (const InputImage& input : context.inputs) {
			out << " | input " << input.clip << " connected=" << input.connected;
			if (input.connected) {
				out << " bounds=" << input.view.desc.bounds.x1 << "," << input.view.desc.bounds.y1 << "," << input.view.desc.bounds.x2 << "," << input.view.desc.bounds.y2
				    << " depth=" << toString(input.view.desc.depth) << " rowBytes=" << input.view.rowBytes;
			}
		}
		if (!renderLogged_) {
			services().log().info(out.str());
			renderLogged_ = true;
		} else {
			services().log().debug(out.str());
		}
		if (context.outputIsGpu()) {
			return renderGpu(context);
		}
		const InputImage* source = context.source();
		if (source != nullptr && source->connected && source->view.desc.depth == o.desc.depth && source->view.desc.components == o.desc.components) {
			return context.forEachBand([&](const RectI& band) { o.subView(band).copyFrom(source->view); });
		}
		o.clear();
		return Error::none();
	}

private:
	/*! A GPU render: on Metal, logs the device and copies the source frame to the output with a
	    blit (or clears it), so the probe exercises the host's GPU path without a kernel. */
	Error renderGpu(RenderContext& context)
	{
#if defined(OFXGRIP_HAS_METAL)
		if (context.outputGpu.api == GpuApi::Metal) {
			std::shared_ptr<metal::MetalContext> backend = metal::MetalContext::forQueue(context.outputGpu.queue);
			if (!backend) {
				return Error::make(ErrorCode::MissingHostFeature, "The host passed no Metal command queue");
			}
			if (!gpuLogged_) {
				services().log().info("metal device=" + backend->deviceName() + " queue=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.outputGpu.queue)) + " needsIntermediateCopy="
				                      + std::to_string(context.outputGpu.needsIntermediateCopy));
				gpuLogged_ = true;
			}
			const GpuImage& out = context.outputGpu;
			const std::size_t outBytes = static_cast<std::size_t>(out.rowBytes) * static_cast<std::size_t>(out.desc.bounds.height());
			metal::MetalCommandBuffer command = backend->commandBuffer();
			if (!command.valid()) {
				return Error::make(ErrorCode::Render, "No Metal command buffer is available");
			}
			const InputImage* source = context.source();
			const bool sameLayout = source != nullptr && source->connected && source->gpu.handle != nullptr && source->gpu.desc.bounds == out.desc.bounds && source->gpu.rowBytes == out.rowBytes
			                        && source->gpu.desc.depth == out.desc.depth && source->gpu.desc.components == out.desc.components;
			Error e = sameLayout ? command.copy(source->gpu.handle, 0, out.handle, 0, outBytes) : command.fill(out.handle, 0, outBytes, 0);
			if (e) {
				return e;
			}
			return command.commit();
		}
#endif
		return Error::make(ErrorCode::Unsupported, "This GPU API is not supported").with("api", std::string(toString(context.outputGpu.api)));
	}

	void dumpInstance(const char* why)
	{
		std::ostringstream out;
		const PropertySet props = ofxServices().instanceProperties();
		out << "instance dump (" << why << ") page=" << ofxServices().resolvePage() << " type=" << props.getString(kOfxPropType, std::string("<missing>"), 0)
		    << " pluginHandle=" << (props.has(kOfxImageEffectPropPluginHandle) ? "present" : "missing") << " context=" << props.getString(kOfxImageEffectPropContext, std::string("<missing>"), 0)
		    << " frameRate=" << props.getDouble(kOfxImageEffectPropFrameRate, -1.0, 0) << " sequentialRender=" << (props.has(kOfxImageEffectInstancePropSequentialRender) ? "present" : "missing")
		    << " srcFilePath=" << props.getString(resolve::kPropSourceFilePath, std::string("<missing>"), 0);
		const std::vector<double> size = props.getDoubles(kOfxImageEffectPropProjectSize);
		if (size.size() == 2) {
			out << " projectSize=" << size[0] << "x" << size[1];
		}
		out << " par=" << props.getDouble(kOfxImageEffectPropProjectPixelAspectRatio, -1.0, 0);
		services().log().info(out.str());

		for (const auto& [id, binding] : ofxServices().bindings()) {
			std::ostringstream line;
			line << "  param " << binding.spec.key << " (" << id << ") type=" << binding.properties.getString(kOfxParamPropType, std::string("?"), 0)
			     << " animates=" << binding.properties.getInt(kOfxParamPropAnimates, -1, 0) << " secret=" << binding.properties.getInt(kOfxParamPropSecret, -1, 0)
			     << " enabled=" << binding.properties.getInt(kOfxParamPropEnabled, -1, 0) << " persistent=" << binding.properties.getInt(kOfxParamPropPersistant, -1, 0);
			const int dims = binding.spec.dimensions();
			if (dims > 0) {
				line << " min=";
				for (int d = 0; d < dims; ++d) {
					line << (d ? "," : "") << binding.properties.getDouble(kOfxParamPropMin, -9999.0, d);
				}
				line << " max=";
				for (int d = 0; d < dims; ++d) {
					line << (d ? "," : "") << binding.properties.getDouble(kOfxParamPropMax, -9999.0, d);
				}
				line << " displayMin=";
				for (int d = 0; d < dims; ++d) {
					line << (d ? "," : "") << binding.properties.getDouble(kOfxParamPropDisplayMin, -9999.0, d);
				}
				line << " displayMax=";
				for (int d = 0; d < dims; ++d) {
					line << (d ? "," : "") << binding.properties.getDouble(kOfxParamPropDisplayMax, -9999.0, d);
				}
				line << " defaultCoordinateSystem=" << (binding.properties.has(kOfxParamPropDefaultCoordinateSystem) ? "present" : "missing");
			}
			Value value;
			if (services().parameters().getCurrent(id, value).ok()) {
				line << " value=" << value.toString();
			}
			services().log().info(line.str());
		}
		for (const ClipSpec& clip : manifest().effectiveClips()) {
			const PropertySet clipProps = ofxServices().clipProperties(clip.name);
			std::ostringstream line;
			line << "  clip " << clip.name << " connected=" << clipProps.getInt(kOfxImageClipPropConnected, -1, 0) << " components=" << clipProps.getString(kOfxImageEffectPropComponents, std::string("?"), 0)
			     << " depth=" << clipProps.getString(kOfxImageEffectPropPixelDepth, std::string("?"), 0) << " par=" << clipProps.getDouble(kOfxImagePropPixelAspectRatio, -1.0, 0)
			     << " frameRate=" << clipProps.getDouble(kOfxImageEffectPropFrameRate, -1.0, 0) << " thumbnail=" << clipProps.getInt(resolve::kClipPropThumbnail, -1, 0);
			services().log().info(line.str());
		}
	}

	bool renderLogged_{false};
	bool gpuLogged_{false};
};

class HostDumpFactory final : public EffectFactory {
public:
	HostDumpFactory() : manifest_(makeManifest()) {}
	const EffectManifest& manifest() const override { return manifest_; }
	std::unique_ptr<Effect> create() override { return std::make_unique<HostDumpEffect>(); }
	void onLoad(const HostProfile& profile, LogService& log) override
	{
		std::ostringstream out;
		out << "HostDump load: extension properties rejected by the host:";
		for (const auto& [name, accepted] : profile.extensionPropertyAccepted) {
			if (!accepted) {
				out << " " << name;
			}
		}
		log.info(out.str());
	}

private:
	EffectManifest manifest_;
};

void registerPlugins()
{
	PluginRegistry::instance().registerFactory(std::make_unique<HostDumpFactory>());
}

}  // namespace

OFXGRIP_BUNDLE(registerPlugins)

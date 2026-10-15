// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Descriptor.hpp>

#include <ofxgrip/core/About.hpp>
#include <ofxgrip/core/Debug.hpp>
#include <ofxgrip/core/Presets.hpp>

#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/PropertySet.hpp>
#include <ofxgrip/ofx/ResolveExtensions.hpp>

#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <climits>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr double kUnboundedDouble = 1.0e6;

const char* threadSafetyName(ThreadSafety safety) noexcept
{
	switch (safety) {
	case ThreadSafety::Unsafe: return kOfxImageEffectRenderUnsafe;
	case ThreadSafety::InstanceSafe: return kOfxImageEffectRenderInstanceSafe;
	case ThreadSafety::FullySafe: return kOfxImageEffectRenderFullySafe;
	}
	return kOfxImageEffectRenderInstanceSafe;
}

bool excludedOnHost(const ParameterSpec& spec, const HostProfile& profile)
{
	auto excluded = [&](std::string_view host) {
		auto it = spec.hosts.find(std::string(host));
		return it != spec.hosts.end() && it->second.exclude;
	};
	return excluded("ofx") || excluded(toString(profile.family));
}

std::string labelOnHost(const ParameterSpec& spec, const HostProfile& profile)
{
	auto it = spec.hosts.find(std::string(toString(profile.family)));
	if (it != spec.hosts.end() && it->second.label) {
		return *it->second.label;
	}
	return spec.label.empty() ? spec.key : spec.label;
}

bool animatesOnHost(const ParameterSpec& spec, const HostProfile& profile)
{
	if (spec.hostFlags.has(HostFlag::NotAnimatable)) {
		return false;
	}
	switch (spec.type) {
	case ParameterType::Toggle:
	case ParameterType::Switch:
		return profile.capabilities.supportsBooleanAnimation;
	case ParameterType::Menu:
	case ParameterType::Presets:
		return profile.capabilities.supportsChoiceAnimation;
	case ParameterType::String:
	case ParameterType::Banner:
	case ParameterType::Status:
	case ParameterType::Capsule:
	case ParameterType::Divider:
		return false;
	case ParameterType::Custom:
		return profile.capabilities.supportsCustomAnimation;
	case ParameterType::Group:
	case ParameterType::Section:
	case ParameterType::PushButton:
	case ParameterType::Help:
	case ParameterType::Analyzer:
		return false;
	default:
		return true;
	}
}

std::vector<double> perDimension(const std::vector<double>& values, int dims, double fallback)
{
	std::vector<double> out(static_cast<std::size_t>(dims), fallback);
	if (values.size() == 1) {
		for (double& v : out) {
			v = values[0];
		}
	} else if (static_cast<int>(values.size()) == dims) {
		out = values;
	}
	return out;
}

void defaultDisplayRange(const ParameterSpec& spec, double& lo, double& hi)
{
	switch (spec.type) {
	case ParameterType::Int:
	case ParameterType::Random:
		lo = 0;
		hi = 100;
		break;
	case ParameterType::Angle:
		lo = -360;
		hi = 360;
		break;
	case ParameterType::Point:
	case ParameterType::Point3D:
		lo = -4096;
		hi = 4096;
		break;
	default:
		lo = 0;
		hi = 1;
		break;
	}
}

Error lowerNumericRanges(const ParameterSpec& spec, const PropertySet& props, bool integer)
{
	const int dims = spec.dimensions();
	if (dims <= 0) {
		return Error::none();
	}
	// Q-RESOLVE-004: both the hard range and the display range are always written. The rule is
	// unconditional, so every host sees the same declaration.
	double defaultLo = 0;
	double defaultHi = 0;
	defaultDisplayRange(spec, defaultLo, defaultHi);
	const double hardLo = integer ? static_cast<double>(INT_MIN / 2) : -kUnboundedDouble;
	const double hardHi = integer ? static_cast<double>(INT_MAX / 2) : kUnboundedDouble;
	std::vector<double> minimum = perDimension(spec.minimum, dims, hardLo);
	std::vector<double> maximum = perDimension(spec.maximum, dims, hardHi);
	std::vector<double> displayMin = spec.displayMinimum.empty() ? (spec.minimum.empty() ? std::vector<double>(static_cast<std::size_t>(dims), defaultLo) : minimum) : perDimension(spec.displayMinimum, dims, defaultLo);
	std::vector<double> displayMax = spec.displayMaximum.empty() ? (spec.maximum.empty() ? std::vector<double>(static_cast<std::size_t>(dims), defaultHi) : maximum) : perDimension(spec.displayMaximum, dims, defaultHi);
	for (int i = 0; i < dims; ++i) {
		const std::size_t d = static_cast<std::size_t>(i);
		if (integer) {
			if (Error e = props.setInt(kOfxParamPropMin, static_cast<int>(std::lround(minimum[d])), i)) {
				return e;
			}
			if (Error e = props.setInt(kOfxParamPropMax, static_cast<int>(std::lround(maximum[d])), i)) {
				return e;
			}
			if (Error e = props.setInt(kOfxParamPropDisplayMin, static_cast<int>(std::lround(displayMin[d])), i)) {
				return e;
			}
			if (Error e = props.setInt(kOfxParamPropDisplayMax, static_cast<int>(std::lround(displayMax[d])), i)) {
				return e;
			}
		} else {
			if (Error e = props.setDouble(kOfxParamPropMin, minimum[d], i)) {
				return e;
			}
			if (Error e = props.setDouble(kOfxParamPropMax, maximum[d], i)) {
				return e;
			}
			if (Error e = props.setDouble(kOfxParamPropDisplayMin, displayMin[d], i)) {
				return e;
			}
			if (Error e = props.setDouble(kOfxParamPropDisplayMax, displayMax[d], i)) {
				return e;
			}
		}
	}
	return Error::none();
}

Error lowerDefault(const ParameterSpec& spec, const PropertySet& props, const char* ofxType)
{
	Value controlDefault;
	if (spec.type == ParameterType::Banner && spec.defaultValue.empty()) {
		auto title = spec.meta.find("title");
		controlDefault = Value(title != spec.meta.end() ? (title->second.is<std::string>() ? title->second.as<std::string>() : title->second.toString()) : spec.label);
	} else if (spec.type == ParameterType::Divider && spec.defaultValue.empty()) {
		controlDefault = Value("\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80");
	}
	const Value& v = controlDefault.empty() ? spec.defaultValue : controlDefault;
	const std::string type(ofxType);
	if (type == kOfxParamTypeDouble) {
		return props.setDouble(kOfxParamPropDefault, v.toDouble(0.0));
	}
	if (type == kOfxParamTypeInteger || type == kOfxParamTypeChoice) {
		return props.setInt(kOfxParamPropDefault, v.toInt(0));
	}
	if (type == kOfxParamTypeBoolean) {
		return props.setInt(kOfxParamPropDefault, v.toBool(false) ? 1 : 0);
	}
	if (type == kOfxParamTypeDouble2D) {
		const Point2D p = v.tryAs<Point2D>() ? v.as<Point2D>() : Point2D{};
		return props.setDoubles(kOfxParamPropDefault, {p.x, p.y});
	}
	if (type == kOfxParamTypeDouble3D) {
		const Point3D p = v.tryAs<Point3D>() ? v.as<Point3D>() : Point3D{};
		return props.setDoubles(kOfxParamPropDefault, {p.x, p.y, p.z});
	}
	if (type == kOfxParamTypeRGB) {
		RGB c;
		if (const RGB* rgb = v.tryAs<RGB>()) {
			c = *rgb;
		} else if (const RGBA* rgba = v.tryAs<RGBA>()) {
			c = {rgba->r, rgba->g, rgba->b};
		}
		return props.setDoubles(kOfxParamPropDefault, {c.r, c.g, c.b});
	}
	if (type == kOfxParamTypeRGBA) {
		RGBA c;
		if (const RGBA* rgba = v.tryAs<RGBA>()) {
			c = *rgba;
		} else if (const RGB* rgb = v.tryAs<RGB>()) {
			c = {rgb->r, rgb->g, rgb->b, 1.0};
		}
		return props.setDoubles(kOfxParamPropDefault, {c.r, c.g, c.b, c.a});
	}
	if (type == kOfxParamTypeString || type == kOfxParamTypeCustom) {
		return props.setString(kOfxParamPropDefault, v.tryAs<std::string>() ? v.as<std::string>() : std::string());
	}
	return Error::none();
}

}  // namespace

const char* ofxParameterTypeFor(ParameterType type, const HostProfile& profile) noexcept
{
	(void)profile;
	switch (type) {
	case ParameterType::Float:
	case ParameterType::Angle:
	case ParameterType::Percent:
	case ParameterType::Progress:
	case ParameterType::TrackingOpacity:
		return kOfxParamTypeDouble;
	case ParameterType::Int:
	case ParameterType::Random:
		return kOfxParamTypeInteger;
	case ParameterType::Toggle:
	case ParameterType::Switch:
		return kOfxParamTypeBoolean;
	case ParameterType::Point:
		return kOfxParamTypeDouble2D;
	case ParameterType::Point3D:
		return kOfxParamTypeDouble3D;
	case ParameterType::RGB:
		return kOfxParamTypeRGB;
	case ParameterType::RGBA:
		return kOfxParamTypeRGBA;
	case ParameterType::String:
	case ParameterType::Banner:
	case ParameterType::Status:
	case ParameterType::Capsule:
	case ParameterType::Divider:
		return kOfxParamTypeString;
	case ParameterType::Menu:
	case ParameterType::Presets:
		return kOfxParamTypeChoice;
	case ParameterType::PushButton:
	case ParameterType::Help:
	case ParameterType::Analyzer:
		return kOfxParamTypePushButton;
	case ParameterType::Group:
	case ParameterType::Section:
		return kOfxParamTypeGroup;
	case ParameterType::Custom:
		return kOfxParamTypeCustom;
	default:
		return nullptr;
	}
}

std::string sanitizeLabel(const std::string& text, bool strict)
{
	std::string out;
	out.reserve(text.size());
	for (char ch : text) {
		const unsigned char c = static_cast<unsigned char>(ch);
		if (c == '<' || c == '>' || c == '&') {
			out.push_back(' ');
		} else if (c < 0x20) {
			out.push_back(' ');
		} else if (strict && c >= 0x7F) {
			out.push_back('?');
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

std::vector<ParameterSpec> frameworkParameters(const EffectManifest& manifest, bool debugAllowed)
{
	std::vector<ParameterSpec> out;
	if (!manifest.about.empty()) {
		out.push_back(aboutMenuParameter(manifest.about, kParameterIdAboutMenu, kAboutMenuParameterKey));
	}
	if (debugAllowed && manifest.property("debugMenu", false)) {
		out.push_back(debugMenuParameter(kParameterIdDebugMenu, kDebugMenuParameterKey));
	}
	ParameterSpec state = ParameterSpec::stringParam(kParameterIdInstanceState, kInstanceStateParameterKey, "OfxGrip State", "");
	state.hostFlags = HostFlag::Hidden | HostFlag::NotAnimatable | HostFlag::NoEvaluateOnChange | HostFlag::NoUndo;
	state.appFlags = AppFlag::NoState | AppFlag::PresetNoValue | AppFlag::PresetNoTags | AppFlag::PresetNoMeta;
	out.push_back(std::move(state));
	return out;
}

Error lowerEffectDescriptor(const EffectManifest& manifest, const HostProfile& profile, const Suites& suites, OfxImageEffectHandle descriptor, LoweringReport& report)
{
	OfxPropertySetHandle handle = nullptr;
	if (suites.imageEffect->getPropertySet(descriptor, &handle) != kOfxStatOK || handle == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The effect descriptor has no property set");
	}
	PropertySet props(suites.property, handle);
	const bool strict = !profile.has(Quirk::MarkdownDescriptionsOK);
	const Capabilities& cap = manifest.capabilities;

	if (Error e = props.setString(kOfxPropLabel, sanitizeLabel(manifest.identity.name, true))) {
		return e;
	}
	props.trySetString(kOfxPropShortLabel, sanitizeLabel(manifest.identity.shortName.empty() ? manifest.identity.name : manifest.identity.shortName, true), &report.rejectedProperties);
	props.trySetString(kOfxPropLongLabel, sanitizeLabel(manifest.identity.name, true), &report.rejectedProperties);
	if (Error e = props.setString(kOfxImageEffectPluginPropGrouping, sanitizeLabel(manifest.identity.grouping, true))) {
		return e;
	}
	if (!manifest.identity.description.empty()) {
		// Q-RESOLVE-011: absent on Resolve; a tolerant write records the fact.
		props.trySetString(kOfxPropPluginDescription, sanitizeLabel(manifest.identity.description, strict), &report.rejectedProperties);
	}

	std::vector<std::string> contexts{ofxContextName(manifest.identity.kind)};
	for (EffectKind extra : manifest.identity.extraContexts) {
		if (extra != manifest.identity.kind) {
			contexts.emplace_back(ofxContextName(extra));
		}
	}
	if (Error e = props.setStrings(kOfxImageEffectPropSupportedContexts, contexts)) {
		return e;
	}
	std::vector<std::string> depths;
	for (BitDepth d : cap.depths) {
		depths.emplace_back(ofxBitDepthName(d));
	}
	if (Error e = props.setStrings(kOfxImageEffectPropSupportedPixelDepths, depths)) {
		return e;
	}
	if (Error e = props.setString(kOfxImageEffectPluginRenderThreadSafety, threadSafetyName(cap.threadSafety))) {
		return e;
	}
	props.trySetInt(kOfxImageEffectPluginPropHostFrameThreading, cap.hostFrameThreading ? 1 : 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPropSupportsMultiResolution, cap.supportsMultiResolution ? 1 : 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPropSupportsTiles, cap.supportsTiles ? 1 : 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPropTemporalClipAccess, cap.temporalClipAccess ? 1 : 0, &report.rejectedProperties);
	props.trySetInt(kOfxImageEffectPluginPropFieldRenderTwiceAlways, cap.fieldRenderTwiceAlways ? 1 : 0, &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropOpenGLRenderSupported, cap.openGLRender ? "true" : "false", &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropMetalRenderSupported, cap.metalRender ? "true" : "false", &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropCudaRenderSupported, cap.cudaRender ? "true" : "false", &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropCudaStreamSupported, cap.cudaStream ? "true" : "false", &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropOpenCLRenderSupported, cap.openCLRender ? "true" : "false", &report.rejectedProperties);
	props.trySetString(kOfxImageEffectPropCPURenderSupported, cap.cpuRender ? "true" : "false", &report.rejectedProperties);
	props.trySetString(resolve::kPropNoSpatialAwareness, cap.noSpatialAwareness ? "true" : "false", &report.rejectedProperties);
	return Error::none();
}

Error lowerContextDescriptor(const EffectManifest& manifest, const HostProfile& profile, const Suites& suites, OfxImageEffectHandle descriptor, EffectKind context, LoweringReport& report, const LoweringContext& lowering)
{
	const bool generator = context == EffectKind::Generator;
	std::vector<ClipSpec> clips = generator ? std::vector<ClipSpec>{} : manifest.effectiveClips();
	for (const ClipSpec& clip : clips) {
		OfxPropertySetHandle clipHandle = nullptr;
		if (suites.imageEffect->clipDefine(descriptor, clip.name.c_str(), &clipHandle) != kOfxStatOK || clipHandle == nullptr) {
			return Error::make(ErrorCode::HostCall, "Clip definition failed").with("clip", clip.name);
		}
		PropertySet props(suites.property, clipHandle);
		std::vector<std::string> components;
		for (Components c : clip.components) {
			components.emplace_back(ofxComponentsName(c));
		}
		if (components.empty()) {
			components.emplace_back(kOfxImageComponentRGBA);
		}
		if (Error e = props.setStrings(kOfxImageEffectPropSupportedComponents, components)) {
			return e;
		}
		props.trySetString(kOfxPropLabel, sanitizeLabel(clip.label.empty() ? clip.name : clip.label, true), &report.rejectedProperties);
		props.trySetInt(kOfxImageClipPropOptional, clip.optional ? 1 : 0, &report.rejectedProperties);
		props.trySetInt(kOfxImageClipPropIsMask, clip.isMask ? 1 : 0, &report.rejectedProperties);
		props.trySetInt(kOfxImageEffectPropTemporalClipAccess, clip.temporalAccess ? 1 : 0, &report.rejectedProperties);
	}
	{
		OfxPropertySetHandle outHandle = nullptr;
		if (suites.imageEffect->clipDefine(descriptor, kOfxImageEffectOutputClipName, &outHandle) != kOfxStatOK || outHandle == nullptr) {
			return Error::make(ErrorCode::HostCall, "Output clip definition failed");
		}
		PropertySet props(suites.property, outHandle);
		if (Error e = props.setStrings(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA, kOfxImageComponentAlpha})) {
			return e;
		}
	}

	OfxParamSetHandle paramSet = nullptr;
	if (suites.imageEffect->getParamSet(descriptor, &paramSet) != kOfxStatOK || paramSet == nullptr) {
		return Error::make(ErrorCode::BadHandle, "The descriptor has no parameter set");
	}

	std::vector<ParameterSpec> flat = manifest.flattenedParameters();
	for (ParameterSpec& extra : frameworkParameters(manifest, lowering.debugAllowed)) {
		flat.push_back(std::move(extra));
	}
	std::map<ParameterId, std::string> keysById;
	for (const ParameterSpec& spec : flat) {
		keysById[spec.id] = spec.key;
	}
	std::vector<std::string> topLevel;
	for (const ParameterSpec& spec : flat) {
		if (excludedOnHost(spec, profile)) {
			report.omitted.push_back(spec.key + " (excluded on " + std::string(toString(profile.family)) + ")");
			continue;
		}
		const char* ofxType = ofxParameterTypeFor(spec.type, profile);
		if (ofxType == nullptr) {
			report.omitted.push_back(spec.key + " (" + std::string(toString(spec.type)) + " has no OpenFX form)");
			continue;
		}
		OfxPropertySetHandle paramHandle = nullptr;
		if (suites.parameter->paramDefine(paramSet, ofxType, spec.key.c_str(), &paramHandle) != kOfxStatOK || paramHandle == nullptr) {
			return Error::make(ErrorCode::HostCall, "Parameter definition failed").with("parameter", spec.key).with("type", ofxType);
		}
		PropertySet props(suites.property, paramHandle);
		if (Error e = props.setString(kOfxPropLabel, sanitizeLabel(labelOnHost(spec, profile), true))) {
			return e;
		}
		props.trySetString(kOfxParamPropScriptName, spec.key, &report.rejectedProperties);
		if (!spec.hint.empty()) {
			props.trySetString(kOfxParamPropHint, sanitizeLabel(spec.hint, false), &report.rejectedProperties);
		}
		if (spec.parent != kParameterIdRoot) {
			auto parentKey = keysById.find(spec.parent);
			if (parentKey != keysById.end()) {
				props.trySetString(kOfxParamPropParent, parentKey->second, &report.rejectedProperties);
			}
		} else {
			topLevel.push_back(spec.key);
		}
		// A parameter shown only in debug mode starts hidden; the debug menu reveals it.
		const bool hidden = spec.hostFlags.has(HostFlag::Hidden) || spec.appFlags.has(AppFlag::InDebugMode);
		// Display-only controls lower to disabled standard parameters the effect writes into.
		const bool displayOnly = spec.type == ParameterType::Status || spec.type == ParameterType::Capsule || spec.type == ParameterType::Progress || spec.type == ParameterType::Banner || spec.type == ParameterType::Divider;
		props.trySetInt(kOfxParamPropSecret, hidden ? 1 : 0, &report.rejectedProperties);
		props.trySetInt(kOfxParamPropEnabled, spec.hostFlags.has(HostFlag::Disabled) || displayOnly ? 0 : 1, &report.rejectedProperties);
		// Q-RESOLVE-015: `animates` is written for every parameter.
		props.trySetInt(kOfxParamPropAnimates, animatesOnHost(spec, profile) ? 1 : 0, &report.rejectedProperties);
		props.trySetInt(kOfxParamPropPersistant, spec.hostFlags.has(HostFlag::DontSave) ? 0 : 1, &report.rejectedProperties);
		props.trySetInt(kOfxParamPropEvaluateOnChange, spec.hostFlags.has(HostFlag::NoEvaluateOnChange) ? 0 : 1, &report.rejectedProperties);
		props.trySetInt(kOfxParamPropCanUndo, spec.hostFlags.has(HostFlag::NoUndo) ? 0 : 1, &report.rejectedProperties);

		const std::string type(ofxType);
		if (type == kOfxParamTypeDouble || type == kOfxParamTypeDouble2D || type == kOfxParamTypeDouble3D) {
			if (spec.type == ParameterType::Angle) {
				props.trySetString(kOfxParamPropDoubleType, kOfxParamDoubleTypeAngle, &report.rejectedProperties);
			} else if (type == kOfxParamTypeDouble2D) {
				props.trySetString(kOfxParamPropDoubleType, kOfxParamDoubleTypeXYAbsolute, &report.rejectedProperties);
			}
			if (spec.increment > 0) {
				props.trySetDouble(kOfxParamPropIncrement, spec.increment, &report.rejectedProperties);
			}
			props.trySetInt(kOfxParamPropDigits, spec.digits, &report.rejectedProperties);
			if (Error e = lowerNumericRanges(spec, props, false)) {
				return e;
			}
		} else if (type == kOfxParamTypeInteger) {
			if (Error e = lowerNumericRanges(spec, props, true)) {
				return e;
			}
		} else if (type == kOfxParamTypeRGB || type == kOfxParamTypeRGBA) {
			if (Error e = lowerNumericRanges(spec, props, false)) {
				return e;
			}
		} else if (type == kOfxParamTypeChoice) {
			std::vector<ChoiceOption> options = spec.options;
			if (spec.type == ParameterType::Presets) {
				// The menu lists the presets present now; a host that freezes options after
				// describe (Q-RESOLVE-021) shows new user presets after a restart.
				options = presetMenuOptions(presetMenuEntriesForTag(manifest, lowering.bundlePath, lowering.userSupportDirectory, spec.tags.empty() ? std::string() : spec.tags.front()));
			}
			std::vector<std::string> labels;
			for (const ChoiceOption& o : options) {
				labels.push_back(sanitizeLabel(o.label.empty() ? o.key : o.label, true));
			}
			if (Error e = props.setStrings(kOfxParamPropChoiceOption, labels)) {
				return e;
			}
		} else if (type == kOfxParamTypeGroup) {
			// A section is an open group with its title as the label.
			const bool open = spec.type == ParameterType::Section || !spec.hostFlags.has(HostFlag::Collapsed);
			props.trySetInt(kOfxParamPropGroupOpen, open ? 1 : 0, &report.rejectedProperties);
		} else if (type == kOfxParamTypeString) {
			const bool multiline = spec.meta.count("multiline") != 0 && spec.meta.at("multiline").toBool(false);
			// Banners and dividers are static text: label mode where the host keeps it
			// (Q-NUKE-009 hosts fall back to a disabled single-line field).
			const bool label = (spec.type == ParameterType::Banner || spec.type == ParameterType::Divider) && !profile.has(Quirk::LabelStringNotPersistent);
			props.trySetString(kOfxParamPropStringMode, label ? kOfxParamStringIsLabel : (multiline ? kOfxParamStringIsMultiLine : kOfxParamStringIsSingleLine), &report.rejectedProperties);
		}
		if (Error e = lowerDefault(spec, props, ofxType)) {
			return e;
		}
		report.declaredNames[spec.id] = spec.key;
	}

	if (profile.capabilities.maxPages > 0 && !topLevel.empty()) {
		OfxPropertySetHandle pageHandle = nullptr;
		if (suites.parameter->paramDefine(paramSet, kOfxParamTypePage, "ofxgrip_page", &pageHandle) == kOfxStatOK && pageHandle != nullptr) {
			PropertySet props(suites.property, pageHandle);
			props.trySetString(kOfxPropLabel, sanitizeLabel(manifest.identity.name, true), &report.rejectedProperties);
			if (Error e = props.setStrings(kOfxParamPropPageChild, topLevel)) {
				report.warnings.push_back("page children rejected: " + e.describe());
			}
		}
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END

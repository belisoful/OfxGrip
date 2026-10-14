// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/ResolveExtensions.hpp>

#include "ofxColour.h"
#include "ofxCore.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxParam.h"

#include <array>
#include <cstring>
#include <utility>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::array<std::pair<Action, const char*>, 35> kActions{{
	{Action::Load, kOfxActionLoad},
	{Action::Unload, kOfxActionUnload},
	{Action::Describe, kOfxActionDescribe},
	{Action::DescribeInContext, kOfxImageEffectActionDescribeInContext},
	{Action::CreateInstance, kOfxActionCreateInstance},
	{Action::DestroyInstance, kOfxActionDestroyInstance},
	{Action::InstanceChanged, kOfxActionInstanceChanged},
	{Action::BeginInstanceChanged, kOfxActionBeginInstanceChanged},
	{Action::EndInstanceChanged, kOfxActionEndInstanceChanged},
	{Action::BeginInstanceEdit, kOfxActionBeginInstanceEdit},
	{Action::EndInstanceEdit, kOfxActionEndInstanceEdit},
	{Action::PurgeCaches, kOfxActionPurgeCaches},
	{Action::SyncPrivateData, kOfxActionSyncPrivateData},
	{Action::GetRegionOfDefinition, kOfxImageEffectActionGetRegionOfDefinition},
	{Action::GetRegionsOfInterest, kOfxImageEffectActionGetRegionsOfInterest},
	{Action::GetTimeDomain, kOfxImageEffectActionGetTimeDomain},
	{Action::GetFramesNeeded, kOfxImageEffectActionGetFramesNeeded},
	{Action::GetClipPreferences, kOfxImageEffectActionGetClipPreferences},
	{Action::IsIdentity, kOfxImageEffectActionIsIdentity},
	{Action::Render, kOfxImageEffectActionRender},
	{Action::BeginSequenceRender, kOfxImageEffectActionBeginSequenceRender},
	{Action::EndSequenceRender, kOfxImageEffectActionEndSequenceRender},
	{Action::GetOutputColourspace, kOfxImageEffectActionGetOutputColourspace},
	{Action::OpenGLContextAttached, kOfxActionOpenGLContextAttached},
	{Action::OpenGLContextDetached, kOfxActionOpenGLContextDetached},
	{Action::InteractDraw, kOfxInteractActionDraw},
	{Action::InteractPenMotion, kOfxInteractActionPenMotion},
	{Action::InteractPenDown, kOfxInteractActionPenDown},
	{Action::InteractPenUp, kOfxInteractActionPenUp},
	{Action::InteractKeyDown, kOfxInteractActionKeyDown},
	{Action::InteractKeyUp, kOfxInteractActionKeyUp},
	{Action::InteractKeyRepeat, kOfxInteractActionKeyRepeat},
	{Action::InteractGainFocus, kOfxInteractActionGainFocus},
	{Action::InteractLoseFocus, kOfxInteractActionLoseFocus},
	{Action::Dialog, "OfxActionDialog"},
}};

RectI readRectI(const PropertySet& props, const char* name)
{
	const std::vector<int> v = props.getInts(name);
	if (v.size() != 4) {
		return {};
	}
	return RectI{v[0], v[1], v[2], v[3]};
}

RectD readRectD(const PropertySet& props, const char* name)
{
	const std::vector<double> v = props.getDoubles(name);
	if (v.size() != 4) {
		return {};
	}
	return RectD{v[0], v[1], v[2], v[3]};
}

Point2D readScale(const PropertySet& props)
{
	const std::vector<double> v = props.getDoubles(kOfxImageEffectPropRenderScale);
	if (v.size() != 2 || v[0] <= 0.0 || v[1] <= 0.0) {
		return {1, 1};
	}
	return Point2D{v[0], v[1]};
}

void readGpu(const PropertySet& props, GpuApi& api, void*& queue)
{
	api = GpuApi::None;
	queue = nullptr;
	if (props.getInt(kOfxImageEffectPropMetalEnabled, 0, 0) != 0) {
		api = GpuApi::Metal;
		queue = props.getPointer(kOfxImageEffectPropMetalCommandQueue).value_or(nullptr);
	} else if (props.getInt(kOfxImageEffectPropCudaEnabled, 0, 0) != 0) {
		api = GpuApi::Cuda;
		queue = props.getPointer(kOfxImageEffectPropCudaStream).value_or(nullptr);
	} else if (props.getInt(kOfxImageEffectPropOpenCLEnabled, 0, 0) != 0) {
		api = GpuApi::OpenCL;
		queue = props.getPointer(kOfxImageEffectPropOpenCLCommandQueue).value_or(nullptr);
	}
}

}  // namespace

Action parseAction(const char* action) noexcept
{
	if (action == nullptr) {
		return Action::Unknown;
	}
	for (const auto& [a, name] : kActions) {
		if (std::strcmp(action, name) == 0) {
			return a;
		}
	}
	return Action::Unknown;
}

const char* actionString(Action action) noexcept
{
	for (const auto& [a, name] : kActions) {
		if (a == action) {
			return name;
		}
	}
	return "Unknown";
}

RenderArgs readRenderArgs(const PropertySet& in)
{
	RenderArgs a;
	a.time = in.getDouble(kOfxPropTime, 0.0, 0);
	a.renderScale = readScale(in);
	a.renderWindow = readRectI(in, kOfxImageEffectPropRenderWindow);
	parseOfxField(in.getString(kOfxImageEffectPropFieldToRender, std::string(kOfxImageFieldNone), 0), a.field);
	a.draft = in.getInt(kOfxImageEffectPropRenderQualityDraft, 0, 0) != 0;
	a.interactive = in.getInt(kOfxPropIsInteractive, 0, 0) != 0;
	a.sequentialStatus = in.getInt(kOfxImageEffectPropSequentialRenderStatus, 0, 0) != 0;
	a.interactiveStatus = in.getInt(kOfxImageEffectPropInteractiveRenderStatus, 0, 0) != 0;
	readGpu(in, a.gpu, a.gpuQueue);
	a.openGLEnabled = in.getInt(kOfxImageEffectPropOpenGLEnabled, 0, 0) != 0;
	a.resolveSourceFrame = in.getInt(resolve::kPropSourceFrame, -1, 0);
	a.resolveEye = in.getInt(resolve::kPropEyeToRender, 0, 0);
	return a;
}

SequenceRenderArgs readSequenceRenderArgs(const PropertySet& in)
{
	SequenceRenderArgs a;
	const std::vector<double> range = in.getDoubles(kOfxImageEffectPropFrameRange);
	if (range.size() == 2) {
		a.firstFrame = range[0];
		a.lastFrame = range[1];
	}
	a.frameStep = in.getDouble(kOfxImageEffectPropFrameStep, 1.0, 0);
	a.interactive = in.getInt(kOfxPropIsInteractive, 0, 0) != 0;
	a.renderScale = readScale(in);
	a.sequentialStatus = in.getInt(kOfxImageEffectPropSequentialRenderStatus, 0, 0) != 0;
	a.interactiveStatus = in.getInt(kOfxImageEffectPropInteractiveRenderStatus, 0, 0) != 0;
	a.draft = in.getInt(kOfxImageEffectPropRenderQualityDraft, 0, 0) != 0;
	readGpu(in, a.gpu, a.gpuQueue);
	return a;
}

IsIdentityArgs readIsIdentityArgs(const PropertySet& in)
{
	IsIdentityArgs a;
	a.time = in.getDouble(kOfxPropTime, 0.0, 0);
	a.renderScale = readScale(in);
	a.renderWindow = readRectI(in, kOfxImageEffectPropRenderWindow);
	parseOfxField(in.getString(kOfxImageEffectPropFieldToRender, std::string(kOfxImageFieldNone), 0), a.field);
	return a;
}

InstanceChangedArgs readInstanceChangedArgs(const PropertySet& in)
{
	InstanceChangedArgs a;
	a.isClip = in.getString(kOfxPropType, std::string(), 0) == kOfxTypeClip;
	a.name = in.getString(kOfxPropName, std::string(), 0);
	a.reason = parseOfxChangeReason(in.getString(kOfxPropChangeReason, std::string(kOfxChangeUserEdited), 0));
	a.time = in.getDouble(kOfxPropTime, 0.0, 0);
	a.renderScale = readScale(in);
	return a;
}

RegionArgs readRegionArgs(const PropertySet& in, bool withRegionOfInterest)
{
	RegionArgs a;
	a.time = in.getDouble(kOfxPropTime, 0.0, 0);
	a.renderScale = readScale(in);
	if (withRegionOfInterest) {
		a.regionOfInterest = readRectD(in, kOfxImageEffectPropRegionOfInterest);
	}
	return a;
}

CreateInstanceArgs readCreateInstanceArgs(const PropertySet& in)
{
	CreateInstanceArgs a;
	a.resolvePage = in.getString(resolve::kPropPage, std::string(), 0);
	return a;
}

const char* ofxContextName(EffectKind kind) noexcept
{
	switch (kind) {
	case EffectKind::Filter: return kOfxImageEffectContextFilter;
	case EffectKind::Generator: return kOfxImageEffectContextGenerator;
	case EffectKind::Transition: return kOfxImageEffectContextTransition;
	case EffectKind::General: return kOfxImageEffectContextGeneral;
	case EffectKind::Retimer: return kOfxImageEffectContextRetimer;
	}
	return kOfxImageEffectContextFilter;
}

bool parseOfxContext(std::string_view name, EffectKind& out) noexcept
{
	for (EffectKind k : {EffectKind::Filter, EffectKind::Generator, EffectKind::Transition, EffectKind::General, EffectKind::Retimer}) {
		if (name == ofxContextName(k)) {
			out = k;
			return true;
		}
	}
	return false;
}

const char* ofxBitDepthName(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::None: return kOfxBitDepthNone;
	case BitDepth::UInt8: return kOfxBitDepthByte;
	case BitDepth::UInt16: return kOfxBitDepthShort;
	case BitDepth::Half: return kOfxBitDepthHalf;
	case BitDepth::Float: return kOfxBitDepthFloat;
	}
	return kOfxBitDepthNone;
}

bool parseOfxBitDepth(std::string_view name, BitDepth& out) noexcept
{
	for (BitDepth d : {BitDepth::None, BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		if (name == ofxBitDepthName(d)) {
			out = d;
			return true;
		}
	}
	return false;
}

const char* ofxComponentsName(Components components) noexcept
{
	switch (components) {
	case Components::None: return kOfxImageComponentNone;
	case Components::RGBA: return kOfxImageComponentRGBA;
	case Components::RGB: return kOfxImageComponentRGB;
	case Components::Alpha: return kOfxImageComponentAlpha;
	}
	return kOfxImageComponentNone;
}

bool parseOfxComponents(std::string_view name, Components& out) noexcept
{
	for (Components c : {Components::None, Components::RGBA, Components::RGB, Components::Alpha}) {
		if (name == ofxComponentsName(c)) {
			out = c;
			return true;
		}
	}
	return false;
}

const char* ofxFieldName(Field field) noexcept
{
	switch (field) {
	case Field::None: return kOfxImageFieldNone;
	case Field::Both: return kOfxImageFieldBoth;
	case Field::Lower: return kOfxImageFieldLower;
	case Field::Upper: return kOfxImageFieldUpper;
	case Field::Single: return kOfxImageFieldSingle;
	case Field::Doubled: return kOfxImageFieldDoubled;
	}
	return kOfxImageFieldNone;
}

bool parseOfxField(std::string_view name, Field& out) noexcept
{
	for (Field f : {Field::None, Field::Both, Field::Lower, Field::Upper, Field::Single, Field::Doubled}) {
		if (name == ofxFieldName(f)) {
			out = f;
			return true;
		}
	}
	return false;
}

const char* ofxPremultiplicationName(Premultiplication p) noexcept
{
	switch (p) {
	case Premultiplication::Opaque: return kOfxImageOpaque;
	case Premultiplication::Premultiplied: return kOfxImagePreMultiplied;
	case Premultiplication::Unpremultiplied: return kOfxImageUnPreMultiplied;
	}
	return kOfxImageOpaque;
}

bool parseOfxPremultiplication(std::string_view name, Premultiplication& out) noexcept
{
	for (Premultiplication p : {Premultiplication::Opaque, Premultiplication::Premultiplied, Premultiplication::Unpremultiplied}) {
		if (name == ofxPremultiplicationName(p)) {
			out = p;
			return true;
		}
	}
	return false;
}

ChangeReason parseOfxChangeReason(std::string_view name) noexcept
{
	if (name == kOfxChangePluginEdited) {
		return ChangeReason::PluginEdited;
	}
	if (name == kOfxChangeTime) {
		return ChangeReason::TimeChanged;
	}
	return ChangeReason::UserEdited;
}

OFXGRIP_NAMESPACE_END

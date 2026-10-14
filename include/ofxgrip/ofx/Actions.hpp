// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Actions.hpp
	@brief The closed action set decoded from OpenFX's action strings, with typed argument
	       structs read from the in-argument property sets.

	Introduced in OfxGrip 0.1.0. The adapter decodes the action string once at the entry point;
	everything after that works on the enum and the typed arguments. An action the adapter does
	not handle answers `kOfxStatReplyDefault`.
*/
#pragma once

#include <ofxgrip/core/Events.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/ofx/PropertySet.hpp>

#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

enum class Action : std::uint8_t {
	Unknown = 0,
	Load,
	Unload,
	Describe,
	DescribeInContext,
	CreateInstance,
	DestroyInstance,
	InstanceChanged,
	BeginInstanceChanged,
	EndInstanceChanged,
	BeginInstanceEdit,
	EndInstanceEdit,
	PurgeCaches,
	SyncPrivateData,
	GetRegionOfDefinition,
	GetRegionsOfInterest,
	GetTimeDomain,
	GetFramesNeeded,
	GetClipPreferences,
	IsIdentity,
	Render,
	BeginSequenceRender,
	EndSequenceRender,
	GetOutputColourspace,
	OpenGLContextAttached,
	OpenGLContextDetached,
	InteractDraw,
	InteractPenMotion,
	InteractPenDown,
	InteractPenUp,
	InteractKeyDown,
	InteractKeyUp,
	InteractKeyRepeat,
	InteractGainFocus,
	InteractLoseFocus,
	Dialog,
};

/*! Decodes an action string. Unknown strings decode to `Action::Unknown`. */
Action parseAction(const char* action) noexcept;
/*! The OpenFX string of an action. */
const char* actionString(Action action) noexcept;

/*! The render action's arguments. Render scale and field come from here, never from an image
    (Q-OFX-ANY-016). `gpu` is the API the host enabled for this render, with its queue. */
struct RenderArgs {
	Time time{0};
	Point2D renderScale{1, 1};
	RectI renderWindow;
	Field field{Field::None};
	bool draft{false};
	bool interactive{false};
	bool sequentialStatus{false};
	bool interactiveStatus{false};
	GpuApi gpu{GpuApi::None};
	void* gpuQueue{nullptr};      /*!< `id<MTLCommandQueue>`, `cudaStream_t`, or `cl_command_queue`. */
	bool openGLEnabled{false};
	int resolveSourceFrame{-1};
	int resolveEye{0};
};

/*! The begin- and end-sequence-render arguments. */
struct SequenceRenderArgs {
	Time firstFrame{0};
	Time lastFrame{0};
	double frameStep{1};
	bool interactive{false};
	Point2D renderScale{1, 1};
	bool sequentialStatus{false};
	bool interactiveStatus{false};
	bool draft{false};
	GpuApi gpu{GpuApi::None};
	void* gpuQueue{nullptr};
};

/*! The is-identity arguments. */
struct IsIdentityArgs {
	Time time{0};
	Point2D renderScale{1, 1};
	RectI renderWindow;
	Field field{Field::None};
};

/*! The instance-changed arguments. */
struct InstanceChangedArgs {
	bool isClip{false};
	std::string name;
	ChangeReason reason{ChangeReason::UserEdited};
	Time time{0};
	Point2D renderScale{1, 1};
};

/*! The region-of-definition and regions-of-interest arguments. */
struct RegionArgs {
	Time time{0};
	Point2D renderScale{1, 1};
	RectD regionOfInterest;   /*!< Filled for regions of interest only. */
};

/*! The create-instance arguments. */
struct CreateInstanceArgs {
	std::string resolvePage;  /*!< `Edit`, `Color`, or `Fusion` on Resolve (Q-RESOLVE-029). */
};

RenderArgs readRenderArgs(const PropertySet& inArgs);
SequenceRenderArgs readSequenceRenderArgs(const PropertySet& inArgs);
IsIdentityArgs readIsIdentityArgs(const PropertySet& inArgs);
InstanceChangedArgs readInstanceChangedArgs(const PropertySet& inArgs);
RegionArgs readRegionArgs(const PropertySet& inArgs, bool withRegionOfInterest);
CreateInstanceArgs readCreateInstanceArgs(const PropertySet& inArgs);

/*! The OpenFX names of the enums, and their parsers. */
const char* ofxContextName(EffectKind kind) noexcept;
bool parseOfxContext(std::string_view name, EffectKind& out) noexcept;
const char* ofxBitDepthName(BitDepth depth) noexcept;
bool parseOfxBitDepth(std::string_view name, BitDepth& out) noexcept;
const char* ofxComponentsName(Components components) noexcept;
bool parseOfxComponents(std::string_view name, Components& out) noexcept;
const char* ofxFieldName(Field field) noexcept;
bool parseOfxField(std::string_view name, Field& out) noexcept;
const char* ofxPremultiplicationName(Premultiplication p) noexcept;
bool parseOfxPremultiplication(std::string_view name, Premultiplication& out) noexcept;
ChangeReason parseOfxChangeReason(std::string_view name) noexcept;

OFXGRIP_NAMESPACE_END

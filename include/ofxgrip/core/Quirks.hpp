// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Quirks.hpp
	@brief The quirk keys compiled from `Research/Host Quirks Registry.md`, with each entry's
	       registry ID and host match.

	Introduced in OfxGrip 0.1.0. One `Quirk` case exists per OpenFX registry entry that code may
	ask for. The table in `Quirks.cpp` records the host-name prefix and version range of the
	entries whose status is `reported` or `verified`. Adding a registry entry means adding a case
	here and a row there; the registry stays the source of truth for the symptom and handling.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The quirk keys. Names match the registry's `Key` field. */
enum class Quirk : std::uint16_t {
	// DaVinci Resolve (Q-RESOLVE-001 to 038)
	ResolveEditionDetection = 0,
	MaskAlwaysConnected,
	ThreadSuiteDummy,
	ParamRangeDefaultClamp,
	NoDefaultCoordinateSystem,
	ParamCopyNoOp,
	MessageSuiteV2Broken,
	OpenGLRenderAdvertisedNotUsed,
	GeneratorSourceNotFetchable,
	ImageScaleAndFieldUntrusted,
	NoPluginDescription,
	NoInteger2D3D,
	SequentialRenderOnInstanceOnly,
	NonConformantInstanceProps,
	BooleanAnimatesByDefault,
	EvaluateOnChangeIgnored,
	ThumbnailRenderFullScale,
	NoTilesNoMultiResolution,
	NoCustomParamInteract,
	ChoiceBoolAnimationChanged,
	NoDynamicChoices,
	NoParametricParams,
	InteractPixelScaleUnity,
	ResolveOfxExtensions,
	NoSymlinkedBundles,
	PluginCacheAndWorkingDir,
	RenderDespiteIdentity,
	NoInputColorSpace,
	EditColorPageFixedOutputSize,
	FusionPageTimeOffset,
	FusionPageMetalNotInPlace,
	SourcePathNeedsScripting,
	NoRerenderRequest,
	TimelineClipFpsMismatch,
	UnknownChangedParamNames,
	FxFactoryRequiresResolve20,
	FramesNeededRangeMissingUnconnected,
	ScriptingEditionLimits,

	// Generic OpenFX host variance (Q-OFX-ANY-001 to 017)
	LegacyCacheXmlEscaping,
	OptionalSuitesVary,
	ClipPrefsMayBeIgnored,
	HostNameUnreliable,
	MacModifierKeyMapping,
	NoOverlayScaleReporting,
	ChoicePersistedByIndex,
	ViewPropertiesDiffer,
	SuggestedColourAbsent,
	ViewportSizeRemoved,
	ParametricPerCurveRangeUnspecified,
	ScreenPixelRatioZero,
	NullThreadSuite,
	TolerantWritesHideFailures,
	PostV1PropsMayBeMissing,
	ImageRenderScaleUntrusted,
	ParMayBeZero,

	// Nuke (Q-NUKE-001 to 028): the subset the adapter handles in this phase
	PersistentMessageBlocksRender,
	SecretIsPermanent,
	ChoiceOptionsFrozenAfterDescribe,
	DescribeErrorFatal,
	FirstClipIsDefault,

	// Natron (Q-NATRON-001 to 012): the subset the adapter handles in this phase
	NatronFakeHostName,
	ScreenPixelRatioProvided,
	FullDynamicUi,
	MarkdownDescriptionsOK,

	Count,
};

/*! How a registry entry matches a host. An empty prefix matches every host. A zero `maxVersion`
    means an open upper bound. */
struct QuirkMatch {
	std::string_view hostNamePrefix;
	Version minVersion;
	Version maxVersion;
};

/*! A registry entry's identity. */
struct QuirkInfo {
	Quirk key;
	std::string_view registryId;   /*!< `Q-RESOLVE-002` */
	std::string_view name;         /*!< `MaskAlwaysConnected` */
	std::string_view area;         /*!< The capability area. */
	std::vector<QuirkMatch> matches;
};

/*! The registry table in declaration order. */
const std::vector<QuirkInfo>& quirkRegistry();
/*! The entry for a key. */
const QuirkInfo& quirkInfo(Quirk key);
/*! The key name. */
std::string_view toString(Quirk key) noexcept;
/*! Parses a key name. */
bool parseQuirk(std::string_view name, Quirk& out) noexcept;
/*! Whether the table says a quirk applies to a host name and version. */
bool quirkMatchesHost(Quirk key, std::string_view hostName, Version hostVersion) noexcept;

OFXGRIP_NAMESPACE_END

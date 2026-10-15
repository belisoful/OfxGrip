// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Quirks.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace {

// Host-name prefixes as the registry records them. Resolve's free edition reported
// `DaVinciResolveLite` in the 12.5 and 16 dumps; the prefix `DaVinciResolve` covers both editions
// (Q-RESOLVE-001).
constexpr std::string_view kResolve = "DaVinciResolve";
constexpr std::string_view kNuke = "uk.co.thefoundry.nuke";
constexpr std::string_view kNatron = "fr.inria.Natron";
constexpr std::string_view kAny = "";

const Version kOpen{0, 0, 0};
const Version kV16{16, 99, 99};
const Version kV14{14, 99, 99};
const Version kV20{19, 99, 99};

std::vector<QuirkInfo> makeRegistry()
{
	// The version ranges carry the registry's "observed" ranges. An entry observed through 16 and
	// not yet re-probed keeps an open upper bound, because the registry's rule is that "observed"
	// does not mean "fixed afterwards"; the probes close ranges as they run.
	return {
		{Quirk::ResolveEditionDetection, "Q-RESOLVE-001", "ResolveEditionDetection", "registration/loading", {{kResolve, kOpen, kOpen}}},
		{Quirk::MaskAlwaysConnected, "Q-RESOLVE-002", "MaskAlwaysConnected", "clips/masks", {{kResolve, kOpen, kOpen}}},
		{Quirk::ThreadSuiteDummy, "Q-RESOLVE-003", "ThreadSuiteDummy", "threading/concurrency", {{kResolve, kOpen, kOpen}}},
		{Quirk::ParamRangeDefaultClamp, "Q-RESOLVE-004", "ParamRangeDefaultClamp", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoDefaultCoordinateSystem, "Q-RESOLVE-005", "NoDefaultCoordinateSystem", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::ParamCopyNoOp, "Q-RESOLVE-006", "ParamCopyNoOp", "param-values/time", {{kResolve, kOpen, kOpen}}},
		{Quirk::MessageSuiteV2Broken, "Q-RESOLVE-007", "MessageSuiteV2Broken", "UI/dialogs", {{kResolve, kOpen, kV14}}},
		{Quirk::OpenGLRenderAdvertisedNotUsed, "Q-RESOLVE-008", "OpenGLRenderAdvertisedNotUsed", "GPU", {{kResolve, kOpen, kOpen}}},
		{Quirk::GeneratorSourceNotFetchable, "Q-RESOLVE-009", "GeneratorSourceNotFetchable", "clips/masks", {{kResolve, kOpen, kOpen}}},
		{Quirk::ImageScaleAndFieldUntrusted, "Q-RESOLVE-010", "ImageScaleAndFieldUntrusted", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoPluginDescription, "Q-RESOLVE-011", "NoPluginDescription", "UI/dialogs", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoInteger2D3D, "Q-RESOLVE-012", "NoInteger2D3D", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::SequentialRenderOnInstanceOnly, "Q-RESOLVE-013", "SequentialRenderOnInstanceOnly", "threading/concurrency", {{kResolve, kOpen, kOpen}}},
		{Quirk::NonConformantInstanceProps, "Q-RESOLVE-014", "NonConformantInstanceProps", "registration/loading", {{kResolve, kOpen, kOpen}}},
		{Quirk::BooleanAnimatesByDefault, "Q-RESOLVE-015", "BooleanAnimatesByDefault", "param-values/time", {{kResolve, kOpen, kOpen}}},
		{Quirk::EvaluateOnChangeIgnored, "Q-RESOLVE-016", "EvaluateOnChangeIgnored", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::ThumbnailRenderFullScale, "Q-RESOLVE-017", "ThumbnailRenderFullScale", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoTilesNoMultiResolution, "Q-RESOLVE-018", "NoTilesNoMultiResolution", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoCustomParamInteract, "Q-RESOLVE-019", "NoCustomParamInteract", "overlays/interacts", {{kResolve, kOpen, kOpen}}},
		{Quirk::ChoiceBoolAnimationChanged, "Q-RESOLVE-020", "ChoiceBoolAnimationChanged", "param-values/time", {{kResolve, kOpen, kV16}}},
		{Quirk::NoDynamicChoices, "Q-RESOLVE-021", "NoDynamicChoices", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoParametricParams, "Q-RESOLVE-022", "NoParametricParams", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::InteractPixelScaleUnity, "Q-RESOLVE-023", "InteractPixelScaleUnity", "HiDPI", {{kResolve, kOpen, kOpen}}},
		{Quirk::ResolveOfxExtensions, "Q-RESOLVE-024", "ResolveOfxExtensions", "GPU", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoSymlinkedBundles, "Q-RESOLVE-025", "NoSymlinkedBundles", "packaging/signing", {{kResolve, kOpen, kOpen}}},
		{Quirk::PluginCacheAndWorkingDir, "Q-RESOLVE-026", "PluginCacheAndWorkingDir", "registration/loading", {{kResolve, kOpen, kOpen}}},
		{Quirk::RenderDespiteIdentity, "Q-RESOLVE-027", "RenderDespiteIdentity", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoInputColorSpace, "Q-RESOLVE-028", "NoInputColorSpace", "color", {{kResolve, kOpen, kOpen}}},
		{Quirk::EditColorPageFixedOutputSize, "Q-RESOLVE-029", "EditColorPageFixedOutputSize", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::FusionPageTimeOffset, "Q-RESOLVE-030", "FusionPageTimeOffset", "time/speed", {{kResolve, kOpen, kOpen}}},
		{Quirk::FusionPageMetalNotInPlace, "Q-RESOLVE-031", "FusionPageMetalNotInPlace", "GPU", {{kResolve, kOpen, kOpen}}},
		{Quirk::SourcePathNeedsScripting, "Q-RESOLVE-032", "SourcePathNeedsScripting", "clips/masks", {{kResolve, kOpen, kOpen}}},
		{Quirk::NoRerenderRequest, "Q-RESOLVE-033", "NoRerenderRequest", "rendering/tiling", {{kResolve, kOpen, kOpen}}},
		{Quirk::TimelineClipFpsMismatch, "Q-RESOLVE-034", "TimelineClipFpsMismatch", "time/speed", {{kResolve, kOpen, kOpen}}},
		{Quirk::UnknownChangedParamNames, "Q-RESOLVE-035", "UnknownChangedParamNames", "parameters", {{kResolve, kOpen, kOpen}}},
		{Quirk::FxFactoryRequiresResolve20, "Q-RESOLVE-036", "FxFactoryRequiresResolve20", "registration/loading", {{kResolve, kOpen, kV20}}},
		{Quirk::FramesNeededRangeMissingUnconnected, "Q-RESOLVE-037", "FramesNeededRangeMissingUnconnected", "clips/masks", {{kResolve, kOpen, kOpen}}},
		{Quirk::ScriptingEditionLimits, "Q-RESOLVE-038", "ScriptingEditionLimits", "scripting", {{kResolve, kOpen, kOpen}}},

		{Quirk::LegacyCacheXmlEscaping, "Q-OFX-ANY-001", "LegacyCacheXmlEscaping", "registration/loading", {{kAny, kOpen, kOpen}}},
		{Quirk::OptionalSuitesVary, "Q-OFX-ANY-002", "OptionalSuitesVary", "registration/loading", {{kAny, kOpen, kOpen}}},
		{Quirk::ClipPrefsMayBeIgnored, "Q-OFX-ANY-003", "ClipPrefsMayBeIgnored", "rendering/tiling", {{kAny, kOpen, kOpen}}},
		{Quirk::HostNameUnreliable, "Q-OFX-ANY-004", "HostNameUnreliable", "registration/loading", {{kAny, kOpen, kOpen}}},
		{Quirk::MacModifierKeyMapping, "Q-OFX-ANY-005", "MacModifierKeyMapping", "overlays/interacts", {{kAny, kOpen, kOpen}}},
		{Quirk::NoOverlayScaleReporting, "Q-OFX-ANY-006", "NoOverlayScaleReporting", "HiDPI", {{kAny, kOpen, kOpen}}},
		{Quirk::ChoicePersistedByIndex, "Q-OFX-ANY-007", "ChoicePersistedByIndex", "parameters", {{kAny, kOpen, kOpen}}},
		{Quirk::ViewPropertiesDiffer, "Q-OFX-ANY-008", "ViewPropertiesDiffer", "multi-plane/channels", {{kNuke, kOpen, kOpen}}},
		{Quirk::SuggestedColourAbsent, "Q-OFX-ANY-009", "SuggestedColourAbsent", "overlays/interacts", {{kAny, kOpen, kOpen}}},
		{Quirk::ViewportSizeRemoved, "Q-OFX-ANY-010", "ViewportSizeRemoved", "overlays/interacts", {{kAny, kOpen, kOpen}}},
		{Quirk::ParametricPerCurveRangeUnspecified, "Q-OFX-ANY-011", "ParametricPerCurveRangeUnspecified", "parameters", {{kAny, kOpen, kOpen}}},
		{Quirk::ScreenPixelRatioZero, "Q-OFX-ANY-012", "ScreenPixelRatioZero", "HiDPI", {{kAny, kOpen, kOpen}}},
		{Quirk::NullThreadSuite, "Q-OFX-ANY-013", "NullThreadSuite", "threading/concurrency", {}},
		{Quirk::TolerantWritesHideFailures, "Q-OFX-ANY-014", "TolerantWritesHideFailures", "registration/loading", {{kAny, kOpen, kOpen}}},
		{Quirk::PostV1PropsMayBeMissing, "Q-OFX-ANY-015", "PostV1PropsMayBeMissing", "registration/loading", {{kAny, kOpen, kOpen}}},
		{Quirk::ImageRenderScaleUntrusted, "Q-OFX-ANY-016", "ImageRenderScaleUntrusted", "rendering/tiling", {{kAny, kOpen, kOpen}}},
		{Quirk::ParMayBeZero, "Q-OFX-ANY-017", "ParMayBeZero", "clips/masks", {{kAny, kOpen, kOpen}}},

		{Quirk::PersistentMessageBlocksRender, "Q-NUKE-001", "PersistentMessageBlocksRender", "UI/dialogs", {{kNuke, kOpen, kOpen}}},
		{Quirk::SecretIsPermanent, "Q-NUKE-010", "SecretIsPermanent", "parameters", {{kNuke, kOpen, kOpen}}},
		{Quirk::ChoiceOptionsFrozenAfterDescribe, "Q-NUKE-006", "ChoiceOptionsFrozenAfterDescribe", "parameters", {{kNuke, kOpen, kOpen}}},
		{Quirk::DescribeErrorFatal, "Q-NUKE-025", "DescribeErrorFatal", "registration/loading", {{kNuke, kOpen, kOpen}}},
		{Quirk::FirstClipIsDefault, "Q-NUKE-024", "FirstClipIsDefault", "clips/masks", {{kNuke, kOpen, kOpen}}},
		{Quirk::LabelStringNotPersistent, "Q-NUKE-009", "LabelStringNotPersistent", "parameters", {{kNuke, kOpen, kOpen}}},

		{Quirk::NatronFakeHostName, "Q-NATRON-001", "NatronFakeHostName", "registration/loading", {{kNatron, kOpen, kOpen}}},
		{Quirk::ScreenPixelRatioProvided, "Q-NATRON-003", "ScreenPixelRatioProvided", "HiDPI", {{kNatron, kOpen, kOpen}}},
		{Quirk::FullDynamicUi, "Q-NATRON-005", "FullDynamicUi", "parameters", {{kNatron, kOpen, kOpen}}},
		{Quirk::MarkdownDescriptionsOK, "Q-NATRON-006", "MarkdownDescriptionsOK", "UI/dialogs", {{kNatron, kOpen, kOpen}}},
	};
}

}  // namespace

const std::vector<QuirkInfo>& quirkRegistry()
{
	static const std::vector<QuirkInfo> registry = makeRegistry();
	return registry;
}

const QuirkInfo& quirkInfo(Quirk key)
{
	const auto& registry = quirkRegistry();
	for (const QuirkInfo& info : registry) {
		if (info.key == key) {
			return info;
		}
	}
	return registry.front();
}

std::string_view toString(Quirk key) noexcept
{
	for (const QuirkInfo& info : quirkRegistry()) {
		if (info.key == key) {
			return info.name;
		}
	}
	return "";
}

bool parseQuirk(std::string_view name, Quirk& out) noexcept
{
	for (const QuirkInfo& info : quirkRegistry()) {
		if (info.name == name || info.registryId == name) {
			out = info.key;
			return true;
		}
	}
	return false;
}

bool quirkMatchesHost(Quirk key, std::string_view hostName, Version hostVersion) noexcept
{
	const QuirkInfo& info = quirkInfo(key);
	if (info.key != key) {
		return false;
	}
	for (const QuirkMatch& match : info.matches) {
		if (!match.hostNamePrefix.empty() && hostName.substr(0, match.hostNamePrefix.size()) != match.hostNamePrefix) {
			continue;
		}
		if (hostVersion < match.minVersion) {
			continue;
		}
		const bool openUpper = match.maxVersion == Version{0, 0, 0};
		if (!openUpper && hostVersion > match.maxVersion) {
			continue;
		}
		return true;
	}
	return false;
}

OFXGRIP_NAMESPACE_END

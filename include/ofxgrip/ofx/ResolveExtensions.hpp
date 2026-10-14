// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  ResolveExtensions.hpp
	@brief The property and parameter-type names DaVinci Resolve adds beyond the OpenFX standard.

	Introduced in OfxGrip 0.1.0. These are the names Resolve's host reads and writes
	(Q-RESOLVE-024, Q-RESOLVE-029, Q-RESOLVE-032). OfxGrip declares them itself so no Blackmagic
	header is needed. Code reaches them through `HostProfile::has(Quirk::ResolveOfxExtensions)`,
	never through a host-name test.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

OFXGRIP_NAMESPACE_BEGIN
namespace resolve {

/*! Which Resolve page created the instance: `Edit`, `Color`, or `Fusion`. An in-argument of the
    create-instance action. */
inline constexpr const char* kPropPage = "OfxImageEffectPropResolvePage";
/*! On the plug-in descriptor, `"true"` means the output of a pixel depends only on that pixel, so
    Resolve may run the effect during LUT generation. */
inline constexpr const char* kPropNoSpatialAwareness = "OfxImageEffectPropNoSpatialAwareness";
/*! The source media path on the instance, available on the Edit and Color pages. */
inline constexpr const char* kPropSourceFilePath = "OfxImageEffectPropSrcFilePath";
/*! The source frame being rendered; an in-argument of the render action. */
inline constexpr const char* kPropSourceFrame = "OfxImageEffectPropSrcFrame";
/*! Which eye is rendered; an in-argument of the render action: 0 for mono or left, 1 for right. */
inline constexpr const char* kPropEyeToRender = "OfxImageEffectPropEyeToRender";
/*! On a clip instance: 1 when the fetched image is for a thumbnail. */
inline constexpr const char* kClipPropThumbnail = "kOfxImageClipPropThumbnail";
/*! The string-valued choice parameter type that predates OpenFX 1.5's `StrChoice`. */
inline constexpr const char* kParamTypeStrChoice = "OfxParamTypeStrChoice";
/*! The option strings of a `StrChoice` parameter. */
inline constexpr const char* kParamPropChoiceEnum = "OfxParamPropChoiceEnum";
/*! Host descriptor: whether `StrChoice` parameters animate. */
inline constexpr const char* kHostPropSupportsStrChoiceAnimation = "OfxParamHostPropSupportsStrChoiceAnimation";
/*! Descriptor: `"true"` when the plug-in accepts planar RRRGGGBBB images (undeclared by OfxGrip). */
inline constexpr const char* kPropPlanarIOSupported = "OfxImageEffectPropPlanarIOSupported";
/*! Render in-argument: 1 when images are planar. */
inline constexpr const char* kPropPlanarIOEnabled = "OfxImageEffectPropPlanarIOEnabled";
/*! The page names. */
inline constexpr const char* kPageEdit = "Edit";
inline constexpr const char* kPageColor = "Color";
inline constexpr const char* kPageFusion = "Fusion";

}  // namespace resolve
OFXGRIP_NAMESPACE_END

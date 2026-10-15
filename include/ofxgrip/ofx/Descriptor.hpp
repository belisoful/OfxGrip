// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Descriptor.hpp
	@brief Lowers an effect manifest to OpenFX descriptor properties, clips, and parameters.

	Introduced in OfxGrip 0.1.0. The lowering applies the registry's handling rules: every numeric
	parameter gets both a hard range and a display range (Q-RESOLVE-004), `animates` is written
	for every parameter (Q-RESOLVE-015), integer vectors become separate integers on hosts without
	them (Q-RESOLVE-012), and labels are sanitized for the legacy cache writer (Q-OFX-ANY-001).
	The type table follows `Research/Comparison.md` §5.4.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/HostProfile.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/ofx/Suites.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! What the lowering did and did not declare. */
struct LoweringReport {
	std::vector<std::string> omitted;        /*!< Parameters that have no OpenFX form on this host. */
	std::vector<std::string> warnings;
	std::vector<std::string> rejectedProperties;  /*!< Extension properties the host refused. */
	std::map<ParameterId, std::string> declaredNames; /*!< ID → OpenFX parameter name. */
};

/*! The OpenFX parameter type a manifest type lowers to, or null when it has no form. */
const char* ofxParameterTypeFor(ParameterType type, const HostProfile& profile) noexcept;

/*! Sanitizes a label for hosts whose plug-in cache writer escapes only quotes (Q-OFX-ANY-001).
    Strict mode keeps printable ASCII except `<`, `>`, and `&`. */
std::string sanitizeLabel(const std::string& text, bool strict);

/*! Lowers the describe-time properties onto the effect descriptor. */
Error lowerEffectDescriptor(const EffectManifest& manifest, const HostProfile& profile, const Suites& suites, OfxImageEffectHandle descriptor, LoweringReport& report);

/*! What the lowering needs beyond the manifest: where the bundle and the user support folder
    are, so a presets control lists the presets present at describe time. */
struct LoweringContext {
	std::filesystem::path bundlePath;
	std::filesystem::path userSupportDirectory;
	bool debugAllowed{true};  /*!< `EffectFactory::allowsDebugFeatures()`. */
};

/*! Lowers the clips and parameters for one context. */
Error lowerContextDescriptor(const EffectManifest& manifest, const HostProfile& profile, const Suites& suites, OfxImageEffectHandle descriptor, EffectKind context, LoweringReport& report, const LoweringContext& lowering = {});

/*! The hidden framework parameters appended to a manifest: the instance state string, and the
    About and Debug menus when the manifest asks for them (`about`, `debugMenu`). */
std::vector<ParameterSpec> frameworkParameters(const EffectManifest& manifest = {}, bool debugAllowed = true);

/*! The OpenFX names and IDs of the framework menus. */
inline constexpr const char* kAboutMenuParameterKey = "ofxgrip_about";
inline constexpr ParameterId kParameterIdAboutMenu = 9993;
inline constexpr const char* kDebugMenuParameterKey = "ofxgrip_debug";
inline constexpr ParameterId kParameterIdDebugMenu = 9994;

/*! The OpenFX name of the hidden instance-state parameter. */
inline constexpr const char* kInstanceStateParameterKey = "ofxgrip_state";

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  ManifestJson.hpp
	@brief Reads an effect manifest from JSON and writes one back.

	Introduced in OfxGrip 0.1.0. The JSON shape follows `Research/Comparison.md` §5.4: a
	`schemaVersion`, an `effect` object, a `parameters` array, optional `clips`, `capabilities`,
	and `migrations`. Line and block comments are accepted. Unknown keys are ignored, so a newer
	manifest loads on an older OfxGrip.
*/
#pragma once

#include <ofxgrip/core/Manifest.hpp>

#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! Parses a manifest. On failure the error names the offending path. The result is not yet
    validated; call `EffectManifest::validate`. */
Error parseManifestJson(std::string_view json, EffectManifest& out);

/*! Serializes a manifest with two-space indentation. */
std::string writeManifestJson(const EffectManifest& manifest);

OFXGRIP_NAMESPACE_END

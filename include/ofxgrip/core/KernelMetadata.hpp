// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelMetadata.hpp
	@brief The metadata block a kernel's Slang source carries: the parameters it declares, the
	       keys that bind them to its globals, and its color space.

	Introduced in OfxGrip 0.4.0. This is FxGrip's `FxGripShaderMetadata` read from a Slang
	source. A metadata block is a block comment whose opener starts a line and is followed by the
	word `FxGripShader`; the body is JSON5, so it accepts unquoted keys, trailing commas, single
	quotes, and `//` comments. A source may hold several blocks: their parameter arrays
	concatenate in source order, and any other top-level key may appear in one block only.

	A parameter record uses the manifest's JSON vocabulary (`ManifestJson.hpp`) plus four keys:

	- `key` → the parameter's key and the Slang identifier of the global it binds to.
	- `constant` → the value binds a specialization constant named by `key`.
	- `allowContinuousConstant` → a float type may bind a specialization constant.
	- `pixelUnits` → the value is a length in image-space units that the render scales to
	  destination pixels. It requires `key`.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The meta names the manifest reader stores the kernel keys under on a `ParameterSpec`. */
inline constexpr std::string_view kKernelMetaConstant = "kernelConstant";
inline constexpr std::string_view kKernelMetaPixelUnits = "kernelPixelUnits";
inline constexpr std::string_view kKernelMetaContinuousConstant = "kernelContinuousConstant";
/*! Set on a record whose key the scanner derived from its label, so it binds no global. */
inline constexpr std::string_view kKernelMetaDerivedKey = "kernelDerivedKey";

/*! The word that marks a metadata block. */
inline constexpr std::string_view kKernelMetadataMarker = "FxGripShader";

/*! The color space a kernel computes in. `primaries` is `host` when the block declares none. */
struct KernelColorSpace {
	std::string transfer{"linear"};
	std::string primaries{"host"};
	bool operator==(const KernelColorSpace&) const = default;
};

/*! How one parameter binds to the kernel's globals. */
struct KernelBinding {
	ParameterId id{kParameterIdNone};
	std::string key;                     /*!< The Slang identifier. */
	ParameterType type{ParameterType::None};
	bool constant{false};
	bool pixelUnits{false};
	bool allowContinuousConstant{false};
	bool operator==(const KernelBinding&) const = default;
};

/*! The scanned metadata. */
struct KernelMetadata {
	KernelColorSpace colorSpace;
	std::vector<ParameterSpec> parameters;  /*!< In source order, groups nested. */
	std::vector<KernelBinding> bindings;    /*!< One per keyed parameter, groups flattened. */
	std::map<std::string, Value> properties; /*!< Other top-level keys, as scalars or JSON text. */
	int blockCount{0};

	/*! Scans every metadata block of a source. No block → an empty result and success. A block
	    that does not parse → `ErrorCode::Codec`; a record that breaks a rule →
	    `ErrorCode::InvalidParameter`. */
	static Error scan(std::string_view source, KernelMetadata& out);

	/*! The binding whose key matches, or null. */
	const KernelBinding* bindingForKey(std::string_view key) const noexcept;
	const KernelBinding* bindingForId(ParameterId id) const noexcept;
	/*! The keys of the constant parameters, in declaration order. */
	std::vector<std::string> constantKeys() const;
};

/*! Rewrites JSON5 text as JSON: unquoted keys quoted, single-quoted strings double-quoted,
    comments removed, trailing commas dropped, and bare leading or trailing decimal points
    completed. Exposed for tests. */
std::string json5ToJson(std::string_view text);

/*! Finds the bodies of every metadata block in a source, in order. */
std::vector<std::string> kernelMetadataBlocks(std::string_view source);

OFXGRIP_NAMESPACE_END

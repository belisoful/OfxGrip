// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelValues.hpp
	@brief The binding plan between a kernel's layout and its metadata, and the packing of
	       parameter values into the uniforms buffer and the specialization constants.

	Introduced in OfxGrip 0.4.0. The plan resolves once per kernel: every uniforms member whose
	name matches a non-constant record's key, every texture named by an image record, and every
	specialization constant named by a constant record. A render then packs the snapshot's values
	by the plan. A record that sets `pixelUnits` holds a length in image-space units, which the
	packer multiplies by the destination's horizontal pixel scale first.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/KernelLayout.hpp>
#include <ofxgrip/core/KernelMetadata.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The resolved bindings. */
struct KernelBindingPlan {
	/*! A uniforms member bound to a parameter. */
	struct Member {
		std::string key;
		ParameterId id{kParameterIdNone};
		std::size_t offset{0};
		KernelScalarType scalarType{KernelScalarType::None};
		unsigned componentCount{0};
		bool pixelUnits{false};
	};
	/*! A specialization constant bound to a parameter. */
	struct Constant {
		std::string key;
		ParameterId id{kParameterIdNone};
		int slotIndex{kKernelNoIndex};
		std::size_t blockOffset{0};
		KernelScalarType scalarType{KernelScalarType::None};
		bool pixelUnits{false};
	};
	/*! A read texture: keyed to an image parameter, or positional over the source images. */
	struct Texture {
		std::string name;
		unsigned mapIndex{0};
		ParameterId id{kParameterIdNone};  /*!< The image parameter, or none for a positional texture. */
		int positional{-1};                /*!< The source image index for a positional texture. */
	};
	std::vector<Member> members;
	std::vector<Constant> constants;
	std::vector<Texture> textures;

	/*! Resolves the bindings. Fails with `ErrorCode::InvalidParameter` when a member or
	    constant has a type the parameter cannot fill, or a keyed texture names a parameter that
	    is not an image, or the destination is keyed. */
	static Error resolve(const KernelLayout& layout, const KernelMetadata& metadata, KernelBindingPlan& out);
};

/*! The values a render packs, keyed by parameter ID. */
using KernelValueMap = std::map<ParameterId, Value>;

/*! One packed specialization constant. */
struct KernelConstantValue {
	int slotIndex{kKernelNoIndex};
	std::size_t blockOffset{0};
	KernelScalarType scalarType{KernelScalarType::None};
	std::array<std::uint8_t, 16> bytes{};
	std::size_t length{0};
};

/*! The components of a value: 1 for scalars, 2 for points, 3 for RGB and 3D points, 4 for RGBA.
    Empty for a string or bytes. */
std::vector<double> kernelComponents(const Value& value);
/*! Writes `count` components converted to a scalar type at `out`; missing components stay zero. */
void writeKernelComponents(KernelScalarType type, unsigned count, const std::vector<double>& components, std::uint8_t* out) noexcept;
/*! Reads `count` components of a scalar type at `in`. */
std::vector<double> readKernelComponents(KernelScalarType type, unsigned count, const std::uint8_t* in) noexcept;

/*! The uniforms buffer for a value set; empty when the kernel declares no uniforms. */
Bytes packKernelUniforms(const KernelLayout& layout, const KernelBindingPlan& plan, const KernelValueMap& values, double pixelScaleX);
/*! The specialization constants for a value set, in the plan's order. A constant without a
    value takes 0. */
std::vector<KernelConstantValue> packKernelConstants(const KernelBindingPlan& plan, const KernelValueMap& values, double pixelScaleX);
/*! The canonical text of the constant values, which keys a pipeline cache. */
std::string kernelConstantSignature(const std::vector<KernelConstantValue>& constants);

OFXGRIP_NAMESPACE_END

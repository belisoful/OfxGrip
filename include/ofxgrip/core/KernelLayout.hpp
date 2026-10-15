// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  KernelLayout.hpp
	@brief The binder that reads a Slang kernel's reflection JSON into the roles the GripKernel
	       contract names.

	Introduced in OfxGrip 0.4.0. `slangc -reflection-json` emits, beside the source it compiles a
	kernel to, the source names, binding indexes, byte offsets, scalar types, and texture access of
	every global the kernel declares. The emitted names are mangled, so the framework binds from
	the JSON and never from a backend's own reflection. This is FxGrip's `FxGripKernelLayout` with
	one addition: the C++ and CUDA targets bind every global by an offset into one globals block
	instead of by slot index, and the layout reads both forms.

	The contract a layout validates:

	- A `ConstantBuffer` whose struct is named `GripTile` → the tile buffer, 544 bytes.
	- Any other `ConstantBuffer` → the uniforms buffer. The kernel declares at most one.
	- A `Texture2D` → a texture with read access; an `RWTexture2D` → the one destination.
	- A `SamplerState` → a sampler.
	- A `[SpecializationConstant]` → a scalar constant. On the C++ target it is a plain global
	  in the block.
	- Exactly one compute entry point whose parameters are system values only.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The reflection JSON major version this release reads. */
inline constexpr unsigned kKernelLayoutVersion = 1;
/*! The name of the GripKernel struct whose constant buffer receives the tile. */
inline constexpr std::string_view kKernelTileStructName = "GripTile";
/*! The byte length of `GripTile`. */
inline constexpr std::size_t kKernelTileLength = 544;
/*! The index that means "no binding". */
inline constexpr int kKernelNoIndex = -1;

/*! The scalar types the reflection JSON names and the framework packs. */
enum class KernelScalarType : std::uint8_t {
	None = 0,   /*!< A type the framework does not pack: an array, a struct, a 64-bit scalar. */
	Float32,
	Float16,
	Int32,
	UInt32,
	Int16,
	UInt16,
	Int8,
	UInt8,
	Bool,       /*!< One byte. */
};

/*! The byte size of a scalar type; 0 for `None`. */
std::size_t kernelScalarSize(KernelScalarType type) noexcept;
/*! The JSON name of a scalar type (`float32`, `int32`, `bool`, …). */
std::string_view toString(KernelScalarType type) noexcept;
/*! Parses a JSON scalar type name. Unknown names give `None`. */
KernelScalarType parseKernelScalarType(std::string_view name) noexcept;

/*! The access a kernel declares on a texture global. */
enum class KernelTextureAccess : std::uint8_t {
	Read = 0,
	ReadWrite,
};

/*! How a layout binds its globals: by slot index per kind (Metal, SPIR-V) or by byte offset into
    one globals block (C++, CUDA). */
enum class KernelBindingForm : std::uint8_t {
	Slots = 0,
	Block,
};

/*! One member of the kernel's uniforms struct. A member whose name matches a parameter's key
    receives that parameter's value at `offset`, converted to `scalarType`. */
struct KernelMember {
	std::string name;
	std::size_t offset{0};
	std::size_t length{0};
	KernelScalarType scalarType{KernelScalarType::None};
	unsigned componentCount{0};  /*!< 1 for a scalar, 2 to 4 for a vector, 0 when not packed. */
};

/*! One texture global. `mapIndex` selects its coordinate map in `GripTile`: the slot index on a
    slot layout, which slangc assigns in declaration order, and the declaration position on a
    block layout. */
struct KernelTexture {
	std::string name;
	unsigned mapIndex{0};
	int slotIndex{kKernelNoIndex};      /*!< The texture index on a slot layout. */
	std::size_t blockOffset{0};         /*!< The offset of the texture object on a block layout. */
	KernelTextureAccess access{KernelTextureAccess::Read};
};

/*! One `SamplerState` global. */
struct KernelSampler {
	std::string name;
	int slotIndex{kKernelNoIndex};
	std::size_t blockOffset{0};
};

/*! One specialization constant. A constant whose name matches a constant parameter's key
    receives that parameter's value: as the function constant at `slotIndex` on a slot layout,
    at `blockOffset` on a block layout. */
struct KernelConstant {
	std::string name;
	int slotIndex{kKernelNoIndex};
	std::size_t blockOffset{0};
	KernelScalarType scalarType{KernelScalarType::None};
};

/*! The validated reflection JSON of one compiled kernel. */
class KernelLayout {
public:
	/*! Reads and validates a reflection JSON. A failure is `ErrorCode::Codec` for unreadable
	    JSON or a version this release does not read, and `ErrorCode::InvalidParameter` for a
	    global, an entry point, or a count that breaks the contract. */
	static Error parse(std::string_view json, KernelLayout& out);

	const std::string& version() const noexcept { return version_; }
	const std::string& entryPoint() const noexcept { return entryPoint_; }
	const std::array<unsigned, 3>& threadGroupSize() const noexcept { return threadGroupSize_; }
	KernelBindingForm form() const noexcept { return form_; }
	/*! The byte length of the globals block on a block layout; 0 on a slot layout. */
	std::size_t globalsLength() const noexcept { return globalsLength_; }

	/*! The uniforms buffer: its slot or offset, its name, its byte length, and its members.
	    `uniformsIndex()` is `kKernelNoIndex` when the kernel declares none. */
	int uniformsIndex() const noexcept { return uniformsIndex_; }
	std::size_t uniformsOffset() const noexcept { return uniformsOffset_; }
	const std::string& uniformsName() const noexcept { return uniformsName_; }
	std::size_t uniformsLength() const noexcept { return uniformsLength_; }
	const std::vector<KernelMember>& members() const noexcept { return members_; }
	bool hasUniforms() const noexcept { return form_ == KernelBindingForm::Block ? hasUniforms_ : uniformsIndex_ != kKernelNoIndex; }

	/*! The tile buffer. */
	int tileIndex() const noexcept { return tileIndex_; }
	std::size_t tileOffset() const noexcept { return tileOffset_; }
	bool hasTile() const noexcept { return form_ == KernelBindingForm::Block ? hasTile_ : tileIndex_ != kKernelNoIndex; }

	/*! Every texture global in declaration order, the destination included. */
	const std::vector<KernelTexture>& textures() const noexcept { return textures_; }
	/*! The one read-write texture. */
	const KernelTexture& destination() const noexcept { return textures_[destination_]; }
	const std::vector<KernelSampler>& samplers() const noexcept { return samplers_; }
	const std::vector<KernelConstant>& constants() const noexcept { return constants_; }

	const KernelMember* memberNamed(std::string_view name) const noexcept;
	const KernelTexture* textureNamed(std::string_view name) const noexcept;
	const KernelConstant* constantNamed(std::string_view name) const noexcept;
	/*! One more than the highest map index, for `GripTile::textureCount`. */
	unsigned textureCount() const noexcept;

private:
	friend class KernelLayoutReader;
	std::string version_;
	std::string entryPoint_;
	std::array<unsigned, 3> threadGroupSize_{8, 8, 1};
	KernelBindingForm form_{KernelBindingForm::Slots};
	std::size_t globalsLength_{0};
	int uniformsIndex_{kKernelNoIndex};
	std::size_t uniformsOffset_{0};
	bool hasUniforms_{false};
	std::string uniformsName_;
	std::size_t uniformsLength_{0};
	std::vector<KernelMember> members_;
	int tileIndex_{kKernelNoIndex};
	std::size_t tileOffset_{0};
	bool hasTile_{false};
	std::vector<KernelTexture> textures_;
	std::size_t destination_{0};
	std::vector<KernelSampler> samplers_;
	std::vector<KernelConstant> constants_;
};

OFXGRIP_NAMESPACE_END

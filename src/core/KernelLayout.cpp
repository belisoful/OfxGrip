// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/KernelLayout.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

constexpr const char* kKindConstantBuffer = "constantBuffer";
constexpr const char* kKindShaderResource = "shaderResource";
constexpr const char* kKindSamplerState = "samplerState";
constexpr const char* kKindSpecializationConstant = "specializationConstant";
constexpr const char* kKindDescriptorTableSlot = "descriptorTableSlot";
constexpr const char* kKindUniform = "uniform";
constexpr const char* kKindStruct = "struct";
constexpr const char* kKindScalar = "scalar";
constexpr const char* kKindVector = "vector";
constexpr const char* kKindResource = "resource";
constexpr const char* kStageCompute = "compute";
constexpr const char* kShapeTexture2D = "texture2D";
constexpr const char* kAccessRead = "read";
constexpr const char* kAccessReadWrite = "readWrite";

Error malformed(const std::string& why)
{
	return Error::make(ErrorCode::Codec, "The kernel reflection JSON is unreadable", why);
}

Error invalid(const std::string& why)
{
	return Error::make(ErrorCode::InvalidParameter, "The kernel reflection JSON breaks the GripKernel contract", why);
}

bool unsignedValue(const Json& j, std::size_t& out)
{
	if (!j.is_number()) {
		return false;
	}
	const double v = j.get<double>();
	if (!std::isfinite(v) || v < 0.0 || v != std::floor(v)) {
		return false;
	}
	out = static_cast<std::size_t>(v);
	return true;
}

std::string stringOf(const Json& j, const char* key)
{
	return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

const Json* objectOf(const Json& j, const char* key)
{
	return j.is_object() && j.contains(key) && j[key].is_object() ? &j[key] : nullptr;
}

const Json* arrayOf(const Json& j, const char* key)
{
	return j.is_object() && j.contains(key) && j[key].is_array() ? &j[key] : nullptr;
}

/*! The scalar type and component count of a scalar or vector type; false for any other kind. */
bool scalarOrVector(const Json* type, KernelScalarType& scalar, unsigned& components)
{
	if (type == nullptr) {
		return false;
	}
	const std::string kind = stringOf(*type, "kind");
	if (kind == kKindScalar) {
		scalar = parseKernelScalarType(stringOf(*type, "scalarType"));
		components = 1;
		return true;
	}
	if (kind == kKindVector) {
		std::size_t count = 0;
		if (!type->contains("elementCount") || !unsignedValue((*type)["elementCount"], count) || count < 1 || count > 4) {
			return false;
		}
		const Json* element = objectOf(*type, "elementType");
		scalar = element == nullptr ? KernelScalarType::None : parseKernelScalarType(stringOf(*element, "scalarType"));
		components = static_cast<unsigned>(count);
		return true;
	}
	return false;
}

/*! The uniform byte size a type reports in its sizes array. */
bool uniformSize(const Json* type, std::size_t& size)
{
	const Json* sizes = type == nullptr ? nullptr : arrayOf(*type, "sizes");
	if (sizes == nullptr) {
		return false;
	}
	for (const Json& entry : *sizes) {
		if (stringOf(entry, "kind") == kKindUniform) {
			return entry.contains("value") && unsignedValue(entry["value"], size);
		}
	}
	return false;
}

}  // namespace

std::size_t kernelScalarSize(KernelScalarType type) noexcept
{
	switch (type) {
	case KernelScalarType::Float32:
	case KernelScalarType::Int32:
	case KernelScalarType::UInt32:
		return 4;
	case KernelScalarType::Float16:
	case KernelScalarType::Int16:
	case KernelScalarType::UInt16:
		return 2;
	case KernelScalarType::Int8:
	case KernelScalarType::UInt8:
	case KernelScalarType::Bool:
		return 1;
	case KernelScalarType::None:
		break;
	}
	return 0;
}

std::string_view toString(KernelScalarType type) noexcept
{
	switch (type) {
	case KernelScalarType::Float32: return "float32";
	case KernelScalarType::Float16: return "float16";
	case KernelScalarType::Int32: return "int32";
	case KernelScalarType::UInt32: return "uint32";
	case KernelScalarType::Int16: return "int16";
	case KernelScalarType::UInt16: return "uint16";
	case KernelScalarType::Int8: return "int8";
	case KernelScalarType::UInt8: return "uint8";
	case KernelScalarType::Bool: return "bool";
	case KernelScalarType::None: break;
	}
	return "none";
}

KernelScalarType parseKernelScalarType(std::string_view name) noexcept
{
	for (KernelScalarType t : {KernelScalarType::Float32, KernelScalarType::Float16, KernelScalarType::Int32, KernelScalarType::UInt32, KernelScalarType::Int16, KernelScalarType::UInt16, KernelScalarType::Int8, KernelScalarType::UInt8, KernelScalarType::Bool}) {
		if (toString(t) == name) {
			return t;
		}
	}
	return KernelScalarType::None;
}

/*! Walks one reflection JSON object and fills a layout; the first broken rule stops it. */
class KernelLayoutReader {
public:
	explicit KernelLayoutReader(KernelLayout& out) : out_(out) {}

	Error read(const Json& root)
	{
		if (Error e = readVersion(root)) {
			return e;
		}
		if (Error e = readEntryPoints(root)) {
			return e;
		}
		if (Error e = readParameters(root)) {
			return e;
		}
		return checkDestination();
	}

private:
	Error readVersion(const Json& root)
	{
		const std::string version = stringOf(root, "version");
		const std::size_t dot = version.find('.');
		unsigned major = 0;
		bool ok = !version.empty() && dot != std::string::npos && dot > 0;
		if (ok) {
			for (std::size_t i = 0; i < dot; ++i) {
				if (version[i] < '0' || version[i] > '9') {
					ok = false;
					break;
				}
				major = major * 10 + static_cast<unsigned>(version[i] - '0');
			}
		}
		if (!ok || major != kKernelLayoutVersion) {
			return malformed("the version \"" + version + "\" is not one this release reads (" + std::to_string(kKernelLayoutVersion) + ".x)");
		}
		out_.version_ = version;
		return Error::none();
	}

	Error readEntryPoints(const Json& root)
	{
		const Json* entryPoints = arrayOf(root, "entryPoints");
		if (entryPoints == nullptr || entryPoints->size() != 1) {
			return invalid("the kernel declares " + std::to_string(entryPoints == nullptr ? 0 : entryPoints->size()) + " entry points; the contract declares one");
		}
		const Json& entry = (*entryPoints)[0];
		const std::string name = stringOf(entry, "name");
		if (name.empty()) {
			return invalid("the entry point has no name");
		}
		if (stringOf(entry, "stage") != kStageCompute) {
			return invalid("the entry point \"" + name + "\" is not a compute entry point");
		}
		const Json* size = arrayOf(entry, "threadGroupSize");
		if (size == nullptr || size->size() != 3) {
			return invalid("the entry point \"" + name + "\" has no threadgroup size of three counts");
		}
		for (std::size_t axis = 0; axis < 3; ++axis) {
			std::size_t count = 0;
			if (!unsignedValue((*size)[axis], count) || count == 0) {
				return invalid("the entry point \"" + name + "\" has no threadgroup size of three positive counts");
			}
			out_.threadGroupSize_[axis] = static_cast<unsigned>(count);
		}
		if (const Json* parameters = arrayOf(entry, "parameters")) {
			for (const Json& parameter : *parameters) {
				if (parameter.is_object() && parameter.contains("binding")) {
					return invalid("the entry point parameter \"" + stringOf(parameter, "name") + "\" has a binding; the entry point takes system values only");
				}
			}
		}
		out_.entryPoint_ = name;
		return Error::none();
	}

	Error readParameters(const Json& root)
	{
		const Json* parameters = arrayOf(root, "parameters");
		if (parameters == nullptr) {
			return malformed("the JSON has no \"parameters\" array");
		}
		// The binding form is decided by the first global: every global of a C++ or CUDA layout
		// binds by uniform offset, every global of a Metal layout by a kind and slot.
		std::size_t position = 0;
		for (const Json& parameter : *parameters) {
			const std::string name = stringOf(parameter, "name");
			const Json* binding = objectOf(parameter, "binding");
			const std::string kind = binding == nullptr ? std::string() : stringOf(*binding, "kind");
			if (name.empty() || kind.empty()) {
				return invalid("the global at position " + std::to_string(position) + " has no name and binding kind");
			}
			if (position == 0) {
				out_.form_ = kind == kKindUniform ? KernelBindingForm::Block : KernelBindingForm::Slots;
			}
			std::size_t index = 0;
			std::size_t offset = 0;
			std::size_t size = 0;
			if (out_.form_ == KernelBindingForm::Block) {
				if (kind != kKindUniform || !binding->contains("offset") || !unsignedValue((*binding)["offset"], offset) || !binding->contains("size") || !unsignedValue((*binding)["size"], size)) {
					return invalid("the global \"" + name + "\" has no uniform offset and size on a block layout");
				}
				out_.globalsLength_ = std::max(out_.globalsLength_, offset + size);
			} else if (!binding->contains("index") || !unsignedValue((*binding)["index"], index)) {
				return invalid("the global \"" + name + "\" has no binding index");
			}
			const Json* type = objectOf(parameter, "type");
			if (Error e = addGlobal(name, kind, static_cast<int>(index), offset, type)) {
				return e;
			}
			++position;
		}
		return Error::none();
	}

	Error addGlobal(const std::string& name, const std::string& kind, int index, std::size_t offset, const Json* type)
	{
		const std::string typeKind = type == nullptr ? std::string() : stringOf(*type, "kind");
		if (out_.form_ == KernelBindingForm::Block) {
			// A block layout names the role through the type.
			if (typeKind == kKindConstantBuffer) {
				return addConstantBuffer(name, index, offset, type);
			}
			if (typeKind == kKindResource) {
				return addTexture(name, index, offset, type);
			}
			if (typeKind == kKindSamplerState) {
				out_.samplers_.push_back(KernelSampler{name, kKernelNoIndex, offset});
				return Error::none();
			}
			if (typeKind == kKindScalar) {
				return addConstant(name, index, offset, type);
			}
			return invalid("the global \"" + name + "\" has the type kind \"" + typeKind + "\", which the contract does not bind");
		}
		if (kind == kKindConstantBuffer) {
			return addConstantBuffer(name, index, offset, type);
		}
		if (kind == kKindShaderResource || kind == kKindDescriptorTableSlot) {
			return addTexture(name, index, offset, type);
		}
		if (kind == kKindSamplerState) {
			out_.samplers_.push_back(KernelSampler{name, index, 0});
			return Error::none();
		}
		if (kind == kKindSpecializationConstant) {
			return addConstant(name, index, offset, type);
		}
		return invalid("the global \"" + name + "\" has the binding kind \"" + kind + "\", which the contract does not bind");
	}

	Error addConstantBuffer(const std::string& name, int index, std::size_t offset, const Json* type)
	{
		const Json* element = type == nullptr ? nullptr : objectOf(*type, "elementType");
		if (element == nullptr || stringOf(*element, "kind") != kKindStruct) {
			return invalid("the constant buffer \"" + name + "\" holds no struct; the contract binds a struct");
		}
		std::size_t length = 0;
		if (!uniformSize(element, length)) {
			return invalid("the constant buffer \"" + name + "\" reports no size");
		}
		if (stringOf(*element, "name") == kKernelTileStructName) {
			if (out_.hasTile_) {
				return invalid("the kernel declares two GripTile buffers; the contract binds one tile");
			}
			if (length != kKernelTileLength) {
				return invalid("the kernel's GripTile is " + std::to_string(length) + " bytes and OfxGrip's is " + std::to_string(kKernelTileLength) + "; rebuild the kernel against the shipped GripKernel.slang");
			}
			out_.hasTile_ = true;
			out_.tileIndex_ = out_.form_ == KernelBindingForm::Block ? kKernelNoIndex : index;
			out_.tileOffset_ = offset;
			return Error::none();
		}
		if (out_.hasUniforms_) {
			return invalid("the kernel declares the constant buffers \"" + out_.uniformsName_ + "\" and \"" + name + "\"; the contract binds one uniforms buffer");
		}
		out_.hasUniforms_ = true;
		out_.uniformsIndex_ = out_.form_ == KernelBindingForm::Block ? kKernelNoIndex : index;
		out_.uniformsOffset_ = offset;
		out_.uniformsName_ = name;
		out_.uniformsLength_ = length;
		return addMembers(*element, name);
	}

	Error addMembers(const Json& element, const std::string& bufferName)
	{
		const Json* fields = arrayOf(element, "fields");
		if (fields == nullptr) {
			return Error::none();
		}
		for (const Json& field : *fields) {
			KernelMember member;
			member.name = stringOf(field, "name");
			const Json* binding = objectOf(field, "binding");
			if (member.name.empty() || binding == nullptr || stringOf(*binding, "kind") != kKindUniform || !binding->contains("offset") || !unsignedValue((*binding)["offset"], member.offset)
			    || !binding->contains("size") || !unsignedValue((*binding)["size"], member.length)) {
				return invalid("a member of \"" + bufferName + "\" has no name, uniform offset, and size");
			}
			if (!scalarOrVector(objectOf(field, "type"), member.scalarType, member.componentCount) || member.scalarType == KernelScalarType::None) {
				member.scalarType = KernelScalarType::None;
				member.componentCount = 0;
			}
			out_.members_.push_back(std::move(member));
		}
		return Error::none();
	}

	Error addTexture(const std::string& name, int index, std::size_t offset, const Json* type)
	{
		const std::string shape = type == nullptr ? std::string() : stringOf(*type, "baseShape");
		if (type == nullptr || stringOf(*type, "kind") != kKindResource || shape != kShapeTexture2D) {
			return invalid("the resource \"" + name + "\" is a " + (shape.empty() ? std::string("resource of no shape") : shape) + "; the contract binds Texture2D and RWTexture2D");
		}
		std::string access = stringOf(*type, "access");
		if (access.empty()) {
			access = kAccessRead;
		}
		KernelTexture texture;
		texture.name = name;
		texture.mapIndex = out_.form_ == KernelBindingForm::Block ? static_cast<unsigned>(out_.textures_.size()) : static_cast<unsigned>(index);
		texture.slotIndex = out_.form_ == KernelBindingForm::Block ? kKernelNoIndex : index;
		texture.blockOffset = offset;
		if (access == kAccessReadWrite) {
			texture.access = KernelTextureAccess::ReadWrite;
		} else if (access != kAccessRead) {
			return invalid("the texture \"" + name + "\" has the access \"" + access + "\"; the contract binds read and readWrite");
		}
		out_.textures_.push_back(std::move(texture));
		return Error::none();
	}

	Error addConstant(const std::string& name, int index, std::size_t offset, const Json* type)
	{
		KernelConstant constant;
		constant.name = name;
		unsigned components = 0;
		if (!scalarOrVector(type, constant.scalarType, components) || components != 1 || constant.scalarType == KernelScalarType::None) {
			return invalid("the specialization constant \"" + name + "\" is not a scalar the framework sets");
		}
		constant.slotIndex = out_.form_ == KernelBindingForm::Block ? kKernelNoIndex : index;
		constant.blockOffset = offset;
		out_.constants_.push_back(std::move(constant));
		return Error::none();
	}

	Error checkDestination()
	{
		std::size_t destinations = 0;
		for (std::size_t i = 0; i < out_.textures_.size(); ++i) {
			if (out_.textures_[i].access == KernelTextureAccess::ReadWrite) {
				++destinations;
				out_.destination_ = i;
			}
		}
		if (destinations == 0) {
			return invalid("the kernel declares no RWTexture2D; the contract declares one destination");
		}
		if (destinations > 1) {
			return invalid("the kernel declares " + std::to_string(destinations) + " RWTexture2D globals; the contract declares one destination");
		}
		return Error::none();
	}

	KernelLayout& out_;
};

Error KernelLayout::parse(std::string_view json, KernelLayout& out)
{
	Json root = Json::parse(json, nullptr, false, true);
	if (root.is_discarded()) {
		return malformed("the data is not valid JSON");
	}
	if (!root.is_object()) {
		return malformed("the JSON is not an object");
	}
	KernelLayout layout;
	KernelLayoutReader reader(layout);
	if (Error e = reader.read(root)) {
		return e;
	}
	out = std::move(layout);
	return Error::none();
}

const KernelMember* KernelLayout::memberNamed(std::string_view name) const noexcept
{
	for (const KernelMember& member : members_) {
		if (member.name == name) {
			return &member;
		}
	}
	return nullptr;
}

const KernelTexture* KernelLayout::textureNamed(std::string_view name) const noexcept
{
	for (const KernelTexture& texture : textures_) {
		if (texture.name == name) {
			return &texture;
		}
	}
	return nullptr;
}

const KernelConstant* KernelLayout::constantNamed(std::string_view name) const noexcept
{
	for (const KernelConstant& constant : constants_) {
		if (constant.name == name) {
			return &constant;
		}
	}
	return nullptr;
}

unsigned KernelLayout::textureCount() const noexcept
{
	unsigned count = 0;
	for (const KernelTexture& texture : textures_) {
		count = std::max(count, texture.mapIndex + 1);
	}
	return count;
}

OFXGRIP_NAMESPACE_END

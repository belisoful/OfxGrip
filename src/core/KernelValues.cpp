// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/KernelValues.hpp>

#include <ofxgrip/core/Image.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

namespace {

unsigned expectedComponents(ParameterType type) noexcept
{
	switch (type) {
	case ParameterType::Float:
	case ParameterType::Angle:
	case ParameterType::Percent:
	case ParameterType::Int:
	case ParameterType::Toggle:
	case ParameterType::Menu:
	case ParameterType::Switch:
		return 1;
	case ParameterType::Point:
		return 2;
	case ParameterType::Point3D:
	case ParameterType::RGB:
		return 3;
	case ParameterType::RGBA:
		return 4;
	default:
		return 0;
	}
}

bool isImageType(ParameterType type) noexcept
{
	return type == ParameterType::ImageRef || type == ParameterType::Gradient;
}

double scaledComponent(double value, bool pixelUnits, double pixelScaleX) noexcept
{
	return pixelUnits ? value * pixelScaleX : value;
}

template <typename T>
void storeScalar(std::uint8_t* out, T value) noexcept
{
	std::memcpy(out, &value, sizeof value);
}

template <typename T>
T loadScalar(const std::uint8_t* in) noexcept
{
	T value{};
	std::memcpy(&value, in, sizeof value);
	return value;
}

}  // namespace

std::vector<double> kernelComponents(const Value& value)
{
	switch (value.kind()) {
	case ValueKind::Bool:
		return {value.as<bool>() ? 1.0 : 0.0};
	case ValueKind::Int:
		return {static_cast<double>(value.as<int>())};
	case ValueKind::Double:
		return {value.as<double>()};
	case ValueKind::Point2D:
		return {value.as<Point2D>().x, value.as<Point2D>().y};
	case ValueKind::Point3D:
		return {value.as<Point3D>().x, value.as<Point3D>().y, value.as<Point3D>().z};
	case ValueKind::RGB:
		return {value.as<RGB>().r, value.as<RGB>().g, value.as<RGB>().b};
	case ValueKind::RGBA:
		return {value.as<RGBA>().r, value.as<RGBA>().g, value.as<RGBA>().b, value.as<RGBA>().a};
	default:
		return {};
	}
}

void writeKernelComponents(KernelScalarType type, unsigned count, const std::vector<double>& components, std::uint8_t* out) noexcept
{
	const std::size_t size = kernelScalarSize(type);
	if (size == 0 || out == nullptr) {
		return;
	}
	for (unsigned i = 0; i < count; ++i) {
		const double v = i < components.size() ? components[i] : 0.0;
		std::uint8_t* slot = out + i * size;
		switch (type) {
		case KernelScalarType::Float32:
			storeScalar(slot, static_cast<float>(v));
			break;
		case KernelScalarType::Float16:
			storeScalar(slot, floatToHalf(static_cast<float>(v)));
			break;
		case KernelScalarType::Int32:
			storeScalar(slot, static_cast<std::int32_t>(std::llround(v)));
			break;
		case KernelScalarType::UInt32:
			storeScalar(slot, static_cast<std::uint32_t>(std::max(0.0, std::round(v))));
			break;
		case KernelScalarType::Int16:
			storeScalar(slot, static_cast<std::int16_t>(std::llround(v)));
			break;
		case KernelScalarType::UInt16:
			storeScalar(slot, static_cast<std::uint16_t>(std::max(0.0, std::round(v))));
			break;
		case KernelScalarType::Int8:
			storeScalar(slot, static_cast<std::int8_t>(std::llround(v)));
			break;
		case KernelScalarType::UInt8:
			storeScalar(slot, static_cast<std::uint8_t>(std::max(0.0, std::round(v))));
			break;
		case KernelScalarType::Bool:
			storeScalar(slot, static_cast<std::uint8_t>(v != 0.0 ? 1 : 0));
			break;
		case KernelScalarType::None:
			break;
		}
	}
}

std::vector<double> readKernelComponents(KernelScalarType type, unsigned count, const std::uint8_t* in) noexcept
{
	std::vector<double> out;
	const std::size_t size = kernelScalarSize(type);
	if (size == 0 || in == nullptr) {
		return out;
	}
	for (unsigned i = 0; i < count; ++i) {
		const std::uint8_t* slot = in + i * size;
		switch (type) {
		case KernelScalarType::Float32: out.push_back(loadScalar<float>(slot)); break;
		case KernelScalarType::Float16: out.push_back(halfToFloat(loadScalar<std::uint16_t>(slot))); break;
		case KernelScalarType::Int32: out.push_back(loadScalar<std::int32_t>(slot)); break;
		case KernelScalarType::UInt32: out.push_back(loadScalar<std::uint32_t>(slot)); break;
		case KernelScalarType::Int16: out.push_back(loadScalar<std::int16_t>(slot)); break;
		case KernelScalarType::UInt16: out.push_back(loadScalar<std::uint16_t>(slot)); break;
		case KernelScalarType::Int8: out.push_back(loadScalar<std::int8_t>(slot)); break;
		case KernelScalarType::UInt8: out.push_back(loadScalar<std::uint8_t>(slot)); break;
		case KernelScalarType::Bool: out.push_back(loadScalar<std::uint8_t>(slot) != 0 ? 1.0 : 0.0); break;
		case KernelScalarType::None: break;
		}
	}
	return out;
}

Error KernelBindingPlan::resolve(const KernelLayout& layout, const KernelMetadata& metadata, KernelBindingPlan& out)
{
	KernelBindingPlan plan;
	for (const KernelMember& member : layout.members()) {
		const KernelBinding* binding = metadata.bindingForKey(member.name);
		if (binding == nullptr || binding->constant) {
			continue;
		}
		const unsigned expected = expectedComponents(binding->type);
		if (expected == 0) {
			return Error::make(ErrorCode::InvalidParameter, "A parameter has a type that binds to no uniform member").with("key", member.name);
		}
		if (member.scalarType == KernelScalarType::None || member.componentCount != expected) {
			return Error::make(ErrorCode::InvalidParameter, "The uniform member needs a scalar or vector of " + std::to_string(expected) + " components").with("key", member.name);
		}
		plan.members.push_back(Member{member.name, binding->id, member.offset, member.scalarType, member.componentCount, binding->pixelUnits});
	}
	int positional = 0;
	for (const KernelTexture& texture : layout.textures()) {
		const KernelBinding* binding = metadata.bindingForKey(texture.name);
		if (texture.access == KernelTextureAccess::ReadWrite) {
			if (binding != nullptr) {
				return Error::make(ErrorCode::InvalidParameter, "The destination texture cannot bind a parameter").with("key", texture.name);
			}
			continue;
		}
		Texture bound;
		bound.name = texture.name;
		bound.mapIndex = texture.mapIndex;
		if (binding == nullptr) {
			bound.positional = positional++;
		} else {
			if (!isImageType(binding->type)) {
				return Error::make(ErrorCode::InvalidParameter, "A parameter has a type that binds to no texture").with("key", texture.name);
			}
			bound.id = binding->id;
		}
		plan.textures.push_back(std::move(bound));
	}
	for (const std::string& key : metadata.constantKeys()) {
		const KernelConstant* constant = layout.constantNamed(key);
		if (constant == nullptr) {
			continue;
		}
		const KernelBinding* binding = metadata.bindingForKey(key);
		if (expectedComponents(binding->type) != 1) {
			return Error::make(ErrorCode::InvalidParameter, "A specialization constant is a scalar and the parameter binds more components").with("key", key);
		}
		plan.constants.push_back(Constant{key, binding->id, constant->slotIndex, constant->blockOffset, constant->scalarType, binding->pixelUnits});
	}
	out = std::move(plan);
	return Error::none();
}

Bytes packKernelUniforms(const KernelLayout& layout, const KernelBindingPlan& plan, const KernelValueMap& values, double pixelScaleX)
{
	if (!layout.hasUniforms()) {
		return {};
	}
	Bytes bytes(layout.uniformsLength(), 0);
	for (const KernelBindingPlan::Member& member : plan.members) {
		auto it = values.find(member.id);
		if (it == values.end()) {
			continue;
		}
		std::vector<double> components = kernelComponents(it->second);
		for (double& c : components) {
			c = scaledComponent(c, member.pixelUnits, pixelScaleX);
		}
		const std::size_t needed = member.offset + kernelScalarSize(member.scalarType) * member.componentCount;
		if (needed <= bytes.size()) {
			writeKernelComponents(member.scalarType, member.componentCount, components, bytes.data() + member.offset);
		}
	}
	return bytes;
}

std::vector<KernelConstantValue> packKernelConstants(const KernelBindingPlan& plan, const KernelValueMap& values, double pixelScaleX)
{
	std::vector<KernelConstantValue> out;
	for (const KernelBindingPlan::Constant& constant : plan.constants) {
		KernelConstantValue value;
		value.slotIndex = constant.slotIndex;
		value.blockOffset = constant.blockOffset;
		value.scalarType = constant.scalarType;
		value.length = kernelScalarSize(constant.scalarType);
		std::vector<double> components;
		if (auto it = values.find(constant.id); it != values.end()) {
			components = kernelComponents(it->second);
		}
		for (double& c : components) {
			c = scaledComponent(c, constant.pixelUnits, pixelScaleX);
		}
		writeKernelComponents(constant.scalarType, 1, components, value.bytes.data());
		out.push_back(value);
	}
	return out;
}

std::string kernelConstantSignature(const std::vector<KernelConstantValue>& constants)
{
	std::ostringstream out;
	for (const KernelConstantValue& c : constants) {
		out << (c.slotIndex == kKernelNoIndex ? "@" + std::to_string(c.blockOffset) : std::to_string(c.slotIndex)) << "=";
		for (std::size_t i = 0; i < c.length; ++i) {
			out << std::hex << static_cast<int>(c.bytes[i]) << std::dec;
			if (i + 1 < c.length) {
				out << '.';
			}
		}
		out << ';';
	}
	return out.str();
}

OFXGRIP_NAMESPACE_END

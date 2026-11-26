// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/ValueDictionary.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kMagic = "OGVD";
constexpr std::uint32_t kVersion = 1;
constexpr std::uint8_t kNodeValue = 0;
constexpr std::uint8_t kNodeArray = 1;
constexpr std::uint8_t kNodeMap = 2;
constexpr int kMaximumDepth = 64;
constexpr std::uint32_t kMaximumPaths = 65536;

double lerp(double left, double right, double weight) noexcept
{
	return (right - left) * weight + left;
}

std::string joined(std::string_view path, std::string_view key)
{
	std::string out(path);
	if (!out.empty()) {
		out.push_back('/');
	}
	out.append(key);
	return out;
}

struct Blender {
	const std::vector<std::string>& exemptPaths;
	const ValueDictionary::CustomInterpolator& custom;
	double weight;

	std::optional<Value> blendValue(const Value& left, const Value& right) const
	{
		switch (left.kind()) {
		case ValueKind::Int: return Value(static_cast<int>(std::lround((static_cast<double>(right.as<int>()) - static_cast<double>(left.as<int>())) * weight) + left.as<int>()));
		case ValueKind::Double: return Value(lerp(left.as<double>(), right.as<double>(), weight));
		case ValueKind::Point2D: {
			const Point2D a = left.as<Point2D>();
			const Point2D b = right.as<Point2D>();
			return Value(Point2D{lerp(a.x, b.x, weight), lerp(a.y, b.y, weight)});
		}
		case ValueKind::Point3D: {
			const Point3D a = left.as<Point3D>();
			const Point3D b = right.as<Point3D>();
			return Value(Point3D{lerp(a.x, b.x, weight), lerp(a.y, b.y, weight), lerp(a.z, b.z, weight)});
		}
		case ValueKind::RGB: {
			const RGB a = left.as<RGB>();
			const RGB b = right.as<RGB>();
			return Value(RGB{lerp(a.r, b.r, weight), lerp(a.g, b.g, weight), lerp(a.b, b.b, weight)});
		}
		case ValueKind::RGBA: {
			const RGBA a = left.as<RGBA>();
			const RGBA b = right.as<RGBA>();
			return Value(RGBA{lerp(a.r, b.r, weight), lerp(a.g, b.g, weight), lerp(a.b, b.b, weight), lerp(a.a, b.a, weight)});
		}
		case ValueKind::Empty:
		case ValueKind::Bool:
		case ValueKind::String: return left;
		case ValueKind::Blob: break;
		}
		return std::nullopt;
	}

	/*! The blend of two entries, or nothing when the entry drops out. */
	std::optional<ValueNode> blend(const ValueNode& left, const ValueNode* right, const std::string& path) const
	{
		if (right == nullptr) {
			return std::nullopt;
		}
		if (std::find(exemptPaths.begin(), exemptPaths.end(), path) != exemptPaths.end()) {
			return left;
		}
		if (const ValueNode::Array* leftArray = left.array()) {
			const ValueNode::Array* rightArray = right->array();
			if (rightArray == nullptr) {
				return std::nullopt;
			}
			ValueNode::Array out;
			out.reserve(leftArray->size());
			for (std::size_t i = 0; i < leftArray->size(); ++i) {
				if (std::optional<ValueNode> element = blend((*leftArray)[i], i < rightArray->size() ? &(*rightArray)[i] : nullptr, joined(path, std::to_string(i)))) {
					out.push_back(std::move(*element));
				}
			}
			return ValueNode(std::move(out));
		}
		if (const ValueNode::Map* leftMap = left.map()) {
			const ValueNode::Map* rightMap = right->map();
			if (rightMap == nullptr) {
				return std::nullopt;
			}
			return ValueNode(blendMap(*leftMap, *rightMap, path));
		}
		const Value* leftValue = left.value();
		const Value* rightValue = right->value();
		if (rightValue == nullptr || leftValue->kind() != rightValue->kind()) {
			return std::nullopt;
		}
		if (std::optional<Value> blended = blendValue(*leftValue, *rightValue)) {
			return ValueNode(std::move(*blended));
		}
		if (custom) {
			if (std::optional<ValueNode> blended = custom(left, *right, path, weight)) {
				return blended;
			}
		}
		return left;
	}

	ValueNode::Map blendMap(const ValueNode::Map& left, const ValueNode::Map& right, const std::string& path) const
	{
		ValueNode::Map out;
		for (const auto& [key, node] : left) {
			if (key.starts_with(kDictionaryPrivatePrefix)) {
				continue;
			}
			const auto it = right.find(key);
			if (std::optional<ValueNode> blended = blend(node, it != right.end() ? &it->second : nullptr, joined(path, key))) {
				out.emplace(key, std::move(*blended));
			}
		}
		return out;
	}
};

void writeNode(ByteWriter& w, const ValueNode& node)
{
	if (const Value* value = node.value()) {
		w.u8(kNodeValue);
		writeValue(w, *value);
		return;
	}
	if (const ValueNode::Array* array = node.array()) {
		w.u8(kNodeArray);
		w.u32(static_cast<std::uint32_t>(array->size()));
		for (const ValueNode& element : *array) {
			writeNode(w, element);
		}
		return;
	}
	const ValueNode::Map& map = *node.map();
	w.u8(kNodeMap);
	w.u32(static_cast<std::uint32_t>(map.size()));
	for (const auto& [key, child] : map) {
		w.str(key);
		writeNode(w, child);
	}
}

bool readNode(ByteReader& r, ValueNode& out, int depth)
{
	std::uint8_t tag = 0;
	if (depth > kMaximumDepth || !r.u8(tag)) {
		return false;
	}
	if (tag == kNodeValue) {
		Value value;
		if (!readValue(r, value)) {
			return false;
		}
		out = ValueNode(std::move(value));
		return true;
	}
	std::uint32_t count = 0;
	if (!r.u32(count)) {
		return false;
	}
	if (tag == kNodeArray) {
		// Each element is read before the next is made, so a count the input cannot hold
		// stops at its first missing byte.
		ValueNode::Array array;
		for (std::uint32_t i = 0; i < count; ++i) {
			ValueNode element;
			if (!readNode(r, element, depth + 1)) {
				return false;
			}
			array.push_back(std::move(element));
		}
		out = ValueNode(std::move(array));
		return true;
	}
	if (tag == kNodeMap) {
		ValueNode::Map map;
		for (std::uint32_t i = 0; i < count; ++i) {
			std::string key;
			ValueNode child;
			if (!r.str(key) || !readNode(r, child, depth + 1)) {
				return false;
			}
			map.insert_or_assign(std::move(key), std::move(child));
		}
		out = ValueNode(std::move(map));
		return true;
	}
	return false;
}

}  // namespace

const ValueNode* ValueDictionary::find(std::string_view key) const noexcept
{
	const auto it = entries_.find(key);
	return it != entries_.end() ? &it->second : nullptr;
}

const Value* ValueDictionary::value(std::string_view key) const noexcept
{
	const ValueNode* node = find(key);
	return node != nullptr ? node->value() : nullptr;
}

void ValueDictionary::set(std::string_view key, ValueNode node)
{
	entries_.insert_or_assign(std::string(key), std::move(node));
}

bool ValueDictionary::remove(std::string_view key)
{
	const auto it = entries_.find(key);
	if (it == entries_.end()) {
		return false;
	}
	entries_.erase(it);
	return true;
}

std::optional<bool> ValueDictionary::boolAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	return v != nullptr && v->is<bool>() ? std::optional<bool>(v->as<bool>()) : std::nullopt;
}

std::optional<double> ValueDictionary::numberAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	if (v == nullptr || !(v->is<double>() || v->is<int>())) {
		return std::nullopt;
	}
	return v->toDouble();
}

std::optional<int> ValueDictionary::intAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	return v != nullptr && v->is<int>() ? std::optional<int>(v->as<int>()) : std::nullopt;
}

std::optional<RGBA> ValueDictionary::rgbaAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	return v != nullptr && v->is<RGBA>() ? std::optional<RGBA>(v->as<RGBA>()) : std::nullopt;
}

std::optional<RGB> ValueDictionary::rgbAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	return v != nullptr && v->is<RGB>() ? std::optional<RGB>(v->as<RGB>()) : std::nullopt;
}

std::optional<std::string> ValueDictionary::stringAt(std::string_view key) const
{
	const Value* v = value(key);
	return v != nullptr && v->is<std::string>() ? std::optional<std::string>(v->as<std::string>()) : std::nullopt;
}

std::optional<Point2D> ValueDictionary::pointAt(std::string_view key) const noexcept
{
	const Value* v = value(key);
	return v != nullptr && v->is<Point2D>() ? std::optional<Point2D>(v->as<Point2D>()) : std::nullopt;
}

bool ValueDictionary::setDefault(std::string_view key, Value value)
{
	if (locked_ && find(key) == nullptr) {
		return false;
	}
	set(key, ValueNode(std::move(value)));
	return true;
}

bool ValueDictionary::setBool(bool value)
{
	return setDefault(kDictionaryBoolKey, Value(value));
}

bool ValueDictionary::setNumber(double value)
{
	return setDefault(kDictionaryFloatKey, Value(value));
}

bool ValueDictionary::setInt(int value)
{
	return setDefault(kDictionaryIntKey, Value(value));
}

bool ValueDictionary::setRgba(RGBA value)
{
	return setDefault(kDictionaryRgbaKey, Value(value));
}

bool ValueDictionary::setRgb(RGB value)
{
	return setDefault(kDictionaryRgbKey, Value(value));
}

bool ValueDictionary::setString(std::string value)
{
	return setDefault(kDictionaryStringKey, Value(std::move(value)));
}

bool ValueDictionary::setPoint(Point2D value)
{
	return setDefault(kDictionaryPointKey, Value(value));
}

ValueDictionary ValueDictionary::interpolated(const ValueDictionary& right, double weight, const CustomInterpolator& custom) const
{
	ValueDictionary out;
	out.locked_ = locked_;
	out.exemptPaths_ = exemptPaths_;
	out.entries_ = Blender{exemptPaths_, custom, weight}.blendMap(entries_, right.entries_, std::string());
	return out;
}

Bytes ValueDictionary::encode() const
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u32(kVersion);
	w.u8(locked_ ? 1 : 0);
	w.u32(static_cast<std::uint32_t>(exemptPaths_.size()));
	for (const std::string& path : exemptPaths_) {
		w.str(path);
	}
	writeNode(w, ValueNode(entries_));
	return out;
}

Error ValueDictionary::decode(std::span<const std::uint8_t> bytes, ValueDictionary& out)
{
	out = ValueDictionary{};
	const auto malformed = []() { return Error::make(ErrorCode::Codec, "The bytes are not a value dictionary"); };
	ByteReader r(bytes);
	std::uint32_t version = 0;
	if (!r.magic(kMagic) || !r.u32(version)) {
		return malformed();
	}
	if (version != kVersion) {
		return Error::make(ErrorCode::Codec, "The value dictionary has a version this build does not read").with("version", std::to_string(version));
	}
	std::uint8_t locked = 0;
	std::uint32_t pathCount = 0;
	if (!r.u8(locked) || locked > 1 || !r.u32(pathCount) || pathCount > kMaximumPaths) {
		return malformed();
	}
	ValueDictionary decoded;
	decoded.locked_ = locked == 1;
	for (std::uint32_t i = 0; i < pathCount; ++i) {
		std::string path;
		if (!r.str(path)) {
			return malformed();
		}
		decoded.exemptPaths_.push_back(std::move(path));
	}
	ValueNode root;
	if (!readNode(r, root, 0) || !root.isMap() || !r.done()) {
		return malformed();
	}
	decoded.entries_ = *root.map();
	out = std::move(decoded);
	return Error::none();
}

OFXGRIP_NAMESPACE_END

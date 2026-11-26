// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  ValueDictionary.hpp
	@brief A keyed value for a custom parameter: scalars, arrays, and nested dictionaries, with
	       typed access, a byte codec, and interpolation between keyframes.

	Introduced in OfxGrip 0.5.0. A custom parameter stores one value. A dictionary lets that
	one value hold several named entries of different types. The well-known keys map each
	parameter type to its entry, so an effect reads and writes a bool, a number, a color, a
	point, or a string by type. This is the counterpart of FxGrip's `FxGripDictionary` and
	`FxGripInterpolatingDictionary`.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One entry of a dictionary: a value, an array of entries, or a map of entries. */
class ValueNode {
public:
	using Array = std::vector<ValueNode>;
	using Map = std::map<std::string, ValueNode, std::less<>>;

	ValueNode() = default;
	ValueNode(Value value) : storage_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
	ValueNode(Array array) : storage_(std::move(array)) {}  // NOLINT(google-explicit-constructor)
	ValueNode(Map map) : storage_(std::move(map)) {}        // NOLINT(google-explicit-constructor)

	bool isValue() const noexcept { return std::holds_alternative<Value>(storage_); }
	bool isArray() const noexcept { return std::holds_alternative<Array>(storage_); }
	bool isMap() const noexcept { return std::holds_alternative<Map>(storage_); }
	/*! The value, or null for an array or a map. */
	const Value* value() const noexcept { return std::get_if<Value>(&storage_); }
	const Array* array() const noexcept { return std::get_if<Array>(&storage_); }
	const Map* map() const noexcept { return std::get_if<Map>(&storage_); }

	bool operator==(const ValueNode&) const = default;

private:
	std::variant<Value, Array, Map> storage_;
};

/*! The key the typed bool accessors use. The same names as FxGrip's `kCustomAPI_…Key`. */
inline constexpr std::string_view kDictionaryBoolKey = "boolValue";
inline constexpr std::string_view kDictionaryFloatKey = "floatValue";
inline constexpr std::string_view kDictionaryIntKey = "intValue";
inline constexpr std::string_view kDictionaryRgbaKey = "rgbaValue";
inline constexpr std::string_view kDictionaryRgbKey = "rgbValue";
inline constexpr std::string_view kDictionaryStringKey = "stringValue";
inline constexpr std::string_view kDictionaryPointKey = "xyValue";
/*! A key with this prefix is private to one keyframe: interpolation leaves it out. */
inline constexpr std::string_view kDictionaryPrivatePrefix = "_";

/*! The dictionary. */
class ValueDictionary {
public:
	/*! Blends two entries the dictionary cannot blend itself. `path` is the entry's key path,
	    joined with `/`. Returning nothing keeps the left entry. */
	using CustomInterpolator = std::function<std::optional<ValueNode>(const ValueNode& left, const ValueNode& right, std::string_view path, double weight)>;

	const ValueNode::Map& entries() const noexcept { return entries_; }
	bool empty() const noexcept { return entries_.empty(); }
	/*! The entry at a key, or null. */
	const ValueNode* find(std::string_view key) const noexcept;
	/*! The value at a key, or null when the key is absent or holds an array or a map. */
	const Value* value(std::string_view key) const noexcept;
	void set(std::string_view key, ValueNode node);
	/*! Removes a key. Returns false when it is absent. */
	bool remove(std::string_view key);

	/*! When locked, a default typed setter writes only a key that already exists. */
	bool locked() const noexcept { return locked_; }
	void setLocked(bool locked) noexcept { locked_ = locked; }

	/*! The key paths interpolation copies from the left keyframe. A path joins keys and
	    array indexes with `/`: `curve/points/0`. */
	const std::vector<std::string>& exemptPaths() const noexcept { return exemptPaths_; }
	void setExemptPaths(std::vector<std::string> paths) { exemptPaths_ = std::move(paths); }

	/*! The typed readers. Each returns nothing when the key is absent or holds another type.
	    A number reads as a double from an int or a double, and as an int from an int. */
	std::optional<bool> boolAt(std::string_view key = kDictionaryBoolKey) const noexcept;
	std::optional<double> numberAt(std::string_view key = kDictionaryFloatKey) const noexcept;
	std::optional<int> intAt(std::string_view key = kDictionaryIntKey) const noexcept;
	std::optional<RGBA> rgbaAt(std::string_view key = kDictionaryRgbaKey) const noexcept;
	std::optional<RGB> rgbAt(std::string_view key = kDictionaryRgbKey) const noexcept;
	std::optional<std::string> stringAt(std::string_view key = kDictionaryStringKey) const;
	std::optional<Point2D> pointAt(std::string_view key = kDictionaryPointKey) const noexcept;

	/*! The default typed setters, on the type's well-known key. Each returns false and writes
	    nothing when the dictionary is locked and the key is absent. */
	bool setBool(bool value);
	bool setNumber(double value);
	bool setInt(int value);
	bool setRgba(RGBA value);
	bool setRgb(RGB value);
	bool setString(std::string value);
	bool setPoint(Point2D value);

	/*! The dictionary blended toward `right`, for a value between two keyframes.

	    - A key whose name starts with `_` is left out.
	    - A key absent on the right, or holding another type there, is left out.
	    - A string, a bool, a byte string, and an entry on an exempt path copy from the left.
	    - An int rounds its blend. A double, a point, and a color blend by component.
	    - An array blends element by element over the left's length. A map blends key by key.

	    The result keeps this dictionary's lock and exempt paths. */
	ValueDictionary interpolated(const ValueDictionary& right, double weight, const CustomInterpolator& custom = {}) const;

	/*! The dictionary as bytes: the same bytes on every platform. */
	Bytes encode() const;
	/*! Reads a dictionary `encode` wrote. Other bytes leave `out` empty and return an error. */
	static Error decode(std::span<const std::uint8_t> bytes, ValueDictionary& out);

	bool operator==(const ValueDictionary&) const = default;

private:
	bool setDefault(std::string_view key, Value value);

	ValueNode::Map entries_;
	std::vector<std::string> exemptPaths_;
	bool locked_{false};
};

OFXGRIP_NAMESPACE_END

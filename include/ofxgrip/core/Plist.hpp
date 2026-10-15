// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Plist.hpp
	@brief A small XML property-list model, reader, and writer, for the `.fxpreset` file form.

	Introduced in OfxGrip 0.3.0. Apple's XML plist is the interchange format FxFactory and FxGrip
	use for presets, so OfxGrip reads and writes it on every platform without Foundation. The
	model covers the plist types: dict (ordered), array, string, integer, real, true, false, data
	(base64), and date (kept as its ISO 8601 text). The reader accepts the files Foundation
	writes; the writer produces files Foundation reads.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

class PlistValue;
using PlistArray = std::vector<PlistValue>;
/*! An ordered dictionary. Keys keep their file order; lookups are linear. */
using PlistDict = std::vector<std::pair<std::string, PlistValue>>;

/*! A plist date, kept as the ISO 8601 text the file carries. */
struct PlistDate {
	std::string iso8601;
	bool operator==(const PlistDate&) const = default;
};

/*! The kinds, in variant order. */
enum class PlistKind : std::uint8_t { Empty = 0, Bool, Integer, Real, String, Data, Date, Array, Dict };

/*! One plist node. */
class PlistValue {
public:
	using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, Bytes, PlistDate, PlistArray, PlistDict>;

	PlistValue() = default;
	PlistValue(bool v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	PlistValue(int v) : storage_(static_cast<std::int64_t>(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(std::int64_t v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	PlistValue(double v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	PlistValue(std::string v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(const char* v) : storage_(std::string(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(Bytes v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(PlistDate v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(PlistArray v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)
	PlistValue(PlistDict v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)

	PlistKind kind() const noexcept { return static_cast<PlistKind>(storage_.index()); }
	bool empty() const noexcept { return storage_.index() == 0; }
	template <typename T>
	bool is() const noexcept
	{
		return std::holds_alternative<T>(storage_);
	}
	template <typename T>
	const T& as() const
	{
		return std::get<T>(storage_);
	}
	template <typename T>
	const T* tryAs() const noexcept
	{
		return std::get_if<T>(&storage_);
	}
	template <typename T>
	T* tryAs() noexcept
	{
		return std::get_if<T>(&storage_);
	}

	/*! Dictionary access. `get` returns null when this is not a dict or the key is absent. */
	const PlistValue* get(std::string_view key) const noexcept;
	PlistValue* get(std::string_view key) noexcept;
	/*! Sets a dictionary entry, replacing an existing key in place. Turns an empty value into
	    a dict. */
	void set(std::string key, PlistValue value);
	/*! A numeric view: bool, integer, and real convert; anything else is `fallback`. */
	double toDouble(double fallback = 0.0) const noexcept;
	std::int64_t toInteger(std::int64_t fallback = 0) const noexcept;
	/*! A string view: a string returns itself; anything else is `fallback`. */
	std::string toString(std::string fallback = {}) const;

	const Storage& storage() const noexcept { return storage_; }
	bool operator==(const PlistValue&) const = default;

private:
	Storage storage_;
};

/*! Parses an XML plist. The root may be any node. */
Error parsePlist(std::string_view xml, PlistValue& out);
/*! Writes an XML plist with the standard header, tab-indented as Foundation writes it. */
std::string writePlist(const PlistValue& root);

/*! Converts a parameter value to its preset file encoding: numbers and strings as themselves,
    booleans as integers (as FxFactory writes toggles), points as `x`/`y`(/`z`) dicts, colors as
    `red`/`green`/`blue`(/`alpha`) dicts, bytes as data. An empty value encodes as an empty dict. */
PlistValue plistFromValue(const Value& value);
/*! The inverse. A dict with color keys reads as RGB or RGBA, with point keys as a point; a
    `<true/>` or `<false/>` reads as a bool. Returns false for an array or an unknown dict. */
bool valueFromPlist(const PlistValue& plist, Value& out);

/*! Base64, shared with the state parameter and plist data nodes. */
std::string base64Encode(const Bytes& bytes);
bool base64Decode(std::string_view text, Bytes& out);

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Value.hpp
	@brief The variant that carries a parameter's value between the host, the snapshot, presets,
	       and meta.

	Introduced in OfxGrip 0.1.0. A `Value` is one of: empty, bool, int, double, 2D point, 3D
	point, RGB, RGBA, string, or bytes. A choice parameter's value is its option index as an
	int; the manifest maps the index to the option's stable key.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! An opaque byte string, for custom parameter values and extension state. */
using Bytes = std::vector<std::uint8_t>;

/*! The kinds a `Value` can hold, in the order of the variant alternatives. The numbers are part
    of the snapshot codec and never change. */
enum class ValueKind : std::uint8_t {
	Empty = 0,
	Bool = 1,
	Int = 2,
	Double = 3,
	Point2D = 4,
	Point3D = 5,
	RGB = 6,
	RGBA = 7,
	String = 8,
	Blob = 9,   /*!< A `Bytes` value. The case is not named `Bytes` because GCC's -Wshadow objects. */
};

/*! A parameter value. */
class Value {
public:
	using Storage = std::variant<std::monostate, bool, int, double, Point2D, Point3D, RGB, RGBA, std::string, Bytes>;

	Value() = default;
	Value(bool v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(int v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(double v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(Point2D v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(Point3D v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(RGB v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(RGBA v) : storage_(v) {}  // NOLINT(google-explicit-constructor)
	Value(std::string v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)
	Value(const char* v) : storage_(std::string(v)) {}  // NOLINT(google-explicit-constructor)
	Value(std::string_view v) : storage_(std::string(v)) {}  // NOLINT(google-explicit-constructor)
	Value(Bytes v) : storage_(std::move(v)) {}  // NOLINT(google-explicit-constructor)

	ValueKind kind() const noexcept { return static_cast<ValueKind>(storage_.index()); }
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

	/*! The value as a double: bool, int, and double convert; everything else yields `fallback`. */
	double toDouble(double fallback = 0.0) const noexcept;
	/*! The value as an int: bool, int, and double (truncated) convert; everything else yields
	    `fallback`. */
	int toInt(int fallback = 0) const noexcept;
	/*! The value as a bool: bool, int, and double (non-zero) convert; a string converts when it is
	    "true" or "false"; everything else yields `fallback`. */
	bool toBool(bool fallback = false) const noexcept;
	/*! A readable rendering, for logs. Bytes render as their length. */
	std::string toString() const;

	const Storage& storage() const noexcept { return storage_; }
	bool operator==(const Value&) const = default;

private:
	Storage storage_;
};

/*! The name of a value kind. */
std::string_view toString(ValueKind kind) noexcept;

OFXGRIP_NAMESPACE_END

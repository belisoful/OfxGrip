// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Types.hpp
	@brief The value types every OfxGrip header shares: identifiers, geometry, time, pixel
	       formats, and the versioned namespace macros.

	Introduced in OfxGrip 0.1.0. Nothing here depends on OpenFX. Coordinates follow the OpenFX
	convention: the origin is at the bottom left, and a rectangle's `x2` and `y2` are exclusive.
*/
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

/*! Opens the `ofxgrip` namespace and its versioned inline namespace. Every public symbol lives
    inside `ofxgrip::v0`, so two plug-ins built on different OfxGrip versions can share one host
    process without symbol collisions. */
#define OFXGRIP_NAMESPACE_BEGIN namespace ofxgrip { inline namespace v0 {
/*! Closes what `OFXGRIP_NAMESPACE_BEGIN` opened. */
#define OFXGRIP_NAMESPACE_END } }

OFXGRIP_NAMESPACE_BEGIN

/*! A parameter's numeric identifier. Declared in the manifest, never derived from a label or a
    position. */
using ParameterId = std::uint32_t;

/*! The identifier that means "no parameter". */
inline constexpr ParameterId kParameterIdNone = 0xFFFFFFFFu;
/*! The identifier of the top-level group that holds every parameter. */
inline constexpr ParameterId kParameterIdRoot = 0;
/*! The first identifier of the range reserved for hidden framework parameters (9991 to 9999). */
inline constexpr ParameterId kParameterIdReservedFirst = 9991;
/*! The last identifier of the reserved range. */
inline constexpr ParameterId kParameterIdReservedLast = 9999;
/*! The identifier FxGrip gives its hidden meta parameter. OfxGrip keeps the meta store inside
    the instance state parameter under a persistence key, so no host parameter uses this ID; it
    stays reserved for record compatibility. */
inline constexpr ParameterId kParameterIdInstanceMeta = 9995;
/*! The hidden parameter that persists the framework's instance state (schema version, extension
    blobs). */
inline constexpr ParameterId kParameterIdInstanceState = 9998;

/*! Whether an identifier lies in the reserved range. */
constexpr bool isReservedParameterId(ParameterId id) noexcept
{
	return id >= kParameterIdReservedFirst && id <= kParameterIdReservedLast;
}

/*! A time in output frames, as OpenFX counts it: 0 is the effect's first frame. */
using Time = double;

/*! A rational number, used for frame rates. */
struct Rational {
	std::int64_t num{0};
	std::int64_t den{1};

	constexpr double value() const noexcept { return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den); }
	constexpr bool operator==(const Rational&) const noexcept = default;
};

/*! A semantic version. */
struct Version {
	unsigned major{0};
	unsigned minor{0};
	unsigned patch{0};

	constexpr auto operator<=>(const Version&) const noexcept = default;
	/*! Parses "1", "1.2", or "1.2.3". Missing fields are 0. Returns false for anything else. */
	static bool parse(std::string_view text, Version& out) noexcept;
	/*! Renders "major.minor.patch". */
	std::string toString() const;
};

/*! A 2D point or vector with double components. */
struct Point2D {
	double x{0};
	double y{0};
	constexpr bool operator==(const Point2D&) const noexcept = default;
};

/*! A 3D point or vector with double components. */
struct Point3D {
	double x{0};
	double y{0};
	double z{0};
	constexpr bool operator==(const Point3D&) const noexcept = default;
};

/*! An RGB color with double components in the parameter's own range. */
struct RGB {
	double r{0};
	double g{0};
	double b{0};
	constexpr bool operator==(const RGB&) const noexcept = default;
};

/*! An RGBA color with double components in the parameter's own range. */
struct RGBA {
	double r{0};
	double g{0};
	double b{0};
	double a{1};
	constexpr bool operator==(const RGBA&) const noexcept = default;
};

/*! An integer rectangle. `x2` and `y2` are exclusive. An empty rectangle has `x2 <= x1` or
    `y2 <= y1`. */
struct RectI {
	int x1{0};
	int y1{0};
	int x2{0};
	int y2{0};

	constexpr int width() const noexcept { return x2 > x1 ? x2 - x1 : 0; }
	constexpr int height() const noexcept { return y2 > y1 ? y2 - y1 : 0; }
	constexpr bool empty() const noexcept { return x2 <= x1 || y2 <= y1; }
	constexpr bool contains(int x, int y) const noexcept { return x >= x1 && x < x2 && y >= y1 && y < y2; }
	constexpr bool operator==(const RectI&) const noexcept = default;

	/*! The intersection of two rectangles. Empty when they do not overlap. */
	constexpr RectI intersect(const RectI& o) const noexcept
	{
		return {std::max(x1, o.x1), std::max(y1, o.y1), std::min(x2, o.x2), std::min(y2, o.y2)};
	}
	/*! The smallest rectangle holding both. An empty operand contributes nothing. */
	constexpr RectI unite(const RectI& o) const noexcept
	{
		if (empty()) {
			return o;
		}
		if (o.empty()) {
			return *this;
		}
		return {std::min(x1, o.x1), std::min(y1, o.y1), std::max(x2, o.x2), std::max(y2, o.y2)};
	}
};

/*! A double rectangle in canonical coordinates. `x2` and `y2` are exclusive. */
struct RectD {
	double x1{0};
	double y1{0};
	double x2{0};
	double y2{0};

	constexpr double width() const noexcept { return x2 > x1 ? x2 - x1 : 0.0; }
	constexpr double height() const noexcept { return y2 > y1 ? y2 - y1 : 0.0; }
	constexpr bool empty() const noexcept { return x2 <= x1 || y2 <= y1; }
	constexpr bool operator==(const RectD&) const noexcept = default;

	constexpr RectD intersect(const RectD& o) const noexcept
	{
		return {std::max(x1, o.x1), std::max(y1, o.y1), std::min(x2, o.x2), std::min(y2, o.y2)};
	}
	constexpr RectD unite(const RectD& o) const noexcept
	{
		if (empty()) {
			return o;
		}
		if (o.empty()) {
			return *this;
		}
		return {std::min(x1, o.x1), std::min(y1, o.y1), std::max(x2, o.x2), std::max(y2, o.y2)};
	}
};

/*! Converts a canonical rectangle to pixels at a render scale and pixel aspect ratio, rounding
    outward so the pixel rectangle covers the canonical one. */
RectI toPixels(const RectD& canonical, Point2D renderScale, double pixelAspect) noexcept;
/*! Converts a pixel rectangle to canonical coordinates. */
RectD toCanonical(const RectI& pixels, Point2D renderScale, double pixelAspect) noexcept;

/*! A pixel component's storage type. */
enum class BitDepth : std::uint8_t {
	None = 0,
	UInt8,
	UInt16,
	Half,
	Float,
};

/*! The bytes one component occupies. */
constexpr std::size_t bytesPerComponent(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::UInt8:
		return 1;
	case BitDepth::UInt16:
	case BitDepth::Half:
		return 2;
	case BitDepth::Float:
		return 4;
	case BitDepth::None:
		break;
	}
	return 0;
}

/*! The components an image carries. */
enum class Components : std::uint8_t {
	None = 0,
	RGBA,
	RGB,
	Alpha,
};

/*! The number of components. */
constexpr std::size_t componentCount(Components c) noexcept
{
	switch (c) {
	case Components::RGBA:
		return 4;
	case Components::RGB:
		return 3;
	case Components::Alpha:
		return 1;
	case Components::None:
		break;
	}
	return 0;
}

/*! An image's field state. */
enum class Field : std::uint8_t {
	None = 0,
	Both,
	Lower,
	Upper,
	Single,
	Doubled,
};

/*! An image's alpha premultiplication. */
enum class Premultiplication : std::uint8_t {
	Opaque = 0,
	Premultiplied,
	Unpremultiplied,
};

/*! The kind of effect, which maps to an OpenFX context. */
enum class EffectKind : std::uint8_t {
	Filter = 0,
	Generator,
	Transition,
	General,
	Retimer,
};

/*! The concurrency an effect's render paths support. Every OfxGrip render path is thread-safe
    (D-003), so the default is FullySafe. */
enum class ThreadSafety : std::uint8_t {
	Unsafe = 0,
	InstanceSafe,
	FullySafe,
};

/*! The name of an `EffectKind`, for logs and JSON. */
std::string_view toString(EffectKind kind) noexcept;
/*! The name of a `BitDepth`. */
std::string_view toString(BitDepth depth) noexcept;
/*! The name of a `Components`. */
std::string_view toString(Components components) noexcept;
/*! The name of a `Field`. */
std::string_view toString(Field field) noexcept;
/*! Parses the names `toString` produces. Returns false when the text matches none. */
bool parseEffectKind(std::string_view text, EffectKind& out) noexcept;
/*! Parses the names `toString` produces. */
bool parseBitDepth(std::string_view text, BitDepth& out) noexcept;

OFXGRIP_NAMESPACE_END

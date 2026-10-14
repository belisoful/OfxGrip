// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  RenderSnapshot.hpp
	@brief The immutable, time-correct snapshot every render reads, and its versioned codec.

	Introduced in OfxGrip 0.1.0. The snapshot implements D-002: it holds every state parameter's
	value at the render time, the host's render information, extension blobs, and the license
	state. The OFX adapter builds one at render start from timed reads; FxGrip builds the same
	shape in `pluginState`. Render reads only this object (D-003). The codec is a little-endian
	tag-length-value stream with a version header, identical on every platform, so a snapshot can
	be cached, logged, compared, and carried across a process boundary.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! What the host told the render. Render scale and field come from the action arguments, never
    from an image (Q-OFX-ANY-016). */
struct RenderInfo {
	Time time{0};
	double frameRate{24.0};
	Point2D renderScale{1, 1};
	Field field{Field::None};
	bool draft{false};
	bool interactive{false};
	bool sequential{false};
	RectI renderWindow;                /*!< The pixel window this render covers. */
	Point2D projectSize{0, 0};
	Point2D projectOffset{0, 0};
	double pixelAspect{1.0};
	std::string colourspace;           /*!< The output colourspace name, when the host reports one. */
	std::string resolvePage;           /*!< `Edit`, `Color`, or `Fusion` on Resolve; else empty. */
	bool operator==(const RenderInfo&) const = default;
};

/*! The license state render code gates on. */
enum class LicenseState : std::uint8_t {
	Unknown = 0,
	Licensed,
	Trial,
	Unlicensed,
};

/*! The snapshot. Immutable once built. */
class RenderSnapshot {
public:
	/*! The codec version this build writes. */
	static constexpr std::uint16_t kCodecVersion = 1;

	const Value* value(ParameterId id) const noexcept;
	bool has(ParameterId id) const noexcept { return value(id) != nullptr; }
	double number(ParameterId id, double fallback = 0.0) const noexcept;
	int integer(ParameterId id, int fallback = 0) const noexcept;
	bool boolean(ParameterId id, bool fallback = false) const noexcept;
	Point2D point(ParameterId id, Point2D fallback = {}) const noexcept;
	Point3D point3D(ParameterId id, Point3D fallback = {}) const noexcept;
	RGB rgb(ParameterId id, RGB fallback = {}) const noexcept;
	RGBA rgba(ParameterId id, RGBA fallback = {}) const noexcept;
	std::string string(ParameterId id, std::string fallback = {}) const;
	const std::map<ParameterId, Value>& values() const noexcept { return values_; }

	const RenderInfo& info() const noexcept { return info_; }
	LicenseState license() const noexcept { return license_; }
	/*! An extension's blob by key, or null. */
	const Bytes* extra(std::string_view key) const noexcept;
	const std::map<std::string, Bytes>& extras() const noexcept { return extras_; }

	/*! Encodes the snapshot. */
	Bytes encode() const;
	/*! Decodes a snapshot. A truncated, wrong-typed, or newer-version stream is an error, never
	    a read past the bytes. */
	static Error decode(std::span<const std::uint8_t> bytes, RenderSnapshot& out);
	bool operator==(const RenderSnapshot&) const = default;

private:
	friend class RenderSnapshotBuilder;
	std::map<ParameterId, Value> values_;
	std::map<std::string, Bytes> extras_;
	RenderInfo info_;
	LicenseState license_{LicenseState::Unknown};
};

/*! Builds a snapshot. The adapter fills the parameter values and the render information, posts
    `BuildSnapshot` so extensions add their blobs, then calls `build`. */
class RenderSnapshotBuilder {
public:
	RenderSnapshotBuilder& set(ParameterId id, Value value);
	RenderSnapshotBuilder& setExtra(std::string key, Bytes data);
	RenderSnapshotBuilder& setInfo(RenderInfo info);
	RenderSnapshotBuilder& setLicense(LicenseState state);
	const RenderInfo& info() const noexcept { return snapshot_.info_; }
	RenderSnapshot build() const { return snapshot_; }

private:
	RenderSnapshot snapshot_;
};

OFXGRIP_NAMESPACE_END

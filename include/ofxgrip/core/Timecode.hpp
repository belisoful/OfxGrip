// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Timecode.hpp
	@brief SMPTE timecode formatting with drop-frame support, independent of any host.

	Introduced in OfxGrip 0.5.0. A frame rate is an exact rational (30000/1001 for 29.97), so
	the frame counts use integer arithmetic. Drop-frame counting skips two frame numbers per
	minute at 29.97, four at 59.94, and eight at 119.88, except in minutes divisible by ten.
	Other rates ignore the drop-frame flag. The `StandardFrameRate` values and their menu names
	match FxGrip's `FxGripFrameRate`, so a stored choice means the same rate in both frameworks.
*/
#pragma once

#include <ofxgrip/core/Types.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! A frame rate as an exact ratio of frames to seconds. */
struct FrameRate {
	std::int64_t numerator{30};
	std::int64_t denominator{1};

	/*! Whether both terms are positive. */
	constexpr bool valid() const noexcept { return numerator > 0 && denominator > 0; }
	/*! The rate in frames per second, or 0 for an invalid rate. */
	constexpr double framesPerSecond() const noexcept { return valid() ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0; }
	/*! The integer rate the timecode fields count against (30 for 29.97), or 0. */
	std::int64_t nominal() const noexcept;
	/*! The exact rate nearest a host's floating-point rate. A rate within 0.01% of an NTSC
	    rate (23.976, 29.97, 59.94, 119.88, …) becomes that rate over 1001. Any other rate
	    becomes a ratio over 1000. A rate that is not positive and finite is invalid. */
	static FrameRate fromFramesPerSecond(double framesPerSecond) noexcept;
	constexpr bool operator==(const FrameRate&) const noexcept = default;
};

/*! The standard frame rates a host or a footage menu offers, in menu order. */
enum class StandardFrameRate : std::int32_t {
	Fps23_976 = 0,
	Fps24,
	Fps25,
	Fps29_97,
	Fps30,
	Fps50,
	Fps59_94,
	Fps60,
	Fps90,
	Fps100,
	Fps119_88,
	Fps120,
};

/*! The exact rate of a standard rate. An unknown value returns 30. */
FrameRate frameRateFor(StandardFrameRate rate) noexcept;
/*! The display names of every `StandardFrameRate`, in enum order, for a choice parameter. */
std::span<const std::string_view> standardFrameRateNames() noexcept;
/*! Whether a rate counts in drop-frame mode: a fractional rate whose nominal rate is 30, 60,
    or 120. */
bool supportsDropFrame(const FrameRate& rate) noexcept;

/*! A frame index split into timecode fields. `frames` counts from zero within the second.
    `dropFrame` is the effective mode: true only when the caller asked for drop-frame and the
    rate supports it. `valid` is false for an invalid rate, and the other fields are then
    zero. */
struct TimecodeComponents {
	std::int64_t hours{0};
	std::int64_t minutes{0};
	std::int64_t seconds{0};
	std::int64_t frames{0};
	std::int64_t nominalFramesPerSecond{0};
	bool dropFrame{false};
	bool valid{false};
	constexpr bool operator==(const TimecodeComponents&) const noexcept = default;
};

/*! The zero-based frame that contains a time in seconds. A time within a millionth of a
    frame below a frame boundary counts as the frame that starts there, so a time a host
    rounded still indexes correctly. Returns 0 for an invalid rate or a time that is not finite. A negative time
    returns a negative index. */
std::int64_t frameIndexForSeconds(double seconds, const FrameRate& rate) noexcept;

/*! Splits a frame index into hours, minutes, seconds, and frames. Hours wrap at 24. A
    negative index counts as zero. */
TimecodeComponents timecodeComponents(std::int64_t frameIndex, const FrameRate& rate, bool dropFrame) noexcept;

/*! The frame index of timecode fields: the inverse of `timecodeComponents` within one day.
    A drop-frame field that names a skipped frame number returns the first frame after the
    skip. Invalid components return 0. */
std::int64_t frameIndexForTimecode(const TimecodeComponents& components, const FrameRate& rate) noexcept;

/*! Formats components as `hh:mm:ss:ff`. Drop-frame mode separates the frames field with a
    semicolon. Rates of 100 and above use three frame digits. Invalid components format as
    `--:--:--:--`. */
std::string timecodeString(const TimecodeComponents& components);
/*! `timecodeComponents` followed by `timecodeString`. */
std::string timecodeString(std::int64_t frameIndex, const FrameRate& rate, bool dropFrame);

OFXGRIP_NAMESPACE_END

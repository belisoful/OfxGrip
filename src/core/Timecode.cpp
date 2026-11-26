// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Timecode.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerTenMinutes = 600;
constexpr std::int64_t kSecondsPerDay = 24 * 3600;

constexpr std::array<std::string_view, 12> kRateNames = {"23.98", "24", "25", "29.97", "30", "50", "59.94", "60", "90", "100", "119.88", "120"};

/*! Frames in a span of seconds at the exact rate, truncated (1798 per minute at 29.97). */
std::int64_t framesInSeconds(std::int64_t seconds, const FrameRate& rate) noexcept
{
	return seconds * rate.numerator / rate.denominator;
}

/*! The frame numbers a drop-frame rate skips at the start of a minute, or 0. */
std::int64_t droppedPerMinute(const FrameRate& rate) noexcept
{
	return rate.nominal() * kSecondsPerMinute - framesInSeconds(kSecondsPerMinute, rate);
}

}  // namespace

std::int64_t FrameRate::nominal() const noexcept
{
	return valid() ? static_cast<std::int64_t>(std::llround(framesPerSecond())) : 0;
}

FrameRate FrameRate::fromFramesPerSecond(double framesPerSecond) noexcept
{
	if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0 || framesPerSecond > 1.0e6) {
		return FrameRate{0, 0};
	}
	const double rounded = std::round(framesPerSecond);
	if (rounded > 0.0 && std::abs(framesPerSecond - rounded) <= rounded * 1.0e-9) {
		return FrameRate{static_cast<std::int64_t>(rounded), 1};
	}
	// An NTSC rate is an integer rate times 1000/1001.
	const double ntscNominal = std::round(framesPerSecond * 1001.0 / 1000.0);
	const double ntsc = ntscNominal * 1000.0 / 1001.0;
	if (ntscNominal > 0.0 && std::abs(framesPerSecond - ntsc) <= ntsc * 1.0e-4) {
		return FrameRate{static_cast<std::int64_t>(ntscNominal) * 1000, 1001};
	}
	return FrameRate{static_cast<std::int64_t>(std::llround(framesPerSecond * 1000.0)), 1000};
}

FrameRate frameRateFor(StandardFrameRate rate) noexcept
{
	switch (rate) {
	case StandardFrameRate::Fps23_976: return {24000, 1001};
	case StandardFrameRate::Fps24: return {24, 1};
	case StandardFrameRate::Fps25: return {25, 1};
	case StandardFrameRate::Fps29_97: return {30000, 1001};
	case StandardFrameRate::Fps30: return {30, 1};
	case StandardFrameRate::Fps50: return {50, 1};
	case StandardFrameRate::Fps59_94: return {60000, 1001};
	case StandardFrameRate::Fps60: return {60, 1};
	case StandardFrameRate::Fps90: return {90, 1};
	case StandardFrameRate::Fps100: return {100, 1};
	case StandardFrameRate::Fps119_88: return {120000, 1001};
	case StandardFrameRate::Fps120: return {120, 1};
	}
	return {30, 1};
}

std::span<const std::string_view> standardFrameRateNames() noexcept
{
	return kRateNames;
}

bool supportsDropFrame(const FrameRate& rate) noexcept
{
	if (!rate.valid()) {
		return false;
	}
	const std::int64_t nominal = rate.nominal();
	if (nominal != 30 && nominal != 60 && nominal != 120) {
		return false;
	}
	// An exact integer rate has nothing to drop.
	return droppedPerMinute(rate) > 0;
}

std::int64_t frameIndexForSeconds(double seconds, const FrameRate& rate) noexcept
{
	if (!rate.valid() || !std::isfinite(seconds)) {
		return 0;
	}
	const double frames = seconds * static_cast<double>(rate.numerator) / static_cast<double>(rate.denominator);
	if (std::abs(frames) > 9.0e15) {
		return 0;
	}
	return static_cast<std::int64_t>(std::floor(frames + 1.0e-6));
}

TimecodeComponents timecodeComponents(std::int64_t frameIndex, const FrameRate& rate, bool dropFrame) noexcept
{
	TimecodeComponents result;
	if (!rate.valid()) {
		return result;
	}
	const std::int64_t nominal = rate.nominal();
	if (nominal <= 0) {
		return result;
	}
	std::int64_t index = std::max<std::int64_t>(frameIndex, 0);
	const bool drops = dropFrame && supportsDropFrame(rate);
	if (drops) {
		const std::int64_t framesPerMinute = framesInSeconds(kSecondsPerMinute, rate);
		const std::int64_t framesPerTenMinutes = framesInSeconds(kSecondsPerTenMinutes, rate);
		const std::int64_t dropped = droppedPerMinute(rate);
		const std::int64_t tenMinuteBlocks = index / framesPerTenMinutes;
		const std::int64_t remainder = index % framesPerTenMinutes;
		std::int64_t skipped = dropped * 9 * tenMinuteBlocks;
		if (remainder > dropped) {
			skipped += dropped * ((remainder - dropped) / framesPerMinute);
		}
		index += skipped;
	}
	const std::int64_t totalSeconds = index / nominal;
	result.frames = index % nominal;
	result.seconds = totalSeconds % 60;
	result.minutes = (totalSeconds / 60) % 60;
	result.hours = (totalSeconds / 3600) % 24;
	result.nominalFramesPerSecond = nominal;
	result.dropFrame = drops;
	result.valid = true;
	return result;
}

std::int64_t frameIndexForTimecode(const TimecodeComponents& components, const FrameRate& rate) noexcept
{
	if (!components.valid || !rate.valid()) {
		return 0;
	}
	const std::int64_t nominal = rate.nominal();
	if (nominal <= 0) {
		return 0;
	}
	const std::int64_t hours = std::clamp<std::int64_t>(components.hours, 0, 23);
	const std::int64_t minutes = std::clamp<std::int64_t>(components.minutes, 0, 59);
	const std::int64_t seconds = std::clamp<std::int64_t>(components.seconds, 0, 59);
	std::int64_t frames = std::clamp<std::int64_t>(components.frames, 0, nominal - 1);
	const std::int64_t totalMinutes = hours * 60 + minutes;
	if (!(components.dropFrame && supportsDropFrame(rate))) {
		return std::min((totalMinutes * 60 + seconds) * nominal + frames, kSecondsPerDay * nominal - 1);
	}
	const std::int64_t dropped = droppedPerMinute(rate);
	// The skipped numbers open every minute that is not a multiple of ten.
	if (seconds == 0 && totalMinutes % 10 != 0) {
		frames = std::max(frames, dropped);
	}
	const std::int64_t counted = (totalMinutes * 60 + seconds) * nominal + frames;
	return counted - dropped * (totalMinutes - totalMinutes / 10);
}

std::string timecodeString(const TimecodeComponents& components)
{
	if (!components.valid) {
		return "--:--:--:--";
	}
	char text[48];
	const char separator = components.dropFrame ? ';' : ':';
	const int frameDigits = components.nominalFramesPerSecond >= 100 ? 3 : 2;
	std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld%c%0*lld", static_cast<long long>(components.hours), static_cast<long long>(components.minutes),
	              static_cast<long long>(components.seconds), separator, frameDigits, static_cast<long long>(components.frames));
	return text;
}

std::string timecodeString(std::int64_t frameIndex, const FrameRate& rate, bool dropFrame)
{
	return timecodeString(timecodeComponents(frameIndex, rate, dropFrame));
}

OFXGRIP_NAMESPACE_END

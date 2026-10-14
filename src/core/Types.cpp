// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Types.hpp>

#include <charconv>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

bool Version::parse(std::string_view text, Version& out) noexcept
{
	Version result;
	unsigned* fields[] = {&result.major, &result.minor, &result.patch};
	std::size_t field = 0;
	std::size_t pos = 0;
	if (text.empty()) {
		return false;
	}
	while (pos <= text.size() && field < 3) {
		std::size_t dot = text.find('.', pos);
		std::string_view piece = text.substr(pos, dot == std::string_view::npos ? std::string_view::npos : dot - pos);
		if (piece.empty()) {
			return false;
		}
		unsigned value = 0;
		auto [ptr, ec] = std::from_chars(piece.data(), piece.data() + piece.size(), value);
		if (ec != std::errc() || ptr != piece.data() + piece.size()) {
			return false;
		}
		*fields[field++] = value;
		if (dot == std::string_view::npos) {
			break;
		}
		pos = dot + 1;
		if (pos == text.size()) {
			return false;
		}
	}
	out = result;
	return true;
}

std::string Version::toString() const
{
	return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

RectI toPixels(const RectD& canonical, Point2D renderScale, double pixelAspect) noexcept
{
	const double par = pixelAspect > 0.0 ? pixelAspect : 1.0;
	const double sx = renderScale.x > 0.0 ? renderScale.x : 1.0;
	const double sy = renderScale.y > 0.0 ? renderScale.y : 1.0;
	RectI r;
	r.x1 = static_cast<int>(std::floor(canonical.x1 * sx / par));
	r.y1 = static_cast<int>(std::floor(canonical.y1 * sy));
	r.x2 = static_cast<int>(std::ceil(canonical.x2 * sx / par));
	r.y2 = static_cast<int>(std::ceil(canonical.y2 * sy));
	return r;
}

RectD toCanonical(const RectI& pixels, Point2D renderScale, double pixelAspect) noexcept
{
	const double par = pixelAspect > 0.0 ? pixelAspect : 1.0;
	const double sx = renderScale.x > 0.0 ? renderScale.x : 1.0;
	const double sy = renderScale.y > 0.0 ? renderScale.y : 1.0;
	RectD r;
	r.x1 = pixels.x1 * par / sx;
	r.y1 = pixels.y1 / sy;
	r.x2 = pixels.x2 * par / sx;
	r.y2 = pixels.y2 / sy;
	return r;
}

std::string_view toString(EffectKind kind) noexcept
{
	switch (kind) {
	case EffectKind::Filter:
		return "filter";
	case EffectKind::Generator:
		return "generator";
	case EffectKind::Transition:
		return "transition";
	case EffectKind::General:
		return "general";
	case EffectKind::Retimer:
		return "retimer";
	}
	return "filter";
}

std::string_view toString(BitDepth depth) noexcept
{
	switch (depth) {
	case BitDepth::None:
		return "none";
	case BitDepth::UInt8:
		return "8u";
	case BitDepth::UInt16:
		return "16u";
	case BitDepth::Half:
		return "16f";
	case BitDepth::Float:
		return "32f";
	}
	return "none";
}

std::string_view toString(Components components) noexcept
{
	switch (components) {
	case Components::None:
		return "none";
	case Components::RGBA:
		return "rgba";
	case Components::RGB:
		return "rgb";
	case Components::Alpha:
		return "alpha";
	}
	return "none";
}

std::string_view toString(Field field) noexcept
{
	switch (field) {
	case Field::None:
		return "none";
	case Field::Both:
		return "both";
	case Field::Lower:
		return "lower";
	case Field::Upper:
		return "upper";
	case Field::Single:
		return "single";
	case Field::Doubled:
		return "doubled";
	}
	return "none";
}

bool parseEffectKind(std::string_view text, EffectKind& out) noexcept
{
	for (EffectKind k : {EffectKind::Filter, EffectKind::Generator, EffectKind::Transition, EffectKind::General, EffectKind::Retimer}) {
		if (toString(k) == text) {
			out = k;
			return true;
		}
	}
	return false;
}

bool parseBitDepth(std::string_view text, BitDepth& out) noexcept
{
	for (BitDepth d : {BitDepth::None, BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		if (toString(d) == text) {
			out = d;
			return true;
		}
	}
	return false;
}

OFXGRIP_NAMESPACE_END

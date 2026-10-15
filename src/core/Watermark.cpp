// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Watermark.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGlyphWidth = 5.0;
constexpr double kGlyphHeight = 7.0;
constexpr double kAdvance = 6.0;  /*!< Cells per character: the glyph plus one blank column. */

/*! The 5×7 font: each glyph is seven rows, top first, with bit 4 the leftmost column. */
struct Glyph {
	char c;
	std::uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'"', {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'&', {0x08, 0x14, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'@', {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'*', {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
};

constexpr std::uint8_t kBlank[7] = {0, 0, 0, 0, 0, 0, 0};

}  // namespace

const std::uint8_t* watermarkGlyph(char c) noexcept
{
	const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	for (const Glyph& glyph : kGlyphs) {
		if (glyph.c == upper) {
			return glyph.rows;
		}
	}
	return kBlank;
}

std::string_view toString(WatermarkStyle style) noexcept
{
	switch (style) {
	case WatermarkStyle::Single: return "single";
	case WatermarkStyle::DiagonalTiled: return "diagonal";
	case WatermarkStyle::Banner: return "banner";
	case WatermarkStyle::Corner: return "corner";
	}
	return "single";
}

std::string_view toString(WatermarkCorner corner) noexcept
{
	switch (corner) {
	case WatermarkCorner::BottomLeft: return "bottomLeft";
	case WatermarkCorner::BottomRight: return "bottomRight";
	case WatermarkCorner::TopLeft: return "topLeft";
	case WatermarkCorner::TopRight: return "topRight";
	}
	return "bottomRight";
}

bool parseWatermarkStyle(std::string_view text, WatermarkStyle& out) noexcept
{
	for (WatermarkStyle s : {WatermarkStyle::Single, WatermarkStyle::DiagonalTiled, WatermarkStyle::Banner, WatermarkStyle::Corner}) {
		if (toString(s) == text) {
			out = s;
			return true;
		}
	}
	if (text == "diagonalTiled") {
		out = WatermarkStyle::DiagonalTiled;
		return true;
	}
	return false;
}

bool parseWatermarkCorner(std::string_view text, WatermarkCorner& out) noexcept
{
	for (WatermarkCorner c : {WatermarkCorner::BottomLeft, WatermarkCorner::BottomRight, WatermarkCorner::TopLeft, WatermarkCorner::TopRight}) {
		if (toString(c) == text) {
			out = c;
			return true;
		}
	}
	return false;
}

WatermarkConfiguration WatermarkConfiguration::unlicensed(std::string text)
{
	WatermarkConfiguration c;
	c.text = std::move(text);
	return c;
}

WatermarkConfiguration WatermarkConfiguration::trial(std::string text)
{
	WatermarkConfiguration c;
	c.text = std::move(text);
	c.style = WatermarkStyle::Corner;
	c.corner = WatermarkCorner::BottomRight;
	c.fontSize = 48;
	c.angleDegrees = 0;
	c.opacity = 0.5;
	return c;
}

Error WatermarkConfiguration::apply(const std::map<std::string, Value>& dictionary)
{
	auto rawString = [](const Value& value) { return value.is<std::string>() ? value.as<std::string>() : value.toString(); };
	for (const auto& [key, value] : dictionary) {
		if (key == "text") {
			text = rawString(value);
		} else if (key == "fontSize") {
			fontSize = value.toDouble(fontSize);
		} else if (key == "color") {
			if (const RGBA* rgba = value.tryAs<RGBA>()) {
				color = *rgba;
			} else if (const RGB* rgb = value.tryAs<RGB>()) {
				color = {rgb->r, rgb->g, rgb->b, 1.0};
			} else {
				return Error::make(ErrorCode::Value, "The watermark colour is not a colour");
			}
		} else if (key == "angleDegrees") {
			angleDegrees = value.toDouble(angleDegrees);
		} else if (key == "opacity") {
			opacity = value.toDouble(opacity);
		} else if (key == "style") {
			if (!parseWatermarkStyle(rawString(value), style)) {
				return Error::make(ErrorCode::Value, "Unknown watermark style").with("style", rawString(value));
			}
		} else if (key == "corner") {
			if (!parseWatermarkCorner(rawString(value), corner)) {
				return Error::make(ErrorCode::Value, "Unknown watermark corner").with("corner", rawString(value));
			}
		} else if (key == "inset") {
			inset = value.toDouble(inset);
		} else if (key == "tileSpacing") {
			if (const Point2D* p = value.tryAs<Point2D>()) {
				tileSpacing = *p;
			} else {
				tileSpacing = {value.toDouble(tileSpacing.x), value.toDouble(tileSpacing.y)};
			}
		}
	}
	return Error::none();
}

std::map<std::string, Value> WatermarkConfiguration::toDictionary() const
{
	std::map<std::string, Value> out;
	out["text"] = Value(text);
	out["fontSize"] = Value(fontSize);
	out["color"] = Value(color);
	out["angleDegrees"] = Value(angleDegrees);
	out["opacity"] = Value(opacity);
	out["style"] = Value(std::string(toString(style)));
	out["corner"] = Value(std::string(toString(corner)));
	out["inset"] = Value(inset);
	out["tileSpacing"] = Value(tileSpacing);
	return out;
}

// ---- compositor ---------------------------------------------------------------------------------

Watermark::Watermark(WatermarkConfiguration configuration) : configuration_(std::move(configuration))
{
	for (char c : configuration_.text) {
		text_.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
	}
	textCells_ = text_.empty() ? 0.0 : static_cast<double>(text_.size()) * kAdvance - 1.0;
}

Watermark::Placement Watermark::placement(Point2D frameSize) const noexcept
{
	Placement p;
	const double heightScale = frameSize.y > 0.0 ? frameSize.y / 1080.0 : 1.0;
	const double fontPixels = std::max(1.0, configuration_.fontSize * heightScale);
	p.scale = fontPixels / kGlyphHeight;
	const double angle = configuration_.style == WatermarkStyle::Banner || configuration_.style == WatermarkStyle::Corner ? 0.0 : configuration_.angleDegrees * kPi / 180.0;
	p.cosA = std::cos(angle);
	p.sinA = std::sin(angle);
	const double textWidth = textCells_ * p.scale;
	const double textHeight = kGlyphHeight * p.scale;
	const double inset = configuration_.inset * heightScale;
	switch (configuration_.style) {
	case WatermarkStyle::Single:
	case WatermarkStyle::DiagonalTiled: {
		// Centered: the origin is the text's left-bottom corner rotated about the frame center.
		const double cx = frameSize.x / 2.0;
		const double cy = frameSize.y / 2.0;
		const double ox = -textWidth / 2.0;
		const double oy = -textHeight / 2.0;
		p.originX = cx + ox * p.cosA - oy * p.sinA;
		p.originY = cy + ox * p.sinA + oy * p.cosA;
		if (configuration_.style == WatermarkStyle::DiagonalTiled) {
			p.tileX = std::max(configuration_.tileSpacing.x * heightScale, textWidth + p.scale * kAdvance) / p.scale;
			p.tileY = std::max(configuration_.tileSpacing.y * heightScale, textHeight * 2.0) / p.scale;
		}
		break;
	}
	case WatermarkStyle::Banner:
		p.originX = frameSize.x / 2.0 - textWidth / 2.0;
		p.originY = inset;
		break;
	case WatermarkStyle::Corner: {
		const bool right = configuration_.corner == WatermarkCorner::BottomRight || configuration_.corner == WatermarkCorner::TopRight;
		const bool top = configuration_.corner == WatermarkCorner::TopLeft || configuration_.corner == WatermarkCorner::TopRight;
		p.originX = right ? frameSize.x - inset - textWidth : inset;
		p.originY = top ? frameSize.y - inset - textHeight : inset;
		break;
	}
	}
	return p;
}

float Watermark::cellCoverage(double cellX, double cellY) const noexcept
{
	if (cellY < 0.0 || cellY >= kGlyphHeight || cellX < 0.0 || cellX >= textCells_) {
		return 0.0f;
	}
	const double slot = std::floor(cellX / kAdvance);
	const double column = cellX - slot * kAdvance;
	if (column >= kGlyphWidth || slot < 0.0 || slot >= static_cast<double>(text_.size())) {
		return 0.0f;
	}
	const std::uint8_t* rows = watermarkGlyph(text_[static_cast<std::size_t>(slot)]);
	// Row 0 is the top of the glyph; cell y grows upward.
	const int row = 6 - static_cast<int>(std::floor(cellY));
	const int bit = 4 - static_cast<int>(std::floor(column));
	return (rows[row] >> bit) & 1 ? 1.0f : 0.0f;
}

float Watermark::coverage(double x, double y, Point2D frameSize) const noexcept
{
	if (text_.empty()) {
		return 0.0f;
	}
	const Placement p = placement(frameSize);
	// Into text space: translate to the origin, unrotate, then scale to cells.
	const double dx = x - p.originX;
	const double dy = y - p.originY;
	double cellX = (dx * p.cosA + dy * p.sinA) / p.scale;
	double cellY = (-dx * p.sinA + dy * p.cosA) / p.scale;
	if (p.tileX > 0.0 && p.tileY > 0.0) {
		cellX = cellX - std::floor(cellX / p.tileX) * p.tileX;
		cellY = cellY - std::floor(cellY / p.tileY) * p.tileY;
		// Every other row shifts by half a period, as a staggered tiling does.
		const double rowIndex = std::floor(((-dx * p.sinA + dy * p.cosA) / p.scale) / p.tileY);
		if (std::fmod(std::abs(rowIndex), 2.0) >= 1.0) {
			cellX = cellX + p.tileX / 2.0;
			cellX = cellX - std::floor(cellX / p.tileX) * p.tileX;
		}
	}
	// Four samples per pixel soften the glyph edges.
	float sum = 0.0f;
	const double half = 0.25 / p.scale;
	sum += cellCoverage(cellX - half, cellY - half);
	sum += cellCoverage(cellX + half, cellY - half);
	sum += cellCoverage(cellX - half, cellY + half);
	sum += cellCoverage(cellX + half, cellY + half);
	return sum / 4.0f;
}

std::vector<float> Watermark::mask(const RectI& window, Point2D frameSize) const
{
	std::vector<float> out(static_cast<std::size_t>(std::max(0, window.width())) * static_cast<std::size_t>(std::max(0, window.height())), 0.0f);
	std::size_t i = 0;
	for (int y = window.y1; y < window.y2; ++y) {
		for (int x = window.x1; x < window.x2; ++x) {
			out[i++] = coverage(x + 0.5, y + 0.5, frameSize);
		}
	}
	return out;
}

void Watermark::composite(const ImageView& view, const RectI& window, Point2D frameSize, Point2D frameOrigin) const
{
	if (!view.valid() || text_.empty()) {
		return;
	}
	const RectI area = window.empty() ? view.desc.bounds : window.intersect(view.desc.bounds);
	const std::size_t components = componentCount(view.desc.components);
	const std::size_t colourComponents = view.desc.components == Components::Alpha ? 0 : std::min<std::size_t>(components, 3);
	const float colour[3] = {static_cast<float>(configuration_.color.r), static_cast<float>(configuration_.color.g), static_cast<float>(configuration_.color.b)};
	const float opacity = static_cast<float>(std::clamp(configuration_.opacity * configuration_.color.a, 0.0, 1.0));
	for (int y = area.y1; y < area.y2; ++y) {
		for (int x = area.x1; x < area.x2; ++x) {
			const float a = coverage(x + 0.5 - frameOrigin.x, y + 0.5 - frameOrigin.y, frameSize) * opacity;
			if (a <= 0.0f) {
				continue;
			}
			std::uint8_t* pixel = view.pixel(x, y);
			if (pixel == nullptr) {
				continue;
			}
			for (std::size_t c = 0; c < colourComponents; ++c) {
				const float value = readComponent(pixel, view.desc.depth, c);
				writeComponent(pixel, view.desc.depth, c, value * (1.0f - a) + colour[c] * a);
			}
		}
	}
}

OFXGRIP_NAMESPACE_END

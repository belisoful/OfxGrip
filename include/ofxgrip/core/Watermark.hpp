// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Watermark.hpp
	@brief The watermark a trial or unlicensed render carries: its configuration, and the
	       compositor that draws it with a built-in glyph font onto a CPU frame or into a mask
	       a GPU backend blends.

	Introduced in OfxGrip 0.4.0. This is FxGrip's `FxGripWatermark` without AppKit: the text
	is set in a 5×7 glyph font the core carries, so the frame looks the same on every host and
	OS. The configuration keeps FxGrip's dictionary keys (`text`, `fontSize`, `color`,
	`angleDegrees`, `opacity`, `style`, `corner`, `inset`, `tileSpacing`).
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! How the watermark covers the frame. */
enum class WatermarkStyle : std::uint8_t {
	Single = 0,      /*!< One instance at the center, rotated by the angle. */
	DiagonalTiled,   /*!< The text repeated over the frame on a grid, rotated by the angle. */
	Banner,          /*!< One instance across the bottom. */
	Corner,          /*!< One instance in a corner, inset. */
};

enum class WatermarkCorner : std::uint8_t {
	BottomLeft = 0,
	BottomRight,
	TopLeft,
	TopRight,
};

/*! The configuration. Sizes are in pixels of a 1080-pixel-high frame and scale with the
    frame's height. */
struct WatermarkConfiguration {
	std::string text;
	double fontSize{99};
	RGBA color{1, 1, 1, 1};
	double angleDegrees{10};
	double opacity{0.73};
	WatermarkStyle style{WatermarkStyle::Single};
	Point2D tileSpacing{480, 240};
	WatermarkCorner corner{WatermarkCorner::BottomRight};
	double inset{24};
	bool operator==(const WatermarkConfiguration&) const = default;

	/*! FxGrip's defaults: the unlicensed mark (single, 99 pt, 10°, 0.73) and the trial mark
	    (corner, bottom right, 48 pt, 0.5). */
	static WatermarkConfiguration unlicensed(std::string text);
	static WatermarkConfiguration trial(std::string text);
	/*! Reads the dictionary form: `text`, `fontSize`, `color` (RGBA or RGB), `angleDegrees`,
	    `opacity`, `style` (`single`, `diagonal`, `banner`, `corner`), `corner` (`bottomLeft`,
	    `bottomRight`, `topLeft`, `topRight`), `inset`, `tileSpacing` (a point). Absent keys keep
	    the current value; an unknown style or corner fails. */
	Error apply(const std::map<std::string, Value>& dictionary);
	std::map<std::string, Value> toDictionary() const;
};

std::string_view toString(WatermarkStyle style) noexcept;
std::string_view toString(WatermarkCorner corner) noexcept;
bool parseWatermarkStyle(std::string_view text, WatermarkStyle& out) noexcept;
bool parseWatermarkCorner(std::string_view text, WatermarkCorner& out) noexcept;

/*! The glyph rows of a character in the built-in 5×7 font: 7 rows, top first, 5 bits each with
    bit 4 the leftmost column. Unknown characters are blank; letters are upper-cased. */
const std::uint8_t* watermarkGlyph(char c) noexcept;

/*! The compositor. */
class Watermark {
public:
	explicit Watermark(WatermarkConfiguration configuration);
	const WatermarkConfiguration& configuration() const noexcept { return configuration_; }

	/*! The coverage in [0, 1] of the frame pixel at (x, y), with the frame's size in pixels. */
	float coverage(double x, double y, Point2D frameSize) const noexcept;
	/*! A coverage mask over a window, row by row from `window.y1`. */
	std::vector<float> mask(const RectI& window, Point2D frameSize) const;
	/*! Composites the mark over a CPU frame's window: `out = out × (1 − a) + color × a` with
	    `a = coverage × opacity`, on the colour components; alpha stays. `frameOrigin` is the
	    pixel the frame's lower-left corner sits at, for images whose bounds start off (0, 0). */
	void composite(const ImageView& view, const RectI& window, Point2D frameSize, Point2D frameOrigin = {}) const;

private:
	struct Placement {
		double scale{1};       /*!< Frame pixels per glyph cell. */
		double cosA{1};
		double sinA{0};
		double originX{0};     /*!< The frame position of the text's left-bottom cell. */
		double originY{0};
		double tileX{0};       /*!< The tiling period along the text, in cells; 0 for no tiling. */
		double tileY{0};
	};
	Placement placement(Point2D frameSize) const noexcept;
	float cellCoverage(double cellX, double cellY) const noexcept;

	WatermarkConfiguration configuration_;
	std::string text_;
	double textCells_{0};  /*!< The text's width in cells. */
};

OFXGRIP_NAMESPACE_END

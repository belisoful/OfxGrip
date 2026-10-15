// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Overlay.hpp
	@brief The on-screen control model: parts bound to parameters that draw themselves, answer
	       hit tests, and apply drags, and the control that composes them.

	Introduced in OfxGrip 0.4.0. This is FxGrip's `FxGripOSCPart` family and
	`FxGripOnScreenControl` without a host API: the core owns the geometry, the hit test, the
	drag state machine, and the parameter binding, and emits its drawing through a `DrawKit`
	(`Research/Comparison.md` §3.8). The OpenFX adapter renders the kit with the draw suite or
	with OpenGL and supplies the canvas transform; the mini host records it for tests.

	Conventions the parts share:

	- The canvas is the host's canonical coordinate space, y up. Pen positions and drawing use
	  it. A handle's size and hit radius are in screen pixels and scale by the pixel scale, so
	  grab targets keep their size at every zoom.
	- Position parameters hold canonical coordinates (OpenFX `XYAbsolute`); the object space of
	  a shape is that space.
	- Sizes (a circle's radius, a box's width) are canonical units; angles default to radians,
	  and `radiansPerUnit` adapts a parameter stored in degrees.
	- Shift constrains a drag to an axis or snaps an angle to 45°; Alt (Option) makes a drag
	  fine or anchors a resize at the center. Modifiers are read, never consumed.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! A colour with components in [0, 1]. */
struct OverlayColor {
	float r{1};
	float g{1};
	float b{1};
	float a{1};
	constexpr bool operator==(const OverlayColor&) const noexcept = default;
};

/*! The standard colours a host may supply; a kit falls back to its own. */
enum class OverlayColorRole : std::uint8_t {
	Background = 0,
	Active,
	Selected,
	Deselected,
	MarqueeForeground,
	MarqueeBackground,
	Text,
};

/*! A line stipple pattern. */
enum class OverlayStipple : std::uint8_t {
	Solid = 0,
	Dot,
	Dash,
	AltDash,
	DotDash,
};

/*! How text aligns to its anchor. */
enum class OverlayTextAlign : std::uint8_t {
	LeftBottom = 0,
	CenterBottom,
	RightBottom,
	LeftTop,
	CenterTop,
	RightTop,
	Center,
};

/*! The modifier keys of an event, in the neutral convention (Q-OFX-ANY-005). */
enum class OverlayModifier : std::uint8_t {
	Shift = 1u << 0,
	Control = 1u << 1,
	Alt = 1u << 2,
	Command = 1u << 3,
};

/*! A set of modifiers. */
struct OverlayModifiers {
	std::uint8_t bits{0};
	constexpr bool has(OverlayModifier m) const noexcept { return (bits & static_cast<std::uint8_t>(m)) != 0; }
	constexpr OverlayModifiers& set(OverlayModifier m, bool on = true) noexcept
	{
		if (on) {
			bits = static_cast<std::uint8_t>(bits | static_cast<std::uint8_t>(m));
		} else {
			bits = static_cast<std::uint8_t>(bits & ~static_cast<std::uint8_t>(m));
		}
		return *this;
	}
	constexpr bool operator==(const OverlayModifiers&) const noexcept = default;
};

/*! The geometry one draw or event runs in. */
struct OverlayGeometry {
	RectD imageBounds{0, 0, 1920, 1080};  /*!< The input image's canonical bounds, for shapes sized by it. */
	Point2D pixelScale{1, 1};             /*!< Canonical units per screen pixel. */
	double screenPixelRatio{1};           /*!< Device pixels per screen pixel, at least 1 (Q-OFX-ANY-012). */
	Time time{0};
	/*! Canonical units per screen pixel along x, for sizes. */
	double unitsPerPixel() const noexcept { return pixelScale.x > 0 ? pixelScale.x : 1.0; }
};

/*! The drawing surface the parts render into. */
class DrawKit {
public:
	virtual ~DrawKit() = default;
	virtual OverlayColor standardColor(OverlayColorRole role) = 0;
	virtual void setColor(const OverlayColor& color) = 0;
	/*! The line width in screen pixels; 0 is one pixel. */
	virtual void setLineWidth(float width) = 0;
	virtual void setStipple(OverlayStipple pattern) = 0;
	/*! Separate segments: `points` holds pairs. */
	virtual void lines(const std::vector<Point2D>& points) = 0;
	virtual void lineStrip(const std::vector<Point2D>& points) = 0;
	virtual void lineLoop(const std::vector<Point2D>& points) = 0;
	/*! A filled axis-aligned rectangle. */
	virtual void rectangle(const Point2D& a, const Point2D& b) = 0;
	/*! A filled polygon. */
	virtual void polygon(const std::vector<Point2D>& points) = 0;
	/*! An elliptical outline within a rectangle. */
	virtual void ellipse(const Point2D& a, const Point2D& b) = 0;
	virtual void text(const std::string& utf8, const Point2D& position, OverlayTextAlign align) = 0;
};

/*! One recorded drawing call, for tests. */
struct DrawRecord {
	std::string op;                   /*!< `color`, `width`, `stipple`, `lines`, `strip`, `loop`, `rect`, `polygon`, `ellipse`, `text`. */
	std::vector<Point2D> points;
	OverlayColor color;
	float width{0};
	OverlayStipple stipple{OverlayStipple::Solid};
	std::string text;
	OverlayTextAlign align{OverlayTextAlign::LeftBottom};
};

/*! A kit that records every call. */
class RecordingDrawKit final : public DrawKit {
public:
	OverlayColor standardColor(OverlayColorRole role) override;
	void setColor(const OverlayColor& color) override;
	void setLineWidth(float width) override;
	void setStipple(OverlayStipple pattern) override;
	void lines(const std::vector<Point2D>& points) override;
	void lineStrip(const std::vector<Point2D>& points) override;
	void lineLoop(const std::vector<Point2D>& points) override;
	void rectangle(const Point2D& a, const Point2D& b) override;
	void polygon(const std::vector<Point2D>& points) override;
	void ellipse(const Point2D& a, const Point2D& b) override;
	void text(const std::string& utf8, const Point2D& position, OverlayTextAlign align) override;
	std::vector<DrawRecord> records;
	/*! The number of records with an op. */
	std::size_t count(std::string_view op) const noexcept;

private:
	OverlayColor current_;
};

/*! The parameter access a control reads and writes through. */
class OverlayParameters {
public:
	virtual ~OverlayParameters() = default;
	virtual bool getPoint(ParameterId id, Time time, Point2D& out) = 0;
	virtual bool setPoint(ParameterId id, Time time, const Point2D& value) = 0;
	virtual bool getNumber(ParameterId id, Time time, double& out) = 0;
	virtual bool setNumber(ParameterId id, Time time, double value) = 0;
	/*! Whether the host draws its own handle for a point parameter (Q-NUKE-005); the part
	    stands down. */
	virtual bool hostDrawsHandle(ParameterId id) { (void)id; return false; }
};

class OverlayControl;

/*! One interactive piece of a control. */
class OverlayPart {
public:
	explicit OverlayPart(int partId) : partId_(partId) {}
	virtual ~OverlayPart() = default;

	int partId() const noexcept { return partId_; }
	OverlayControl* control() const noexcept { return control_; }

	/*! Whether the canvas point falls on this part. */
	virtual bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) { (void)canvasPoint; (void)geometry; return false; }
	/*! Applies a drag to the point, with the delta from the drag's last position. Returns
	    whether a parameter changed. */
	virtual bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry)
	{
		(void)canvasPoint;
		(void)canvasDelta;
		(void)modifiers;
		(void)geometry;
		return false;
	}
	/*! Draws the part, selected or not. */
	virtual void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) { (void)kit; (void)selected; (void)geometry; }
	/*! Handles a press on this part before any drag. Returns whether a parameter changed. */
	virtual bool penDown(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry) { (void)canvasPoint; (void)modifiers; (void)geometry; return false; }
	/*! Handles a key on any part. Returns whether a parameter changed. */
	virtual bool keyDown(int keySymbol, OverlayModifiers modifiers, const OverlayGeometry& geometry) { (void)keySymbol; (void)modifiers; (void)geometry; return false; }
	/*! Whether the part reads Shift itself, so the control's axis constraint stays off. */
	virtual bool handlesConstrainDrag() const { return false; }
	/*! Whether the part reads Alt itself, so the control's fine drag stays off. */
	virtual bool handlesFineDrag() const { return false; }
	/*! Whether the part stands down because the host draws the handle itself. */
	virtual bool standsDown(OverlayParameters& parameters) const { (void)parameters; return false; }

	/*! The standard square handle around a canvas point. */
	static void drawHandle(DrawKit& kit, const Point2D& center, double halfSide, bool selected, const OverlayGeometry& geometry);
	/*! The outline colour, and the fills of a selected and an unselected shape. */
	static OverlayColor outlineColor() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
	static OverlayColor selectedColor() noexcept { return {1.0f, 0.8f, 0.2f, 1.0f}; }
	static OverlayColor fillColor(bool selected) noexcept { return selected ? OverlayColor{1.0f, 0.8f, 0.2f, 0.25f} : OverlayColor{1.0f, 1.0f, 1.0f, 0.12f}; }

private:
	friend class OverlayControl;
	int partId_;
	OverlayControl* control_{nullptr};
};

/*! The control. Parts hit-test last-added first, draw in order, and the active part receives
    the drag. */
class OverlayControl {
public:
	explicit OverlayControl(OverlayParameters& parameters) : parameters_(parameters) {}
	virtual ~OverlayControl() = default;
	OverlayControl(const OverlayControl&) = delete;
	OverlayControl& operator=(const OverlayControl&) = delete;

	OverlayParameters& parameters() noexcept { return parameters_; }
	OverlayPart* addPart(std::unique_ptr<OverlayPart> part);
	void addParts(std::vector<std::unique_ptr<OverlayPart>> parts);
	const std::vector<std::unique_ptr<OverlayPart>>& parts() const noexcept { return parts_; }
	OverlayPart* part(int partId) const noexcept;

	/*! The part under a canvas point, or 0. */
	int hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry);
	/*! The events. Each returns whether a parameter changed, which asks the host to redraw. */
	bool penDown(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry);
	bool penMotion(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry);
	bool penUp(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry);
	bool keyDown(int keySymbol, OverlayModifiers modifiers, const OverlayGeometry& geometry);
	void draw(DrawKit& kit, const OverlayGeometry& geometry);

	int activePart() const noexcept { return active_; }
	int hoveredPart() const noexcept { return hovered_; }
	bool dragging() const noexcept { return dragging_; }
	/*! The fraction of pointer travel a fine (Alt) drag applies. */
	double fineDragScale{0.1};

	/*! Hooks a subclass may override to draw before and after the parts. */
	virtual void drawBackground(DrawKit& kit, const OverlayGeometry& geometry) { (void)kit; (void)geometry; }
	virtual void drawForeground(DrawKit& kit, const OverlayGeometry& geometry) { (void)kit; (void)geometry; }

private:
	OverlayParameters& parameters_;
	std::vector<std::unique_ptr<OverlayPart>> parts_;
	int active_{0};
	int hovered_{0};
	bool dragging_{false};
	Point2D dragStart_;
	Point2D dragLast_;
	Point2D dragLastRaw_;
};

/*! The parts. Every size and hit radius is in screen pixels. */

/*! A square handle bound to a point parameter; a drag writes the pointer's position. */
class PointHandlePart : public OverlayPart {
public:
	PointHandlePart(int partId, ParameterId parameter) : OverlayPart(partId), parameter_(parameter) {}
	ParameterId parameter() const noexcept { return parameter_; }
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	bool standsDown(OverlayParameters& parameters) const override { return parameters.hostDrawsHandle(parameter_); }

private:
	ParameterId parameter_;
};

/*! A segment bound to start and end point parameters; a drag moves both. */
class LinePart : public OverlayPart {
public:
	LinePart(int partId, ParameterId start, ParameterId end) : OverlayPart(partId), start_(start), end_(end) {}
	double hitRadius{6};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	ParameterId start_;
	ParameterId end_;
};

/*! Names a corner of a rectangle. */
enum class RectCorner : std::uint8_t {
	LowerLeft = 0,
	LowerRight,
	UpperRight,
	UpperLeft,
};

/*! A rectangle bound to lower-left and upper-right point parameters, with an optional angle
    parameter that rotates it about its center. A drag moves both corners. */
class RectPart : public OverlayPart {
public:
	RectPart(int partId, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle = kParameterIdNone)
	: OverlayPart(partId), lowerLeft_(lowerLeft), upperRight_(upperRight), angle_(angle)
	{
	}
	double radiansPerUnit{1};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	ParameterId lowerLeft_;
	ParameterId upperRight_;
	ParameterId angle_;
};

/*! A resize handle on a corner of a two-corner rectangle. Shift locks the aspect ratio; Alt
    anchors the center. */
class RectCornerPart : public OverlayPart {
public:
	RectCornerPart(int partId, RectCorner corner, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle = kParameterIdNone)
	: OverlayPart(partId), corner_(corner), lowerLeft_(lowerLeft), upperRight_(upperRight), angle_(angle)
	{
	}
	double radiansPerUnit{1};
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	bool handlesConstrainDrag() const override { return true; }
	bool handlesFineDrag() const override { return true; }

private:
	RectCorner corner_;
	ParameterId lowerLeft_;
	ParameterId upperRight_;
	ParameterId angle_;
};

/*! A rotation spoke from a center to a tip handle at a fixed screen radius, bound to an angle
    parameter. Shift snaps to 45°. The center is a point parameter, or the midpoint of two. */
class AngleDialPart : public OverlayPart {
public:
	AngleDialPart(int partId, ParameterId center, ParameterId angle) : OverlayPart(partId), center_(center), angle_(angle) {}
	/*! A dial on the midpoint of two corner parameters. */
	AngleDialPart(int partId, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle) : OverlayPart(partId), center_(lowerLeft), second_(upperRight), angle_(angle) {}
	double radiansPerUnit{1};
	double spokeRadius{40};
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	bool handlesConstrainDrag() const override { return true; }

private:
	bool center(const OverlayGeometry& geometry, Point2D& out) const;
	ParameterId center_;
	ParameterId second_{kParameterIdNone};
	ParameterId angle_;
};

/*! A circle bound to a center point parameter and a radius parameter in canonical units. A
    drag moves the center. */
class CirclePart : public OverlayPart {
public:
	CirclePart(int partId, ParameterId center, ParameterId radius) : OverlayPart(partId), center_(center), radius_(radius) {}
	unsigned segments{32};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	ParameterId center_;
	ParameterId radius_;
};

/*! A handle on a circle's rim that writes the radius. */
class CircleRadiusHandlePart : public OverlayPart {
public:
	CircleRadiusHandlePart(int partId, ParameterId center, ParameterId radius) : OverlayPart(partId), center_(center), radius_(radius) {}
	double rimAngle{0};
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	ParameterId center_;
	ParameterId radius_;
};

/*! A rotation handle at the tip of a spoke from a circle's center in the angle parameter's
    direction. The spoke reaches `spokePixels` past the rim, clear of the radius handle. Shift
    snaps to 45°. */
class RotationHandlePart : public OverlayPart {
public:
	RotationHandlePart(int partId, ParameterId center, ParameterId radius, ParameterId angle) : OverlayPart(partId), center_(center), radius_(radius), angle_(angle) {}
	double radiansPerUnit{1};
	double spokePixels{24};
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	bool handlesConstrainDrag() const override { return true; }

private:
	bool handle(const OverlayGeometry& geometry, Point2D& center, Point2D& tip) const;
	ParameterId center_;
	ParameterId radius_;
	ParameterId angle_;
};

/*! A chain of point parameters drawn as connected segments; a drag moves every point. */
class PolylinePart : public OverlayPart {
public:
	PolylinePart(int partId, std::vector<ParameterId> points, bool closed) : OverlayPart(partId), points_(std::move(points)), closed_(closed) {}
	double hitRadius{6};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	bool read(const OverlayGeometry& geometry, std::vector<Point2D>& out) const;
	std::vector<ParameterId> points_;
	bool closed_;
};

/*! A rotatable box bound to a center point, a width, a height, and an angle. A drag moves the
    center. */
class BoxPart : public OverlayPart {
public:
	BoxPart(int partId, ParameterId center, ParameterId width, ParameterId height, ParameterId angle = kParameterIdNone)
	: OverlayPart(partId), center_(center), width_(width), height_(height), angle_(angle)
	{
	}
	double radiansPerUnit{1};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;

private:
	ParameterId center_;
	ParameterId width_;
	ParameterId height_;
	ParameterId angle_;
};

/*! A resize handle on a box corner. The default anchors the opposite corner and moves the
    center; Alt anchors the center; Shift locks the aspect ratio. */
class BoxCornerPart : public OverlayPart {
public:
	BoxCornerPart(int partId, RectCorner corner, ParameterId center, ParameterId width, ParameterId height, ParameterId angle = kParameterIdNone)
	: OverlayPart(partId), corner_(corner), center_(center), width_(width), height_(height), angle_(angle)
	{
	}
	double radiansPerUnit{1};
	double hitRadius{10};
	double handleRadius{4};
	bool hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry) override;
	bool drag(const Point2D& canvasPoint, const Point2D& canvasDelta, OverlayModifiers modifiers, const OverlayGeometry& geometry) override;
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	bool handlesConstrainDrag() const override { return true; }
	bool handlesFineDrag() const override { return true; }

private:
	RectCorner corner_;
	ParameterId center_;
	ParameterId width_;
	ParameterId height_;
	ParameterId angle_;
};

/*! A display-only text readout at a fixed screen position, or anchored to a point parameter. */
class HudPart : public OverlayPart {
public:
	HudPart(int partId, std::string text) : OverlayPart(partId), text_(std::move(text)) {}
	std::function<std::string(const OverlayGeometry&)> textProvider;  /*!< Overrides the text at draw time. */
	ParameterId anchor{kParameterIdNone};
	Point2D canvasAnchor{20, 20};
	Point2D canvasOffset{0, 0};
	OverlayTextAlign align{OverlayTextAlign::LeftBottom};
	void draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry) override;
	const std::string& text() const noexcept { return text_; }
	void setText(std::string text) { text_ = std::move(text); }

private:
	std::string text_;
};

/*! The parts a shape family's composite includes. */
enum class OverlayShapeOption : std::uint8_t {
	Body = 1u << 0,
	CornerHandles = 1u << 1,
	VertexHandles = 1u << 2,
	RadiusHandle = 1u << 3,
	RotationHandle = 1u << 4,
};
constexpr std::uint8_t operator|(OverlayShapeOption a, OverlayShapeOption b) noexcept { return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b)); }
inline constexpr std::uint8_t kOverlayShapeAll = 0xFF;

/*! Composites. Parts number sequentially from `firstPartId` in the documented inclusion order,
    counting only the parts the options include. */
std::vector<std::unique_ptr<OverlayPart>> rectParts(std::uint8_t options, int firstPartId, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle = kParameterIdNone);
std::vector<std::unique_ptr<OverlayPart>> circleParts(std::uint8_t options, int firstPartId, ParameterId center, ParameterId radius, ParameterId angle = kParameterIdNone);
std::vector<std::unique_ptr<OverlayPart>> lineParts(std::uint8_t options, int firstPartId, ParameterId start, ParameterId end);
std::vector<std::unique_ptr<OverlayPart>> polylineParts(std::uint8_t options, int firstPartId, const std::vector<ParameterId>& points, bool closed);
std::vector<std::unique_ptr<OverlayPart>> boxParts(std::uint8_t options, int firstPartId, ParameterId center, ParameterId width, ParameterId height, ParameterId angle = kParameterIdNone);

/*! Snaps an angle to 45° increments when Shift is held. */
double overlayConstrainedAngle(double radians, OverlayModifiers modifiers) noexcept;
/*! The squared distance from a point to a segment. */
double overlayDistanceSquaredToSegment(const Point2D& p, const Point2D& a, const Point2D& b) noexcept;

OFXGRIP_NAMESPACE_END

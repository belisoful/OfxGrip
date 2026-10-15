// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Overlay.hpp>

#include <algorithm>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr double kPi = 3.14159265358979323846;

Point2D rotate(const Point2D& v, double radians) noexcept
{
	const double s = std::sin(radians);
	const double c = std::cos(radians);
	return {v.x * c - v.y * s, v.x * s + v.y * c};
}

Point2D add(const Point2D& a, const Point2D& b) noexcept
{
	return {a.x + b.x, a.y + b.y};
}

Point2D subtract(const Point2D& a, const Point2D& b) noexcept
{
	return {a.x - b.x, a.y - b.y};
}

double distanceSquared(const Point2D& a, const Point2D& b) noexcept
{
	const double dx = a.x - b.x;
	const double dy = a.y - b.y;
	return dx * dx + dy * dy;
}

bool readAngle(OverlayParameters& parameters, ParameterId angle, double radiansPerUnit, Time time, double& radians)
{
	radians = 0.0;
	if (angle == kParameterIdNone) {
		return true;
	}
	double value = 0.0;
	if (!parameters.getNumber(angle, time, value)) {
		return false;
	}
	radians = value * radiansPerUnit;
	return true;
}

/*! The four corners of a two-corner rectangle, rotated about its center. */
bool rectCorners(OverlayParameters& parameters, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle, double radiansPerUnit, Time time, Point2D corners[4], double* radiansOut = nullptr)
{
	Point2D ll;
	Point2D ur;
	double radians = 0.0;
	if (!parameters.getPoint(lowerLeft, time, ll) || !parameters.getPoint(upperRight, time, ur) || !readAngle(parameters, angle, radiansPerUnit, time, radians)) {
		return false;
	}
	const Point2D raw[4] = {ll, {ur.x, ll.y}, ur, {ll.x, ur.y}};
	const Point2D center{(ll.x + ur.x) / 2.0, (ll.y + ur.y) / 2.0};
	for (int i = 0; i < 4; ++i) {
		corners[i] = radians != 0.0 ? add(center, rotate(subtract(raw[i], center), radians)) : raw[i];
	}
	if (radiansOut != nullptr) {
		*radiansOut = radians;
	}
	return true;
}

Point2D rectCorner(RectCorner corner, const Point2D& ll, const Point2D& ur) noexcept
{
	switch (corner) {
	case RectCorner::LowerLeft: return ll;
	case RectCorner::LowerRight: return {ur.x, ll.y};
	case RectCorner::UpperRight: return ur;
	case RectCorner::UpperLeft: return {ll.x, ur.y};
	}
	return ll;
}

struct BoxGeometry {
	Point2D center;
	double halfWidth{0};
	double halfHeight{0};
	double radians{0};
};

bool readBox(OverlayParameters& parameters, ParameterId center, ParameterId width, ParameterId height, ParameterId angle, double radiansPerUnit, Time time, BoxGeometry& out)
{
	double w = 0.0;
	double h = 0.0;
	if (!parameters.getPoint(center, time, out.center) || !parameters.getNumber(width, time, w) || !parameters.getNumber(height, time, h) || !readAngle(parameters, angle, radiansPerUnit, time, out.radians)) {
		return false;
	}
	out.halfWidth = std::abs(w) / 2.0;
	out.halfHeight = std::abs(h) / 2.0;
	return true;
}

Point2D boxLocalPoint(const Point2D& canvas, const BoxGeometry& box) noexcept
{
	return rotate(subtract(canvas, box.center), -box.radians);
}

Point2D boxCanvasPoint(const Point2D& local, const BoxGeometry& box) noexcept
{
	return add(box.center, rotate(local, box.radians));
}

Point2D boxLocalCorner(RectCorner corner, const BoxGeometry& box) noexcept
{
	const double sx = corner == RectCorner::LowerRight || corner == RectCorner::UpperRight ? 1.0 : -1.0;
	const double sy = corner == RectCorner::UpperRight || corner == RectCorner::UpperLeft ? 1.0 : -1.0;
	return {sx * box.halfWidth, sy * box.halfHeight};
}

}  // namespace

double overlayConstrainedAngle(double radians, OverlayModifiers modifiers) noexcept
{
	if (!modifiers.has(OverlayModifier::Shift)) {
		return radians;
	}
	const double step = kPi / 4.0;
	return std::round(radians / step) * step;
}

double overlayDistanceSquaredToSegment(const Point2D& p, const Point2D& a, const Point2D& b) noexcept
{
	const double abx = b.x - a.x;
	const double aby = b.y - a.y;
	const double lengthSquared = abx * abx + aby * aby;
	double t = 0.0;
	if (lengthSquared > 0.0) {
		t = ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSquared;
		t = std::clamp(t, 0.0, 1.0);
	}
	const double dx = p.x - (a.x + t * abx);
	const double dy = p.y - (a.y + t * aby);
	return dx * dx + dy * dy;
}

// ---- recording kit ------------------------------------------------------------------------------

OverlayColor RecordingDrawKit::standardColor(OverlayColorRole role)
{
	switch (role) {
	case OverlayColorRole::Background: return {0.0f, 0.0f, 0.0f, 1.0f};
	case OverlayColorRole::Active: return {1.0f, 0.8f, 0.2f, 1.0f};
	case OverlayColorRole::Selected: return {1.0f, 1.0f, 0.0f, 1.0f};
	case OverlayColorRole::Deselected: return {0.8f, 0.8f, 0.8f, 1.0f};
	case OverlayColorRole::MarqueeForeground: return {1.0f, 1.0f, 1.0f, 1.0f};
	case OverlayColorRole::MarqueeBackground: return {0.0f, 0.0f, 0.0f, 0.5f};
	case OverlayColorRole::Text: return {1.0f, 1.0f, 1.0f, 1.0f};
	}
	return {};
}

void RecordingDrawKit::setColor(const OverlayColor& color)
{
	current_ = color;
	DrawRecord r;
	r.op = "color";
	r.color = color;
	records.push_back(std::move(r));
}

void RecordingDrawKit::setLineWidth(float width)
{
	DrawRecord r;
	r.op = "width";
	r.width = width;
	records.push_back(std::move(r));
}

void RecordingDrawKit::setStipple(OverlayStipple pattern)
{
	DrawRecord r;
	r.op = "stipple";
	r.stipple = pattern;
	records.push_back(std::move(r));
}

void RecordingDrawKit::lines(const std::vector<Point2D>& points)
{
	DrawRecord r;
	r.op = "lines";
	r.points = points;
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::lineStrip(const std::vector<Point2D>& points)
{
	DrawRecord r;
	r.op = "strip";
	r.points = points;
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::lineLoop(const std::vector<Point2D>& points)
{
	DrawRecord r;
	r.op = "loop";
	r.points = points;
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::rectangle(const Point2D& a, const Point2D& b)
{
	DrawRecord r;
	r.op = "rect";
	r.points = {a, b};
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::polygon(const std::vector<Point2D>& points)
{
	DrawRecord r;
	r.op = "polygon";
	r.points = points;
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::ellipse(const Point2D& a, const Point2D& b)
{
	DrawRecord r;
	r.op = "ellipse";
	r.points = {a, b};
	r.color = current_;
	records.push_back(std::move(r));
}

void RecordingDrawKit::text(const std::string& utf8, const Point2D& position, OverlayTextAlign align)
{
	DrawRecord r;
	r.op = "text";
	r.text = utf8;
	r.points = {position};
	r.align = align;
	r.color = current_;
	records.push_back(std::move(r));
}

std::size_t RecordingDrawKit::count(std::string_view op) const noexcept
{
	return static_cast<std::size_t>(std::count_if(records.begin(), records.end(), [op](const DrawRecord& r) { return r.op == op; }));
}

// ---- parts: shared drawing ----------------------------------------------------------------------

void OverlayPart::drawHandle(DrawKit& kit, const Point2D& center, double halfSide, bool selected, const OverlayGeometry& geometry)
{
	const double half = halfSide * geometry.unitsPerPixel();
	kit.setColor(fillColor(true));
	if (selected) {
		kit.rectangle({center.x - half, center.y - half}, {center.x + half, center.y + half});
	}
	kit.setColor(selected ? selectedColor() : outlineColor());
	kit.lineLoop({{center.x - half, center.y - half}, {center.x + half, center.y - half}, {center.x + half, center.y + half}, {center.x - half, center.y + half}});
}

// ---- control ------------------------------------------------------------------------------------

OverlayPart* OverlayControl::addPart(std::unique_ptr<OverlayPart> part)
{
	if (!part) {
		return nullptr;
	}
	part->control_ = this;
	parts_.push_back(std::move(part));
	return parts_.back().get();
}

void OverlayControl::addParts(std::vector<std::unique_ptr<OverlayPart>> parts)
{
	for (std::unique_ptr<OverlayPart>& part : parts) {
		addPart(std::move(part));
	}
}

OverlayPart* OverlayControl::part(int partId) const noexcept
{
	for (const auto& p : parts_) {
		if (p->partId() == partId) {
			return p.get();
		}
	}
	return nullptr;
}

int OverlayControl::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) {
		OverlayPart& part = **it;
		if (part.standsDown(parameters_)) {
			continue;
		}
		if (part.hitTest(canvasPoint, geometry)) {
			return part.partId();
		}
	}
	return 0;
}

bool OverlayControl::penDown(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	active_ = hitTest(canvasPoint, geometry);
	dragging_ = active_ != 0;
	dragStart_ = canvasPoint;
	dragLast_ = canvasPoint;
	dragLastRaw_ = canvasPoint;
	if (!dragging_) {
		return false;
	}
	return part(active_)->penDown(canvasPoint, modifiers, geometry);
}

bool OverlayControl::penMotion(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	if (!dragging_) {
		const int hovered = hitTest(canvasPoint, geometry);
		const bool changed = hovered != hovered_;
		hovered_ = hovered;
		return changed;
	}
	OverlayPart* active = part(active_);
	if (active == nullptr) {
		return false;
	}
	// The base applies Alt as a fine drag and Shift as an axis constraint unless the part reads
	// the modifier itself.
	Point2D target = canvasPoint;
	if (modifiers.has(OverlayModifier::Alt) && !active->handlesFineDrag()) {
		const Point2D rawDelta = subtract(canvasPoint, dragLastRaw_);
		target = add(dragLast_, Point2D{rawDelta.x * fineDragScale, rawDelta.y * fineDragScale});
	}
	if (modifiers.has(OverlayModifier::Shift) && !active->handlesConstrainDrag()) {
		const Point2D travel = subtract(target, dragStart_);
		if (std::abs(travel.x) >= std::abs(travel.y)) {
			target.y = dragStart_.y;
		} else {
			target.x = dragStart_.x;
		}
	}
	const Point2D delta = subtract(target, dragLast_);
	dragLast_ = target;
	dragLastRaw_ = canvasPoint;
	return active->drag(target, delta, modifiers, geometry);
}

bool OverlayControl::penUp(const Point2D& canvasPoint, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	bool changed = false;
	// A release at the last motion's position applies nothing more: a part that drags relative
	// to the current geometry would otherwise move twice.
	if (dragging_ && !(canvasPoint == dragLastRaw_)) {
		changed = penMotion(canvasPoint, modifiers, geometry);
	}
	dragging_ = false;
	active_ = 0;
	return changed;
}

bool OverlayControl::keyDown(int keySymbol, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	bool changed = false;
	for (const auto& p : parts_) {
		changed = p->keyDown(keySymbol, modifiers, geometry) || changed;
	}
	return changed;
}

void OverlayControl::draw(DrawKit& kit, const OverlayGeometry& geometry)
{
	drawBackground(kit, geometry);
	for (const auto& p : parts_) {
		if (p->standsDown(parameters_)) {
			continue;
		}
		kit.setLineWidth(1.0f);
		kit.setStipple(OverlayStipple::Solid);
		p->draw(kit, p->partId() == active_ || (!dragging_ && p->partId() == hovered_), geometry);
	}
	drawForeground(kit, geometry);
}

// ---- point handle -------------------------------------------------------------------------------

bool PointHandlePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D position;
	if (!control()->parameters().getPoint(parameter_, geometry.time, position)) {
		return false;
	}
	const double radius = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, position) <= radius * radius;
}

bool PointHandlePart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers, const OverlayGeometry& geometry)
{
	return control()->parameters().setPoint(parameter_, geometry.time, canvasPoint);
}

void PointHandlePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D position;
	if (!control()->parameters().getPoint(parameter_, geometry.time, position)) {
		return;
	}
	drawHandle(kit, position, handleRadius, selected, geometry);
}

// ---- line ---------------------------------------------------------------------------------------

bool LinePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D a;
	Point2D b;
	if (!control()->parameters().getPoint(start_, geometry.time, a) || !control()->parameters().getPoint(end_, geometry.time, b)) {
		return false;
	}
	const double radius = hitRadius * geometry.unitsPerPixel();
	return overlayDistanceSquaredToSegment(canvasPoint, a, b) <= radius * radius;
}

bool LinePart::drag(const Point2D&, const Point2D& canvasDelta, OverlayModifiers, const OverlayGeometry& geometry)
{
	Point2D a;
	Point2D b;
	OverlayParameters& parameters = control()->parameters();
	if (!parameters.getPoint(start_, geometry.time, a) || !parameters.getPoint(end_, geometry.time, b)) {
		return false;
	}
	const bool movedStart = parameters.setPoint(start_, geometry.time, add(a, canvasDelta));
	const bool movedEnd = parameters.setPoint(end_, geometry.time, add(b, canvasDelta));
	return movedStart && movedEnd;
}

void LinePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D a;
	Point2D b;
	if (!control()->parameters().getPoint(start_, geometry.time, a) || !control()->parameters().getPoint(end_, geometry.time, b)) {
		return;
	}
	kit.setColor(selected ? selectedColor() : outlineColor());
	kit.lines({a, b});
}

// ---- rectangle ----------------------------------------------------------------------------------

bool RectPart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D ll;
	Point2D ur;
	double radians = 0.0;
	OverlayParameters& parameters = control()->parameters();
	if (!parameters.getPoint(lowerLeft_, geometry.time, ll) || !parameters.getPoint(upperRight_, geometry.time, ur) || !readAngle(parameters, angle_, radiansPerUnit, geometry.time, radians)) {
		return false;
	}
	Point2D p = canvasPoint;
	if (radians != 0.0) {
		const Point2D center{(ll.x + ur.x) / 2.0, (ll.y + ur.y) / 2.0};
		p = add(center, rotate(subtract(canvasPoint, center), -radians));
	}
	// The corners are not normalized: a corner dragged past its opposite inverts the rectangle.
	return std::min(ll.x, ur.x) <= p.x && p.x <= std::max(ll.x, ur.x) && std::min(ll.y, ur.y) <= p.y && p.y <= std::max(ll.y, ur.y);
}

bool RectPart::drag(const Point2D&, const Point2D& canvasDelta, OverlayModifiers, const OverlayGeometry& geometry)
{
	Point2D ll;
	Point2D ur;
	OverlayParameters& parameters = control()->parameters();
	if (!parameters.getPoint(lowerLeft_, geometry.time, ll) || !parameters.getPoint(upperRight_, geometry.time, ur)) {
		return false;
	}
	const bool movedLL = parameters.setPoint(lowerLeft_, geometry.time, add(ll, canvasDelta));
	const bool movedUR = parameters.setPoint(upperRight_, geometry.time, add(ur, canvasDelta));
	return movedLL && movedUR;
}

void RectPart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D corners[4];
	if (!rectCorners(control()->parameters(), lowerLeft_, upperRight_, angle_, radiansPerUnit, geometry.time, corners)) {
		return;
	}
	const std::vector<Point2D> loop{corners[0], corners[1], corners[2], corners[3]};
	kit.setColor(fillColor(selected));
	kit.polygon(loop);
	kit.setColor(outlineColor());
	kit.lineLoop(loop);
}

// ---- rectangle corner ---------------------------------------------------------------------------

bool RectCornerPart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D corners[4];
	if (!rectCorners(control()->parameters(), lowerLeft_, upperRight_, angle_, radiansPerUnit, geometry.time, corners)) {
		return false;
	}
	const double radius = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, corners[static_cast<int>(corner_)]) <= radius * radius;
}

bool RectCornerPart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	Point2D ll;
	Point2D ur;
	double radians = 0.0;
	OverlayParameters& parameters = control()->parameters();
	if (!parameters.getPoint(lowerLeft_, geometry.time, ll) || !parameters.getPoint(upperRight_, geometry.time, ur) || !readAngle(parameters, angle_, radiansPerUnit, geometry.time, radians)) {
		return false;
	}
	const Point2D center{(ll.x + ur.x) / 2.0, (ll.y + ur.y) / 2.0};
	// The parameters store pre-rotation coordinates: unrotate the pointer about the midpoint.
	Point2D p = radians != 0.0 ? add(center, rotate(subtract(canvasPoint, center), -radians)) : canvasPoint;
	const bool anchorCenter = modifiers.has(OverlayModifier::Alt);
	const Point2D opposite = rectCorner(static_cast<RectCorner>((static_cast<int>(corner_) + 2) % 4), ll, ur);
	const Point2D anchor = anchorCenter ? center : opposite;
	const double width = std::abs(ur.x - ll.x);
	const double height = std::abs(ur.y - ll.y);
	if (modifiers.has(OverlayModifier::Shift) && width > 0.0 && height > 0.0) {
		// Shift locks the aspect ratio: scale both axes by the dominant one from the anchor.
		const double signX = p.x >= anchor.x ? 1.0 : -1.0;
		const double signY = p.y >= anchor.y ? 1.0 : -1.0;
		const double scale = std::max(std::abs(p.x - anchor.x) / width, std::abs(p.y - anchor.y) / height);
		p = {anchor.x + signX * scale * width, anchor.y + signY * scale * height};
	}
	const Point2D mirrored{2.0 * center.x - p.x, 2.0 * center.y - p.y};
	switch (corner_) {
	case RectCorner::LowerLeft:
		ll = p;
		if (anchorCenter) {
			ur = mirrored;
		}
		break;
	case RectCorner::LowerRight:
		ur.x = p.x;
		ll.y = p.y;
		if (anchorCenter) {
			ll.x = mirrored.x;
			ur.y = mirrored.y;
		}
		break;
	case RectCorner::UpperRight:
		ur = p;
		if (anchorCenter) {
			ll = mirrored;
		}
		break;
	case RectCorner::UpperLeft:
		ll.x = p.x;
		ur.y = p.y;
		if (anchorCenter) {
			ur.x = mirrored.x;
			ll.y = mirrored.y;
		}
		break;
	}
	const bool wroteLL = parameters.setPoint(lowerLeft_, geometry.time, ll);
	const bool wroteUR = parameters.setPoint(upperRight_, geometry.time, ur);
	return wroteLL && wroteUR;
}

void RectCornerPart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D corners[4];
	if (!rectCorners(control()->parameters(), lowerLeft_, upperRight_, angle_, radiansPerUnit, geometry.time, corners)) {
		return;
	}
	drawHandle(kit, corners[static_cast<int>(corner_)], handleRadius, selected, geometry);
}

// ---- angle dial ---------------------------------------------------------------------------------

bool AngleDialPart::center(const OverlayGeometry& geometry, Point2D& out) const
{
	OverlayParameters& parameters = control()->parameters();
	if (second_ == kParameterIdNone) {
		return parameters.getPoint(center_, geometry.time, out);
	}
	Point2D a;
	Point2D b;
	if (!parameters.getPoint(center_, geometry.time, a) || !parameters.getPoint(second_, geometry.time, b)) {
		return false;
	}
	out = {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
	return true;
}

bool AngleDialPart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D c;
	double radians = 0.0;
	if (!center(geometry, c) || !readAngle(control()->parameters(), angle_, radiansPerUnit, geometry.time, radians)) {
		return false;
	}
	const double spoke = spokeRadius * geometry.unitsPerPixel();
	const Point2D tip{c.x + std::cos(radians) * spoke, c.y + std::sin(radians) * spoke};
	const double radius = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, tip) <= radius * radius;
}

bool AngleDialPart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	Point2D c;
	if (!center(geometry, c)) {
		return false;
	}
	const double dx = canvasPoint.x - c.x;
	const double dy = canvasPoint.y - c.y;
	if (dx == 0.0 && dy == 0.0) {
		return false;
	}
	const double radians = overlayConstrainedAngle(std::atan2(dy, dx), modifiers);
	return control()->parameters().setNumber(angle_, geometry.time, radians / radiansPerUnit);
}

void AngleDialPart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D c;
	double radians = 0.0;
	if (!center(geometry, c) || !readAngle(control()->parameters(), angle_, radiansPerUnit, geometry.time, radians)) {
		return;
	}
	const double spoke = spokeRadius * geometry.unitsPerPixel();
	const Point2D tip{c.x + std::cos(radians) * spoke, c.y + std::sin(radians) * spoke};
	kit.setColor(outlineColor());
	kit.lines({c, tip});
	drawHandle(kit, tip, handleRadius, selected, geometry);
}

// ---- circle -------------------------------------------------------------------------------------

bool CirclePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D c;
	double radius = 0.0;
	if (!control()->parameters().getPoint(center_, geometry.time, c) || !control()->parameters().getNumber(radius_, geometry.time, radius)) {
		return false;
	}
	return distanceSquared(canvasPoint, c) < radius * radius;
}

bool CirclePart::drag(const Point2D&, const Point2D& canvasDelta, OverlayModifiers, const OverlayGeometry& geometry)
{
	Point2D c;
	if (!control()->parameters().getPoint(center_, geometry.time, c)) {
		return false;
	}
	return control()->parameters().setPoint(center_, geometry.time, add(c, canvasDelta));
}

void CirclePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D c;
	double radius = 0.0;
	if (!control()->parameters().getPoint(center_, geometry.time, c) || !control()->parameters().getNumber(radius_, geometry.time, radius)) {
		return;
	}
	const unsigned count = std::max(segments, 3u);
	std::vector<Point2D> rim;
	rim.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		const double a = static_cast<double>(i) * 2.0 * kPi / static_cast<double>(count);
		rim.push_back({c.x + std::cos(a) * radius, c.y + std::sin(a) * radius});
	}
	kit.setColor(fillColor(selected));
	kit.polygon(rim);
	kit.setColor(outlineColor());
	kit.lineLoop(rim);
}

// ---- circle radius handle -----------------------------------------------------------------------

bool CircleRadiusHandlePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D c;
	double radius = 0.0;
	if (!control()->parameters().getPoint(center_, geometry.time, c) || !control()->parameters().getNumber(radius_, geometry.time, radius)) {
		return false;
	}
	const Point2D handle{c.x + std::cos(rimAngle) * radius, c.y + std::sin(rimAngle) * radius};
	const double r = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, handle) <= r * r;
}

bool CircleRadiusHandlePart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers, const OverlayGeometry& geometry)
{
	Point2D c;
	if (!control()->parameters().getPoint(center_, geometry.time, c)) {
		return false;
	}
	return control()->parameters().setNumber(radius_, geometry.time, std::sqrt(distanceSquared(canvasPoint, c)));
}

void CircleRadiusHandlePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D c;
	double radius = 0.0;
	if (!control()->parameters().getPoint(center_, geometry.time, c) || !control()->parameters().getNumber(radius_, geometry.time, radius)) {
		return;
	}
	drawHandle(kit, {c.x + std::cos(rimAngle) * radius, c.y + std::sin(rimAngle) * radius}, handleRadius, selected, geometry);
}

// ---- rotation handle ----------------------------------------------------------------------------

bool RotationHandlePart::handle(const OverlayGeometry& geometry, Point2D& center, Point2D& tip) const
{
	double radius = 0.0;
	double radians = 0.0;
	OverlayParameters& parameters = control()->parameters();
	if (!parameters.getPoint(center_, geometry.time, center) || !parameters.getNumber(radius_, geometry.time, radius) || !readAngle(parameters, angle_, radiansPerUnit, geometry.time, radians)) {
		return false;
	}
	// The spoke reaches past the rim, so the tip sits clear of the radius handle at angle 0.
	const double reach = radius + spokePixels * geometry.unitsPerPixel();
	tip = {center.x + std::cos(radians) * reach, center.y + std::sin(radians) * reach};
	return true;
}

bool RotationHandlePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	Point2D c;
	Point2D tip;
	if (!handle(geometry, c, tip)) {
		return false;
	}
	const double r = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, tip) <= r * r;
}

bool RotationHandlePart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	Point2D c;
	if (!control()->parameters().getPoint(center_, geometry.time, c)) {
		return false;
	}
	const double dx = canvasPoint.x - c.x;
	const double dy = canvasPoint.y - c.y;
	if (dx == 0.0 && dy == 0.0) {
		return false;
	}
	const double radians = overlayConstrainedAngle(std::atan2(dy, dx), modifiers);
	return control()->parameters().setNumber(angle_, geometry.time, radians / radiansPerUnit);
}

void RotationHandlePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	Point2D c;
	Point2D tip;
	if (!handle(geometry, c, tip)) {
		return;
	}
	kit.setColor(outlineColor());
	kit.lines({c, tip});
	drawHandle(kit, tip, handleRadius, selected, geometry);
}

// ---- polyline -----------------------------------------------------------------------------------

bool PolylinePart::read(const OverlayGeometry& geometry, std::vector<Point2D>& out) const
{
	out.clear();
	for (ParameterId id : points_) {
		Point2D p;
		if (!control()->parameters().getPoint(id, geometry.time, p)) {
			return false;
		}
		out.push_back(p);
	}
	return out.size() >= 2;
}

bool PolylinePart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	std::vector<Point2D> points;
	if (!read(geometry, points)) {
		return false;
	}
	const double radius = hitRadius * geometry.unitsPerPixel();
	const std::size_t segments = closed_ ? points.size() : points.size() - 1;
	for (std::size_t i = 0; i < segments; ++i) {
		if (overlayDistanceSquaredToSegment(canvasPoint, points[i], points[(i + 1) % points.size()]) <= radius * radius) {
			return true;
		}
	}
	return false;
}

bool PolylinePart::drag(const Point2D&, const Point2D& canvasDelta, OverlayModifiers, const OverlayGeometry& geometry)
{
	bool moved = !points_.empty();
	for (ParameterId id : points_) {
		Point2D p;
		if (!control()->parameters().getPoint(id, geometry.time, p)) {
			return false;
		}
		moved = control()->parameters().setPoint(id, geometry.time, add(p, canvasDelta)) && moved;
	}
	return moved;
}

void PolylinePart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	std::vector<Point2D> points;
	if (!read(geometry, points)) {
		return;
	}
	kit.setColor(selected ? selectedColor() : outlineColor());
	if (closed_) {
		kit.lineLoop(points);
	} else {
		kit.lineStrip(points);
	}
}

// ---- box ----------------------------------------------------------------------------------------

bool BoxPart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	BoxGeometry box;
	if (!readBox(control()->parameters(), center_, width_, height_, angle_, radiansPerUnit, geometry.time, box)) {
		return false;
	}
	const Point2D local = boxLocalPoint(canvasPoint, box);
	return std::abs(local.x) <= box.halfWidth && std::abs(local.y) <= box.halfHeight;
}

bool BoxPart::drag(const Point2D&, const Point2D& canvasDelta, OverlayModifiers, const OverlayGeometry& geometry)
{
	Point2D c;
	if (!control()->parameters().getPoint(center_, geometry.time, c)) {
		return false;
	}
	return control()->parameters().setPoint(center_, geometry.time, add(c, canvasDelta));
}

void BoxPart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	BoxGeometry box;
	if (!readBox(control()->parameters(), center_, width_, height_, angle_, radiansPerUnit, geometry.time, box)) {
		return;
	}
	const std::vector<Point2D> corners{boxCanvasPoint(boxLocalCorner(RectCorner::LowerLeft, box), box), boxCanvasPoint(boxLocalCorner(RectCorner::LowerRight, box), box),
	                                   boxCanvasPoint(boxLocalCorner(RectCorner::UpperRight, box), box), boxCanvasPoint(boxLocalCorner(RectCorner::UpperLeft, box), box)};
	kit.setColor(fillColor(selected));
	kit.polygon(corners);
	kit.setColor(outlineColor());
	kit.lineLoop(corners);
}

// ---- box corner ---------------------------------------------------------------------------------

bool BoxCornerPart::hitTest(const Point2D& canvasPoint, const OverlayGeometry& geometry)
{
	BoxGeometry box;
	if (!readBox(control()->parameters(), center_, width_, height_, angle_, radiansPerUnit, geometry.time, box)) {
		return false;
	}
	const Point2D corner = boxCanvasPoint(boxLocalCorner(corner_, box), box);
	const double r = hitRadius * geometry.unitsPerPixel();
	return distanceSquared(canvasPoint, corner) <= r * r;
}

bool BoxCornerPart::drag(const Point2D& canvasPoint, const Point2D&, OverlayModifiers modifiers, const OverlayGeometry& geometry)
{
	BoxGeometry box;
	OverlayParameters& parameters = control()->parameters();
	if (!readBox(parameters, center_, width_, height_, angle_, radiansPerUnit, geometry.time, box)) {
		return false;
	}
	// The pointer and the corner signs live in the box's local frame.
	const Point2D local = boxLocalPoint(canvasPoint, box);
	const Point2D signs = boxLocalCorner(corner_, BoxGeometry{{}, 1.0, 1.0, 0.0});
	const bool aspectLock = modifiers.has(OverlayModifier::Shift) && box.halfWidth > 0.0 && box.halfHeight > 0.0;
	const bool anchorCenter = modifiers.has(OverlayModifier::Alt);
	double halfWidth = 0.0;
	double halfHeight = 0.0;
	Point2D newCenter = box.center;
	if (anchorCenter) {
		halfWidth = std::abs(local.x);
		halfHeight = std::abs(local.y);
		if (aspectLock) {
			const double scale = std::max(halfWidth / box.halfWidth, halfHeight / box.halfHeight);
			halfWidth = scale * box.halfWidth;
			halfHeight = scale * box.halfHeight;
		}
	} else {
		// The default anchors the diagonally opposite corner; the center shifts.
		const Point2D opposite{-signs.x * box.halfWidth, -signs.y * box.halfHeight};
		Point2D dragged = local;
		if (aspectLock) {
			const double scale = std::max(std::abs(local.x - opposite.x) / (2.0 * box.halfWidth), std::abs(local.y - opposite.y) / (2.0 * box.halfHeight));
			halfWidth = scale * box.halfWidth;
			halfHeight = scale * box.halfHeight;
			dragged = {opposite.x + signs.x * 2.0 * halfWidth, opposite.y + signs.y * 2.0 * halfHeight};
		} else {
			halfWidth = std::abs(local.x - opposite.x) / 2.0;
			halfHeight = std::abs(local.y - opposite.y) / 2.0;
		}
		newCenter = boxCanvasPoint({(dragged.x + opposite.x) / 2.0, (dragged.y + opposite.y) / 2.0}, box);
	}
	const bool wroteWidth = parameters.setNumber(width_, geometry.time, 2.0 * halfWidth);
	const bool wroteHeight = parameters.setNumber(height_, geometry.time, 2.0 * halfHeight);
	const bool wroteCenter = anchorCenter || parameters.setPoint(center_, geometry.time, newCenter);
	return wroteWidth && wroteHeight && wroteCenter;
}

void BoxCornerPart::draw(DrawKit& kit, bool selected, const OverlayGeometry& geometry)
{
	BoxGeometry box;
	if (!readBox(control()->parameters(), center_, width_, height_, angle_, radiansPerUnit, geometry.time, box)) {
		return;
	}
	drawHandle(kit, boxCanvasPoint(boxLocalCorner(corner_, box), box), handleRadius, selected, geometry);
}

// ---- HUD ----------------------------------------------------------------------------------------

void HudPart::draw(DrawKit& kit, bool, const OverlayGeometry& geometry)
{
	Point2D position = canvasAnchor;
	if (anchor != kParameterIdNone && !control()->parameters().getPoint(anchor, geometry.time, position)) {
		return;
	}
	position = add(position, Point2D{canvasOffset.x * geometry.unitsPerPixel(), canvasOffset.y * geometry.unitsPerPixel()});
	const std::string content = textProvider ? textProvider(geometry) : text_;
	if (content.empty()) {
		return;
	}
	kit.setColor(kit.standardColor(OverlayColorRole::Text));
	kit.text(content, position, align);
}

// ---- composites ---------------------------------------------------------------------------------

namespace {

bool wants(std::uint8_t options, OverlayShapeOption option) noexcept
{
	return (options & static_cast<std::uint8_t>(option)) != 0;
}

}  // namespace

std::vector<std::unique_ptr<OverlayPart>> rectParts(std::uint8_t options, int firstPartId, ParameterId lowerLeft, ParameterId upperRight, ParameterId angle)
{
	std::vector<std::unique_ptr<OverlayPart>> parts;
	int id = firstPartId;
	if (wants(options, OverlayShapeOption::Body)) {
		parts.push_back(std::make_unique<RectPart>(id++, lowerLeft, upperRight, angle));
	}
	if (wants(options, OverlayShapeOption::CornerHandles)) {
		for (RectCorner corner : {RectCorner::LowerLeft, RectCorner::LowerRight, RectCorner::UpperRight, RectCorner::UpperLeft}) {
			parts.push_back(std::make_unique<RectCornerPart>(id++, corner, lowerLeft, upperRight, angle));
		}
	}
	if (wants(options, OverlayShapeOption::RotationHandle) && angle != kParameterIdNone) {
		parts.push_back(std::make_unique<AngleDialPart>(id++, lowerLeft, upperRight, angle));
	}
	return parts;
}

std::vector<std::unique_ptr<OverlayPart>> circleParts(std::uint8_t options, int firstPartId, ParameterId center, ParameterId radius, ParameterId angle)
{
	std::vector<std::unique_ptr<OverlayPart>> parts;
	int id = firstPartId;
	if (wants(options, OverlayShapeOption::Body)) {
		parts.push_back(std::make_unique<CirclePart>(id++, center, radius));
	}
	if (wants(options, OverlayShapeOption::RadiusHandle)) {
		parts.push_back(std::make_unique<CircleRadiusHandlePart>(id++, center, radius));
	}
	if (wants(options, OverlayShapeOption::RotationHandle) && angle != kParameterIdNone) {
		parts.push_back(std::make_unique<RotationHandlePart>(id++, center, radius, angle));
	}
	return parts;
}

std::vector<std::unique_ptr<OverlayPart>> lineParts(std::uint8_t options, int firstPartId, ParameterId start, ParameterId end)
{
	std::vector<std::unique_ptr<OverlayPart>> parts;
	int id = firstPartId;
	if (wants(options, OverlayShapeOption::Body)) {
		parts.push_back(std::make_unique<LinePart>(id++, start, end));
	}
	if (wants(options, OverlayShapeOption::VertexHandles)) {
		parts.push_back(std::make_unique<PointHandlePart>(id++, start));
		parts.push_back(std::make_unique<PointHandlePart>(id++, end));
	}
	return parts;
}

std::vector<std::unique_ptr<OverlayPart>> polylineParts(std::uint8_t options, int firstPartId, const std::vector<ParameterId>& points, bool closed)
{
	std::vector<std::unique_ptr<OverlayPart>> parts;
	int id = firstPartId;
	if (wants(options, OverlayShapeOption::Body)) {
		parts.push_back(std::make_unique<PolylinePart>(id++, points, closed));
	}
	if (wants(options, OverlayShapeOption::VertexHandles)) {
		for (ParameterId point : points) {
			parts.push_back(std::make_unique<PointHandlePart>(id++, point));
		}
	}
	return parts;
}

std::vector<std::unique_ptr<OverlayPart>> boxParts(std::uint8_t options, int firstPartId, ParameterId center, ParameterId width, ParameterId height, ParameterId angle)
{
	std::vector<std::unique_ptr<OverlayPart>> parts;
	int id = firstPartId;
	if (wants(options, OverlayShapeOption::Body)) {
		parts.push_back(std::make_unique<BoxPart>(id++, center, width, height, angle));
	}
	if (wants(options, OverlayShapeOption::CornerHandles)) {
		for (RectCorner corner : {RectCorner::LowerLeft, RectCorner::LowerRight, RectCorner::UpperRight, RectCorner::UpperLeft}) {
			parts.push_back(std::make_unique<BoxCornerPart>(id++, corner, center, width, height, angle));
		}
	}
	if (wants(options, OverlayShapeOption::RotationHandle) && angle != kParameterIdNone) {
		parts.push_back(std::make_unique<AngleDialPart>(id++, center, angle));
	}
	return parts;
}

OFXGRIP_NAMESPACE_END

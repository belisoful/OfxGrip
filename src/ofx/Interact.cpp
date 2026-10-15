// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Interact.hpp>

#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxDrawSuite.h"
#include "ofxInteract.h"
#include "ofxKeySyms.h"

#if defined(OFXGRIP_HAS_OPENGL)
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include <algorithm>
#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr double kPi = 3.14159265358979323846;

Point2D readPoint(const PropertySet& in, const char* name, Point2D fallback)
{
	const std::vector<double> v = in.getDoubles(name);
	return v.size() == 2 ? Point2D{v[0], v[1]} : fallback;
}

}  // namespace

InteractArgs readInteractArgs(const PropertySet& in)
{
	InteractArgs a;
	a.time = in.getDouble(kOfxPropTime, 0.0, 0);
	const std::vector<double> scale = in.getDoubles(kOfxImageEffectPropRenderScale);
	if (scale.size() == 2 && scale[0] > 0.0 && scale[1] > 0.0) {
		a.renderScale = {scale[0], scale[1]};
	}
	const Point2D pixelScale = readPoint(in, kOfxInteractPropPixelScale, {1, 1});
	// A scale of 0 reads as 1: hosts that report nothing must not shrink every handle to nothing.
	a.pixelScale = {pixelScale.x > 0.0 ? pixelScale.x : 1.0, pixelScale.y > 0.0 ? pixelScale.y : 1.0};
	a.penPosition = readPoint(in, kOfxInteractPropPenPosition, {});
	a.penViewportPosition = readPoint(in, kOfxInteractPropPenViewportPosition, {});
	a.penPressure = in.getDouble(kOfxInteractPropPenPressure, 1.0, 0);
	const std::vector<double> background = in.getDoubles(kOfxInteractPropBackgroundColour);
	if (background.size() >= 3) {
		a.background = {static_cast<float>(background[0]), static_cast<float>(background[1]), static_cast<float>(background[2]), 1.0f};
	}
	// Q-OFX-ANY-012 and Q-NATRON-003: the ratio is used only when present and at least 1.
	a.screenPixelRatio = std::max(1.0, in.getDouble(kInteractPropScreenPixelRatio, 1.0, 0));
	a.keySymbol = in.getInt(kOfxPropKeySym, 0, 0);
	a.keyString = in.getString(kOfxPropKeyString, std::string(), 0);
	a.drawContext = in.getPointer(kOfxInteractPropDrawContext).value_or(nullptr);
	return a;
}

bool interactModifierForKey(int keySymbol, bool macMapping, OverlayModifier& out) noexcept
{
	switch (keySymbol) {
	case kOfxKey_Shift_L:
	case kOfxKey_Shift_R:
		out = OverlayModifier::Shift;
		return true;
	case kOfxKey_Control_L:
	case kOfxKey_Control_R:
		// Q-OFX-ANY-005: on macOS the Command key arrives as Control and Control as Meta.
		out = macMapping ? OverlayModifier::Command : OverlayModifier::Control;
		return true;
	case kOfxKey_Alt_L:
	case kOfxKey_Alt_R:
		out = OverlayModifier::Alt;
		return true;
	case kOfxKey_Meta_L:
	case kOfxKey_Meta_R:
	case kOfxKey_Super_L:
	case kOfxKey_Super_R:
		out = macMapping ? OverlayModifier::Control : OverlayModifier::Command;
		return true;
	default:
		return false;
	}
}

// ---- parameters ---------------------------------------------------------------------------------

bool EffectOverlayParameters::getPoint(ParameterId id, Time time, Point2D& out)
{
	Value value;
	if (!effect_.services().parameters().get(id, time, value).ok()) {
		return false;
	}
	if (const Point2D* p = value.tryAs<Point2D>()) {
		out = *p;
		return true;
	}
	if (const Point3D* p = value.tryAs<Point3D>()) {
		out = {p->x, p->y};
		return true;
	}
	return false;
}

bool EffectOverlayParameters::setPoint(ParameterId id, Time time, const Point2D& value)
{
	return effect_.services().parameters().set(id, time, Value(value)).ok();
}

bool EffectOverlayParameters::getNumber(ParameterId id, Time time, double& out)
{
	Value value;
	if (!effect_.services().parameters().get(id, time, value).ok()) {
		return false;
	}
	if (value.is<std::string>() || value.is<Bytes>() || value.empty()) {
		return false;
	}
	out = value.toDouble();
	return true;
}

bool EffectOverlayParameters::setNumber(ParameterId id, Time time, double value)
{
	const ParameterSpec* spec = effect_.services().parameters().spec(id);
	if (spec != nullptr && spec->type == ParameterType::Int) {
		return effect_.services().parameters().set(id, time, Value(static_cast<int>(std::lround(value)))).ok();
	}
	return effect_.services().parameters().set(id, time, Value(value)).ok();
}

bool EffectOverlayParameters::hostDrawsHandle(ParameterId id)
{
	if (!effect_.services().profile().has(Quirk::NativeXYHandles)) {
		return false;
	}
	const ParameterSpec* spec = effect_.services().parameters().spec(id);
	return spec != nullptr && spec->type == ParameterType::Point;
}

// ---- draw suite kit -----------------------------------------------------------------------------

OverlayColor DrawSuiteKit::standardColor(OverlayColorRole role)
{
	OfxRGBAColourF colour{1, 1, 1, 1};
	if (suite_ != nullptr && suite_->getColour != nullptr && context_ != nullptr) {
		if (suite_->getColour(static_cast<OfxDrawContextHandle>(context_), static_cast<OfxStandardColour>(role), &colour) == kOfxStatOK) {
			return {colour.r, colour.g, colour.b, colour.a};
		}
	}
	return RecordingDrawKit().standardColor(role);
}

void DrawSuiteKit::setColor(const OverlayColor& color)
{
	if (suite_ == nullptr || suite_->setColour == nullptr || context_ == nullptr) {
		return;
	}
	const OfxRGBAColourF colour{color.r, color.g, color.b, color.a};
	suite_->setColour(static_cast<OfxDrawContextHandle>(context_), &colour);
}

void DrawSuiteKit::setLineWidth(float width)
{
	if (suite_ != nullptr && suite_->setLineWidth != nullptr && context_ != nullptr) {
		suite_->setLineWidth(static_cast<OfxDrawContextHandle>(context_), width);
	}
}

void DrawSuiteKit::setStipple(OverlayStipple pattern)
{
	if (suite_ != nullptr && suite_->setLineStipple != nullptr && context_ != nullptr) {
		suite_->setLineStipple(static_cast<OfxDrawContextHandle>(context_), static_cast<OfxDrawLineStipplePattern>(pattern));
	}
}

void DrawSuiteKit::draw(int primitive, const std::vector<Point2D>& points)
{
	if (suite_ == nullptr || suite_->draw == nullptr || context_ == nullptr || points.empty()) {
		return;
	}
	std::vector<OfxPointD> native;
	native.reserve(points.size());
	for (const Point2D& p : points) {
		native.push_back(OfxPointD{p.x, p.y});
	}
	suite_->draw(static_cast<OfxDrawContextHandle>(context_), static_cast<OfxDrawPrimitive>(primitive), native.data(), static_cast<int>(native.size()));
}

void DrawSuiteKit::lines(const std::vector<Point2D>& points)
{
	draw(kOfxDrawPrimitiveLines, points);
}

void DrawSuiteKit::lineStrip(const std::vector<Point2D>& points)
{
	draw(kOfxDrawPrimitiveLineStrip, points);
}

void DrawSuiteKit::lineLoop(const std::vector<Point2D>& points)
{
	draw(kOfxDrawPrimitiveLineLoop, points);
}

void DrawSuiteKit::rectangle(const Point2D& a, const Point2D& b)
{
	draw(kOfxDrawPrimitiveRectangle, {a, b});
}

void DrawSuiteKit::polygon(const std::vector<Point2D>& points)
{
	draw(kOfxDrawPrimitivePolygon, points);
}

void DrawSuiteKit::ellipse(const Point2D& a, const Point2D& b)
{
	draw(kOfxDrawPrimitiveEllipse, {a, b});
}

void DrawSuiteKit::text(const std::string& utf8, const Point2D& position, OverlayTextAlign align)
{
	if (suite_ == nullptr || suite_->drawText == nullptr || context_ == nullptr) {
		return;
	}
	int flags = 0;
	switch (align) {
	case OverlayTextAlign::LeftBottom: flags = kOfxDrawTextAlignmentLeft | kOfxDrawTextAlignmentBottom; break;
	case OverlayTextAlign::CenterBottom: flags = kOfxDrawTextAlignmentCenterH | kOfxDrawTextAlignmentBottom; break;
	case OverlayTextAlign::RightBottom: flags = kOfxDrawTextAlignmentRight | kOfxDrawTextAlignmentBottom; break;
	case OverlayTextAlign::LeftTop: flags = kOfxDrawTextAlignmentLeft | kOfxDrawTextAlignmentTop; break;
	case OverlayTextAlign::CenterTop: flags = kOfxDrawTextAlignmentCenterH | kOfxDrawTextAlignmentTop; break;
	case OverlayTextAlign::RightTop: flags = kOfxDrawTextAlignmentRight | kOfxDrawTextAlignmentTop; break;
	case OverlayTextAlign::Center: flags = kOfxDrawTextAlignmentCenterH | kOfxDrawTextAlignmentCenterV; break;
	}
	const OfxPointD at{position.x, position.y};
	suite_->drawText(static_cast<OfxDrawContextHandle>(context_), utf8.c_str(), &at, flags);
}

// ---- OpenGL kit ---------------------------------------------------------------------------------

bool GlDrawKit::available() noexcept
{
#if defined(OFXGRIP_HAS_OPENGL)
	return true;
#else
	return false;
#endif
}

GlDrawKit::GlDrawKit()
{
#if defined(OFXGRIP_HAS_OPENGL) && defined(__APPLE__)
	active_ = CGLGetCurrentContext() != nullptr;
#elif defined(OFXGRIP_HAS_OPENGL) && defined(_WIN32)
	active_ = wglGetCurrentContext() != nullptr;
#elif defined(OFXGRIP_HAS_OPENGL)
	active_ = true;
#endif
}

OverlayColor GlDrawKit::standardColor(OverlayColorRole role)
{
	return RecordingDrawKit().standardColor(role);
}

#if defined(OFXGRIP_HAS_OPENGL)

namespace {

void emit(GLenum mode, const std::vector<Point2D>& points)
{
	glBegin(mode);
	for (const Point2D& p : points) {
		glVertex2d(p.x, p.y);
	}
	glEnd();
}

}  // namespace

void GlDrawKit::setColor(const OverlayColor& color)
{
	if (!active_) {
		return;
	}
	glColor4f(color.r, color.g, color.b, color.a);
}

void GlDrawKit::setLineWidth(float width)
{
	if (!active_) {
		return;
	}
	glLineWidth(width <= 0.0f ? 1.0f : width);
}

void GlDrawKit::setStipple(OverlayStipple pattern)
{
	if (!active_) {
		return;
	}
	if (pattern == OverlayStipple::Solid) {
		glDisable(GL_LINE_STIPPLE);
		return;
	}
	GLushort bits = 0xFFFF;
	switch (pattern) {
	case OverlayStipple::Dot: bits = 0xAAAA; break;
	case OverlayStipple::Dash: bits = 0xF0F0; break;
	case OverlayStipple::AltDash: bits = 0x0F0F; break;
	case OverlayStipple::DotDash: bits = 0xE4E4; break;
	case OverlayStipple::Solid: break;
	}
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, bits);
}

void GlDrawKit::lines(const std::vector<Point2D>& points)
{
	if (!active_) {
		return;
	}
	emit(GL_LINES, points);
}

void GlDrawKit::lineStrip(const std::vector<Point2D>& points)
{
	if (!active_) {
		return;
	}
	emit(GL_LINE_STRIP, points);
}

void GlDrawKit::lineLoop(const std::vector<Point2D>& points)
{
	if (!active_) {
		return;
	}
	emit(GL_LINE_LOOP, points);
}

void GlDrawKit::rectangle(const Point2D& a, const Point2D& b)
{
	if (!active_) {
		return;
	}
	emit(GL_QUADS, {a, {b.x, a.y}, b, {a.x, b.y}});
}

void GlDrawKit::polygon(const std::vector<Point2D>& points)
{
	if (!active_) {
		return;
	}
	emit(GL_POLYGON, points);
}

void GlDrawKit::ellipse(const Point2D& a, const Point2D& b)
{
	if (!active_) {
		return;
	}
	std::vector<Point2D> rim;
	const Point2D center{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
	const double rx = std::abs(b.x - a.x) / 2.0;
	const double ry = std::abs(b.y - a.y) / 2.0;
	for (int i = 0; i < 48; ++i) {
		const double t = static_cast<double>(i) * 2.0 * kPi / 48.0;
		rim.push_back({center.x + std::cos(t) * rx, center.y + std::sin(t) * ry});
	}
	emit(GL_LINE_LOOP, rim);
}

void GlDrawKit::text(const std::string&, const Point2D&, OverlayTextAlign)
{
	if (!active_) {
		return;
	}
	// Fixed-function OpenGL draws no text; the draw suite does.
}

void GlDrawKit::finish()
{
	if (!active_) {
		return;
	}
	// Q-NUKE-011: the host expects the modelview matrix mode current when drawing ends.
	glDisable(GL_LINE_STIPPLE);
	glMatrixMode(GL_MODELVIEW);
}

#else

void GlDrawKit::setColor(const OverlayColor&) {}
void GlDrawKit::setLineWidth(float) {}
void GlDrawKit::setStipple(OverlayStipple) {}
void GlDrawKit::lines(const std::vector<Point2D>&) {}
void GlDrawKit::lineStrip(const std::vector<Point2D>&) {}
void GlDrawKit::lineLoop(const std::vector<Point2D>&) {}
void GlDrawKit::rectangle(const Point2D&, const Point2D&) {}
void GlDrawKit::polygon(const std::vector<Point2D>&) {}
void GlDrawKit::ellipse(const Point2D&, const Point2D&) {}
void GlDrawKit::text(const std::string&, const Point2D&, OverlayTextAlign) {}
void GlDrawKit::finish() {}

#endif

// ---- the live overlay ---------------------------------------------------------------------------

OverlayInteract::OverlayInteract(Effect& effect, void* handle) : effect_(effect), parameters_(effect), handle_(handle)
{
	control_ = effect.createOverlay(parameters_);
}

OverlayInteract::~OverlayInteract() = default;

OverlayGeometry OverlayInteract::geometryFor(const InteractArgs& args)
{
	OverlayGeometry g;
	g.time = args.time;
	g.pixelScale = args.pixelScale;
	g.screenPixelRatio = args.screenPixelRatio;
	OfxHostServices& services = effect_.ofxServices();
	const Point2D size = services.time().projectSize();
	const Point2D offset = services.time().projectOffset();
	g.imageBounds = {offset.x, offset.y, offset.x + size.x, offset.y + size.y};
	// The source clip's region of definition is the image the control draws over.
	if (OfxImageClipHandle source = services.clipHandle(kOfxImageEffectSimpleSourceClipName)) {
		OfxRectD rod{};
		if (services.suites().imageEffect->clipGetRegionOfDefinition != nullptr && services.suites().imageEffect->clipGetRegionOfDefinition(source, args.time, &rod) == kOfxStatOK && rod.x2 > rod.x1 && rod.y2 > rod.y1) {
			g.imageBounds = {rod.x1, rod.y1, rod.x2, rod.y2};
		}
	}
	geometry_ = g;
	return g;
}

void OverlayInteract::applyModifier(int keySymbol, bool down)
{
	OverlayModifier modifier{};
	if (interactModifierForKey(keySymbol, effect_.services().profile().has(Quirk::MacModifierKeyMapping), modifier)) {
		modifiers_.set(modifier, down);
	}
}

OfxStatus OverlayInteract::dispatch(Action action, const PropertySet& in, const PropertySet&)
{
	OfxHostServices::PhaseScope scope(effect_.ofxServices(), Phase::Interact);
	const InteractArgs args = readInteractArgs(in);
	switch (action) {
	case Action::InteractDraw:
		return draw(args);
	case Action::InteractPenDown:
	case Action::InteractPenMotion:
	case Action::InteractPenUp:
		return pen(action, args);
	case Action::InteractKeyDown:
	case Action::InteractKeyUp:
	case Action::InteractKeyRepeat:
		return key(action, args);
	case Action::InteractGainFocus:
		return kOfxStatOK;
	case Action::InteractLoseFocus:
		modifiers_ = {};
		if (control_->dragging()) {
			control_->penUp(geometry_.imageBounds.x1 == 0 ? Point2D{} : Point2D{}, modifiers_, geometry_);
		}
		edit_.reset();
		return kOfxStatOK;
	default:
		return kOfxStatReplyDefault;
	}
}

OfxStatus OverlayInteract::draw(const InteractArgs& args)
{
	const OverlayGeometry geometry = geometryFor(args);
	++draws_;
	const Suites& suites = effect_.ofxServices().suites();
	if (args.drawContext != nullptr && suites.draw != nullptr) {
		DrawSuiteKit kit(suites.draw, args.drawContext);
		control_->draw(kit, geometry);
		return kOfxStatOK;
	}
	GlDrawKit kit;
	control_->draw(kit, geometry);
	kit.finish();
	return kOfxStatOK;
}

OfxStatus OverlayInteract::pen(Action action, const InteractArgs& args)
{
	const OverlayGeometry geometry = geometryFor(args);
	bool changed = false;
	bool handled = false;
	switch (action) {
	case Action::InteractPenDown:
		changed = control_->penDown(args.penPosition, modifiers_, geometry);
		handled = control_->dragging();
		if (handled && !edit_) {
			// One edit per drag, so the host undoes the whole drag as one step.
			edit_.emplace(effect_.services(), "Overlay", false);
			changedInDrag_ = false;
		}
		break;
	case Action::InteractPenMotion:
		handled = control_->dragging();
		changed = control_->penMotion(args.penPosition, modifiers_, geometry);
		if (!handled && changed) {
			// A hover change asks the host to redraw.
			const Suites& suites = effect_.ofxServices().suites();
			if (suites.interact != nullptr && suites.interact->interactRedraw != nullptr) {
				suites.interact->interactRedraw(static_cast<OfxInteractHandle>(handle_));
			}
		}
		break;
	case Action::InteractPenUp:
		handled = control_->dragging();
		changed = control_->penUp(args.penPosition, modifiers_, geometry);
		if (edit_) {
			edit_.reset();
			if (changedInDrag_ || changed) {
				effect_.flush();
			}
		}
		break;
	default:
		break;
	}
	changedInDrag_ = changedInDrag_ || changed;
	return handled ? kOfxStatOK : kOfxStatReplyDefault;
}

OfxStatus OverlayInteract::key(Action action, const InteractArgs& args)
{
	const OverlayGeometry geometry = geometryFor(args);
	OverlayModifier modifier{};
	const bool isModifier = interactModifierForKey(args.keySymbol, false, modifier);
	if (action == Action::InteractKeyDown || action == Action::InteractKeyRepeat) {
		applyModifier(args.keySymbol, true);
		if (isModifier) {
			// Modifier keys are read, never consumed.
			return kOfxStatReplyDefault;
		}
		const bool changed = control_->keyDown(args.keySymbol, modifiers_, geometry);
		if (changed) {
			effect_.flush();
		}
		return changed ? kOfxStatOK : kOfxStatReplyDefault;
	}
	applyModifier(args.keySymbol, false);
	return kOfxStatReplyDefault;
}

// ---- trampolines --------------------------------------------------------------------------------

namespace detail {

template <int N>
OfxStatus slotInteractEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	return PluginRegistry::instance().interactEntry(N, action, handle, inArgs, outArgs);
}

template <std::size_t... I>
constexpr std::array<InteractEntryFn, sizeof...(I)> makeInteractTable(std::index_sequence<I...>)
{
	return {{&slotInteractEntry<static_cast<int>(I)>...}};
}

InteractEntryFn interactEntryTrampoline(int index) noexcept
{
	static constexpr auto table = makeInteractTable(std::make_index_sequence<kMaxPluginsPerBundle>{});
	if (index < 0 || index >= kMaxPluginsPerBundle) {
		return nullptr;
	}
	return table[static_cast<std::size_t>(index)];
}

}  // namespace detail

OFXGRIP_NAMESPACE_END

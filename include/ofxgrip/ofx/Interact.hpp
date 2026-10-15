// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Interact.hpp
	@brief The overlay interact adapter: the OpenFX interact actions, their arguments, the draw
	       kit over the draw suite (with the OpenGL fallback), and the live overlay of one effect
	       instance.

	Introduced in OfxGrip 0.4.0. An effect that sets the manifest property `overlay` and
	returns a control from `Effect::createOverlay` gets an overlay interact declared on its
	descriptor: the OpenFX 1.5 draw-suite interact where the host vends the suite, else the
	OpenGL interact. The adapter decodes pen and key actions into the control's calls, tracks
	the modifier keys from key events (Q-OFX-ANY-005), brackets a drag in one parameter edit so
	it undoes as one step, clamps the screen pixel ratio (Q-OFX-ANY-012, Q-NATRON-003), and
	renders the control's drawing through the kit the host allows.
*/
#pragma once

#include <ofxgrip/core/Overlay.hpp>
#include <ofxgrip/ofx/Actions.hpp>
#include <ofxgrip/ofx/Effect.hpp>
#include <ofxgrip/ofx/Suites.hpp>

#include <memory>
#include <optional>
#include <string>

OFXGRIP_NAMESPACE_BEGIN

/*! The Natron extension that carries the device pixel ratio (Q-NATRON-003). */
inline constexpr const char* kInteractPropScreenPixelRatio = "OfxInteractPropScreenPixelRatio";

/*! The interact actions' arguments. */
struct InteractArgs {
	Time time{0};
	Point2D renderScale{1, 1};
	Point2D pixelScale{1, 1};          /*!< Canonical units per screen pixel; (1, 1) when missing. */
	Point2D penPosition;               /*!< Canonical coordinates. */
	Point2D penViewportPosition;
	double penPressure{1};
	OverlayColor background{0, 0, 0, 1};
	double screenPixelRatio{1};        /*!< Clamped to at least 1. */
	int keySymbol{0};
	std::string keyString;
	void* drawContext{nullptr};        /*!< The `OfxDrawContextHandle` of a draw action on a V2 interact. */
};

InteractArgs readInteractArgs(const PropertySet& in);

/*! The parameter seam over an effect's service. A point parameter the host draws its own
    handle for (Q-NUKE-005) stands the part down. */
class EffectOverlayParameters final : public OverlayParameters {
public:
	explicit EffectOverlayParameters(Effect& effect) : effect_(effect) {}
	bool getPoint(ParameterId id, Time time, Point2D& out) override;
	bool setPoint(ParameterId id, Time time, const Point2D& value) override;
	bool getNumber(ParameterId id, Time time, double& out) override;
	bool setNumber(ParameterId id, Time time, double value) override;
	bool hostDrawsHandle(ParameterId id) override;

private:
	Effect& effect_;
};

/*! The kit over the OpenFX 1.5 draw suite. */
class DrawSuiteKit final : public DrawKit {
public:
	DrawSuiteKit(const OfxDrawSuiteV1* suite, void* context) : suite_(suite), context_(context) {}
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

private:
	void draw(int primitive, const std::vector<Point2D>& points);
	const OfxDrawSuiteV1* suite_;
	void* context_;
};

/*! The kit over OpenGL 2.1 immediate mode, for hosts without the draw suite. Draws nothing
    when the build has no OpenGL. It reads the current matrices and leaves the modelview matrix
    current (Q-NUKE-011). */
class GlDrawKit final : public DrawKit {
public:
	/*! Binds to the current OpenGL context. Without one (a headless host, a test) every call is
	    a no-op, so a draw action still succeeds. */
	GlDrawKit();
	/*! Whether this build can draw with OpenGL. */
	static bool available() noexcept;
	/*! Whether a context was current when the kit was made. */
	bool active() const noexcept { return active_; }
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
	/*! Restores the modelview matrix mode. Called when a draw action ends. */
	void finish();

private:
	bool active_{false};
};

/*! One live overlay: the control, the effect it edits, and the drag in progress. */
class OverlayInteract {
public:
	/*! Creates the effect's control over this interact's parameter seam. `hasControl()` is
	    false when the effect returned none. */
	OverlayInteract(Effect& effect, void* handle);
	~OverlayInteract();
	bool hasControl() const noexcept { return control_ != nullptr; }
	OverlayInteract(const OverlayInteract&) = delete;
	OverlayInteract& operator=(const OverlayInteract&) = delete;

	/*! Runs one interact action. Pen and key actions answer `kOfxStatOK` when the control used
	    them and `kOfxStatReplyDefault` otherwise. */
	OfxStatus dispatch(Action action, const PropertySet& in, const PropertySet& out);

	Effect& effect() noexcept { return effect_; }
	OverlayControl& control() noexcept { return *control_; }
	OverlayModifiers modifiers() const noexcept { return modifiers_; }
	/*! The geometry of the last action, for tests and probes. */
	const OverlayGeometry& lastGeometry() const noexcept { return geometry_; }
	/*! The number of draws so far. */
	unsigned drawCount() const noexcept { return draws_; }
	/*! The `OfxInteractHandle`. */
	void* handle() const noexcept { return handle_; }

private:
	OverlayGeometry geometryFor(const InteractArgs& args);
	void applyModifier(int keySymbol, bool down);
	OfxStatus draw(const InteractArgs& args);
	OfxStatus pen(Action action, const InteractArgs& args);
	OfxStatus key(Action action, const InteractArgs& args);

	Effect& effect_;
	EffectOverlayParameters parameters_;
	std::unique_ptr<OverlayControl> control_;
	void* handle_;
	OverlayModifiers modifiers_;
	OverlayGeometry geometry_;
	std::optional<EditScope> edit_;
	bool changedInDrag_{false};
	unsigned draws_{0};
};

/*! The interact entry point the adapter declares on a plug-in's descriptor, per registry slot. */
namespace detail {
using InteractEntryFn = OfxStatus (*)(const char*, const void*, OfxPropertySetHandle, OfxPropertySetHandle);
InteractEntryFn interactEntryTrampoline(int index) noexcept;
}  // namespace detail

/*! Maps an OpenFX key symbol to the modifier it carries, honoring the platform's mapping
    (Q-OFX-ANY-005). Returns false for a key that is no modifier. */
bool interactModifierForKey(int keySymbol, bool macMapping, OverlayModifier& out) noexcept;

OFXGRIP_NAMESPACE_END

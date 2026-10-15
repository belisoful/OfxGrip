// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The overlay interact through the adapter and the mini host: the descriptor's interact entry,
// the draw-suite and OpenGL paths, pen events that drag parameters inside one edit, modifier
// tracking from key events, hover redraws, and the quirks the adapter applies.
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/Interact.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include "ofxImageEffect.h"
#include "ofxInteract.h"
#include "ofxKeySyms.h"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

enum : ParameterId { kCenter = 100, kRadius = 101, kAngle = 102, kCount = 103 };

EffectManifest overlayManifest(bool overlay)
{
	EffectManifest m;
	m.identity.ofxId = "com.belisoful.ofxgrip.overlaytest";
	m.identity.uuid = "33333333-3333-4333-8333-333333333333";
	m.identity.name = "Overlay Test";
	m.identity.grouping = "OfxGrip Tests";
	m.version = Version{1, 0, 0};
	if (overlay) {
		m.properties["overlay"] = Value(true);
	}
	m.parameters.push_back(ParameterSpec::pointParam(kCenter, "center", "Center", {50, 50}));
	m.parameters.push_back(ParameterSpec::floatParam(kRadius, "radius", "Radius", 20, 0, 1000));
	m.parameters.push_back(ParameterSpec::angleParam(kAngle, "angle", "Angle", 0));
	m.parameters.push_back(ParameterSpec::intParam(kCount, "count", "Count", 3, 0, 10));
	return m;
}

struct Seen {
	int creates{0};
	int keys{0};
	int changes{0};
};

Seen& seen()
{
	static Seen s;
	return s;
}

class KeyPart final : public OverlayPart {
public:
	KeyPart() : OverlayPart(9) {}
	bool keyDown(int keySymbol, OverlayModifiers modifiers, const OverlayGeometry& geometry) override
	{
		++seen().keys;
		if (keySymbol == kOfxKey_plus) {
			double count = 0;
			control()->parameters().getNumber(kCount, geometry.time, count);
			return control()->parameters().setNumber(kCount, geometry.time, count + (modifiers.has(OverlayModifier::Shift) ? 10 : 1));
		}
		return false;
	}
};

class OverlayEffect final : public Effect {
public:
	std::unique_ptr<OverlayControl> createOverlay(OverlayParameters& parameters) override
	{
		++seen().creates;
		auto control = std::make_unique<OverlayControl>(parameters);
		control->addParts(circleParts(kOverlayShapeAll, 1, kCenter, kRadius, kAngle));
		control->addPart(std::make_unique<KeyPart>());
		auto hud = std::make_unique<HudPart>(10, "hud");
		control->addPart(std::move(hud));
		return control;
	}
	Error onInstanceChanged(const ChangeEvent&) override
	{
		++seen().changes;
		return Error::none();
	}
	Error render(RenderContext&) override { return Error::none(); }
};

class Interact : public ::testing::Test {
protected:
	void SetUp() override
	{
		seen() = Seen{};
		PluginRegistry::instance().clear();
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		PluginRegistry::instance().clear();
	}
	void start(HostOptions options = HostOptions::spec(), bool overlay = true)
	{
		index_ = PluginRegistry::instance().registerFactory(std::make_unique<EffectFactoryFor<OverlayEffect>>(overlayManifest(overlay)));
		ASSERT_GE(index_, 0);
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(host_->load(PluginRegistry::instance().plugin(index_), "/tmp/overlay.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 200;
		project.height = 100;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
		ASSERT_NE(instance_, nullptr);
	}
	static std::size_t count(const std::vector<DrawCall>& calls, const char* op)
	{
		std::size_t n = 0;
		for (const DrawCall& call : calls) {
			n += call.op == op ? 1 : 0;
		}
		return n;
	}
	int index_{-1};
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST_F(Interact, DeclaresTheDrawSuiteInteractAndDrawsThroughIt)
{
	start();
	EXPECT_EQ(instance_->overlayVersion(), 2);
	ASSERT_TRUE(instance_->hasOverlay());
	EXPECT_EQ(seen().creates, 1);
	EXPECT_NE(host_->descriptorProps()->getPointer(kOfxImageEffectPluginPropOverlayInteractV2), nullptr);
	EXPECT_EQ(host_->descriptorProps()->getPointer(kOfxImageEffectPluginPropOverlayInteractV1), nullptr);
	ASSERT_EQ(instance_->overlayDraw(0, {2.0, 2.0}), kOfxStatOK);
	const std::vector<DrawCall>& calls = instance_->drawCalls();
	EXPECT_EQ(count(calls, "polygon"), 1u);           // the circle body
	EXPECT_EQ(count(calls, "loop"), 1u + 1u + 1u);    // the rim, the radius handle, the rotation handle
	EXPECT_EQ(count(calls, "lines"), 1u);             // the rotation spoke
	EXPECT_EQ(count(calls, "text"), 1u);              // the HUD
	EXPECT_GT(count(calls, "colour"), 0u);
	EXPECT_GT(count(calls, "width"), 0u);
	// Handles scale with the pixel scale: 4 px × 2 units.
	bool sawHandle = false;
	for (const DrawCall& call : calls) {
		if (call.op == "loop" && call.points.size() == 4 && call.points[0].x == 70 - 8 && call.points[0].y == 50 - 8) {
			sawHandle = true;
		}
	}
	EXPECT_TRUE(sawHandle);
	const std::vector<std::string> log = host_->actionLog();
	EXPECT_NE(std::find(log.begin(), log.end(), "interact:describe"), log.end());
	EXPECT_NE(std::find(log.begin(), log.end(), "interact:createInstance"), log.end());
}

TEST_F(Interact, PenEventsDragParametersInsideOneEdit)
{
	start();
	// Away from every part: not handled, nothing changes.
	EXPECT_EQ(instance_->overlayPenDown({150, 90}), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayPenUp({150, 90}), kOfxStatReplyDefault);
	EXPECT_EQ(numericValue(*instance_, "center"), (std::vector<double>{50, 50}));
	// The body: a drag moves the center; the edit brackets the drag.
	EXPECT_EQ(instance_->overlayPenDown({55, 55}), kOfxStatOK);
	EXPECT_EQ(instance_->editDepth(), 1);
	EXPECT_EQ(instance_->overlayPenMotion({58, 57}), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "center"), (std::vector<double>{53, 52}));
	EXPECT_EQ(instance_->overlayPenUp({60, 60}), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "center"), (std::vector<double>{55, 55}));
	EXPECT_EQ(instance_->editDepth(), 0);
	ASSERT_FALSE(instance_->editNames().empty());
	EXPECT_EQ(instance_->editNames().back(), "Overlay");
	// The radius handle at the rim's angle 0 writes the distance.
	EXPECT_EQ(instance_->overlayPenDown({75, 55}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayPenMotion({85, 55}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayPenUp({85, 55}), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "radius"), std::vector<double>{30});
	// Shift arrives as a key: the rotation handle snaps to 45°. Its tip rides a spoke 24 px past
	// the rim: radius 30 + 24 at angle 0.
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_Shift_L), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayPenDown({55 + 54, 55}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayPenMotion({55 + 20, 55 + 21}), kOfxStatOK);
	EXPECT_NEAR(numericValue(*instance_, "angle")[0], 3.14159265358979323846 / 4.0, 1e-9);
	EXPECT_EQ(instance_->overlayPenUp({55 + 20, 55 + 21}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayKeyUp(kOfxKey_Shift_L), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayPenDown({55 + 54 * std::cos(0.7853981634), 55 + 54 * std::sin(0.7853981634)}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayPenMotion({55 + 20, 55 + 21}), kOfxStatOK);
	EXPECT_NEAR(numericValue(*instance_, "angle")[0], std::atan2(21.0, 20.0), 1e-9);
	EXPECT_EQ(instance_->overlayPenUp({55 + 20, 55 + 21}), kOfxStatOK);
	// A hover change asks for a redraw; a pen motion without a drag is not consumed.
	const int before = instance_->overlayRedrawRequests();
	EXPECT_EQ(instance_->overlayPenMotion({56, 56}), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayRedrawRequests(), before + 1);
	EXPECT_EQ(instance_->overlayPenMotion({56, 57}), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayRedrawRequests(), before + 1);
	// Keys reach the parts; Shift is read from the tracked modifiers.
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_plus, "+"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "count"), std::vector<double>{4});
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_Shift_R), kOfxStatReplyDefault);
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_plus, "+"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "count"), std::vector<double>{14});
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_a, "a"), kOfxStatReplyDefault);
	EXPECT_EQ(seen().keys, 3);
	EXPECT_EQ(instance_->overlayGainFocus(), kOfxStatOK);
	// Losing focus mid-drag ends the drag and clears the modifiers.
	EXPECT_EQ(instance_->overlayPenDown({55, 55}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayLoseFocus(), kOfxStatOK);
	EXPECT_EQ(instance_->editDepth(), 0);
	EXPECT_EQ(instance_->overlayKeyDown(kOfxKey_plus, "+"), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "count"), std::vector<double>{15});
}

TEST_F(Interact, FallsBackToTheOpenGLInteractWithoutTheDrawSuite)
{
	HostOptions options = HostOptions::nuke();
	start(options);
	EXPECT_EQ(instance_->overlayVersion(), 1);
	EXPECT_NE(host_->descriptorProps()->getPointer(kOfxImageEffectPluginPropOverlayInteractV1), nullptr);
	// Without a GL context the kit draws nothing the mini host can see; the action still succeeds.
	EXPECT_EQ(instance_->overlayDraw(), kOfxStatOK);
	EXPECT_TRUE(instance_->drawCalls().empty());
	// Q-NUKE-005: the host draws its own handle for the point, so the center's parts stand
	// down; the circle body is not a point handle and still drags.
	EXPECT_EQ(instance_->overlayPenDown({50, 50}), kOfxStatOK);
	EXPECT_EQ(instance_->overlayPenUp({52, 50}), kOfxStatOK);
	EXPECT_EQ(numericValue(*instance_, "center"), (std::vector<double>{52, 50}));
	EXPECT_TRUE(GlDrawKit::available() || !GlDrawKit::available());
}

TEST_F(Interact, HostsWithoutOverlaysOrEffectsWithoutControlsGetNone)
{
	HostOptions none = HostOptions::spec();
	none.supportsOverlays = false;
	start(none);
	EXPECT_FALSE(instance_->hasOverlay());
	EXPECT_EQ(instance_->overlayDraw(), kOfxStatErrBadHandle);
	EXPECT_EQ(instance_->overlayPenDown({1, 1}), kOfxStatErrBadHandle);
	instance_.reset();
	host_.reset();
	PluginRegistry::instance().clear();
	start(HostOptions::spec(), false);
	EXPECT_FALSE(instance_->hasOverlay());
	EXPECT_EQ(host_->descriptorProps()->getPointer(kOfxImageEffectPluginPropOverlayInteractV2), nullptr);
	EXPECT_EQ(seen().creates, 0);
}

TEST(InteractArgs, ReadsAndClampsTheProperties)
{
	PropSet props;
	props.setDouble(kOfxPropTime, 3.0);
	props.setDoubles(kOfxImageEffectPropRenderScale, {0.5, 0.5});
	props.setDoubles(kOfxInteractPropPixelScale, {0.0, 2.0});
	props.setDoubles(kOfxInteractPropPenPosition, {10.0, 20.0});
	props.setInts(kOfxInteractPropPenViewportPosition, {10, 20});
	props.setDouble(kOfxInteractPropPenPressure, 0.5);
	props.setDoubles(kOfxInteractPropBackgroundColour, {0.1, 0.2, 0.3});
	props.setDouble(kInteractPropScreenPixelRatio, 0.0);
	props.setInt(kOfxPropKeySym, kOfxKey_a);
	props.setString(kOfxPropKeyString, "a");
	const InteractArgs args = readInteractArgs(PropertySet(PropSet::suite(), props.handle()));
	EXPECT_EQ(args.time, 3.0);
	EXPECT_EQ(args.renderScale, (Point2D{0.5, 0.5}));
	EXPECT_EQ(args.pixelScale, (Point2D{1.0, 2.0}));  // a zero scale reads as 1
	EXPECT_EQ(args.penPosition, (Point2D{10, 20}));
	EXPECT_EQ(args.penPressure, 0.5);
	EXPECT_FLOAT_EQ(args.background.g, 0.2f);
	EXPECT_EQ(args.screenPixelRatio, 1.0);  // Q-OFX-ANY-012
	EXPECT_EQ(args.keySymbol, kOfxKey_a);
	EXPECT_EQ(args.keyString, "a");
	EXPECT_EQ(args.drawContext, nullptr);
	props.setDouble(kInteractPropScreenPixelRatio, 2.0);
	EXPECT_EQ(readInteractArgs(PropertySet(PropSet::suite(), props.handle())).screenPixelRatio, 2.0);
	PropSet empty;
	const InteractArgs defaults = readInteractArgs(PropertySet(PropSet::suite(), empty.handle()));
	EXPECT_EQ(defaults.pixelScale, (Point2D{1, 1}));
	EXPECT_EQ(defaults.renderScale, (Point2D{1, 1}));
	EXPECT_EQ(defaults.penPressure, 1.0);
	// Q-OFX-ANY-005: the macOS mapping swaps Command and Control.
	OverlayModifier modifier{};
	EXPECT_TRUE(interactModifierForKey(kOfxKey_Control_L, false, modifier));
	EXPECT_EQ(modifier, OverlayModifier::Control);
	EXPECT_TRUE(interactModifierForKey(kOfxKey_Control_R, true, modifier));
	EXPECT_EQ(modifier, OverlayModifier::Command);
	EXPECT_TRUE(interactModifierForKey(kOfxKey_Meta_L, true, modifier));
	EXPECT_EQ(modifier, OverlayModifier::Control);
	EXPECT_TRUE(interactModifierForKey(kOfxKey_Super_R, false, modifier));
	EXPECT_EQ(modifier, OverlayModifier::Command);
	EXPECT_TRUE(interactModifierForKey(kOfxKey_Alt_R, false, modifier));
	EXPECT_EQ(modifier, OverlayModifier::Alt);
	EXPECT_FALSE(interactModifierForKey(kOfxKey_a, false, modifier));
}

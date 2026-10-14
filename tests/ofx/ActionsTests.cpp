// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/PropertySetImpl.hpp>
#include <ofxgrip/ofx/Actions.hpp>

#include "ofxGPURender.h"
#include "ofxImageEffect.h"

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Actions, ParsesEveryKnownActionString)
{
	EXPECT_EQ(parseAction(kOfxActionLoad), Action::Load);
	EXPECT_EQ(parseAction(kOfxImageEffectActionRender), Action::Render);
	EXPECT_EQ(parseAction(kOfxImageEffectActionDescribeInContext), Action::DescribeInContext);
	EXPECT_EQ(parseAction(kOfxInteractActionPenDown), Action::InteractPenDown);
	EXPECT_EQ(parseAction("OfxActionSomethingNew"), Action::Unknown);
	EXPECT_EQ(parseAction(nullptr), Action::Unknown);
	for (int i = 1; i <= static_cast<int>(Action::Dialog); ++i) {
		const Action a = static_cast<Action>(i);
		EXPECT_EQ(parseAction(actionString(a)), a) << actionString(a);
	}
}

TEST(Actions, ReadsRenderArgs)
{
	minihost::PropSet in;
	in.setDouble(kOfxPropTime, 7.0);
	in.setDoubles(kOfxImageEffectPropRenderScale, {0.5, 0.25});
	in.setInts(kOfxImageEffectPropRenderWindow, {1, 2, 3, 4});
	in.setString(kOfxImageEffectPropFieldToRender, kOfxImageFieldUpper);
	in.setInt(kOfxImageEffectPropRenderQualityDraft, 1);
	in.setInt(kOfxImageEffectPropMetalEnabled, 1);
	int fakeQueue = 0;
	in.setPointer(kOfxImageEffectPropMetalCommandQueue, &fakeQueue);
	const RenderArgs a = readRenderArgs(PropertySet(minihost::PropSet::suite(), in.handle()));
	EXPECT_DOUBLE_EQ(a.time, 7.0);
	EXPECT_EQ(a.renderScale.y, 0.25);
	EXPECT_EQ(a.renderWindow, (RectI{1, 2, 3, 4}));
	EXPECT_EQ(a.field, Field::Upper);
	EXPECT_TRUE(a.draft);
	EXPECT_EQ(a.gpu, GpuApi::Metal);
	EXPECT_EQ(a.gpuQueue, &fakeQueue);
}

TEST(Actions, MissingArgsFallBackToDefaults)
{
	minihost::PropSet in;
	const RenderArgs a = readRenderArgs(PropertySet(minihost::PropSet::suite(), in.handle()));
	EXPECT_DOUBLE_EQ(a.time, 0.0);
	EXPECT_EQ(a.renderScale, (Point2D{1, 1}));
	EXPECT_TRUE(a.renderWindow.empty());
	EXPECT_EQ(a.gpu, GpuApi::None);
	in.setDoubles(kOfxImageEffectPropRenderScale, {0.0, 0.0});
	EXPECT_EQ(readRenderArgs(PropertySet(minihost::PropSet::suite(), in.handle())).renderScale, (Point2D{1, 1}));
}

TEST(Actions, ReadsInstanceChangedArgs)
{
	minihost::PropSet in;
	in.setString(kOfxPropType, kOfxTypeParameter);
	in.setString(kOfxPropName, "gain");
	in.setString(kOfxPropChangeReason, kOfxChangePluginEdited);
	in.setDouble(kOfxPropTime, 3.0);
	const InstanceChangedArgs a = readInstanceChangedArgs(PropertySet(minihost::PropSet::suite(), in.handle()));
	EXPECT_FALSE(a.isClip);
	EXPECT_EQ(a.name, "gain");
	EXPECT_EQ(a.reason, ChangeReason::PluginEdited);
	in.setString(kOfxPropType, kOfxTypeClip);
	in.setString(kOfxPropChangeReason, kOfxChangeTime);
	const InstanceChangedArgs c = readInstanceChangedArgs(PropertySet(minihost::PropSet::suite(), in.handle()));
	EXPECT_TRUE(c.isClip);
	EXPECT_EQ(c.reason, ChangeReason::TimeChanged);
}

TEST(Actions, ReadsRegionSequenceAndCreateArgs)
{
	minihost::PropSet in;
	in.setDouble(kOfxPropTime, 2.0);
	in.setDoubles(kOfxImageEffectPropRegionOfInterest, {0, 0, 10, 20});
	const RegionArgs r = readRegionArgs(PropertySet(minihost::PropSet::suite(), in.handle()), true);
	EXPECT_EQ(r.regionOfInterest.y2, 20.0);
	in.setDoubles(kOfxImageEffectPropFrameRange, {5, 9});
	in.setDouble(kOfxImageEffectPropFrameStep, 2.0);
	const SequenceRenderArgs s = readSequenceRenderArgs(PropertySet(minihost::PropSet::suite(), in.handle()));
	EXPECT_EQ(s.firstFrame, 5.0);
	EXPECT_EQ(s.lastFrame, 9.0);
	EXPECT_EQ(s.frameStep, 2.0);
	in.setString("OfxImageEffectPropResolvePage", "Fusion");
	EXPECT_EQ(readCreateInstanceArgs(PropertySet(minihost::PropSet::suite(), in.handle())).resolvePage, "Fusion");
	in.setInts(kOfxImageEffectPropRenderWindow, {0, 0, 8, 8});
	EXPECT_EQ(readIsIdentityArgs(PropertySet(minihost::PropSet::suite(), in.handle())).renderWindow.x2, 8);
}

TEST(Actions, OfxNameTablesRoundTrip)
{
	EffectKind kind{};
	EXPECT_TRUE(parseOfxContext(kOfxImageEffectContextGenerator, kind));
	EXPECT_EQ(kind, EffectKind::Generator);
	EXPECT_FALSE(parseOfxContext("nope", kind));
	BitDepth depth{};
	EXPECT_TRUE(parseOfxBitDepth(kOfxBitDepthHalf, depth));
	EXPECT_EQ(depth, BitDepth::Half);
	Components components{};
	EXPECT_TRUE(parseOfxComponents(kOfxImageComponentAlpha, components));
	EXPECT_EQ(components, Components::Alpha);
	Field field{};
	EXPECT_TRUE(parseOfxField(kOfxImageFieldDoubled, field));
	EXPECT_EQ(field, Field::Doubled);
	Premultiplication p{};
	EXPECT_TRUE(parseOfxPremultiplication(kOfxImageUnPreMultiplied, p));
	EXPECT_EQ(p, Premultiplication::Unpremultiplied);
	EXPECT_EQ(std::string(ofxPremultiplicationName(Premultiplication::Opaque)), kOfxImageOpaque);
}

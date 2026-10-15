// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The on-screen control model over an in-memory parameter store: hit testing, drag routing,
// modifiers, the shape families, and the drawing each part emits through the recording kit.
#include <ofxgrip/core/Overlay.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <map>

using namespace ofxgrip;

namespace {

constexpr double kPi = 3.14159265358979323846;

class Store final : public OverlayParameters {
public:
	bool getPoint(ParameterId id, Time, Point2D& out) override
	{
		auto it = points.find(id);
		if (it == points.end()) {
			return false;
		}
		out = it->second;
		return true;
	}
	bool setPoint(ParameterId id, Time, const Point2D& value) override
	{
		if (points.count(id) == 0 || readOnly) {
			return false;
		}
		points[id] = value;
		++writes;
		return true;
	}
	bool getNumber(ParameterId id, Time, double& out) override
	{
		auto it = numbers.find(id);
		if (it == numbers.end()) {
			return false;
		}
		out = it->second;
		return true;
	}
	bool setNumber(ParameterId id, Time, double value) override
	{
		if (numbers.count(id) == 0 || readOnly) {
			return false;
		}
		numbers[id] = value;
		++writes;
		return true;
	}
	bool hostDrawsHandle(ParameterId id) override { return hostHandles.count(id) != 0; }
	std::map<ParameterId, Point2D> points;
	std::map<ParameterId, double> numbers;
	std::map<ParameterId, bool> hostHandles;
	bool readOnly{false};
	int writes{0};
};

enum : ParameterId { kA = 1, kB = 2, kC = 3, kD = 4, kRadius = 10, kAngle = 11, kWidth = 12, kHeight = 13 };

OverlayGeometry geometryAt(double unitsPerPixel = 1.0)
{
	OverlayGeometry g;
	g.pixelScale = {unitsPerPixel, unitsPerPixel};
	g.imageBounds = {0, 0, 100, 100};
	return g;
}

}  // namespace

TEST(Overlay, HelpersConstrainAnglesAndMeasureSegments)
{
	OverlayModifiers none;
	OverlayModifiers shift;
	shift.set(OverlayModifier::Shift);
	EXPECT_DOUBLE_EQ(overlayConstrainedAngle(0.3, none), 0.3);
	EXPECT_DOUBLE_EQ(overlayConstrainedAngle(0.3, shift), 0.0);
	EXPECT_DOUBLE_EQ(overlayConstrainedAngle(0.7, shift), kPi / 4.0);
	EXPECT_DOUBLE_EQ(overlayDistanceSquaredToSegment({5, 3}, {0, 0}, {10, 0}), 9.0);
	EXPECT_DOUBLE_EQ(overlayDistanceSquaredToSegment({-3, 4}, {0, 0}, {10, 0}), 25.0);
	EXPECT_DOUBLE_EQ(overlayDistanceSquaredToSegment({3, 4}, {0, 0}, {0, 0}), 25.0);
	shift.set(OverlayModifier::Shift, false);
	EXPECT_EQ(shift, none);
	EXPECT_EQ(OverlayGeometry{}.unitsPerPixel(), 1.0);
	OverlayGeometry zero;
	zero.pixelScale = {0, 0};
	EXPECT_EQ(zero.unitsPerPixel(), 1.0);
}

TEST(Overlay, PointHandleHitsDragsAndDraws)
{
	Store store;
	store.points[kA] = {50, 50};
	OverlayControl control(store);
	OverlayPart* part = control.addPart(std::make_unique<PointHandlePart>(1, kA));
	ASSERT_NE(part, nullptr);
	EXPECT_EQ(part->control(), &control);
	EXPECT_EQ(control.part(1), part);
	EXPECT_EQ(control.part(9), nullptr);
	EXPECT_EQ(control.addPart(nullptr), nullptr);
	const OverlayGeometry g = geometryAt(2.0);  // 2 canonical units per screen pixel
	EXPECT_EQ(control.hitTest({50, 50}, g), 1);
	EXPECT_EQ(control.hitTest({50 + 19, 50}, g), 1);  // 10 px × 2
	EXPECT_EQ(control.hitTest({50 + 21, 50}, g), 0);
	// Hover changes report a redraw; a drag writes the pointer's position.
	EXPECT_TRUE(control.penMotion({50, 50}, {}, g));
	EXPECT_EQ(control.hoveredPart(), 1);
	EXPECT_FALSE(control.penMotion({51, 50}, {}, g));
	EXPECT_TRUE(control.penMotion({90, 90}, {}, g));
	EXPECT_EQ(control.hoveredPart(), 0);
	EXPECT_FALSE(control.penDown({90, 90}, {}, g));
	EXPECT_FALSE(control.dragging());
	EXPECT_FALSE(control.penDown({50, 50}, {}, g));
	EXPECT_TRUE(control.dragging());
	EXPECT_EQ(control.activePart(), 1);
	EXPECT_TRUE(control.penMotion({60, 55}, {}, g));
	EXPECT_EQ(store.points[kA], (Point2D{60, 55}));
	// Shift constrains to the dominant axis from the drag start.
	OverlayModifiers shift;
	shift.set(OverlayModifier::Shift);
	EXPECT_TRUE(control.penMotion({70, 58}, shift, g));
	EXPECT_EQ(store.points[kA], (Point2D{70, 50}));
	EXPECT_TRUE(control.penMotion({52, 80}, shift, g));
	EXPECT_EQ(store.points[kA], (Point2D{50, 80}));
	// Alt applies a tenth of the pointer's travel.
	OverlayModifiers alt;
	alt.set(OverlayModifier::Alt);
	EXPECT_TRUE(control.penMotion({62, 90}, alt, g));
	EXPECT_NEAR(store.points[kA].x, 51.0, 1e-9);
	EXPECT_NEAR(store.points[kA].y, 81.0, 1e-9);
	EXPECT_TRUE(control.penUp({40, 40}, {}, g));
	EXPECT_EQ(store.points[kA], (Point2D{40, 40}));
	EXPECT_FALSE(control.dragging());
	EXPECT_EQ(control.activePart(), 0);
	EXPECT_FALSE(control.penUp({1, 1}, {}, g));
	// Drawing: the handle emits an outline loop, a fill when selected, around the point.
	RecordingDrawKit kit;
	control.draw(kit, g);
	EXPECT_EQ(kit.count("loop"), 1u);
	EXPECT_EQ(kit.count("rect"), 0u);
	ASSERT_EQ(kit.records.back().points.size(), 4u);
	EXPECT_EQ(kit.records.back().points[0], (Point2D{40 - 8, 40 - 8}));
	control.penMotion({40, 40}, {}, g);  // hovered → selected
	RecordingDrawKit selected;
	control.draw(selected, g);
	EXPECT_EQ(selected.count("rect"), 1u);
	// A read-only store refuses the write and the drag reports no change.
	store.readOnly = true;
	control.penDown({40, 40}, {}, g);
	EXPECT_FALSE(control.penMotion({45, 45}, {}, g));
	control.penUp({45, 45}, {}, g);
	store.readOnly = false;
	// A host-drawn handle stands the part down.
	store.hostHandles[kA] = true;
	EXPECT_EQ(control.hitTest({40, 40}, g), 0);
	RecordingDrawKit none;
	control.draw(none, g);
	EXPECT_EQ(none.count("loop"), 0u);
	// A missing parameter neither hits nor draws nor drags.
	store.hostHandles.clear();
	store.points.clear();
	EXPECT_EQ(control.hitTest({40, 40}, g), 0);
	control.draw(none, g);
	EXPECT_EQ(none.count("loop"), 0u);
	EXPECT_FALSE(part->drag({1, 1}, {1, 1}, {}, g));
}

TEST(Overlay, LineAndPolylineMoveEveryPoint)
{
	Store store;
	store.points[kA] = {0, 0};
	store.points[kB] = {100, 0};
	store.points[kC] = {100, 100};
	OverlayControl control(store);
	control.addParts(lineParts(kOverlayShapeAll, 1, kA, kB));
	ASSERT_EQ(control.parts().size(), 3u);
	const OverlayGeometry g = geometryAt();
	EXPECT_EQ(control.hitTest({50, 5}, g), 1);   // the body
	EXPECT_EQ(control.hitTest({50, 7}, g), 0);
	EXPECT_EQ(control.hitTest({0, 0}, g), 2);    // handles win overlapping hits
	EXPECT_EQ(control.hitTest({100, 0}, g), 3);
	control.penDown({50, 5}, {}, g);
	EXPECT_TRUE(control.penMotion({60, 15}, {}, g));
	EXPECT_EQ(store.points[kA], (Point2D{10, 10}));
	EXPECT_EQ(store.points[kB], (Point2D{110, 10}));
	control.penUp({60, 15}, {}, g);
	RecordingDrawKit kit;
	control.draw(kit, g);
	EXPECT_EQ(kit.count("lines"), 1u);
	EXPECT_EQ(kit.count("loop"), 2u);

	OverlayControl poly(store);
	poly.addParts(polylineParts(kOverlayShapeAll, 10, {kA, kB, kC}, true));
	ASSERT_EQ(poly.parts().size(), 4u);
	EXPECT_EQ(poly.hitTest({60, 60}, g), 10);  // the closing segment C→A
	poly.penDown({60, 60}, {}, g);
	EXPECT_TRUE(poly.penMotion({61, 60}, {}, g));
	EXPECT_EQ(store.points[kC], (Point2D{101, 100}));
	poly.penUp({61, 60}, {}, g);
	RecordingDrawKit polyKit;
	poly.draw(polyKit, g);
	EXPECT_EQ(polyKit.count("loop"), 1u + 3u);
	OverlayControl open(store);
	open.addParts(polylineParts(static_cast<std::uint8_t>(OverlayShapeOption::Body), 20, {kA, kB, kC}, false));
	EXPECT_EQ(open.hitTest({60, 60}, g), 0);
	RecordingDrawKit openKit;
	open.draw(openKit, g);
	EXPECT_EQ(openKit.count("strip"), 1u);
	// Too few points: nothing.
	OverlayControl single(store);
	single.addParts(polylineParts(kOverlayShapeAll, 30, {kA}, false));
	EXPECT_EQ(single.hitTest({10, 10}, g), 31);
	RecordingDrawKit singleKit;
	single.draw(singleKit, g);
	EXPECT_EQ(singleKit.count("strip"), 0u);
	store.points.erase(kB);
	EXPECT_EQ(control.hitTest({50, 5}, g), 0);
	EXPECT_FALSE(control.part(1)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_FALSE(poly.part(10)->drag({0, 0}, {1, 1}, {}, g));
}

TEST(Overlay, RectangleFamilyResizesWithModifiers)
{
	Store store;
	store.points[kA] = {10, 10};
	store.points[kB] = {50, 30};
	store.numbers[kAngle] = 0.0;
	OverlayControl control(store);
	control.addParts(rectParts(kOverlayShapeAll, 1, kA, kB, kAngle));
	ASSERT_EQ(control.parts().size(), 6u);  // body, 4 corners, dial
	const OverlayGeometry g = geometryAt();
	EXPECT_EQ(control.hitTest({30, 20}, g), 1);
	EXPECT_EQ(control.hitTest({10, 10}, g), 2);
	EXPECT_EQ(control.hitTest({50, 10}, g), 3);
	EXPECT_EQ(control.hitTest({50, 30}, g), 4);
	EXPECT_EQ(control.hitTest({10, 30}, g), 5);
	EXPECT_EQ(control.hitTest({30 + 40, 20}, g), 6);  // the dial tip, 40 px from the center
	// The body moves both corners.
	control.penDown({30, 20}, {}, g);
	control.penMotion({35, 22}, {}, g);
	control.penUp({35, 22}, {}, g);
	EXPECT_EQ(store.points[kA], (Point2D{15, 12}));
	EXPECT_EQ(store.points[kB], (Point2D{55, 32}));
	// The upper-right corner writes itself; Shift keeps the aspect from the opposite corner.
	control.penDown({55, 32}, {}, g);
	control.penMotion({75, 42}, {}, g);
	EXPECT_EQ(store.points[kB], (Point2D{75, 42}));
	OverlayModifiers shift;
	shift.set(OverlayModifier::Shift);
	control.penMotion({95, 20}, shift, g);  // width 60 → scale 80/60 wins over height
	EXPECT_NEAR(store.points[kB].x, 95.0, 1e-9);
	EXPECT_NEAR(store.points[kB].y, 12.0 + 30.0 * 80.0 / 60.0, 1e-9);
	control.penUp({95, 20}, shift, g);
	// Alt resizes about the center: the lower-left mirrors.
	store.points[kA] = {0, 0};
	store.points[kB] = {40, 20};
	OverlayModifiers alt;
	alt.set(OverlayModifier::Alt);
	control.penDown({40, 20}, {}, g);
	control.penMotion({50, 24}, alt, g);
	control.penUp({50, 24}, alt, g);
	EXPECT_EQ(store.points[kB], (Point2D{50, 24}));
	EXPECT_EQ(store.points[kA], (Point2D{-10, -4}));
	// The other corners write their components.
	store.points[kA] = {0, 0};
	store.points[kB] = {40, 20};
	control.penDown({40, 0}, {}, g);
	control.penMotion({44, -2}, {}, g);
	control.penUp({44, -2}, {}, g);
	EXPECT_EQ(store.points[kA], (Point2D{0, -2}));
	EXPECT_EQ(store.points[kB], (Point2D{44, 20}));
	control.penDown({0, 20}, {}, g);
	control.penMotion({-3, 25}, alt, g);
	control.penUp({-3, 25}, alt, g);
	EXPECT_EQ(store.points[kA], (Point2D{-3, -7}));
	EXPECT_EQ(store.points[kB], (Point2D{47, 25}));
	control.penDown({-3, -7}, {}, g);
	control.penMotion({0, 0}, {}, g);
	control.penUp({0, 0}, {}, g);
	EXPECT_EQ(store.points[kA], (Point2D{0, 0}));
	// The dial writes the pointer's angle about the midpoint; Shift snaps to 45°.
	control.penDown({47.0 / 2.0 + 40.0, 12.5}, {}, g);
	control.penMotion({47.0 / 2.0, 12.5 + 30.0}, {}, g);
	EXPECT_NEAR(store.numbers[kAngle], kPi / 2.0, 1e-9);
	control.penMotion({47.0 / 2.0 + 20.0, 12.5 + 21.0}, shift, g);
	EXPECT_NEAR(store.numbers[kAngle], kPi / 4.0, 1e-9);
	EXPECT_FALSE(control.penMotion({47.0 / 2.0, 12.5}, {}, g));  // at the center: no angle
	control.penUp({47.0 / 2.0, 12.5}, {}, g);
	// A rotated rectangle hit-tests in the unrotated frame.
	store.points[kA] = {0, 0};
	store.points[kB] = {40, 20};
	store.numbers[kAngle] = kPi / 2.0;
	EXPECT_EQ(control.hitTest({20, 20}, g), 1);  // inside the rotated box (20 wide, 40 tall around the center)
	EXPECT_EQ(control.hitTest({20 + 15, 10}, g), 0);
	EXPECT_EQ(control.hitTest({30, -10}, g), 2);  // the rotated lower-left corner
	EXPECT_EQ(control.hitTest({30, 30}, g), 3);   // the rotated lower-right corner
	control.penDown({30, 30}, {}, g);
	control.penMotion({30, 32}, {}, g);  // unrotated: (42, 0)
	control.penUp({30, 32}, {}, g);
	EXPECT_NEAR(store.points[kB].x, 42.0, 1e-9);
	EXPECT_NEAR(store.points[kA].y, 0.0, 1e-9);
	RecordingDrawKit kit;
	control.draw(kit, g);
	EXPECT_EQ(kit.count("polygon"), 1u);
	EXPECT_EQ(kit.count("loop"), 1u + 4u + 1u);
	EXPECT_EQ(kit.count("lines"), 1u);
	// Without an angle parameter the dial is omitted.
	OverlayControl plain(store);
	plain.addParts(rectParts(kOverlayShapeAll, 1, kA, kB));
	EXPECT_EQ(plain.parts().size(), 5u);
	store.points.erase(kA);
	EXPECT_EQ(control.hitTest({20, 10}, g), 0);
	RecordingDrawKit empty;
	control.draw(empty, g);
	EXPECT_EQ(empty.count("polygon"), 0u);
	EXPECT_FALSE(control.part(1)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_FALSE(control.part(2)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_FALSE(control.part(6)->drag({0, 0}, {1, 1}, {}, g));
}

TEST(Overlay, CircleFamilyMovesResizesAndRotates)
{
	Store store;
	store.points[kA] = {50, 50};
	store.numbers[kRadius] = 20.0;
	store.numbers[kAngle] = 0.0;
	OverlayControl control(store);
	control.addParts(circleParts(kOverlayShapeAll, 1, kA, kRadius, kAngle));
	ASSERT_EQ(control.parts().size(), 3u);
	const OverlayGeometry g = geometryAt();
	EXPECT_EQ(control.hitTest({55, 55}, g), 1);
	EXPECT_EQ(control.hitTest({50, 80}, g), 0);
	EXPECT_EQ(control.hitTest({70, 50}, g), 2);  // the radius handle on the rim
	EXPECT_EQ(control.hitTest({94, 50}, g), 3);  // the rotation tip 24 px past the rim at angle 0
	store.numbers[kAngle] = kPi / 2.0;
	EXPECT_EQ(control.hitTest({70, 50}, g), 2);
	EXPECT_EQ(control.hitTest({50, 94}, g), 3);
	control.penDown({55, 55}, {}, g);
	control.penMotion({57, 58}, {}, g);
	control.penUp({57, 58}, {}, g);
	EXPECT_EQ(store.points[kA], (Point2D{52, 53}));
	control.penDown({72, 53}, {}, g);
	control.penMotion({82, 53}, {}, g);
	control.penUp({82, 53}, {}, g);
	EXPECT_NEAR(store.numbers[kRadius], 30.0, 1e-9);
	control.penDown({52, 107}, {}, g);
	control.penMotion({22, 53}, {}, g);
	EXPECT_NEAR(store.numbers[kAngle], kPi, 1e-9);
	OverlayModifiers shift;
	shift.set(OverlayModifier::Shift);
	control.penMotion({52 + 10, 53 + 9}, shift, g);
	EXPECT_NEAR(store.numbers[kAngle], kPi / 4.0, 1e-9);
	EXPECT_FALSE(control.penMotion({52, 53}, {}, g));
	control.penUp({52, 53}, {}, g);
	RecordingDrawKit kit;
	control.draw(kit, g);
	EXPECT_EQ(kit.count("polygon"), 1u);
	EXPECT_EQ(kit.count("loop"), 1u + 1u + 1u);
	EXPECT_EQ(kit.count("lines"), 1u);
	OverlayControl noAngle(store);
	noAngle.addParts(circleParts(kOverlayShapeAll, 1, kA, kRadius));
	EXPECT_EQ(noAngle.parts().size(), 2u);
	store.numbers.erase(kRadius);
	EXPECT_EQ(control.hitTest({52, 53}, g), 0);
	RecordingDrawKit empty;
	control.draw(empty, g);
	EXPECT_EQ(empty.count("polygon"), 0u);
	EXPECT_FALSE(control.part(2)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_TRUE(control.part(3)->drag({0, 0}, {1, 1}, {}, g));  // the angle needs no radius
	store.points.erase(kA);
	EXPECT_FALSE(control.part(1)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_FALSE(control.part(2)->drag({0, 0}, {1, 1}, {}, g));
	EXPECT_FALSE(control.part(3)->drag({0, 0}, {1, 1}, {}, g));
}

TEST(Overlay, BoxFamilyResizesInItsLocalFrame)
{
	Store store;
	store.points[kA] = {50, 50};
	store.numbers[kWidth] = 40.0;
	store.numbers[kHeight] = 20.0;
	store.numbers[kAngle] = 0.0;
	OverlayControl control(store);
	control.addParts(boxParts(kOverlayShapeAll, 1, kA, kWidth, kHeight, kAngle));
	ASSERT_EQ(control.parts().size(), 6u);
	const OverlayGeometry g = geometryAt();
	EXPECT_EQ(control.hitTest({50, 50}, g), 1);
	EXPECT_EQ(control.hitTest({30, 40}, g), 2);
	EXPECT_EQ(control.hitTest({70, 40}, g), 3);
	EXPECT_EQ(control.hitTest({70, 60}, g), 4);
	EXPECT_EQ(control.hitTest({30, 60}, g), 5);
	EXPECT_EQ(control.hitTest({90, 50}, g), 6);
	EXPECT_EQ(control.hitTest({50, 75}, g), 0);
	control.penDown({50, 50}, {}, g);
	control.penMotion({52, 51}, {}, g);
	control.penUp({52, 51}, {}, g);
	EXPECT_EQ(store.points[kA], (Point2D{52, 51}));
	// The upper-right corner: the opposite corner stays, the center shifts.
	control.penDown({72, 61}, {}, g);
	control.penMotion({82, 71}, {}, g);
	control.penUp({82, 71}, {}, g);
	EXPECT_NEAR(store.numbers[kWidth], 50.0, 1e-9);
	EXPECT_NEAR(store.numbers[kHeight], 30.0, 1e-9);
	EXPECT_NEAR(store.points[kA].x, 57.0, 1e-9);
	EXPECT_NEAR(store.points[kA].y, 56.0, 1e-9);
	// Alt anchors the center; Shift locks the aspect.
	OverlayModifiers alt;
	alt.set(OverlayModifier::Alt);
	control.penDown({82, 71}, alt, g);
	control.penMotion({87, 71}, alt, g);
	control.penUp({87, 71}, alt, g);
	EXPECT_NEAR(store.numbers[kWidth], 60.0, 1e-9);
	EXPECT_NEAR(store.numbers[kHeight], 30.0, 1e-9);
	EXPECT_NEAR(store.points[kA].x, 57.0, 1e-9);
	OverlayModifiers shiftAlt;
	shiftAlt.set(OverlayModifier::Alt).set(OverlayModifier::Shift);
	control.penDown({87, 71}, shiftAlt, g);
	control.penMotion({117, 71}, shiftAlt, g);
	control.penUp({117, 71}, shiftAlt, g);
	EXPECT_NEAR(store.numbers[kWidth], 120.0, 1e-9);
	EXPECT_NEAR(store.numbers[kHeight], 60.0, 1e-9);
	OverlayModifiers shift;
	shift.set(OverlayModifier::Shift);
	store.numbers[kWidth] = 40.0;
	store.numbers[kHeight] = 20.0;
	store.points[kA] = {50, 50};
	control.penDown({70, 60}, shift, g);
	control.penMotion({90, 60}, shift, g);  // width doubles, so height doubles
	control.penUp({90, 60}, shift, g);
	EXPECT_NEAR(store.numbers[kWidth], 60.0, 1e-9);
	EXPECT_NEAR(store.numbers[kHeight], 30.0, 1e-9);
	EXPECT_NEAR(store.points[kA].x, 60.0, 1e-9);
	EXPECT_NEAR(store.points[kA].y, 55.0, 1e-9);
	// Rotated by 90°: the local frame turns.
	store.numbers[kWidth] = 40.0;
	store.numbers[kHeight] = 20.0;
	store.points[kA] = {50, 50};
	store.numbers[kAngle] = kPi / 2.0;
	EXPECT_EQ(control.hitTest({50, 65}, g), 1);
	EXPECT_EQ(control.hitTest({65, 50}, g), 0);
	EXPECT_EQ(control.hitTest({40, 70}, g), 4);  // the upper-right corner rotated
	RecordingDrawKit kit;
	control.draw(kit, g);
	EXPECT_EQ(kit.count("polygon"), 1u);
	EXPECT_EQ(kit.count("loop"), 6u);
	OverlayControl noAngle(store);
	noAngle.addParts(boxParts(kOverlayShapeAll, 1, kA, kWidth, kHeight));
	EXPECT_EQ(noAngle.parts().size(), 5u);
	store.numbers.erase(kWidth);
	EXPECT_EQ(control.hitTest({50, 50}, g), 0);
	RecordingDrawKit empty;
	control.draw(empty, g);
	EXPECT_EQ(empty.count("polygon"), 0u);
	EXPECT_FALSE(control.part(2)->drag({0, 0}, {1, 1}, {}, g));
	store.points.erase(kA);
	EXPECT_FALSE(control.part(1)->drag({0, 0}, {1, 1}, {}, g));
}

TEST(Overlay, HudDrawsTextAtAnAnchorOrAParameter)
{
	Store store;
	store.points[kA] = {30, 40};
	OverlayControl control(store);
	auto hud = std::make_unique<HudPart>(1, "hello");
	HudPart* raw = hud.get();
	control.addPart(std::move(hud));
	const OverlayGeometry g = geometryAt(2.0);
	RecordingDrawKit kit;
	control.draw(kit, g);
	ASSERT_EQ(kit.count("text"), 1u);
	EXPECT_EQ(kit.records.back().text, "hello");
	EXPECT_EQ(kit.records.back().points[0], (Point2D{20, 20}));
	EXPECT_EQ(control.hitTest({20, 20}, g), 0);
	raw->anchor = kA;
	raw->canvasOffset = {5, -5};
	raw->textProvider = [](const OverlayGeometry& geometry) { return "t=" + std::to_string(static_cast<int>(geometry.time)); };
	RecordingDrawKit anchored;
	control.draw(anchored, g);
	EXPECT_EQ(anchored.records.back().text, "t=0");
	EXPECT_EQ(anchored.records.back().points[0], (Point2D{40, 30}));
	raw->textProvider = nullptr;
	raw->setText("");
	EXPECT_TRUE(raw->text().empty());
	RecordingDrawKit none;
	control.draw(none, g);
	EXPECT_EQ(none.count("text"), 0u);
	raw->setText("x");
	store.points.erase(kA);
	control.draw(none, g);
	EXPECT_EQ(none.count("text"), 0u);
	EXPECT_FALSE(control.keyDown(65, {}, g));
	EXPECT_EQ(RecordingDrawKit().standardColor(OverlayColorRole::Text), (OverlayColor{1, 1, 1, 1}));
	EXPECT_EQ(RecordingDrawKit().standardColor(OverlayColorRole::MarqueeBackground).a, 0.5f);
	RecordingDrawKit every;
	every.setStipple(OverlayStipple::Dash);
	every.ellipse({0, 0}, {1, 1});
	EXPECT_EQ(every.count("stipple"), 1u);
	EXPECT_EQ(every.count("ellipse"), 1u);
	EXPECT_EQ(every.records[0].stipple, OverlayStipple::Dash);
}

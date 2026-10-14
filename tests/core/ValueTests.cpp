// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Value.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Value, KindsFollowTheVariantOrder)
{
	EXPECT_EQ(Value().kind(), ValueKind::Empty);
	EXPECT_EQ(Value(true).kind(), ValueKind::Bool);
	EXPECT_EQ(Value(3).kind(), ValueKind::Int);
	EXPECT_EQ(Value(2.5).kind(), ValueKind::Double);
	EXPECT_EQ(Value(Point2D{1, 2}).kind(), ValueKind::Point2D);
	EXPECT_EQ(Value(Point3D{1, 2, 3}).kind(), ValueKind::Point3D);
	EXPECT_EQ(Value(RGB{1, 0, 0}).kind(), ValueKind::RGB);
	EXPECT_EQ(Value(RGBA{1, 0, 0, 1}).kind(), ValueKind::RGBA);
	EXPECT_EQ(Value("text").kind(), ValueKind::String);
	EXPECT_EQ(Value(Bytes{1, 2}).kind(), ValueKind::Bytes);
}

TEST(Value, NumericConversions)
{
	EXPECT_DOUBLE_EQ(Value(true).toDouble(), 1.0);
	EXPECT_DOUBLE_EQ(Value(7).toDouble(), 7.0);
	EXPECT_DOUBLE_EQ(Value(2.5).toDouble(), 2.5);
	EXPECT_DOUBLE_EQ(Value("x").toDouble(9.0), 9.0);
	EXPECT_EQ(Value(2.9).toInt(), 2);
	EXPECT_EQ(Value(true).toInt(), 1);
	EXPECT_EQ(Value(1e300).toInt(5), 5);
	EXPECT_TRUE(Value(1).toBool());
	EXPECT_FALSE(Value(0.0).toBool(true));
	EXPECT_TRUE(Value("true").toBool());
	EXPECT_FALSE(Value("no").toBool(true));
	EXPECT_TRUE(Value("maybe").toBool(true));
	EXPECT_TRUE(Value(Point2D{}).toBool(true));
}

TEST(Value, TypedAccess)
{
	Value v(Point2D{3, 4});
	EXPECT_TRUE(v.is<Point2D>());
	EXPECT_EQ(v.as<Point2D>().y, 4.0);
	EXPECT_EQ(v.tryAs<RGB>(), nullptr);
	EXPECT_NE(v.tryAs<Point2D>(), nullptr);
}

TEST(Value, ToStringRendersEveryKind)
{
	EXPECT_EQ(Value().toString(), "<empty>");
	EXPECT_EQ(Value(true).toString(), "true");
	EXPECT_EQ(Value(5).toString(), "5");
	EXPECT_EQ(Value("hi").toString(), "\"hi\"");
	EXPECT_EQ(Value(Bytes{1, 2, 3}).toString(), "<3 bytes>");
	EXPECT_NE(Value(RGBA{1, 0, 0, 1}).toString().find("rgba("), std::string::npos);
	EXPECT_NE(Value(Point3D{1, 2, 3}).toString().find("3)"), std::string::npos);
	EXPECT_EQ(toString(ValueKind::Point2D), "point2d");
}

TEST(Value, Equality)
{
	EXPECT_EQ(Value(1.5), Value(1.5));
	EXPECT_NE(Value(1), Value(1.0));
	EXPECT_EQ(Value("a"), Value(std::string("a")));
}

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The dictionary value: entries, the typed accessors and the lock, interpolation between
// keyframes, and the codec.
#include <ofxgrip/core/ValueDictionary.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(ValueDictionary, HoldsEntriesOfEveryShape)
{
	ValueDictionary d;
	EXPECT_TRUE(d.empty());
	d.set("gain", Value(0.5));
	d.set("list", ValueNode::Array{Value(1), Value("two")});
	d.set("nested", ValueNode::Map{{"on", Value(true)}});
	EXPECT_FALSE(d.empty());
	ASSERT_NE(d.find("gain"), nullptr);
	EXPECT_TRUE(d.find("gain")->isValue());
	EXPECT_TRUE(d.find("list")->isArray());
	EXPECT_TRUE(d.find("nested")->isMap());
	EXPECT_EQ(d.find("absent"), nullptr);
	ASSERT_NE(d.value("gain"), nullptr);
	EXPECT_EQ(*d.value("gain"), Value(0.5));
	// An array or a map is no value.
	EXPECT_EQ(d.value("list"), nullptr);
	EXPECT_EQ(d.find("list")->value(), nullptr);
	EXPECT_EQ(d.find("gain")->array(), nullptr);
	EXPECT_EQ(d.find("gain")->map(), nullptr);
	EXPECT_EQ(d.find("list")->array()->size(), 2u);
	EXPECT_EQ(d.find("nested")->map()->at("on"), ValueNode(Value(true)));
	d.set("gain", Value(2.0));
	EXPECT_EQ(*d.value("gain"), Value(2.0));
	EXPECT_TRUE(d.remove("gain"));
	EXPECT_FALSE(d.remove("gain"));
	EXPECT_EQ(d.entries().size(), 2u);
	EXPECT_TRUE(ValueNode().isValue());
}

TEST(ValueDictionary, TypedAccessOnTheWellKnownKeys)
{
	ValueDictionary d;
	EXPECT_FALSE(d.boolAt().has_value());
	EXPECT_FALSE(d.stringAt().has_value());
	EXPECT_TRUE(d.setBool(true));
	EXPECT_TRUE(d.setNumber(1.5));
	EXPECT_TRUE(d.setInt(7));
	EXPECT_TRUE(d.setRgba({0.1, 0.2, 0.3, 0.4}));
	EXPECT_TRUE(d.setRgb({0.5, 0.6, 0.7}));
	EXPECT_TRUE(d.setString("text"));
	EXPECT_TRUE(d.setPoint({3, 4}));
	EXPECT_EQ(d.boolAt(), true);
	EXPECT_EQ(d.numberAt(), 1.5);
	EXPECT_EQ(d.intAt(), 7);
	EXPECT_EQ(d.rgbaAt(), (RGBA{0.1, 0.2, 0.3, 0.4}));
	EXPECT_EQ(d.rgbAt(), (RGB{0.5, 0.6, 0.7}));
	EXPECT_EQ(d.stringAt(), "text");
	EXPECT_EQ(d.pointAt(), (Point2D{3, 4}));
	EXPECT_EQ(d.value(kDictionaryIntKey)->as<int>(), 7);
	// A number reads from an int. An int does not read from a double. Other types read nothing.
	EXPECT_EQ(d.numberAt(kDictionaryIntKey), 7.0);
	EXPECT_FALSE(d.intAt(kDictionaryFloatKey).has_value());
	EXPECT_FALSE(d.boolAt(kDictionaryIntKey).has_value());
	EXPECT_FALSE(d.numberAt(kDictionaryStringKey).has_value());
	EXPECT_FALSE(d.rgbaAt(kDictionaryRgbKey).has_value());
	EXPECT_FALSE(d.rgbAt(kDictionaryRgbaKey).has_value());
	EXPECT_FALSE(d.stringAt(kDictionaryBoolKey).has_value());
	EXPECT_FALSE(d.pointAt(kDictionaryRgbKey).has_value());
	d.set("custom", Value(9));
	EXPECT_EQ(d.intAt("custom"), 9);
}

TEST(ValueDictionary, ALockedDictionaryTakesNoNewDefaultKeys)
{
	ValueDictionary d;
	EXPECT_TRUE(d.setInt(1));
	d.setLocked(true);
	EXPECT_TRUE(d.locked());
	EXPECT_TRUE(d.setInt(2));
	EXPECT_EQ(d.intAt(), 2);
	EXPECT_FALSE(d.setBool(true));
	EXPECT_FALSE(d.setNumber(1.0));
	EXPECT_FALSE(d.setRgba({}));
	EXPECT_FALSE(d.setRgb({}));
	EXPECT_FALSE(d.setString("x"));
	EXPECT_FALSE(d.setPoint({}));
	EXPECT_EQ(d.entries().size(), 1u);
	// A write by key is not a default setter.
	d.set("other", Value(true));
	EXPECT_EQ(d.entries().size(), 2u);
}

TEST(ValueDictionary, BlendsBetweenKeyframes)
{
	ValueDictionary left;
	left.set("number", Value(1.0));
	left.set("count", Value(10));
	left.set("on", Value(true));
	left.set("name", Value("left"));
	left.set("point", Value(Point2D{0, 10}));
	left.set("point3", Value(Point3D{0, 0, 0}));
	left.set("rgb", Value(RGB{0, 0, 0}));
	left.set("rgba", Value(RGBA{0, 0, 0, 0}));
	left.set("bytes", Value(Bytes{1, 2}));
	left.set("empty", Value());
	left.set("_private", Value(5.0));
	left.set("leftOnly", Value(3.0));
	left.set("retyped", Value(3.0));
	left.set("list", ValueNode::Array{Value(0.0), Value(10.0), Value(20.0)});
	left.set("nested", ValueNode::Map{{"deep", Value(100.0)}, {"_note", Value("x")}, {"gone", Value(1.0)}});
	left.set("shape", ValueNode::Array{Value(1.0)});
	left.set("tree", ValueNode::Map{{"a", Value(1.0)}});
	ValueDictionary right;
	right.set("number", Value(3.0));
	right.set("count", Value(13));
	right.set("on", Value(false));
	right.set("name", Value("right"));
	right.set("point", Value(Point2D{8, 2}));
	right.set("point3", Value(Point3D{4, 8, 12}));
	right.set("rgb", Value(RGB{1, 0.5, 0.25}));
	right.set("rgba", Value(RGBA{1, 1, 1, 1}));
	right.set("bytes", Value(Bytes{9}));
	right.set("empty", Value());
	right.set("_private", Value(9.0));
	right.set("retyped", Value("text"));
	right.set("rightOnly", Value(1.0));
	right.set("list", ValueNode::Array{Value(4.0), Value("wrong"), Value(40.0), Value(99.0)});
	right.set("nested", ValueNode::Map{{"deep", Value(200.0)}, {"_note", Value("y")}});
	right.set("shape", ValueNode::Map{});
	right.set("tree", Value(1.0));

	const ValueDictionary half = left.interpolated(right, 0.5);
	EXPECT_EQ(half.numberAt("number"), 2.0);
	// An int rounds its blend: 10 + round(1.5) = 12.
	EXPECT_EQ(half.intAt("count"), 12);
	EXPECT_EQ(half.boolAt("on"), true);
	EXPECT_EQ(half.stringAt("name"), "left");
	EXPECT_EQ(half.pointAt("point"), (Point2D{4, 6}));
	EXPECT_EQ(*half.value("point3"), Value(Point3D{2, 4, 6}));
	EXPECT_EQ(half.rgbAt("rgb"), (RGB{0.5, 0.25, 0.125}));
	EXPECT_EQ(half.rgbaAt("rgba"), (RGBA{0.5, 0.5, 0.5, 0.5}));
	EXPECT_EQ(*half.value("bytes"), Value(Bytes{1, 2}));
	EXPECT_TRUE(half.value("empty")->empty());
	// A private key, a key on one side, and a key of two types drop out.
	EXPECT_EQ(half.find("_private"), nullptr);
	EXPECT_EQ(half.find("leftOnly"), nullptr);
	EXPECT_EQ(half.find("rightOnly"), nullptr);
	EXPECT_EQ(half.find("retyped"), nullptr);
	EXPECT_EQ(half.find("shape"), nullptr);
	EXPECT_EQ(half.find("tree"), nullptr);
	// An array blends over the left's length, and a mismatched element drops out.
	EXPECT_EQ(*half.find("list"), ValueNode(ValueNode::Array{Value(2.0), Value(30.0)}));
	EXPECT_EQ(*half.find("nested"), ValueNode(ValueNode::Map{{"deep", Value(150.0)}}));
	EXPECT_EQ(left.interpolated(right, 0.0).numberAt("number"), 1.0);
	EXPECT_EQ(left.interpolated(right, 1.0).numberAt("number"), 3.0);
	// A shorter right array ends the blend at its length.
	ValueDictionary shortLeft;
	shortLeft.set("list", ValueNode::Array{Value(0.0), Value(10.0)});
	ValueDictionary shortRight;
	shortRight.set("list", ValueNode::Array{Value(2.0)});
	EXPECT_EQ(*shortLeft.interpolated(shortRight, 0.5).find("list"), ValueNode(ValueNode::Array{Value(1.0)}));
}

TEST(ValueDictionary, ExemptPathsAndTheCustomHook)
{
	ValueDictionary left;
	left.set("number", Value(1.0));
	left.set("list", ValueNode::Array{Value(0.0), Value(10.0)});
	left.set("nested", ValueNode::Map{{"deep", Value(100.0)}, {"other", Value(0.0)}});
	left.set("blob", Value(Bytes{10}));
	left.setExemptPaths({"number", "list/1", "nested/deep"});
	left.setLocked(true);
	ValueDictionary right;
	right.set("number", Value(3.0));
	right.set("list", ValueNode::Array{Value(4.0), Value(30.0)});
	right.set("nested", ValueNode::Map{{"deep", Value(200.0)}, {"other", Value(8.0)}});
	right.set("blob", Value(Bytes{20}));
	const ValueDictionary half = left.interpolated(right, 0.5);
	// An exempt path copies from the left, at any depth.
	EXPECT_EQ(half.numberAt("number"), 1.0);
	EXPECT_EQ(*half.find("list"), ValueNode(ValueNode::Array{Value(2.0), Value(10.0)}));
	EXPECT_EQ(*half.find("nested"), ValueNode(ValueNode::Map{{"deep", Value(100.0)}, {"other", Value(4.0)}}));
	EXPECT_EQ(*half.value("blob"), Value(Bytes{10}));
	// The result keeps the lock and the paths.
	EXPECT_TRUE(half.locked());
	EXPECT_EQ(half.exemptPaths(), left.exemptPaths());

	// The hook blends what the dictionary cannot, and sees the entry's path.
	std::vector<std::string> seen;
	const ValueDictionary::CustomInterpolator average = [&seen](const ValueNode& a, const ValueNode& b, std::string_view path, double weight) -> std::optional<ValueNode> {
		seen.emplace_back(path);
		const Bytes& x = a.value()->as<Bytes>();
		const Bytes& y = b.value()->as<Bytes>();
		return ValueNode(Value(Bytes{static_cast<std::uint8_t>(static_cast<double>(x[0]) * (1.0 - weight) + static_cast<double>(y[0]) * weight)}));
	};
	const ValueDictionary hooked = left.interpolated(right, 0.5, average);
	EXPECT_EQ(*hooked.value("blob"), Value(Bytes{15}));
	EXPECT_EQ(seen, (std::vector<std::string>{"blob"}));
	// A hook that declines keeps the left entry.
	const ValueDictionary::CustomInterpolator decline = [](const ValueNode&, const ValueNode&, std::string_view, double) -> std::optional<ValueNode> { return std::nullopt; };
	EXPECT_EQ(*left.interpolated(right, 0.5, decline).value("blob"), Value(Bytes{10}));
}

TEST(ValueDictionary, Codec)
{
	ValueDictionary d;
	d.setInt(7);
	d.setString("text");
	d.setRgba({0.1, 0.2, 0.3, 0.4});
	d.set("bytes", Value(Bytes{1, 2, 3}));
	d.set("list", ValueNode::Array{Value(1), ValueNode::Array{Value(2.5)}, ValueNode::Map{{"k", Value(true)}}});
	d.set("nested", ValueNode::Map{{"point", Value(Point2D{1, 2})}, {"empty", ValueNode::Array{}}});
	d.setExemptPaths({"list/0", "nested/point"});
	d.setLocked(true);
	const Bytes bytes = d.encode();
	ValueDictionary decoded;
	Error e = ValueDictionary::decode(bytes, decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_EQ(decoded, d);
	e = ValueDictionary::decode(ValueDictionary().encode(), decoded);
	ASSERT_TRUE(e.ok()) << e.describe();
	EXPECT_TRUE(decoded.empty());
	EXPECT_FALSE(decoded.locked());
	decoded = d;
	for (std::size_t length = 0; length < bytes.size(); ++length) {
		EXPECT_EQ(ValueDictionary::decode(std::span<const std::uint8_t>(bytes.data(), length), decoded).code(), ErrorCode::Codec) << "length " << length;
		EXPECT_TRUE(decoded.empty());
	}
	Bytes trailing = bytes;
	trailing.push_back(0);
	EXPECT_EQ(ValueDictionary::decode(trailing, decoded).code(), ErrorCode::Codec);
	Bytes future = bytes;
	future[4] = 2;
	EXPECT_EQ(ValueDictionary::decode(future, decoded).code(), ErrorCode::Codec);
	Bytes badLock = bytes;
	badLock[8] = 2;
	EXPECT_EQ(ValueDictionary::decode(badLock, decoded).code(), ErrorCode::Codec);
	// The root must be a map, and a node tag must be known.
	Bytes rootValue = ValueDictionary().encode();
	rootValue[13] = 0;
	EXPECT_EQ(ValueDictionary::decode(rootValue, decoded).code(), ErrorCode::Codec);
	Bytes badTag = ValueDictionary().encode();
	badTag[13] = 7;
	EXPECT_EQ(ValueDictionary::decode(badTag, decoded).code(), ErrorCode::Codec);
	// Nesting past the limit is refused.
	ValueNode deep = ValueNode(Value(1));
	for (int i = 0; i < 70; ++i) {
		deep = ValueNode(ValueNode::Array{deep});
	}
	ValueDictionary tower;
	tower.set("deep", deep);
	EXPECT_EQ(ValueDictionary::decode(tower.encode(), decoded).code(), ErrorCode::Codec);
}

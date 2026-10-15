// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Plist.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

const char* kSample = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<!-- a comment -->
	<key>name</key>
	<string>Circle &amp; &lt;Square&gt; &#169; &#x41;</string>
	<key>count</key>
	<integer>-12</integer>
	<key>ratio</key>
	<real>0.5</real>
	<key>on</key>
	<true/>
	<key>off</key>
	<false></false>
	<key>bytes</key>
	<data>
	AQID
	</data>
	<key>when</key>
	<date>2026-10-14T12:00:00Z</date>
	<key>list</key>
	<array>
		<integer>1</integer>
		<string>two</string>
		<array/>
		<dict/>
	</array>
	<key>nested</key>
	<dict>
		<key>x</key>
		<real>1</real>
		<key>y</key>
		<real>2</real>
	</dict>
</dict>
</plist>)";

}  // namespace

TEST(Plist, ParsesEveryNodeKind)
{
	PlistValue root;
	Error e = parsePlist(kSample, root);
	ASSERT_TRUE(e.ok()) << e.describe();
	ASSERT_EQ(root.kind(), PlistKind::Dict);
	EXPECT_EQ(root.get("name")->as<std::string>(), "Circle & <Square> \xC2\xA9 A");
	EXPECT_EQ(root.get("count")->as<std::int64_t>(), -12);
	EXPECT_EQ(root.get("ratio")->as<double>(), 0.5);
	EXPECT_TRUE(root.get("on")->as<bool>());
	EXPECT_FALSE(root.get("off")->as<bool>());
	EXPECT_EQ(root.get("bytes")->as<Bytes>(), (Bytes{1, 2, 3}));
	EXPECT_EQ(root.get("when")->as<PlistDate>().iso8601, "2026-10-14T12:00:00Z");
	const PlistArray& list = root.get("list")->as<PlistArray>();
	ASSERT_EQ(list.size(), 4u);
	EXPECT_EQ(list[1].as<std::string>(), "two");
	EXPECT_TRUE(list[2].as<PlistArray>().empty());
	EXPECT_TRUE(list[3].as<PlistDict>().empty());
	EXPECT_EQ(root.get("nested")->get("y")->toDouble(), 2.0);
	EXPECT_EQ(root.get("missing"), nullptr);
	EXPECT_EQ(root.get("count")->get("x"), nullptr);
	EXPECT_EQ(root.get("count")->toDouble(), -12.0);
	EXPECT_EQ(root.get("ratio")->toInteger(), 1);
	EXPECT_EQ(root.get("on")->toInteger(), 1);
	EXPECT_EQ(root.get("on")->toDouble(), 1.0);
	EXPECT_EQ(root.get("name")->toInteger(7), 7);
	EXPECT_EQ(root.get("name")->toDouble(7), 7.0);
	EXPECT_EQ(root.get("count")->toString("x"), "x");
	PlistValue* mutableNested = root.get("nested");
	ASSERT_NE(mutableNested, nullptr);
	mutableNested->set("x", PlistValue(9.0));
	mutableNested->set("z", PlistValue(3.0));
	EXPECT_EQ(mutableNested->get("x")->toDouble(), 9.0);
	EXPECT_EQ(mutableNested->as<PlistDict>().size(), 3u);
	PlistValue fresh;
	fresh.set("a", PlistValue(1));
	EXPECT_EQ(fresh.kind(), PlistKind::Dict);
	PlistValue number(5);
	number.set("a", PlistValue(1));
	EXPECT_EQ(number.kind(), PlistKind::Integer);
	EXPECT_EQ(number.get("a"), nullptr);
	const PlistValue& constNumber = number;
	EXPECT_EQ(constNumber.get("a"), nullptr);
}

TEST(Plist, WritesAndReadsBack)
{
	PlistValue root;
	Error e = parsePlist(kSample, root);
	ASSERT_TRUE(e.ok()) << e.describe();
	const std::string text = writePlist(root);
	EXPECT_NE(text.find("<!DOCTYPE plist"), std::string::npos);
	EXPECT_NE(text.find("&amp; &lt;Square&gt;"), std::string::npos);
	PlistValue back;
	ASSERT_TRUE(parsePlist(text, back).ok());
	EXPECT_EQ(back, root);
	EXPECT_EQ(writePlist(back), text);
	EXPECT_NE(writePlist(PlistValue()).find("<dict/>"), std::string::npos);
	EXPECT_NE(writePlist(PlistValue(1.0)).find("<real>1.0</real>"), std::string::npos);
	EXPECT_NE(writePlist(PlistValue(0.25)).find("<real>0.25</real>"), std::string::npos);
	EXPECT_NE(writePlist(PlistValue(PlistArray{})).find("<array/>"), std::string::npos);
	PlistValue empty;
	ASSERT_TRUE(parsePlist("<plist version=\"1.0\"></plist>", empty).ok());
	EXPECT_TRUE(empty.empty());
}

TEST(Plist, RejectsMalformedInput)
{
	for (const char* bad : {"", "<dict></dict>", "<plist><dict><key>a</key></dict></plist>", "<plist><dict><integer>1</integer></dict></plist>", "<plist><integer>x</integer></plist>", "<plist><real>1.2.3</real></plist>",
	                        "<plist><data>!!</data></plist>", "<plist><string>unterminated</plist>", "<plist><thing>1</thing></plist>", "<plist><array><integer>1</integer></plist>", "<plist><dict><key>a</key><string>b</string></plist>",
	                        "<plist><dict><key>a</key>", "<plist><true>x</true></plist>", "<plist"}) {
		PlistValue out;
		Error e = parsePlist(bad, out);
		EXPECT_EQ(e.code(), ErrorCode::Codec) << bad;
	}
}

TEST(Plist, ValueEncodingsRoundTrip)
{
	const std::vector<Value> values{Value(true), Value(-3), Value(2.25), Value(Point2D{1, 2}), Value(Point3D{1, 2, 3}), Value(RGB{0.1, 0.2, 0.3}), Value(RGBA{0.1, 0.2, 0.3, 0.4}), Value("text"), Value(Bytes{1, 2, 3}), Value()};
	for (const Value& v : values) {
		const PlistValue plist = plistFromValue(v);
		Value back;
		ASSERT_TRUE(valueFromPlist(plist, back)) << v.toString();
		if (v.kind() == ValueKind::Bool) {
			// Toggles are written as integers, as FxFactory does.
			EXPECT_EQ(back, Value(1));
		} else {
			EXPECT_EQ(back, v) << v.toString();
		}
	}
	Value out;
	EXPECT_TRUE(valueFromPlist(PlistValue(true), out));
	EXPECT_EQ(out, Value(true));
	EXPECT_TRUE(valueFromPlist(PlistValue(PlistDate{"2026"}), out));
	EXPECT_EQ(out, Value("2026"));
	EXPECT_FALSE(valueFromPlist(PlistValue(PlistArray{PlistValue(1)}), out));
	EXPECT_FALSE(valueFromPlist(PlistValue(PlistDict{{"foo", PlistValue(1)}}), out));
	// FxFactory colors carry a colorspace entry, which is ignored.
	PlistValue color(PlistDict{{"red", PlistValue(1.0)}, {"green", PlistValue(0.5)}, {"blue", PlistValue(0.0)}, {"colorspace", PlistValue(1)}});
	EXPECT_TRUE(valueFromPlist(color, out));
	EXPECT_EQ(out, Value(RGB{1.0, 0.5, 0.0}));
	EXPECT_EQ(plistFromValue(Value()).kind(), PlistKind::Dict);
}

TEST(Plist, Base64)
{
	EXPECT_EQ(base64Encode(Bytes{}), "");
	EXPECT_EQ(base64Encode(Bytes{'M'}), "TQ==");
	EXPECT_EQ(base64Encode(Bytes{'M', 'a'}), "TWE=");
	EXPECT_EQ(base64Encode(Bytes{'M', 'a', 'n'}), "TWFu");
	Bytes out;
	EXPECT_TRUE(base64Decode("TWFu", out));
	EXPECT_EQ(out, (Bytes{'M', 'a', 'n'}));
	EXPECT_TRUE(base64Decode("TQ==", out));
	EXPECT_EQ(out, Bytes{'M'});
	EXPECT_TRUE(base64Decode(" TW\nE=", out));
	EXPECT_EQ(out, (Bytes{'M', 'a'}));
	EXPECT_FALSE(base64Decode("T$", out));
}

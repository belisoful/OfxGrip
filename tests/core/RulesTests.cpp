// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Rules.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

MapRuleContext context()
{
	MapRuleContext c;
	c.values["mode"] = Value(1);
	c.options["mode"] = {"soft", "hard", "off"};
	c.values["amount"] = Value(0.5);
	c.values["count"] = Value(3);
	c.values["bypass"] = Value(false);
	c.values["on"] = Value(true);
	c.values["name"] = Value("alpha");
	c.values["empty"] = Value("");
	c.values["point"] = Value(Point2D{1, 2});
	c.values["blob"] = Value(Bytes{1});
	c.values["nothing"] = Value();
	return c;
}

bool eval(const char* text)
{
	Rule rule;
	Error e = Rule::parse(text, rule);
	EXPECT_TRUE(e.ok()) << text << ": " << e.describe();
	bool out = false;
	Error ev = rule.evaluate(context(), out);
	EXPECT_TRUE(ev.ok()) << text << ": " << ev.describe();
	return out;
}

Error evalError(const char* text)
{
	Rule rule;
	Error e = Rule::parse(text, rule);
	EXPECT_TRUE(e.ok()) << text << ": " << e.describe();
	bool out = true;
	Error ev = rule.evaluate(context(), out);
	EXPECT_FALSE(out);
	return ev;
}

}  // namespace

TEST(Rules, EmptyRuleIsTrueAndHasNoReferences)
{
	Rule rule;
	EXPECT_TRUE(rule.empty());
	bool out = false;
	EXPECT_TRUE(rule.evaluate(context(), out).ok());
	EXPECT_TRUE(out);
	ASSERT_TRUE(Rule::parse("   ", rule).ok());
	EXPECT_TRUE(rule.empty());
	EXPECT_TRUE(rule.references().empty());
}

TEST(Rules, ComparisonsAndOptionKeys)
{
	EXPECT_TRUE(eval("mode == 'hard'"));
	EXPECT_TRUE(eval("'hard' == mode"));
	EXPECT_FALSE(eval("mode == 'soft'"));
	EXPECT_TRUE(eval("mode != 'soft'"));
	EXPECT_TRUE(eval("mode == 1"));
	EXPECT_TRUE(eval("mode = 1"));
	EXPECT_TRUE(eval("mode in ('soft', 'hard')"));
	EXPECT_FALSE(eval("mode in ('soft', 'off')"));
	EXPECT_TRUE(eval("count in (1, 2, 3)"));
	EXPECT_TRUE(eval("amount > 0.25 && amount <= 0.5"));
	EXPECT_TRUE(eval("amount >= 0.5"));
	EXPECT_FALSE(eval("amount < 0.5"));
	EXPECT_TRUE(eval("count > -1"));
	EXPECT_TRUE(eval("name == \"alpha\""));
	EXPECT_TRUE(eval("name < 'beta'"));
	EXPECT_TRUE(eval("name > 'aardvark'"));
	EXPECT_TRUE(eval("name != 'beta'"));
	EXPECT_TRUE(eval("on == true"));
	EXPECT_TRUE(eval("bypass == false"));
	EXPECT_TRUE(eval("point == point"));
	EXPECT_FALSE(eval("point != point"));
	EXPECT_TRUE(eval("name == 'al\\'pha' || true"));
}

TEST(Rules, LogicAndTruthiness)
{
	EXPECT_TRUE(eval("on"));
	EXPECT_FALSE(eval("bypass"));
	EXPECT_TRUE(eval("!bypass"));
	EXPECT_TRUE(eval("not bypass"));
	EXPECT_TRUE(eval("on and !bypass"));
	EXPECT_TRUE(eval("bypass or on"));
	EXPECT_FALSE(eval("bypass || (on && mode == 'soft')"));
	EXPECT_TRUE(eval("(bypass || on) && mode == 'hard'"));
	EXPECT_TRUE(eval("count"));
	EXPECT_TRUE(eval("amount"));
	EXPECT_TRUE(eval("name"));
	EXPECT_FALSE(eval("empty"));
	EXPECT_TRUE(eval("blob"));
	EXPECT_FALSE(eval("nothing"));
	EXPECT_TRUE(eval("true"));
	EXPECT_FALSE(eval("false"));
	EXPECT_FALSE(eval("!!bypass"));
	// Short circuits: the right side is not evaluated, so an unknown key there is fine.
	EXPECT_FALSE(eval("bypass && unknown"));
	EXPECT_TRUE(eval("on || unknown"));
}

TEST(Rules, ReferencesAreCollectedInOrder)
{
	Rule rule;
	ASSERT_TRUE(Rule::parse("mode == 'soft' && (amount > 0 || mode == 'hard') && !bypass", rule).ok());
	EXPECT_EQ(rule.references(), (std::vector<std::string>{"mode", "amount", "bypass"}));
	EXPECT_EQ(rule.text(), "mode == 'soft' && (amount > 0 || mode == 'hard') && !bypass");
	Rule copy = rule;
	bool out = false;
	EXPECT_TRUE(copy.evaluate(context(), out).ok());
	EXPECT_FALSE(out);
}

TEST(Rules, SyntaxErrorsCarryThePosition)
{
	for (const char* bad : {"mode ==", "== 1", "(mode == 1", "mode in 1", "mode in (", "mode in (1,", "mode == 'soft", "amount > 1.2.3", "a # b", "mode == 1 )", "a && ", "in", "&&", "()"}) {
		Rule rule;
		Error e = Rule::parse(bad, rule);
		EXPECT_EQ(e.code(), ErrorCode::InvalidManifest) << bad;
		bool hasPosition = false;
		for (const auto& [key, value] : e.context()) {
			hasPosition = hasPosition || key == "position";
		}
		EXPECT_TRUE(hasPosition) << bad;
	}
}

TEST(Rules, EvaluationErrors)
{
	EXPECT_EQ(evalError("unknown == 1").code(), ErrorCode::NotFound);
	EXPECT_EQ(evalError("unknown").code(), ErrorCode::NotFound);
	EXPECT_EQ(evalError("name == 1").code(), ErrorCode::Value);
	EXPECT_EQ(evalError("mode == 'nonesuch'").code(), ErrorCode::Value);
	EXPECT_EQ(evalError("point < point").code(), ErrorCode::Value);
	EXPECT_EQ(evalError("point").code(), ErrorCode::Value);
	EXPECT_EQ(evalError("name in (1, 2)").code(), ErrorCode::Value);
	EXPECT_EQ(evalError("point in (point) && point > 1").code(), ErrorCode::Value);
}

TEST(Rules, MapContextAnswersOptionsAndValues)
{
	MapRuleContext c = context();
	EXPECT_EQ(c.optionIndex("mode", "off"), 2);
	EXPECT_FALSE(c.optionIndex("mode", "zzz").has_value());
	EXPECT_FALSE(c.optionIndex("amount", "x").has_value());
	EXPECT_FALSE(c.value("missing").has_value());
	RuleContext& base = c;
	EXPECT_TRUE(base.value("mode").has_value());
	class Plain final : public RuleContext {
	public:
		std::optional<Value> value(std::string_view) const override { return Value(1); }
	};
	Plain plain;
	EXPECT_FALSE(plain.optionIndex("a", "b").has_value());
	Rule rule;
	ASSERT_TRUE(Rule::parse("x == 'soft'", rule).ok());
	bool out = true;
	EXPECT_EQ(rule.evaluate(plain, out).code(), ErrorCode::Value);
}

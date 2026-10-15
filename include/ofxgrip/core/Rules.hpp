// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Rules.hpp
	@brief The declarative rule language behind `visibleWhen` and `enabledWhen`: a parsed
	       boolean expression over parameter values.

	Introduced in OfxGrip 0.2.0. A rule is a small expression: parameter keys, literals,
	comparisons, `in`, `&&`, `||`, and `!`, with parentheses. `mode == 'soft'` compares a choice
	parameter with an option key; `amount > 0.5 && !bypass` combines a numeric comparison with a
	bare toggle. A rule parses once at validation and evaluates against a `RuleContext` that
	answers parameter values by key. This is the expression form `Research/Comparison.md` §5.4
	proposes in place of FxGrip's menu-driven target presets, which OfxGrip keeps as well.

	Grammar:
	  rule       := or
	  or         := and ( ('||' | 'or') and )*
	  and        := unary ( ('&&' | 'and') unary )*
	  unary      := ('!' | 'not') unary | primary
	  primary    := '(' rule ')' | operand ( compare operand | 'in' '(' operand (',' operand)* ')' )?
	  compare    := '==' | '!=' | '<' | '<=' | '>' | '>='
	  operand    := key | number | 'string' | "string" | true | false
	  key        := letter ( letter | digit | '_' | '.' )*
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! What a rule evaluates against. */
class RuleContext {
public:
	virtual ~RuleContext() = default;
	/*! The current value of a parameter key, or none when the key is unknown. */
	virtual std::optional<Value> value(std::string_view key) const = 0;
	/*! The option index of a choice parameter's option key, or none when the parameter is not a
	    choice or the key is unknown. A rule comparing a choice with a string literal uses it. */
	virtual std::optional<int> optionIndex(std::string_view key, std::string_view option) const
	{
		(void)key;
		(void)option;
		return std::nullopt;
	}
};

/*! A rule context over a fixed map of values, for tests and for manifest-level checks. */
class MapRuleContext final : public RuleContext {
public:
	std::map<std::string, Value> values;
	std::map<std::string, std::vector<std::string>> options;  /*!< key → option keys in order */
	std::optional<Value> value(std::string_view key) const override;
	std::optional<int> optionIndex(std::string_view key, std::string_view option) const override;
};

struct RuleNode;

/*! A parsed rule. An empty rule evaluates to true. Copies share the parsed tree. */
class Rule {
public:
	Rule() = default;
	/*! Parses `text`. An empty or blank text parses to an empty rule. A syntax error carries the
	    character position in its developer message. */
	static Error parse(std::string_view text, Rule& out);
	bool empty() const noexcept { return root_ == nullptr; }
	const std::string& text() const noexcept { return text_; }
	/*! The parameter keys the rule reads, in order of first appearance. */
	const std::vector<std::string>& references() const noexcept { return references_; }
	/*! Evaluates the rule. An unknown key or an incomparable pair of values is an error, and
	    `out` is left false. */
	Error evaluate(const RuleContext& context, bool& out) const;

private:
	std::string text_;
	std::shared_ptr<const RuleNode> root_;
	std::vector<std::string> references_;
};

OFXGRIP_NAMESPACE_END

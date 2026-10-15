// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Rules.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

OFXGRIP_NAMESPACE_BEGIN

// ---- the tree -----------------------------------------------------------------------------------

enum class RuleOp : std::uint8_t { Literal, Reference, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, In, And, Or, Not };

struct RuleNode {
	RuleOp op{RuleOp::Literal};
	Value literal;
	std::string key;
	std::vector<std::shared_ptr<const RuleNode>> children;
};

namespace {

// ---- tokens -------------------------------------------------------------------------------------

enum class TokenKind : std::uint8_t { End, Key, Number, String, True, False, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, And, Or, Not, In, LParen, RParen, Comma };

struct Token {
	TokenKind kind{TokenKind::End};
	std::string text;
	double number{0};
	std::size_t position{0};
};

bool isKeyStart(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isKeyChar(char c)
{
	return isKeyStart(c) || (c >= '0' && c <= '9') || c == '.';
}

Error syntaxError(std::string_view what, std::size_t position)
{
	return Error::make(ErrorCode::InvalidManifest, "The rule has a syntax error", std::string(what) + " at " + std::to_string(position)).with("position", std::to_string(position));
}

Error tokenize(std::string_view text, std::vector<Token>& out)
{
	std::size_t i = 0;
	while (i < text.size()) {
		const char c = text[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			++i;
			continue;
		}
		Token token;
		token.position = i;
		if (isKeyStart(c)) {
			std::size_t j = i;
			while (j < text.size() && isKeyChar(text[j])) {
				++j;
			}
			token.text = std::string(text.substr(i, j - i));
			if (token.text == "true") {
				token.kind = TokenKind::True;
			} else if (token.text == "false") {
				token.kind = TokenKind::False;
			} else if (token.text == "and") {
				token.kind = TokenKind::And;
			} else if (token.text == "or") {
				token.kind = TokenKind::Or;
			} else if (token.text == "not") {
				token.kind = TokenKind::Not;
			} else if (token.text == "in") {
				token.kind = TokenKind::In;
			} else {
				token.kind = TokenKind::Key;
			}
			i = j;
		} else if ((c >= '0' && c <= '9') || (c == '-' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9')) {
			std::size_t j = i + 1;
			while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || text[j] == '.')) {
				++j;
			}
			token.text = std::string(text.substr(i, j - i));
			char* end = nullptr;
			token.number = std::strtod(token.text.c_str(), &end);
			if (end == nullptr || *end != '\0') {
				return syntaxError("bad number", i);
			}
			token.kind = TokenKind::Number;
			i = j;
		} else if (c == '\'' || c == '"') {
			std::size_t j = i + 1;
			std::string value;
			while (j < text.size() && text[j] != c) {
				if (text[j] == '\\' && j + 1 < text.size()) {
					++j;
				}
				value.push_back(text[j]);
				++j;
			}
			if (j >= text.size()) {
				return syntaxError("unterminated string", i);
			}
			token.kind = TokenKind::String;
			token.text = std::move(value);
			i = j + 1;
		} else {
			const std::string_view rest = text.substr(i);
			auto starts = [&rest](std::string_view p) { return rest.substr(0, p.size()) == p; };
			if (starts("==")) {
				token.kind = TokenKind::Equal;
				i += 2;
			} else if (starts("!=")) {
				token.kind = TokenKind::NotEqual;
				i += 2;
			} else if (starts("<=")) {
				token.kind = TokenKind::LessEqual;
				i += 2;
			} else if (starts(">=")) {
				token.kind = TokenKind::GreaterEqual;
				i += 2;
			} else if (starts("&&")) {
				token.kind = TokenKind::And;
				i += 2;
			} else if (starts("||")) {
				token.kind = TokenKind::Or;
				i += 2;
			} else if (c == '<') {
				token.kind = TokenKind::Less;
				++i;
			} else if (c == '>') {
				token.kind = TokenKind::Greater;
				++i;
			} else if (c == '!') {
				token.kind = TokenKind::Not;
				++i;
			} else if (c == '(') {
				token.kind = TokenKind::LParen;
				++i;
			} else if (c == ')') {
				token.kind = TokenKind::RParen;
				++i;
			} else if (c == ',') {
				token.kind = TokenKind::Comma;
				++i;
			} else if (c == '=') {
				// A single '=' is accepted as equality so JSON authors are not tripped by it.
				token.kind = TokenKind::Equal;
				++i;
			} else {
				return syntaxError(std::string("unexpected character '") + c + "'", i);
			}
		}
		out.push_back(std::move(token));
	}
	Token end;
	end.kind = TokenKind::End;
	end.position = text.size();
	out.push_back(std::move(end));
	return Error::none();
}

// ---- parser -------------------------------------------------------------------------------------

using NodePtr = std::shared_ptr<const RuleNode>;

class Parser {
public:
	Parser(std::vector<Token> tokens, std::vector<std::string>& references) : tokens_(std::move(tokens)), references_(references) {}

	Error parseRule(NodePtr& out)
	{
		if (Error e = parseOr(out)) {
			return e;
		}
		if (peek().kind != TokenKind::End) {
			return syntaxError("unexpected token", peek().position);
		}
		return Error::none();
	}

private:
	const Token& peek() const { return tokens_[pos_]; }
	Token take() { return tokens_[pos_++]; }
	bool accept(TokenKind kind)
	{
		if (peek().kind == kind) {
			++pos_;
			return true;
		}
		return false;
	}

	static NodePtr make(RuleOp op, std::vector<NodePtr> children = {})
	{
		auto node = std::make_shared<RuleNode>();
		node->op = op;
		node->children = std::move(children);
		return node;
	}

	Error parseOr(NodePtr& out)
	{
		if (Error e = parseAnd(out)) {
			return e;
		}
		while (accept(TokenKind::Or)) {
			NodePtr rhs;
			if (Error e = parseAnd(rhs)) {
				return e;
			}
			out = make(RuleOp::Or, {out, rhs});
		}
		return Error::none();
	}

	Error parseAnd(NodePtr& out)
	{
		if (Error e = parseUnary(out)) {
			return e;
		}
		while (accept(TokenKind::And)) {
			NodePtr rhs;
			if (Error e = parseUnary(rhs)) {
				return e;
			}
			out = make(RuleOp::And, {out, rhs});
		}
		return Error::none();
	}

	Error parseUnary(NodePtr& out)
	{
		if (accept(TokenKind::Not)) {
			NodePtr inner;
			if (Error e = parseUnary(inner)) {
				return e;
			}
			out = make(RuleOp::Not, {inner});
			return Error::none();
		}
		return parsePrimary(out);
	}

	Error parseOperand(NodePtr& out)
	{
		const Token token = take();
		auto node = std::make_shared<RuleNode>();
		switch (token.kind) {
		case TokenKind::Key:
			node->op = RuleOp::Reference;
			node->key = token.text;
			if (std::find(references_.begin(), references_.end(), token.text) == references_.end()) {
				references_.push_back(token.text);
			}
			break;
		case TokenKind::Number:
			node->op = RuleOp::Literal;
			node->literal = Value(token.number);
			break;
		case TokenKind::String:
			node->op = RuleOp::Literal;
			node->literal = Value(token.text);
			break;
		case TokenKind::True:
			node->op = RuleOp::Literal;
			node->literal = Value(true);
			break;
		case TokenKind::False:
			node->op = RuleOp::Literal;
			node->literal = Value(false);
			break;
		default:
			return syntaxError("expected a parameter key or a literal", token.position);
		}
		out = node;
		return Error::none();
	}

	Error parsePrimary(NodePtr& out)
	{
		if (accept(TokenKind::LParen)) {
			if (Error e = parseOr(out)) {
				return e;
			}
			if (!accept(TokenKind::RParen)) {
				return syntaxError("expected ')'", peek().position);
			}
			return Error::none();
		}
		NodePtr lhs;
		if (Error e = parseOperand(lhs)) {
			return e;
		}
		RuleOp op = RuleOp::Literal;
		switch (peek().kind) {
		case TokenKind::Equal: op = RuleOp::Equal; break;
		case TokenKind::NotEqual: op = RuleOp::NotEqual; break;
		case TokenKind::Less: op = RuleOp::Less; break;
		case TokenKind::LessEqual: op = RuleOp::LessEqual; break;
		case TokenKind::Greater: op = RuleOp::Greater; break;
		case TokenKind::GreaterEqual: op = RuleOp::GreaterEqual; break;
		case TokenKind::In: op = RuleOp::In; break;
		default:
			out = lhs;
			return Error::none();
		}
		take();
		if (op == RuleOp::In) {
			if (!accept(TokenKind::LParen)) {
				return syntaxError("expected '(' after 'in'", peek().position);
			}
			std::vector<NodePtr> children{lhs};
			do {
				NodePtr item;
				if (Error e = parseOperand(item)) {
					return e;
				}
				children.push_back(item);
			} while (accept(TokenKind::Comma));
			if (!accept(TokenKind::RParen)) {
				return syntaxError("expected ')'", peek().position);
			}
			out = make(RuleOp::In, std::move(children));
			return Error::none();
		}
		NodePtr rhs;
		if (Error e = parseOperand(rhs)) {
			return e;
		}
		out = make(op, {lhs, rhs});
		return Error::none();
	}

	std::vector<Token> tokens_;
	std::size_t pos_{0};
	std::vector<std::string>& references_;
};

// ---- evaluation ---------------------------------------------------------------------------------

bool isNumeric(const Value& v)
{
	return v.kind() == ValueKind::Bool || v.kind() == ValueKind::Int || v.kind() == ValueKind::Double;
}

Error unknownKey(std::string_view key)
{
	return Error::make(ErrorCode::NotFound, "The rule names an unknown parameter").with("key", std::string(key));
}

Error incomparable(const Value& a, const Value& b)
{
	return Error::make(ErrorCode::Value, "The rule compares values of different kinds").with("left", std::string(toString(a.kind()))).with("right", std::string(toString(b.kind())));
}

Error operandValue(const RuleNode& node, const RuleContext& context, Value& out)
{
	if (node.op == RuleOp::Literal) {
		out = node.literal;
		return Error::none();
	}
	if (node.op == RuleOp::Reference) {
		std::optional<Value> v = context.value(node.key);
		if (!v) {
			return unknownKey(node.key);
		}
		out = std::move(*v);
		return Error::none();
	}
	return Error::make(ErrorCode::Value, "The rule uses a boolean expression as an operand");
}

/*! Compares two operands. A choice parameter (an int reference) against a string literal
    compares through the option index. Returns -1, 0, or 1 in `order`, or an error. */
Error compareOperands(const RuleNode& left, const RuleNode& right, const RuleContext& context, int& order, bool& equalOnly)
{
	Value a;
	Value b;
	if (Error e = operandValue(left, context, a)) {
		return e;
	}
	if (Error e = operandValue(right, context, b)) {
		return e;
	}
	equalOnly = false;
	auto resolveOption = [&context](const RuleNode& reference, const Value& literal, Value& resolved) {
		if (reference.op != RuleOp::Reference || literal.kind() != ValueKind::String) {
			return false;
		}
		std::optional<int> index = context.optionIndex(reference.key, literal.as<std::string>());
		if (!index) {
			return false;
		}
		resolved = Value(*index);
		return true;
	};
	Value resolved;
	if (isNumeric(a) && resolveOption(left, b, resolved)) {
		b = resolved;
	} else if (isNumeric(b) && resolveOption(right, a, resolved)) {
		a = resolved;
	}
	if (isNumeric(a) && isNumeric(b)) {
		const double x = a.toDouble();
		const double y = b.toDouble();
		order = x < y ? -1 : (x > y ? 1 : 0);
		return Error::none();
	}
	if (a.kind() == ValueKind::String && b.kind() == ValueKind::String) {
		const int c = a.as<std::string>().compare(b.as<std::string>());
		order = c < 0 ? -1 : (c > 0 ? 1 : 0);
		return Error::none();
	}
	if (a.kind() == b.kind()) {
		order = a == b ? 0 : 1;
		equalOnly = true;
		return Error::none();
	}
	return incomparable(a, b);
}

Error truthiness(const RuleNode& node, const RuleContext& context, bool& out)
{
	Value v;
	if (Error e = operandValue(node, context, v)) {
		return e;
	}
	switch (v.kind()) {
	case ValueKind::Empty:
		out = false;
		return Error::none();
	case ValueKind::Bool:
	case ValueKind::Int:
	case ValueKind::Double:
		out = v.toBool();
		return Error::none();
	case ValueKind::String:
		out = !v.as<std::string>().empty();
		return Error::none();
	case ValueKind::Blob:
		out = !v.as<Bytes>().empty();
		return Error::none();
	default:
		return Error::make(ErrorCode::Value, "The rule tests a value that has no truth value").with("kind", std::string(toString(v.kind())));
	}
}

Error evaluateNode(const RuleNode& node, const RuleContext& context, bool& out)
{
	switch (node.op) {
	case RuleOp::Literal:
	case RuleOp::Reference:
		return truthiness(node, context, out);
	case RuleOp::Not: {
		bool inner = false;
		if (Error e = evaluateNode(*node.children[0], context, inner)) {
			return e;
		}
		out = !inner;
		return Error::none();
	}
	case RuleOp::And: {
		bool a = false;
		bool b = false;
		if (Error e = evaluateNode(*node.children[0], context, a)) {
			return e;
		}
		if (!a) {
			out = false;
			return Error::none();
		}
		if (Error e = evaluateNode(*node.children[1], context, b)) {
			return e;
		}
		out = b;
		return Error::none();
	}
	case RuleOp::Or: {
		bool a = false;
		bool b = false;
		if (Error e = evaluateNode(*node.children[0], context, a)) {
			return e;
		}
		if (a) {
			out = true;
			return Error::none();
		}
		if (Error e = evaluateNode(*node.children[1], context, b)) {
			return e;
		}
		out = b;
		return Error::none();
	}
	case RuleOp::In: {
		out = false;
		for (std::size_t i = 1; i < node.children.size(); ++i) {
			int order = 0;
			bool equalOnly = false;
			if (Error e = compareOperands(*node.children[0], *node.children[i], context, order, equalOnly)) {
				return e;
			}
			if (order == 0) {
				out = true;
				return Error::none();
			}
		}
		return Error::none();
	}
	case RuleOp::Equal:
	case RuleOp::NotEqual:
	case RuleOp::Less:
	case RuleOp::LessEqual:
	case RuleOp::Greater:
	case RuleOp::GreaterEqual: {
		int order = 0;
		bool equalOnly = false;
		if (Error e = compareOperands(*node.children[0], *node.children[1], context, order, equalOnly)) {
			return e;
		}
		if (equalOnly && node.op != RuleOp::Equal && node.op != RuleOp::NotEqual) {
			return Error::make(ErrorCode::Value, "The rule orders values that have no order");
		}
		switch (node.op) {
		case RuleOp::Equal: out = order == 0; break;
		case RuleOp::NotEqual: out = order != 0; break;
		case RuleOp::Less: out = order < 0; break;
		case RuleOp::LessEqual: out = order <= 0; break;
		case RuleOp::Greater: out = order > 0; break;
		default: out = order >= 0; break;
		}
		return Error::none();
	}
	}
	return Error::make(ErrorCode::Value, "The rule tree is malformed");
}

}  // namespace

// ---- MapRuleContext -----------------------------------------------------------------------------

std::optional<Value> MapRuleContext::value(std::string_view key) const
{
	auto it = values.find(std::string(key));
	if (it == values.end()) {
		return std::nullopt;
	}
	return it->second;
}

std::optional<int> MapRuleContext::optionIndex(std::string_view key, std::string_view option) const
{
	auto it = options.find(std::string(key));
	if (it == options.end()) {
		return std::nullopt;
	}
	auto pos = std::find(it->second.begin(), it->second.end(), option);
	if (pos == it->second.end()) {
		return std::nullopt;
	}
	return static_cast<int>(pos - it->second.begin());
}

// ---- Rule ---------------------------------------------------------------------------------------

Error Rule::parse(std::string_view text, Rule& out)
{
	Rule rule;
	rule.text_ = std::string(text);
	std::vector<Token> tokens;
	if (Error e = tokenize(text, tokens)) {
		return e.with("rule", std::string(text));
	}
	if (tokens.size() == 1) {
		out = std::move(rule);
		return Error::none();
	}
	Parser parser(std::move(tokens), rule.references_);
	NodePtr root;
	if (Error e = parser.parseRule(root)) {
		return e.with("rule", std::string(text));
	}
	rule.root_ = std::move(root);
	out = std::move(rule);
	return Error::none();
}

Error Rule::evaluate(const RuleContext& context, bool& out) const
{
	out = false;
	if (root_ == nullptr) {
		out = true;
		return Error::none();
	}
	if (Error e = evaluateNode(*root_, context, out)) {
		out = false;
		return e.with("rule", text_);
	}
	return Error::none();
}

OFXGRIP_NAMESPACE_END

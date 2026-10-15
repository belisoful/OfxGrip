// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Plist.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

OFXGRIP_NAMESPACE_BEGIN

// ---- base64 -------------------------------------------------------------------------------------

namespace {
const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const Bytes& in)
{
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	std::size_t i = 0;
	while (i + 2 < in.size()) {
		const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out.push_back(kBase64[(v >> 6) & 63]);
		out.push_back(kBase64[v & 63]);
		i += 3;
	}
	if (i + 1 == in.size()) {
		const std::uint32_t v = static_cast<std::uint32_t>(in[i]) << 16;
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out += "==";
	} else if (i + 2 == in.size()) {
		const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8);
		out.push_back(kBase64[(v >> 18) & 63]);
		out.push_back(kBase64[(v >> 12) & 63]);
		out.push_back(kBase64[(v >> 6) & 63]);
		out.push_back('=');
	}
	return out;
}

bool base64Decode(std::string_view in, Bytes& out)
{
	out.clear();
	std::uint32_t buffer = 0;
	int bits = 0;
	for (char c : in) {
		if (c == '=') {
			break;
		}
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
			continue;
		}
		const char* p = std::char_traits<char>::find(kBase64, 64, c);
		if (p == nullptr) {
			return false;
		}
		buffer = (buffer << 6) | static_cast<std::uint32_t>(p - kBase64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
		}
	}
	return true;
}

// ---- PlistValue ---------------------------------------------------------------------------------

const PlistValue* PlistValue::get(std::string_view key) const noexcept
{
	const PlistDict* dict = tryAs<PlistDict>();
	if (dict == nullptr) {
		return nullptr;
	}
	for (const auto& [k, v] : *dict) {
		if (k == key) {
			return &v;
		}
	}
	return nullptr;
}

PlistValue* PlistValue::get(std::string_view key) noexcept
{
	PlistDict* dict = tryAs<PlistDict>();
	if (dict == nullptr) {
		return nullptr;
	}
	for (auto& [k, v] : *dict) {
		if (k == key) {
			return &v;
		}
	}
	return nullptr;
}

void PlistValue::set(std::string key, PlistValue value)
{
	if (empty()) {
		storage_ = PlistDict{};
	}
	PlistDict* dict = tryAs<PlistDict>();
	if (dict == nullptr) {
		return;
	}
	for (auto& [k, v] : *dict) {
		if (k == key) {
			v = std::move(value);
			return;
		}
	}
	dict->emplace_back(std::move(key), std::move(value));
}

double PlistValue::toDouble(double fallback) const noexcept
{
	switch (kind()) {
	case PlistKind::Bool: return as<bool>() ? 1.0 : 0.0;
	case PlistKind::Integer: return static_cast<double>(as<std::int64_t>());
	case PlistKind::Real: return as<double>();
	default: return fallback;
	}
}

std::int64_t PlistValue::toInteger(std::int64_t fallback) const noexcept
{
	switch (kind()) {
	case PlistKind::Bool: return as<bool>() ? 1 : 0;
	case PlistKind::Integer: return as<std::int64_t>();
	case PlistKind::Real: return static_cast<std::int64_t>(std::llround(as<double>()));
	default: return fallback;
	}
}

std::string PlistValue::toString(std::string fallback) const
{
	return is<std::string>() ? as<std::string>() : fallback;
}

// ---- reader -------------------------------------------------------------------------------------

namespace {

class XmlReader {
public:
	explicit XmlReader(std::string_view text) : text_(text) {}

	Error parseDocument(PlistValue& out)
	{
		skipProlog();
		std::string tag;
		if (!openTag(tag) || tag != "plist") {
			return fail("expected <plist>");
		}
		skipSpace();
		if (peekClose("plist")) {
			out = PlistValue();
			return Error::none();
		}
		if (Error e = parseNode(out)) {
			return e;
		}
		skipSpace();
		if (!closeTag("plist")) {
			return fail("expected </plist>");
		}
		return Error::none();
	}

	Error parseNode(PlistValue& out)
	{
		skipSpace();
		std::string tag;
		bool selfClosing = false;
		if (!openTagEx(tag, selfClosing)) {
			return fail("expected a plist node");
		}
		if (tag == "true" || tag == "false") {
			if (!selfClosing && !closeTag(tag)) {
				return fail("expected a self-closing boolean");
			}
			out = PlistValue(tag == "true");
			return Error::none();
		}
		if (tag == "dict") {
			PlistDict dict;
			if (!selfClosing) {
				for (;;) {
					skipSpace();
					if (closeTag("dict")) {
						break;
					}
					std::string keyTag;
					if (!openTag(keyTag) || keyTag != "key") {
						return fail("expected <key>");
					}
					std::string key = unescape(textUntilClose("key"));
					PlistValue value;
					if (Error e = parseNode(value)) {
						return e;
					}
					dict.emplace_back(std::move(key), std::move(value));
				}
			}
			out = PlistValue(std::move(dict));
			return Error::none();
		}
		if (tag == "array") {
			PlistArray array;
			if (!selfClosing) {
				for (;;) {
					skipSpace();
					if (closeTag("array")) {
						break;
					}
					PlistValue value;
					if (Error e = parseNode(value)) {
						return e;
					}
					array.push_back(std::move(value));
				}
			}
			out = PlistValue(std::move(array));
			return Error::none();
		}
		const std::string body = selfClosing ? std::string() : unescape(textUntilClose(tag));
		if (!selfClosing && failed_) {
			return fail("unterminated <" + tag + ">");
		}
		if (tag == "string") {
			out = PlistValue(body);
			return Error::none();
		}
		if (tag == "integer") {
			const std::string trimmed = trim(body);
			char* end = nullptr;
			const long long v = std::strtoll(trimmed.c_str(), &end, 10);
			if (trimmed.empty() || end == nullptr || *end != '\0') {
				return fail("bad integer");
			}
			out = PlistValue(static_cast<std::int64_t>(v));
			return Error::none();
		}
		if (tag == "real") {
			const std::string trimmed = trim(body);
			char* end = nullptr;
			const double v = std::strtod(trimmed.c_str(), &end);
			if (trimmed.empty() || end == nullptr || *end != '\0') {
				return fail("bad real");
			}
			out = PlistValue(v);
			return Error::none();
		}
		if (tag == "data") {
			Bytes bytes;
			if (!base64Decode(body, bytes)) {
				return fail("bad data");
			}
			out = PlistValue(std::move(bytes));
			return Error::none();
		}
		if (tag == "date") {
			out = PlistValue(PlistDate{trim(body)});
			return Error::none();
		}
		return fail("unknown node <" + tag + ">");
	}

private:
	Error fail(std::string why)
	{
		failed_ = true;
		return Error::make(ErrorCode::Codec, "The property list is unreadable", why + " at " + std::to_string(pos_)).with("position", std::to_string(pos_));
	}

	static std::string trim(const std::string& s)
	{
		std::size_t a = 0;
		std::size_t b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\r' || s[a] == '\t')) {
			++a;
		}
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\r' || s[b - 1] == '\t')) {
			--b;
		}
		return s.substr(a, b - a);
	}

	void skipSpace()
	{
		for (;;) {
			while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t')) {
				++pos_;
			}
			if (text_.substr(pos_, 4) == "<!--") {
				const std::size_t end = text_.find("-->", pos_);
				pos_ = end == std::string_view::npos ? text_.size() : end + 3;
				continue;
			}
			break;
		}
	}

	void skipProlog()
	{
		for (;;) {
			skipSpace();
			if (text_.substr(pos_, 2) == "<?" || text_.substr(pos_, 2) == "<!") {
				const std::size_t end = text_.find('>', pos_);
				pos_ = end == std::string_view::npos ? text_.size() : end + 1;
				continue;
			}
			break;
		}
	}

	bool openTag(std::string& tag)
	{
		bool selfClosing = false;
		return openTagEx(tag, selfClosing) && !selfClosing;
	}

	bool openTagEx(std::string& tag, bool& selfClosing)
	{
		skipSpace();
		if (pos_ >= text_.size() || text_[pos_] != '<' || (pos_ + 1 < text_.size() && text_[pos_ + 1] == '/')) {
			return false;
		}
		const std::size_t end = text_.find('>', pos_);
		if (end == std::string_view::npos) {
			return false;
		}
		std::string_view inside = text_.substr(pos_ + 1, end - pos_ - 1);
		selfClosing = !inside.empty() && inside.back() == '/';
		if (selfClosing) {
			inside.remove_suffix(1);
		}
		const std::size_t space = inside.find_first_of(" \t\n\r");
		tag = std::string(space == std::string_view::npos ? inside : inside.substr(0, space));
		pos_ = end + 1;
		return !tag.empty();
	}

	bool peekClose(std::string_view tag) const
	{
		return text_.substr(pos_, tag.size() + 3) == "</" + std::string(tag) + ">";
	}

	bool closeTag(std::string_view tag)
	{
		skipSpace();
		if (!peekClose(tag)) {
			return false;
		}
		pos_ += tag.size() + 3;
		return true;
	}

	std::string textUntilClose(std::string_view tag)
	{
		const std::string close = "</" + std::string(tag) + ">";
		const std::size_t end = text_.find(close, pos_);
		if (end == std::string_view::npos) {
			failed_ = true;
			pos_ = text_.size();
			return {};
		}
		std::string body(text_.substr(pos_, end - pos_));
		pos_ = end + close.size();
		return body;
	}

	static std::string unescape(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());
		for (std::size_t i = 0; i < in.size(); ++i) {
			if (in[i] != '&') {
				out.push_back(in[i]);
				continue;
			}
			const std::size_t semi = in.find(';', i);
			if (semi == std::string::npos) {
				out.push_back('&');
				continue;
			}
			const std::string_view entity(in.data() + i + 1, semi - i - 1);
			if (entity == "lt") {
				out.push_back('<');
			} else if (entity == "gt") {
				out.push_back('>');
			} else if (entity == "amp") {
				out.push_back('&');
			} else if (entity == "quot") {
				out.push_back('"');
			} else if (entity == "apos") {
				out.push_back('\'');
			} else if (!entity.empty() && entity[0] == '#') {
				const unsigned long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? std::strtoul(std::string(entity.substr(2)).c_str(), nullptr, 16) : std::strtoul(std::string(entity.substr(1)).c_str(), nullptr, 10);
				appendUtf8(out, code);
			} else {
				out.push_back('&');
				continue;
			}
			i = semi;
		}
		return out;
	}

	static void appendUtf8(std::string& out, unsigned long code)
	{
		if (code < 0x80) {
			out.push_back(static_cast<char>(code));
		} else if (code < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (code >> 6)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		} else if (code < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (code >> 12)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (code >> 18)));
			out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
		}
	}

	std::string_view text_;
	std::size_t pos_{0};
	bool failed_{false};
};

std::string escapeXml(std::string_view in)
{
	std::string out;
	out.reserve(in.size());
	for (char c : in) {
		switch (c) {
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '&': out += "&amp;"; break;
		default: out.push_back(c); break;
		}
	}
	return out;
}

std::string formatReal(double v)
{
	if (std::isnan(v) || std::isinf(v)) {
		return "0.0";
	}
	char buffer[64];
	const auto result = std::to_chars(buffer, buffer + sizeof buffer, v);
	std::string text(buffer, result.ptr);
	if (text.find('.') == std::string::npos && text.find('e') == std::string::npos) {
		text += ".0";
	}
	return text;
}

void writeNode(std::string& out, const PlistValue& node, int depth)
{
	const std::string indent(static_cast<std::size_t>(depth), '\t');
	switch (node.kind()) {
	case PlistKind::Empty:
		out += indent + "<dict/>\n";
		break;
	case PlistKind::Bool:
		out += indent + (node.as<bool>() ? "<true/>\n" : "<false/>\n");
		break;
	case PlistKind::Integer:
		out += indent + "<integer>" + std::to_string(node.as<std::int64_t>()) + "</integer>\n";
		break;
	case PlistKind::Real:
		out += indent + "<real>" + formatReal(node.as<double>()) + "</real>\n";
		break;
	case PlistKind::String:
		out += indent + "<string>" + escapeXml(node.as<std::string>()) + "</string>\n";
		break;
	case PlistKind::Data:
		out += indent + "<data>" + base64Encode(node.as<Bytes>()) + "</data>\n";
		break;
	case PlistKind::Date:
		out += indent + "<date>" + escapeXml(node.as<PlistDate>().iso8601) + "</date>\n";
		break;
	case PlistKind::Array: {
		const PlistArray& array = node.as<PlistArray>();
		if (array.empty()) {
			out += indent + "<array/>\n";
			break;
		}
		out += indent + "<array>\n";
		for (const PlistValue& item : array) {
			writeNode(out, item, depth + 1);
		}
		out += indent + "</array>\n";
		break;
	}
	case PlistKind::Dict: {
		const PlistDict& dict = node.as<PlistDict>();
		if (dict.empty()) {
			out += indent + "<dict/>\n";
			break;
		}
		out += indent + "<dict>\n";
		for (const auto& [key, value] : dict) {
			out += indent + "\t<key>" + escapeXml(key) + "</key>\n";
			writeNode(out, value, depth + 1);
		}
		out += indent + "</dict>\n";
		break;
	}
	}
}

}  // namespace

Error parsePlist(std::string_view xml, PlistValue& out)
{
	XmlReader reader(xml);
	PlistValue root;
	if (Error e = reader.parseDocument(root)) {
		return e;
	}
	out = std::move(root);
	return Error::none();
}

std::string writePlist(const PlistValue& root)
{
	std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n";
	writeNode(out, root, 0);
	out += "</plist>\n";
	return out;
}

// ---- value encodings ----------------------------------------------------------------------------

PlistValue plistFromValue(const Value& value)
{
	switch (value.kind()) {
	case ValueKind::Empty:
		return PlistValue(PlistDict{});
	case ValueKind::Bool:
		return PlistValue(static_cast<std::int64_t>(value.as<bool>() ? 1 : 0));
	case ValueKind::Int:
		return PlistValue(static_cast<std::int64_t>(value.as<int>()));
	case ValueKind::Double:
		return PlistValue(value.as<double>());
	case ValueKind::Point2D:
		return PlistValue(PlistDict{{"x", PlistValue(value.as<Point2D>().x)}, {"y", PlistValue(value.as<Point2D>().y)}});
	case ValueKind::Point3D:
		return PlistValue(PlistDict{{"x", PlistValue(value.as<Point3D>().x)}, {"y", PlistValue(value.as<Point3D>().y)}, {"z", PlistValue(value.as<Point3D>().z)}});
	case ValueKind::RGB:
		return PlistValue(PlistDict{{"blue", PlistValue(value.as<RGB>().b)}, {"green", PlistValue(value.as<RGB>().g)}, {"red", PlistValue(value.as<RGB>().r)}});
	case ValueKind::RGBA:
		return PlistValue(PlistDict{{"alpha", PlistValue(value.as<RGBA>().a)}, {"blue", PlistValue(value.as<RGBA>().b)}, {"green", PlistValue(value.as<RGBA>().g)}, {"red", PlistValue(value.as<RGBA>().r)}});
	case ValueKind::String:
		return PlistValue(value.as<std::string>());
	case ValueKind::Blob:
		return PlistValue(value.as<Bytes>());
	}
	return PlistValue();
}

bool valueFromPlist(const PlistValue& plist, Value& out)
{
	switch (plist.kind()) {
	case PlistKind::Empty:
		out = Value();
		return true;
	case PlistKind::Bool:
		out = Value(plist.as<bool>());
		return true;
	case PlistKind::Integer:
		out = Value(static_cast<int>(plist.as<std::int64_t>()));
		return true;
	case PlistKind::Real:
		out = Value(plist.as<double>());
		return true;
	case PlistKind::String:
	case PlistKind::Date:
		out = Value(plist.is<std::string>() ? plist.as<std::string>() : plist.as<PlistDate>().iso8601);
		return true;
	case PlistKind::Data:
		out = Value(plist.as<Bytes>());
		return true;
	case PlistKind::Array:
		return false;
	case PlistKind::Dict: {
		const PlistValue* red = plist.get("red");
		const PlistValue* green = plist.get("green");
		const PlistValue* blue = plist.get("blue");
		if (red != nullptr && green != nullptr && blue != nullptr) {
			const PlistValue* alpha = plist.get("alpha");
			if (alpha != nullptr) {
				out = Value(RGBA{red->toDouble(), green->toDouble(), blue->toDouble(), alpha->toDouble(1.0)});
			} else {
				out = Value(RGB{red->toDouble(), green->toDouble(), blue->toDouble()});
			}
			return true;
		}
		const PlistValue* x = plist.get("x");
		const PlistValue* y = plist.get("y");
		if (x != nullptr && y != nullptr) {
			const PlistValue* z = plist.get("z");
			if (z != nullptr) {
				out = Value(Point3D{x->toDouble(), y->toDouble(), z->toDouble()});
			} else {
				out = Value(Point2D{x->toDouble(), y->toDouble()});
			}
			return true;
		}
		if (plist.as<PlistDict>().empty()) {
			out = Value();
			return true;
		}
		return false;
	}
	}
	return false;
}

OFXGRIP_NAMESPACE_END

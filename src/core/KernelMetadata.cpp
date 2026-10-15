// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/KernelMetadata.hpp>

#include <ofxgrip/core/ManifestJson.hpp>

#include <nlohmann/json.hpp>

#include <cctype>

OFXGRIP_NAMESPACE_BEGIN

namespace {

using Json = nlohmann::json;

bool identifierStart(char c)
{
	return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

bool identifierPart(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

std::size_t skipSpace(std::string_view text, std::size_t i)
{
	while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
		++i;
	}
	return i;
}

}  // namespace

std::string json5ToJson(std::string_view text)
{
	std::string out;
	out.reserve(text.size() + 16);
	std::size_t i = 0;
	while (i < text.size()) {
		const char c = text[i];
		// Comments.
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
			while (i < text.size() && text[i] != '\n') {
				++i;
			}
			continue;
		}
		if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
			i += 2;
			while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
				++i;
			}
			i = i + 2 <= text.size() ? i + 2 : text.size();
			continue;
		}
		// Strings: double-quoted pass through; single-quoted become double-quoted.
		if (c == '"' || c == '\'') {
			const char quote = c;
			out.push_back('"');
			++i;
			while (i < text.size() && text[i] != quote) {
				if (text[i] == '\\' && i + 1 < text.size()) {
					if (text[i + 1] == '\'' ) {
						out.push_back('\'');
					} else {
						out.push_back('\\');
						out.push_back(text[i + 1]);
					}
					i += 2;
					continue;
				}
				if (text[i] == '"' && quote == '\'') {
					out.append("\\\"");
				} else {
					out.push_back(text[i]);
				}
				++i;
			}
			out.push_back('"');
			++i;
			continue;
		}
		// A trailing comma before a closing bracket is dropped.
		if (c == ',') {
			const std::size_t next = skipSpace(text, i + 1);
			if (next < text.size() && (text[next] == '}' || text[next] == ']')) {
				++i;
				continue;
			}
			out.push_back(c);
			++i;
			continue;
		}
		// An unquoted key or a bare word.
		if (identifierStart(c)) {
			std::size_t end = i;
			while (end < text.size() && identifierPart(text[end])) {
				++end;
			}
			const std::string_view word = text.substr(i, end - i);
			const std::size_t next = skipSpace(text, end);
			const bool isKey = next < text.size() && text[next] == ':';
			if (isKey || (word != "true" && word != "false" && word != "null" && word != "Infinity" && word != "NaN")) {
				out.push_back('"');
				out.append(word);
				out.push_back('"');
			} else {
				out.append(word);
			}
			i = end;
			continue;
		}
		// Numbers: `+1`, `.5`, and `5.` complete to JSON forms; hex passes through as decimal.
		if (c == '+' || c == '.' || c == '-' || std::isdigit(static_cast<unsigned char>(c)) != 0) {
			std::size_t end = i;
			if (text[end] == '+' || text[end] == '-') {
				++end;
			}
			if (end + 1 < text.size() && text[end] == '0' && (text[end + 1] == 'x' || text[end + 1] == 'X')) {
				std::size_t h = end + 2;
				unsigned long long value = 0;
				while (h < text.size() && std::isxdigit(static_cast<unsigned char>(text[h])) != 0) {
					const char d = static_cast<char>(std::tolower(static_cast<unsigned char>(text[h])));
					value = value * 16 + static_cast<unsigned long long>(d >= 'a' ? d - 'a' + 10 : d - '0');
					++h;
				}
				if (c == '-') {
					out.push_back('-');
				}
				out.append(std::to_string(value));
				i = h;
				continue;
			}
			while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) != 0 || text[end] == '.' || text[end] == 'e' || text[end] == 'E' || ((text[end] == '+' || text[end] == '-') && (text[end - 1] == 'e' || text[end - 1] == 'E')))) {
				++end;
			}
			std::string number(text.substr(i, end - i));
			if (!number.empty() && number.front() == '+') {
				number.erase(0, 1);
			}
			if (!number.empty() && number.front() == '.') {
				number.insert(0, "0");
			} else if (number.size() > 1 && number.front() == '-' && number[1] == '.') {
				number.insert(1, "0");
			}
			const std::size_t dot = number.find('.');
			if (dot != std::string::npos && (dot + 1 == number.size() || std::isdigit(static_cast<unsigned char>(number[dot + 1])) == 0)) {
				number.insert(dot + 1, "0");
			}
			out.append(number);
			i = end;
			continue;
		}
		out.push_back(c);
		++i;
	}
	return out;
}

std::vector<std::string> kernelMetadataBlocks(std::string_view source)
{
	std::vector<std::string> blocks;
	std::size_t pos = 0;
	while (pos < source.size()) {
		const std::size_t open = source.find("/*", pos);
		if (open == std::string_view::npos) {
			break;
		}
		// The opener must start a line (leading blanks allowed).
		std::size_t lineStart = open;
		while (lineStart > 0 && source[lineStart - 1] != '\n') {
			--lineStart;
		}
		bool startsLine = true;
		for (std::size_t i = lineStart; i < open; ++i) {
			if (source[i] != ' ' && source[i] != '\t') {
				startsLine = false;
				break;
			}
		}
		const std::size_t close = source.find("*/", open + 2);
		if (close == std::string_view::npos) {
			break;
		}
		if (startsLine) {
			const std::size_t marker = skipSpace(source, open + 2);
			if (source.substr(marker, kKernelMetadataMarker.size()) == kKernelMetadataMarker && (marker + kKernelMetadataMarker.size() >= close || !identifierPart(source[marker + kKernelMetadataMarker.size()]))) {
				blocks.emplace_back(source.substr(marker + kKernelMetadataMarker.size(), close - marker - kKernelMetadataMarker.size()));
			}
		}
		pos = close + 2;
	}
	return blocks;
}

Error KernelMetadata::scan(std::string_view source, KernelMetadata& out)
{
	KernelMetadata result;
	const std::vector<std::string> blocks = kernelMetadataBlocks(source);
	std::map<std::string, int> seen;
	for (const std::string& body : blocks) {
		const std::string json = json5ToJson(body);
		Json root = Json::parse(json, nullptr, false, true);
		if (root.is_discarded() || !root.is_object()) {
			return Error::make(ErrorCode::Codec, "A kernel metadata block is not a JSON5 object").with("block", std::to_string(result.blockCount + 1));
		}
		for (const auto& [name, value] : root.items()) {
			if (name == "parameters") {
				if (!value.is_array()) {
					return Error::make(ErrorCode::InvalidParameter, "The kernel metadata's parameters are not an array");
				}
				std::vector<ParameterSpec> specs;
				if (Error e = parseParameterListJson(value.dump(), specs)) {
					return e;
				}
				for (ParameterSpec& spec : specs) {
					result.parameters.push_back(std::move(spec));
				}
				continue;
			}
			if (seen[name]++ > 0) {
				return Error::make(ErrorCode::InvalidParameter, "A kernel metadata key appears in two blocks").with("key", name);
			}
			if (name == "colorSpace") {
				if (!value.is_object()) {
					return Error::make(ErrorCode::InvalidParameter, "The kernel metadata's colorSpace is not an object");
				}
				if (value.contains("transfer") && value["transfer"].is_string()) {
					result.colorSpace.transfer = value["transfer"].get<std::string>();
				}
				if (value.contains("primaries") && value["primaries"].is_string()) {
					result.colorSpace.primaries = value["primaries"].get<std::string>();
				}
				continue;
			}
			if (value.is_boolean()) {
				result.properties[name] = Value(value.get<bool>());
			} else if (value.is_number_integer()) {
				result.properties[name] = Value(value.get<int>());
			} else if (value.is_number_float()) {
				result.properties[name] = Value(value.get<double>());
			} else if (value.is_string()) {
				result.properties[name] = Value(value.get<std::string>());
			} else {
				result.properties[name] = Value(value.dump());
			}
		}
		++result.blockCount;
	}
	// A record without a key, such as a group, takes one from its label, as OpenFX names every
	// parameter. Keyed records bind by their key.
	std::function<void(std::vector<ParameterSpec>&)> deriveKeys = [&](std::vector<ParameterSpec>& list) {
		for (ParameterSpec& spec : list) {
			if (spec.key.empty()) {
				std::string key;
				for (char c : spec.label) {
					key.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
				}
				if (key.empty() || std::isdigit(static_cast<unsigned char>(key.front())) != 0) {
					key = "p" + std::to_string(spec.id) + (key.empty() ? std::string() : "_" + key);
				}
				spec.key = key;
				spec.meta[std::string(kKernelMetaDerivedKey)] = Value(true);
			}
			deriveKeys(spec.children);
		}
	};
	deriveKeys(result.parameters);
	std::function<Error(const std::vector<ParameterSpec>&)> collect = [&](const std::vector<ParameterSpec>& list) -> Error {
		for (const ParameterSpec& spec : list) {
			if (!spec.children.empty()) {
				if (Error e = collect(spec.children)) {
					return e;
				}
			}
			const bool derived = spec.meta.count(std::string(kKernelMetaDerivedKey)) != 0;
			if (spec.type == ParameterType::Group || spec.type == ParameterType::Section || spec.key.empty() || derived) {
				continue;
			}
			KernelBinding binding;
			binding.id = spec.id;
			binding.key = spec.key;
			binding.type = spec.type;
			auto flag = [&spec](std::string_view name) {
				auto it = spec.meta.find(std::string(name));
				return it != spec.meta.end() && it->second.toBool(false);
			};
			binding.constant = flag(kKernelMetaConstant);
			binding.pixelUnits = flag(kKernelMetaPixelUnits);
			binding.allowContinuousConstant = flag(kKernelMetaContinuousConstant);
			if (binding.constant) {
				const bool scalar = spec.dimensions() == 1 || spec.type == ParameterType::Toggle || spec.type == ParameterType::Menu || spec.type == ParameterType::Int;
				const bool continuous = spec.type == ParameterType::Float || spec.type == ParameterType::Percent || spec.type == ParameterType::Angle;
				if (!scalar) {
					return Error::make(ErrorCode::InvalidParameter, "A constant parameter must be a scalar").with("key", spec.key);
				}
				if (continuous && !binding.allowContinuousConstant) {
					return Error::make(ErrorCode::InvalidParameter, "A continuous parameter binds a specialization constant only with allowContinuousConstant").with("key", spec.key);
				}
			}
			result.bindings.push_back(std::move(binding));
		}
		return Error::none();
	};
	if (Error e = collect(result.parameters)) {
		return e;
	}
	out = std::move(result);
	return Error::none();
}

const KernelBinding* KernelMetadata::bindingForKey(std::string_view key) const noexcept
{
	for (const KernelBinding& binding : bindings) {
		if (binding.key == key) {
			return &binding;
		}
	}
	return nullptr;
}

const KernelBinding* KernelMetadata::bindingForId(ParameterId id) const noexcept
{
	for (const KernelBinding& binding : bindings) {
		if (binding.id == id) {
			return &binding;
		}
	}
	return nullptr;
}

std::vector<std::string> KernelMetadata::constantKeys() const
{
	std::vector<std::string> keys;
	for (const KernelBinding& binding : bindings) {
		if (binding.constant) {
			keys.push_back(binding.key);
		}
	}
	return keys;
}

OFXGRIP_NAMESPACE_END

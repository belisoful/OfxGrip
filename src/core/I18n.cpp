// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/I18n.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

// ---- StringTable --------------------------------------------------------------------------------

namespace {

bool readQuoted(std::string_view text, std::size_t& pos, std::string& out)
{
	if (pos >= text.size() || text[pos] != '"') {
		return false;
	}
	++pos;
	out.clear();
	while (pos < text.size()) {
		const char c = text[pos++];
		if (c == '"') {
			return true;
		}
		if (c == '\\' && pos < text.size()) {
			const char e = text[pos++];
			switch (e) {
			case 'n': out.push_back('\n'); break;
			case 't': out.push_back('\t'); break;
			case 'r': out.push_back('\r'); break;
			default: out.push_back(e); break;
			}
			continue;
		}
		out.push_back(c);
	}
	return false;
}

void skipSpaceAndComments(std::string_view text, std::size_t& pos)
{
	for (;;) {
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
			++pos;
		}
		if (text.substr(pos, 2) == "/*") {
			const std::size_t end = text.find("*/", pos + 2);
			pos = end == std::string_view::npos ? text.size() : end + 2;
			continue;
		}
		if (text.substr(pos, 2) == "//") {
			const std::size_t end = text.find('\n', pos);
			pos = end == std::string_view::npos ? text.size() : end + 1;
			continue;
		}
		return;
	}
}

bool readBareKey(std::string_view text, std::size_t& pos, std::string& out)
{
	out.clear();
	while (pos < text.size()) {
		const char c = text[pos];
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
		if (!ok) {
			break;
		}
		out.push_back(c);
		++pos;
	}
	return !out.empty();
}

}  // namespace

Error StringTable::parseStrings(std::string_view text, StringTable& out)
{
	StringTable table;
	std::size_t pos = 0;
	// A UTF-8 byte order mark is skipped.
	if (text.substr(0, 3) == "\xEF\xBB\xBF") {
		pos = 3;
	}
	for (;;) {
		skipSpaceAndComments(text, pos);
		if (pos >= text.size()) {
			break;
		}
		std::string key;
		if (!readQuoted(text, pos, key) && !readBareKey(text, pos, key)) {
			return Error::make(ErrorCode::Codec, "The strings file is unreadable", "expected a key at " + std::to_string(pos));
		}
		skipSpaceAndComments(text, pos);
		if (pos >= text.size() || text[pos] != '=') {
			return Error::make(ErrorCode::Codec, "The strings file is unreadable", "expected '=' at " + std::to_string(pos));
		}
		++pos;
		skipSpaceAndComments(text, pos);
		std::string value;
		if (!readQuoted(text, pos, value)) {
			return Error::make(ErrorCode::Codec, "The strings file is unreadable", "expected a quoted value at " + std::to_string(pos));
		}
		skipSpaceAndComments(text, pos);
		if (pos >= text.size() || text[pos] != ';') {
			return Error::make(ErrorCode::Codec, "The strings file is unreadable", "expected ';' at " + std::to_string(pos));
		}
		++pos;
		table.set(std::move(key), std::move(value));
	}
	out = std::move(table);
	return Error::none();
}

Error StringTable::loadFile(const std::filesystem::path& file, StringTable& out)
{
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		return Error::make(ErrorCode::NotFound, "The strings file cannot be read").with("path", file.string());
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	if (Error e = parseStrings(buffer.str(), out)) {
		return e.with("path", file.string());
	}
	return Error::none();
}

void StringTable::set(std::string key, std::string value)
{
	auto existing = forward_.find(key);
	if (existing != forward_.end()) {
		reverse_.erase(existing->second);
	}
	reverse_[value] = key;
	forward_[std::move(key)] = std::move(value);
}

std::string StringTable::localize(std::string_view key) const
{
	auto it = forward_.find(std::string(key));
	return it == forward_.end() ? std::string(key) : it->second;
}

std::string StringTable::delocalize(std::string_view text) const
{
	auto it = reverse_.find(std::string(text));
	return it == reverse_.end() ? std::string(text) : it->second;
}

// ---- discovery ----------------------------------------------------------------------------------

std::vector<std::string> preferredLanguages()
{
	std::vector<std::string> out;
	auto add = [&out](const char* value) {
		if (value == nullptr || *value == '\0') {
			return;
		}
		std::string language(value);
		const std::size_t cut = language.find_first_of("_.@");
		if (cut != std::string::npos) {
			language.resize(cut);
		}
		if (!language.empty() && language != "C" && language != "POSIX" && std::find(out.begin(), out.end(), language) == out.end()) {
			out.push_back(language);
		}
	};
	add(std::getenv("OFXGRIP_LANGUAGE"));
	add(std::getenv("LC_ALL"));
	add(std::getenv("LC_MESSAGES"));
	add(std::getenv("LANG"));
	if (std::find(out.begin(), out.end(), "en") == out.end()) {
		out.emplace_back("en");
	}
	return out;
}

std::optional<StringTable> findStringTable(const std::filesystem::path& bundlePath, const std::vector<std::string>& languages)
{
	if (bundlePath.empty()) {
		return std::nullopt;
	}
	std::filesystem::path bundle = bundlePath;
	for (std::filesystem::path p = bundlePath; !p.empty(); p = p.parent_path()) {
		if (p.extension() == ".bundle") {
			bundle = p;
			break;
		}
		if (p.parent_path() == p) {
			break;
		}
	}
	const std::filesystem::path resources = bundle / "Contents" / "Resources";
	std::vector<std::filesystem::path> candidates;
	for (const std::string& language : languages) {
		candidates.push_back(resources / (language + ".lproj") / "Localizable.strings");
	}
	candidates.push_back(resources / "Base.lproj" / "Localizable.strings");
	candidates.push_back(resources / "Localizable.strings");
	for (const std::filesystem::path& file : candidates) {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(file, ec)) {
			continue;
		}
		StringTable table;
		if (StringTable::loadFile(file, table).ok()) {
			return table;
		}
	}
	return std::nullopt;
}

namespace {

void localizeParameters(std::vector<ParameterSpec>& list, const StringTable& table)
{
	for (ParameterSpec& spec : list) {
		spec.label = table.localize(spec.label);
		if (!spec.hint.empty()) {
			spec.hint = table.localize(spec.hint);
		}
		for (ChoiceOption& option : spec.options) {
			option.label = table.localize(option.label.empty() ? option.key : option.label);
		}
		if (spec.type == ParameterType::String && spec.defaultValue.is<std::string>()) {
			spec.defaultValue = Value(table.localize(spec.defaultValue.as<std::string>()));
		}
		for (auto& [host, override] : spec.hosts) {
			if (override.label) {
				override.label = table.localize(*override.label);
			}
		}
		localizeParameters(spec.children, table);
	}
}

}  // namespace

void localizeManifest(EffectManifest& manifest, const StringTable& table)
{
	manifest.identity.name = table.localize(manifest.identity.name);
	if (!manifest.identity.shortName.empty()) {
		manifest.identity.shortName = table.localize(manifest.identity.shortName);
	}
	if (!manifest.identity.description.empty()) {
		manifest.identity.description = table.localize(manifest.identity.description);
	}
	if (!manifest.identity.grouping.empty()) {
		manifest.identity.grouping = table.localize(manifest.identity.grouping);
	}
	localizeParameters(manifest.parameters, table);
	manifest.about.name = table.localize(manifest.about.name);
	if (!manifest.about.mainText.empty()) {
		manifest.about.mainText = table.localize(manifest.about.mainText);
	}
	for (AboutEntry& entry : manifest.about.entries) {
		if (!entry.label.empty()) {
			entry.label = table.localize(entry.label);
		}
		if (!entry.dialogText.empty()) {
			entry.dialogText = table.localize(entry.dialogText);
		}
	}
	for (ManifestPreset& preset : manifest.presets) {
		preset.name = table.localize(preset.name);
	}
}

// ---- I18nExtension ------------------------------------------------------------------------------

I18nExtension::I18nExtension(StringTable table) : table_(std::move(table)), switches_() {}

I18nExtension::I18nExtension(StringTable table, Switches switches) : table_(std::move(table)), switches_(switches) {}

std::vector<EventId> I18nExtension::interests() const
{
	return {EventId::ApiSetLabelPre, EventId::ApiGetLabel, EventId::ApiSetValuePre, EventId::ApiGetValue, EventId::ApiSetOptionsPre};
}

void I18nExtension::handle(Event& event)
{
	auto& api = static_cast<ApiEvent&>(event);
	const bool stringParameter = api.spec != nullptr && (api.spec->type == ParameterType::String || api.spec->type == ParameterType::Status || api.spec->type == ParameterType::Banner || api.spec->type == ParameterType::Capsule);
	switch (event.id) {
	case EventId::ApiSetLabelPre:
		if (switches_.localizeNames) {
			api.label = table_.localize(api.label);
		}
		break;
	case EventId::ApiGetLabel:
		if (switches_.delocalizeNames) {
			api.label = table_.delocalize(api.label);
		}
		break;
	case EventId::ApiSetValuePre:
		if (switches_.localizeValues && stringParameter && api.value.is<std::string>()) {
			api.value = Value(table_.localize(api.value.as<std::string>()));
		}
		break;
	case EventId::ApiGetValue:
		if (switches_.delocalizeValues && stringParameter && api.value.is<std::string>()) {
			api.value = Value(table_.delocalize(api.value.as<std::string>()));
		}
		break;
	case EventId::ApiSetOptionsPre:
		if (switches_.localizeMenus) {
			for (ChoiceOption& option : api.options) {
				option.label = table_.localize(option.label.empty() ? option.key : option.label);
			}
		}
		break;
	default:
		break;
	}
}

OFXGRIP_NAMESPACE_END

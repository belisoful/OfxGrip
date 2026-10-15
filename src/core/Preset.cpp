// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Preset.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

namespace {

bool parseParameterId(const std::string& key, ParameterId& out)
{
	if (key.empty()) {
		return false;
	}
	char* end = nullptr;
	const unsigned long v = std::strtoul(key.c_str(), &end, 10);
	if (end == nullptr || *end != '\0') {
		return false;
	}
	out = static_cast<ParameterId>(v);
	return true;
}

PlistValue valuesToPlist(const std::map<ParameterId, Value>& values)
{
	PlistDict dict;
	for (const auto& [id, value] : values) {
		dict.emplace_back(std::to_string(id), plistFromValue(value));
	}
	return PlistValue(std::move(dict));
}

}  // namespace

PlistValue Preset::toPlist() const
{
	PlistDict root;
	root.emplace_back(presetkeys::kFxFactoryCreatedByParameterId, PlistValue(static_cast<std::int64_t>(createdBy == kParameterIdNone ? 0 : createdBy)));
	root.emplace_back(presetkeys::kFxFactoryParameterValues, valuesToPlist(values));
	if (!pluginAuthor.empty()) {
		root.emplace_back(presetkeys::kFxFactoryPluginAuthor, PlistValue(pluginAuthor));
	}
	if (!pluginLocalizedNames.empty()) {
		PlistDict names;
		for (const auto& [language, text] : pluginLocalizedNames) {
			names.emplace_back(language, PlistValue(text));
		}
		root.emplace_back(presetkeys::kFxFactoryLocalizedName, PlistValue(std::move(names)));
	} else if (!pluginLocalizedName.empty()) {
		root.emplace_back(presetkeys::kFxFactoryLocalizedName, PlistValue(pluginLocalizedName));
	}
	if (!pluginUuid.empty()) {
		root.emplace_back(presetkeys::kFxFactoryPluginUuid, PlistValue(pluginUuid));
	}
	if (!pluginVersion.empty()) {
		root.emplace_back(presetkeys::kFxFactoryPluginVersion, PlistValue(pluginVersion));
	}
	if (!productId.empty()) {
		root.emplace_back(presetkeys::kFxFactoryProductId, PlistValue(productId));
	}
	if (!framework.empty()) {
		root.emplace_back(presetkeys::kFxGripFramework, PlistValue(framework));
	}
	if (!uuid.empty()) {
		root.emplace_back(presetkeys::kFxGripUuid, PlistValue(uuid));
	}
	if (!name.empty()) {
		root.emplace_back(presetkeys::kFxGripDisplayName, PlistValue(name));
	}
	if (!tag.empty()) {
		root.emplace_back(presetkeys::kFxGripTag, PlistValue(tag));
	}
	if (!createdTime.empty()) {
		root.emplace_back(presetkeys::kFxGripCreatedTime, PlistValue(createdTime));
	}
	if (!tags.empty()) {
		PlistDict byId;
		for (const auto& [id, list] : tags) {
			PlistArray array;
			for (const std::string& t : list) {
				array.emplace_back(t);
			}
			byId.emplace_back(std::to_string(id), PlistValue(std::move(array)));
		}
		root.emplace_back(presetkeys::kFxGripParameterTags, PlistValue(std::move(byId)));
	}
	if (!meta.empty()) {
		PlistDict byId;
		for (const auto& [id, entries] : meta) {
			PlistDict inner;
			for (const auto& [key, value] : entries) {
				inner.emplace_back(key, plistFromValue(value));
			}
			byId.emplace_back(std::to_string(id), PlistValue(std::move(inner)));
		}
		root.emplace_back(presetkeys::kFxGripParameterMeta, PlistValue(std::move(byId)));
	}
	return PlistValue(std::move(root));
}

Error Preset::fromPlist(const PlistValue& plist, Preset& out)
{
	const PlistDict* root = plist.tryAs<PlistDict>();
	if (root == nullptr) {
		return Error::make(ErrorCode::Codec, "The preset is not a dictionary");
	}
	Preset p;
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryCreatedByParameterId)) {
		p.createdBy = static_cast<ParameterId>(v->toInteger(0));
	}
	if (const PlistValue* values = plist.get(presetkeys::kFxFactoryParameterValues)) {
		if (const PlistDict* dict = values->tryAs<PlistDict>()) {
			for (const auto& [key, node] : *dict) {
				ParameterId id = kParameterIdNone;
				Value value;
				if (parseParameterId(key, id) && valueFromPlist(node, value)) {
					p.values[id] = std::move(value);
				}
			}
		}
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryPluginAuthor)) {
		p.pluginAuthor = v->toString();
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryLocalizedName)) {
		if (const PlistDict* names = v->tryAs<PlistDict>()) {
			for (const auto& [language, text] : *names) {
				p.pluginLocalizedNames[language] = text.toString();
			}
			if (!p.pluginLocalizedNames.empty()) {
				auto english = p.pluginLocalizedNames.find("English");
				p.pluginLocalizedName = english != p.pluginLocalizedNames.end() ? english->second : p.pluginLocalizedNames.begin()->second;
			}
		} else {
			p.pluginLocalizedName = v->toString();
		}
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryPluginUuid)) {
		p.pluginUuid = v->toString();
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryPluginVersion)) {
		if (v->is<std::string>()) {
			p.pluginVersion = v->as<std::string>();
		} else if (v->kind() == PlistKind::Integer || v->kind() == PlistKind::Real) {
			p.pluginVersion = std::to_string(v->toInteger());
		}
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxFactoryProductId)) {
		p.productId = v->toString();
	}
	p.framework = "FxFactory";
	if (const PlistValue* v = plist.get(presetkeys::kFxGripFramework)) {
		p.framework = v->toString("FxGrip");
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxGripUuid)) {
		p.uuid = v->toString();
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxGripDisplayName)) {
		p.name = v->toString();
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxGripTag)) {
		p.tag = v->toString();
	}
	if (const PlistValue* v = plist.get(presetkeys::kFxGripCreatedTime)) {
		p.createdTime = v->is<PlistDate>() ? v->as<PlistDate>().iso8601 : v->toString();
	}
	if (const PlistValue* tags = plist.get(presetkeys::kFxGripParameterTags)) {
		if (const PlistDict* dict = tags->tryAs<PlistDict>()) {
			for (const auto& [key, node] : *dict) {
				ParameterId id = kParameterIdNone;
				const PlistArray* list = node.tryAs<PlistArray>();
				if (!parseParameterId(key, id) || list == nullptr) {
					continue;
				}
				std::vector<std::string>& out_tags = p.tags[id];
				for (const PlistValue& t : *list) {
					if (t.is<std::string>()) {
						out_tags.push_back(t.as<std::string>());
					}
				}
			}
		}
	}
	if (const PlistValue* meta = plist.get(presetkeys::kFxGripParameterMeta)) {
		if (const PlistDict* dict = meta->tryAs<PlistDict>()) {
			for (const auto& [key, node] : *dict) {
				ParameterId id = kParameterIdNone;
				const PlistDict* entries = node.tryAs<PlistDict>();
				if (!parseParameterId(key, id) || entries == nullptr) {
					continue;
				}
				for (const auto& [name, value] : *entries) {
					Value v;
					if (valueFromPlist(value, v)) {
						p.meta[id][name] = std::move(v);
					}
				}
			}
		}
	}
	out = std::move(p);
	return Error::none();
}

std::string Preset::serialize() const
{
	return writePlist(toPlist());
}

Error Preset::parse(std::string_view xml, Preset& out)
{
	PlistValue root;
	if (Error e = parsePlist(xml, root)) {
		return e;
	}
	return fromPlist(root, out);
}

Error Preset::load(const std::filesystem::path& file, Preset& out)
{
	std::ifstream in(file, std::ios::binary);
	if (!in) {
		return Error::make(ErrorCode::NotFound, "The preset file cannot be read").with("path", file.string());
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	Preset p;
	if (Error e = parse(buffer.str(), p)) {
		return e.with("path", file.string());
	}
	p.source = PresetSource::File;
	p.path = file;
	if (p.name.empty()) {
		p.name = file.stem().string();
	}
	out = std::move(p);
	return Error::none();
}

Error Preset::save(const std::filesystem::path& file) const
{
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		return Error::make(ErrorCode::Failed, "The preset file cannot be written").with("path", file.string());
	}
	out << serialize();
	return out ? Error::none() : Error::make(ErrorCode::Failed, "The preset file cannot be written").with("path", file.string());
}

std::string Preset::makeUuid()
{
	static thread_local std::mt19937_64 engine{std::random_device{}()};
	std::uniform_int_distribution<std::uint64_t> dist;
	const std::uint64_t a = dist(engine);
	const std::uint64_t b = dist(engine);
	char buffer[40];
	std::snprintf(buffer, sizeof buffer, "%08X-%04X-4%03X-%04X-%012llX", static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFF),
	              static_cast<unsigned>(0x8000 | ((b >> 48) & 0x3FFF)), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
	return buffer;
}

std::string Preset::nowIso8601()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	char buffer[32];
	std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
	return buffer;
}

std::string Preset::fileNameFor(std::string_view displayName)
{
	std::string out;
	for (char c : displayName) {
		const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
		out.push_back(bad ? '_' : c);
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
		out.pop_back();
	}
	if (out.empty()) {
		out = "Preset";
	}
	return out + presetkeys::kExtension;
}

OFXGRIP_NAMESPACE_END

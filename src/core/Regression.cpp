// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Regression.hpp>

OFXGRIP_NAMESPACE_BEGIN

bool looksLikeUuid(std::string_view text) noexcept
{
	if (text.size() != 36) {
		return false;
	}
	for (std::size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (c != '-') {
				return false;
			}
			continue;
		}
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex) {
			return false;
		}
	}
	return true;
}

std::vector<RegressionReport> checkManifest(const EffectManifest& manifest)
{
	std::vector<RegressionReport> out;
	auto report = [&out](const char* check, LogLevel level, std::string message) { out.push_back({check, level, std::move(message)}); };
	if (manifest.identity.uuid.empty()) {
		report("uuid", LogLevel::Warning, "The effect has no UUID; presets cannot check compatibility. Generate one with uuidgen.");
	} else if (!looksLikeUuid(manifest.identity.uuid)) {
		report("uuid", LogLevel::Error, "The effect UUID is not an 8-4-4-4-12 UUID: " + manifest.identity.uuid);
	}
	if (manifest.version.major == 0) {
		report("version", LogLevel::Warning, "The effect's major version is 0; hosts key plug-ins by major version.");
	}
	if (manifest.identity.ofxId.find('.') == std::string::npos) {
		report("identifier", LogLevel::Warning, "The OFX identifier is not reverse-DNS: " + manifest.identity.ofxId);
	}
	for (const ParameterSpec& spec : manifest.flattenedParameters()) {
		if (spec.label.empty() && spec.type != ParameterType::Divider) {
			report("label", LogLevel::Warning, "Parameter " + spec.key + " has no label.");
		}
		if (spec.hint.empty() && spec.hasState()) {
			report("hint", LogLevel::Debug, "Parameter " + spec.key + " has no hint.");
		}
		if ((spec.type == ParameterType::Menu) && spec.options.size() < 2) {
			report("options", LogLevel::Warning, "Choice " + spec.key + " offers fewer than two options.");
		}
		for (std::size_t i = 0; i < spec.minimum.size() && i < spec.maximum.size(); ++i) {
			if (spec.minimum[i] == spec.maximum[i]) {
				report("range", LogLevel::Warning, "Parameter " + spec.key + " has a range with no width.");
				break;
			}
		}
		if (spec.type == ParameterType::Presets) {
			if (spec.tags.empty()) {
				report("presets", LogLevel::Warning, "Presets control " + spec.key + " has no tag, so it lists nothing.");
			} else {
				bool listed = false;
				for (const ManifestPreset& preset : manifest.presets) {
					listed = listed || preset.tag == spec.tags.front();
				}
				if (!listed) {
					report("presets", LogLevel::Debug, "Presets control " + spec.key + " lists no manifest preset for its tag " + spec.tags.front() + ".");
				}
			}
		}
	}
	for (const AboutEntry& entry : manifest.about.entries) {
		if (entry.kind == AboutEntry::Kind::Link && entry.url.rfind("http://", 0) != 0 && entry.url.rfind("https://", 0) != 0 && entry.url.rfind("mailto:", 0) != 0) {
			report("about", LogLevel::Warning, "About link " + entry.label + " is not an http, https, or mailto URL.");
		}
	}
	return out;
}

bool RegressionExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	reports_ = checkManifest(host.manifest());
	LogService& log = host.services().log();
	for (const RegressionReport& r : reports_) {
		log.log(r.level, "Regression (" + r.check + "): " + r.message);
	}
	return true;
}

OFXGRIP_NAMESPACE_END

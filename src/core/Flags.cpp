// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Flags.hpp>

#include <array>
#include <utility>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::array<std::pair<HostFlag, std::string_view>, 10> kHostFlagNames{{
	{HostFlag::NotAnimatable, "notanimatable"},
	{HostFlag::Hidden, "hidden"},
	{HostFlag::Disabled, "disabled"},
	{HostFlag::Collapsed, "collapsed"},
	{HostFlag::DontSave, "dontsave"},
	{HostFlag::NoEvaluateOnChange, "noevaluateonchange"},
	{HostFlag::NoUndo, "noundo"},
	{HostFlag::CustomUI, "customui"},
	{HostFlag::IgnoreMinMax, "ignoreminmax"},
	{HostFlag::UseFullViewWidth, "fullviewwidth"},
}};

constexpr std::array<std::pair<AppFlag, std::string_view>, 7> kAppFlagNames{{
	{AppFlag::NoState, "nostate"},
	{AppFlag::PresetNoValue, "presetnovalue"},
	{AppFlag::PresetNoTags, "presetnotags"},
	{AppFlag::PresetNoMeta, "presetnometa"},
	{AppFlag::HiddenProxy, "hiddenproxy"},
	{AppFlag::InDebugMode, "indebugmode"},
	{AppFlag::NoDebug, "nodebug"},
}};

std::string lowercase(std::string_view text)
{
	std::string out(text);
	for (char& c : out) {
		if (c >= 'A' && c <= 'Z') {
			c = static_cast<char>(c - 'A' + 'a');
		}
	}
	return out;
}

}  // namespace

std::string_view toString(HostFlag flag) noexcept
{
	for (const auto& [f, name] : kHostFlagNames) {
		if (f == flag) {
			return name;
		}
	}
	return "";
}

std::string_view toString(AppFlag flag) noexcept
{
	for (const auto& [f, name] : kAppFlagNames) {
		if (f == flag) {
			return name;
		}
	}
	return "";
}

bool parseHostFlag(std::string_view name, HostFlag& out) noexcept
{
	for (const auto& [f, n] : kHostFlagNames) {
		if (n == name) {
			out = f;
			return true;
		}
	}
	return false;
}

bool parseAppFlag(std::string_view name, AppFlag& out) noexcept
{
	for (const auto& [f, n] : kAppFlagNames) {
		if (n == name) {
			out = f;
			return true;
		}
	}
	return false;
}

ParsedFlags parseFlags(const std::vector<std::string>& names, HostFlags initialHost, AppFlags initialApp)
{
	ParsedFlags result;
	result.host = initialHost;
	result.app = initialApp;
	for (const std::string& raw : names) {
		std::string name = lowercase(raw);
		bool on = true;
		if (!name.empty() && (name[0] == '+' || name[0] == '-')) {
			on = name[0] == '+';
			name.erase(0, 1);
		}
		if (name.empty()) {
			continue;
		}
		HostFlag hostFlag{};
		AppFlag appFlag{};
		if (parseHostFlag(name, hostFlag)) {
			result.host.set(hostFlag, on);
		} else if (parseAppFlag(name, appFlag)) {
			result.app.set(appFlag, on);
		} else {
			result.unknown.push_back(name);
		}
	}
	return result;
}

ParsedFlags parseFlags(std::string_view list, HostFlags initialHost, AppFlags initialApp)
{
	std::vector<std::string> names;
	std::string current;
	for (char c : list) {
		if (c == ' ' || c == ',' || c == ';' || c == '|' || c == '\t' || c == '\n') {
			if (!current.empty()) {
				names.push_back(current);
				current.clear();
			}
		} else {
			current.push_back(c);
		}
	}
	if (!current.empty()) {
		names.push_back(current);
	}
	return parseFlags(names, initialHost, initialApp);
}

std::vector<std::string> flagNames(HostFlags host, AppFlags app)
{
	std::vector<std::string> names;
	for (const auto& [f, name] : kHostFlagNames) {
		if (host.has(f)) {
			names.emplace_back(name);
		}
	}
	for (const auto& [f, name] : kAppFlagNames) {
		if (app.has(f)) {
			names.emplace_back(name);
		}
	}
	return names;
}

OFXGRIP_NAMESPACE_END

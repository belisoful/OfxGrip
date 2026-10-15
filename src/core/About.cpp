// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/About.hpp>

OFXGRIP_NAMESPACE_BEGIN

ParameterSpec aboutMenuParameter(const AboutMenu& menu, ParameterId id, std::string key)
{
	std::vector<ChoiceOption> options;
	options.push_back({"about", menu.mainText.empty() ? (menu.name.empty() ? std::string("About") : menu.name) : menu.mainText});
	int separators = 0;
	int rows = 0;
	for (const AboutEntry& entry : menu.entries) {
		++rows;
		switch (entry.kind) {
		case AboutEntry::Kind::Separator:
			options.push_back({"separator" + std::to_string(++separators), "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"});
			break;
		case AboutEntry::Kind::Link:
			options.push_back({"link" + std::to_string(rows), entry.label});
			break;
		case AboutEntry::Kind::Text:
			options.push_back({"text" + std::to_string(rows), entry.label});
			break;
		case AboutEntry::Kind::Dialog:
			options.push_back({"dialog" + std::to_string(rows), entry.label + "..."});
			break;
		}
	}
	ParameterSpec spec = ParameterSpec::menuParam(id, std::move(key), menu.name.empty() ? std::string("About") : menu.name, std::move(options), 0);
	spec.hostFlags = HostFlag::NotAnimatable | HostFlag::NoEvaluateOnChange | HostFlag::NoUndo | HostFlag::DontSave;
	spec.appFlags = AppFlag::NoState | AppFlag::PresetNoValue | AppFlag::PresetNoTags | AppFlag::PresetNoMeta;
	return spec;
}

void AboutExtension::handle(Event& event)
{
	if (event.id != EventId::InstanceChanged) {
		return;
	}
	const auto& change = static_cast<const ChangeEvent&>(event);
	if (change.parameter != parameter_ || change.reason == ChangeReason::PluginEdited) {
		return;
	}
	ParameterService& params = host()->services().parameters();
	Value current;
	if (!params.getCurrent(parameter_, current).ok()) {
		return;
	}
	const int index = current.toInt(0);
	if (index > 0 && static_cast<std::size_t>(index) <= menu_.entries.size()) {
		activate(menu_.entries[static_cast<std::size_t>(index - 1)]);
	}
	if (index != 0) {
		params.setCurrent(parameter_, Value(0));
	}
}

void AboutExtension::activate(const AboutEntry& entry)
{
	HostServices& services = host()->services();
	switch (entry.kind) {
	case AboutEntry::Kind::Link: {
		std::vector<std::string> urls{entry.url};
		urls.insert(urls.end(), entry.fallbacks.begin(), entry.fallbacks.end());
		for (const std::string& url : urls) {
			if (url.empty()) {
				continue;
			}
			if (services.paths().openUrl(url)) {
				opened_.push_back(url);
				return;
			}
		}
		services.feedback().message(MessageKind::Message, "ofxgrip.about.link", entry.label + ": " + entry.url);
		return;
	}
	case AboutEntry::Kind::Dialog:
		services.feedback().message(MessageKind::Message, "ofxgrip.about.dialog", entry.dialogText.empty() ? entry.label : entry.dialogText);
		return;
	case AboutEntry::Kind::Text:
	case AboutEntry::Kind::Separator:
		return;
	}
}

OFXGRIP_NAMESPACE_END

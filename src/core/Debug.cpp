// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Debug.hpp>

OFXGRIP_NAMESPACE_BEGIN

ParameterSpec debugMenuParameter(ParameterId id, std::string key)
{
	ParameterSpec spec = ParameterSpec::menuParam(id, std::move(key), "Debug", {{"debug", "Debug"}, {"reveal", "Reveal hidden parameters"}, {"hide", "Hide parameters again"}}, 0);
	spec.hostFlags = HostFlag::NotAnimatable | HostFlag::NoEvaluateOnChange | HostFlag::NoUndo | HostFlag::DontSave;
	spec.appFlags = AppFlag::NoState | AppFlag::PresetNoValue | AppFlag::PresetNoTags | AppFlag::PresetNoMeta | AppFlag::NoDebug;
	return spec;
}

void DebugExtension::handle(Event& event)
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
	if (index == 1) {
		reveal();
	} else if (index == 2) {
		hide();
	}
	if (index != 0) {
		params.setCurrent(parameter_, Value(0));
	}
}

Error DebugExtension::reveal()
{
	ParameterService& params = host()->services().parameters();
	Error first;
	for (ParameterId id : params.ids()) {
		const ParameterSpec* spec = params.spec(id);
		if (spec == nullptr || id == parameter_ || spec->appFlags.has(AppFlag::NoDebug) || isReservedParameterId(id)) {
			continue;
		}
		HostFlags flags;
		if (!params.getFlags(id, flags).ok() || !flags.has(HostFlag::Hidden)) {
			continue;
		}
		if (Error e = params.setFlags(id, flags.set(HostFlag::Hidden, false))) {
			if (first.ok()) {
				first = e;
			}
			continue;
		}
		revealed_.insert(id);
	}
	debugMode_ = true;
	return first;
}

Error DebugExtension::hide()
{
	ParameterService& params = host()->services().parameters();
	Error first;
	for (ParameterId id : revealed_) {
		HostFlags flags;
		if (!params.getFlags(id, flags).ok()) {
			continue;
		}
		if (Error e = params.setFlags(id, flags.set(HostFlag::Hidden, true)); e && first.ok()) {
			first = e;
		}
	}
	revealed_.clear();
	debugMode_ = false;
	return first;
}

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/MetaExtension.hpp>

OFXGRIP_NAMESPACE_BEGIN

std::vector<EventId> MetaExtension::interests() const
{
	return {EventId::CreateInstance, EventId::InstanceChanged, EventId::Flush};
}

int MetaExtension::priority(EventId id) const
{
	switch (id) {
	case EventId::CreateInstance:
		return kPriorityFrameworkCapture;
	case EventId::InstanceChanged:
		return kPriorityTargetPresets;
	case EventId::Flush:
		return kPriorityFrameworkFlush;
	default:
		return defaultPriority();
	}
}

bool MetaExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	seedFromManifest();
	return true;
}

void MetaExtension::seedFromManifest()
{
	ParameterService& params = host()->services().parameters();
	for (ParameterId id : params.ids()) {
		if (const ParameterSpec* spec = params.spec(id)) {
			manager_.seed(*spec);
		}
	}
}

void MetaExtension::loadStored()
{
	PersistenceService& persistence = host()->services().persistence();
	Bytes stored;
	if (!persistence.load(kPersistenceKey, stored).ok()) {
		return;
	}
	MetaManager loaded;
	if (Error e = MetaManager::decode(stored, loaded)) {
		host()->services().log().warning("The stored meta is unreadable and was discarded: " + e.describe());
		return;
	}
	manager_.mergeFrom(loaded);
	loadedFromDocument_ = true;
}

void MetaExtension::handle(Event& event)
{
	switch (event.id) {
	case EventId::CreateInstance:
		loadStored();
		break;
	case EventId::InstanceChanged:
		onChange(static_cast<const ChangeEvent&>(event));
		break;
	case EventId::Flush:
		save();
		break;
	default:
		break;
	}
}

void MetaExtension::onChange(const ChangeEvent& change)
{
	if (change.parameter == kParameterIdNone || change.reason == ChangeReason::PluginEdited) {
		return;
	}
	ParameterService& params = host()->services().parameters();
	const ParameterSpec* spec = params.spec(change.parameter);
	if (spec == nullptr) {
		return;
	}
	const bool choice = spec->type == ParameterType::Menu || spec->type == ParameterType::Presets;
	const bool toggle = spec->type == ParameterType::Toggle || spec->type == ParameterType::Switch;
	if (!choice && !toggle) {
		return;
	}
	Value value;
	if (!params.getCurrent(change.parameter, value).ok()) {
		return;
	}
	if (!spec->targetPresets.empty()) {
		const std::string option = choice ? std::string(spec->optionKey(value.toInt(-1))) : std::string(value.toBool() ? "true" : "false");
		auto it = spec->targetPresets.find(option);
		if (it != spec->targetPresets.end()) {
			if (Error e = applyTargetPreset(it->second)) {
				host()->services().log().warning("Target preset failed: " + e.describe());
			}
		}
	}
	// A record with a reset value is a momentary control: it snaps back after every user change.
	if (!spec->resetValue.empty() && value != spec->resetValue) {
		params.setCurrent(change.parameter, spec->resetValue);
	}
}

Error MetaExtension::applyTargetPreset(const TargetPreset& preset)
{
	ParameterService& params = host()->services().parameters();
	Error first;
	auto note = [&first](Error e) {
		if (e && first.ok()) {
			first = std::move(e);
		}
	};
	for (const auto& [key, value] : preset.values) {
		const ParameterId id = params.idForKey(key);
		if (id == kParameterIdNone) {
			continue;
		}
		note(params.setCurrent(id, value));
	}
	for (const auto& [key, edits] : preset.flags) {
		const ParameterId id = params.idForKey(key);
		if (id == kParameterIdNone) {
			continue;
		}
		HostFlags current;
		if (Error e = params.getFlags(id, current)) {
			note(std::move(e));
			continue;
		}
		const ParsedFlags parsed = parseFlags(edits, current, {});
		note(params.setFlags(id, parsed.host));
	}
	for (const auto& [key, tags] : preset.tags) {
		const ParameterId id = params.idForKey(key);
		if (id == kParameterIdNone) {
			continue;
		}
		manager_.setTags(id, tags);
	}
	for (const auto& [key, entries] : preset.meta) {
		const ParameterId id = params.idForKey(key);
		if (id == kParameterIdNone) {
			continue;
		}
		for (const auto& [name, value] : entries) {
			manager_.setMeta(id, name, value);
		}
	}
	for (const auto& [key, label] : preset.labels) {
		const ParameterId id = params.idForKey(key);
		if (id == kParameterIdNone) {
			continue;
		}
		note(params.setLabel(id, label));
	}
	return first;
}

void MetaExtension::save()
{
	if (!manager_.dirty()) {
		return;
	}
	if (host()->services().persistence().store(kPersistenceKey, manager_.encode()).ok()) {
		manager_.clearDirty();
	}
}

OFXGRIP_NAMESPACE_END

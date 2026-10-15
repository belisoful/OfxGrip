// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/VisibilityExtension.hpp>

OFXGRIP_NAMESPACE_BEGIN

namespace {

/*! Answers rule lookups from the parameter service at the current time. */
class ServiceRuleContext final : public RuleContext {
public:
	explicit ServiceRuleContext(ParameterService& params) : params_(params) {}
	std::optional<Value> value(std::string_view key) const override
	{
		const ParameterId id = params_.idForKey(key);
		if (id == kParameterIdNone) {
			return std::nullopt;
		}
		Value v;
		if (!params_.getCurrent(id, v).ok()) {
			return std::nullopt;
		}
		return v;
	}
	std::optional<int> optionIndex(std::string_view key, std::string_view option) const override
	{
		const ParameterId id = params_.idForKey(key);
		const ParameterSpec* spec = id == kParameterIdNone ? nullptr : params_.spec(id);
		if (spec == nullptr) {
			return std::nullopt;
		}
		const int index = spec->optionIndex(option);
		if (index < 0) {
			return std::nullopt;
		}
		return index;
	}

private:
	ParameterService& params_;
};

}  // namespace

std::vector<EventId> VisibilityExtension::interests() const
{
	return {EventId::CreateInstance, EventId::InstanceChanged, EventId::SyncPrivateData};
}

int VisibilityExtension::priority(EventId id) const
{
	switch (id) {
	case EventId::CreateInstance:
		return kPriorityFrameworkReconstruct;
	case EventId::InstanceChanged:
	case EventId::SyncPrivateData:
		return kPriorityRules;
	default:
		return defaultPriority();
	}
}

bool VisibilityExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	secretIsPermanent_ = host.services().profile().has(Quirk::SecretIsPermanent);
	ParameterService& params = host.services().parameters();
	for (ParameterId id : params.ids()) {
		const ParameterSpec* spec = params.spec(id);
		if (spec == nullptr || (spec->visibleWhen.empty() && spec->enabledWhen.empty())) {
			continue;
		}
		Entry entry;
		entry.id = id;
		if (Error e = Rule::parse(spec->visibleWhen, entry.visible)) {
			parseFailures_[spec->key] = e.describe();
			host.services().log().warning("visibleWhen on " + spec->key + " ignored: " + e.describe());
		}
		if (Error e = Rule::parse(spec->enabledWhen, entry.enabled)) {
			parseFailures_[spec->key] = e.describe();
			host.services().log().warning("enabledWhen on " + spec->key + " ignored: " + e.describe());
		}
		if (entry.visible.empty() && entry.enabled.empty()) {
			continue;
		}
		for (const std::string& key : entry.visible.references()) {
			dependents_[key].insert(id);
		}
		for (const std::string& key : entry.enabled.references()) {
			dependents_[key].insert(id);
		}
		ruled_.push_back(id);
		entries_[id] = std::move(entry);
	}
	return true;
}

void VisibilityExtension::handle(Event& event)
{
	switch (event.id) {
	case EventId::CreateInstance:
	case EventId::SyncPrivateData:
		applyAll();
		break;
	case EventId::InstanceChanged: {
		const auto& change = static_cast<const ChangeEvent&>(event);
		if (change.parameter != kParameterIdNone) {
			applyDependents(change.name);
		}
		break;
	}
	default:
		break;
	}
}

Error VisibilityExtension::applyAll()
{
	Error first;
	for (ParameterId id : ruled_) {
		if (Error e = apply(entries_[id]); e && first.ok()) {
			first = std::move(e);
		}
	}
	return first;
}

Error VisibilityExtension::applyDependents(std::string_view key)
{
	auto it = dependents_.find(std::string(key));
	if (it == dependents_.end()) {
		return Error::none();
	}
	Error first;
	for (ParameterId id : ruled_) {
		if (it->second.count(id) == 0) {
			continue;
		}
		if (Error e = apply(entries_[id]); e && first.ok()) {
			first = std::move(e);
		}
	}
	return first;
}

Error VisibilityExtension::apply(const Entry& entry)
{
	ParameterService& params = host()->services().parameters();
	ServiceRuleContext context(params);
	HostFlags flags;
	if (Error e = params.getFlags(entry.id, flags)) {
		return e;
	}
	HostFlags updated = flags;
	bool enabled = true;
	if (!entry.enabled.empty()) {
		if (Error e = entry.enabled.evaluate(context, enabled)) {
			return e;
		}
	}
	if (!entry.visible.empty()) {
		bool visible = true;
		if (Error e = entry.visible.evaluate(context, visible)) {
			return e;
		}
		if (secretIsPermanent_) {
			// Q-NUKE-010: the secret flag cannot change after describe; disable instead.
			enabled = enabled && visible;
		} else {
			updated.set(HostFlag::Hidden, !visible);
		}
	}
	if (!entry.enabled.empty() || secretIsPermanent_) {
		updated.set(HostFlag::Disabled, !enabled);
	}
	if (updated == flags) {
		return Error::none();
	}
	return params.setFlags(entry.id, updated);
}

OFXGRIP_NAMESPACE_END

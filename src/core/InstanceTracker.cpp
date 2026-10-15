// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/InstanceTracker.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

InstanceRegistry& InstanceRegistry::shared()
{
	static InstanceRegistry registry;
	return registry;
}

void InstanceRegistry::add(std::string_view uuid, EffectHost* instance)
{
	if (instance == nullptr || uuid.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<EffectHost*>& list = table_[std::string(uuid)];
	if (std::find(list.begin(), list.end(), instance) == list.end()) {
		list.push_back(instance);
	}
}

void InstanceRegistry::remove(std::string_view uuid, EffectHost* instance)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = table_.find(std::string(uuid));
	if (it == table_.end()) {
		return;
	}
	it->second.erase(std::remove(it->second.begin(), it->second.end(), instance), it->second.end());
	if (it->second.empty()) {
		table_.erase(it);
	}
}

std::vector<EffectHost*> InstanceRegistry::instances(std::string_view uuid) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = table_.find(std::string(uuid));
	return it == table_.end() ? std::vector<EffectHost*>{} : it->second;
}

std::size_t InstanceRegistry::count(std::string_view uuid) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = table_.find(std::string(uuid));
	return it == table_.end() ? 0 : it->second.size();
}

std::size_t InstanceRegistry::uuidCount() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return table_.size();
}

InstanceTrackerExtension::~InstanceTrackerExtension()
{
	unregister();
}

bool InstanceTrackerExtension::load(EffectHost& host, int index)
{
	if (!Extension::load(host, index)) {
		return false;
	}
	const EffectManifest& manifest = host.manifest();
	uuid_ = manifest.identity.uuid.empty() ? manifest.identity.ofxId : manifest.identity.uuid;
	registered_ = &host;
	InstanceRegistry::shared().add(uuid_, registered_);
	return true;
}

void InstanceTrackerExtension::handle(Event& event)
{
	if (event.id == EventId::DestroyInstance) {
		unregister();
	}
}

void InstanceTrackerExtension::unregister()
{
	if (registered_ != nullptr) {
		InstanceRegistry::shared().remove(uuid_, registered_);
		registered_ = nullptr;
	}
}

OFXGRIP_NAMESPACE_END

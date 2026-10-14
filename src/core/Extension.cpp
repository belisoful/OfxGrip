// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Extension.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

std::string Extension::key() const
{
	std::string base = name();
	if (index_ > 0 || individuate()) {
		base += std::to_string(index_);
	}
	return base;
}

int Extension::priority(EventId) const
{
	return defaultPriority_;
}

bool Extension::load(EffectHost& host, int index)
{
	host_ = &host;
	index_ = index;
	if (!active_) {
		return true;
	}
	for (EventId id : interests()) {
		subscriptions_.push_back(host.bus().subscribe(id, priority(id), [this](Event& e) { handle(e); }));
	}
	return true;
}

void Extension::unload()
{
	subscriptions_.clear();
	host_ = nullptr;
}

ExtensionSystem::~ExtensionSystem()
{
	unloadAll();
}

Extension* ExtensionSystem::load(std::unique_ptr<Extension> extension)
{
	if (!extension) {
		return nullptr;
	}
	const std::string name = extension->name();
	const int index = counts_[name]++;
	if (!extension->load(host_, index)) {
		counts_[name]--;
		return nullptr;
	}
	Extension* raw = extension.get();
	const std::string key = raw->key();
	if (auto existing = extensions_.find(key); existing != extensions_.end()) {
		order_.erase(std::remove(order_.begin(), order_.end(), existing->second.get()), order_.end());
		existing->second->unload();
	}
	extensions_[key] = std::move(extension);
	order_.push_back(raw);
	return raw;
}

void ExtensionSystem::unloadAll()
{
	for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
		(*it)->unload();
	}
	order_.clear();
	extensions_.clear();
	counts_.clear();
}

Extension* ExtensionSystem::find(std::string_view key) const noexcept
{
	auto it = extensions_.find(std::string(key));
	return it == extensions_.end() ? nullptr : it->second.get();
}

Error ExtensionSystem::dispatch(EventId id)
{
	PlainEvent event(id);
	return host_.bus().post(event);
}

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  InstanceTracker.hpp
	@brief A process-wide registry of live effect instances keyed by plug-in UUID, and the
	       extension that registers an instance for its lifetime.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripInstanceTracker`. The registry holds
	non-owning pointers; an entry is removed when the instance is destroyed. Timeline-neighbor
	queries are not available on OpenFX, which gives an instance no timeline position.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The registry. */
class InstanceRegistry {
public:
	static InstanceRegistry& shared();

	void add(std::string_view uuid, EffectHost* instance);
	void remove(std::string_view uuid, EffectHost* instance);
	/*! The live instances for a UUID, in registration order. Never hold the result across a
	    host callback: an instance may be destroyed meanwhile. */
	std::vector<EffectHost*> instances(std::string_view uuid) const;
	std::size_t count(std::string_view uuid) const;
	/*! The number of UUIDs with live instances. */
	std::size_t uuidCount() const;

private:
	mutable std::mutex mutex_;
	std::map<std::string, std::vector<EffectHost*>> table_;
};

/*! The extension. Registers at load under the manifest UUID (or the OFX identifier when the
    manifest has none) and removes itself when destroyed. */
class InstanceTrackerExtension final : public Extension {
public:
	~InstanceTrackerExtension() override;
	std::string name() const override { return "InstanceTracker"; }
	std::vector<EventId> interests() const override { return {EventId::DestroyInstance}; }
	void handle(Event& event) override;
	bool load(EffectHost& host, int index) override;

	const std::string& uuid() const noexcept { return uuid_; }
	/*! The live sibling instances, this one included. */
	std::vector<EffectHost*> siblings() const { return InstanceRegistry::shared().instances(uuid_); }
	std::size_t siblingCount() const { return InstanceRegistry::shared().count(uuid_); }

private:
	void unregister();
	std::string uuid_;
	EffectHost* registered_{nullptr};
};

OFXGRIP_NAMESPACE_END

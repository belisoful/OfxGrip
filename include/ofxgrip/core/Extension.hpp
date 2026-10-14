// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Extension.hpp
	@brief Extensions: observers that add behavior to an effect by handling bus events, and the
	       system that owns and dispatches to them.

	Introduced in OfxGrip 0.1.0. An extension declares the events it handles in `interests()`,
	and the system subscribes `handle` for each at `priority(id)`. The effect owns its extensions
	keyed by `key()`; a second instance of the same class gets an index appended, as FxGrip's
	`extIndividuate` does. The effect host seam (`EffectHost`) gives an extension the services,
	the bus, and the manifest, and an optional pointer to the full effect base.
*/
#pragma once

#include <ofxgrip/core/Events.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

class Effect;

/*! The narrow owner contract the parameter subsystem and the extensions depend on. */
class EffectHost {
public:
	virtual ~EffectHost() = default;
	virtual HostServices& services() = 0;
	virtual EventBus& bus() = 0;
	virtual const EffectManifest& manifest() const = 0;
	/*! The full effect base behind this host, or null for a plain host. */
	virtual Effect* effectBase() { return nullptr; }
};

/*! An extension. */
class Extension {
public:
	virtual ~Extension() = default;

	/*! The registry key, by default the class's `name()`. */
	std::string key() const;
	/*! The extension's own name, used for the key. */
	virtual std::string name() const = 0;
	/*! The events the extension handles. */
	virtual std::vector<EventId> interests() const = 0;
	/*! The delivery priority for an event. The default is `kPriorityDefault`. */
	virtual int priority(EventId id) const;
	/*! Handles one event. */
	virtual void handle(Event& event) = 0;
	/*! Whether every instance of this class gets an indexed key, even the first. */
	virtual bool individuate() const { return false; }

	/*! Binds the extension to a host and subscribes its handlers. Returns false to decline
	    loading, in which case the system drops it. */
	virtual bool load(EffectHost& host, int index);
	/*! Drops the subscriptions. */
	void unload();

	bool active() const noexcept { return active_; }
	/*! Set before loading; an inactive extension subscribes nothing. */
	void setActive(bool on) noexcept { active_ = on; }
	int defaultPriority() const noexcept { return defaultPriority_; }
	void setDefaultPriority(int p) noexcept { defaultPriority_ = p; }
	EffectHost* host() const noexcept { return host_; }
	int index() const noexcept { return index_; }

private:
	EffectHost* host_{nullptr};
	int index_{0};
	bool active_{true};
	int defaultPriority_{kPriorityDefault};
	std::vector<Subscription> subscriptions_;
};

/*! Owns and dispatches to extensions over a host. The effect base owns one; a plug-in on its own
    base can own one directly, as FxGrip's `FxGripExtensionSystem` allows. */
class ExtensionSystem {
public:
	explicit ExtensionSystem(EffectHost& host) : host_(host) {}
	~ExtensionSystem();
	ExtensionSystem(const ExtensionSystem&) = delete;
	ExtensionSystem& operator=(const ExtensionSystem&) = delete;

	/*! Loads an extension. Returns the loaded extension, or null when it declined. */
	Extension* load(std::unique_ptr<Extension> extension);
	/*! Unloads and destroys every extension in reverse load order. */
	void unloadAll();
	Extension* find(std::string_view key) const noexcept;
	template <typename T>
	T* find() const noexcept
	{
		for (const auto& e : order_) {
			if (auto* t = dynamic_cast<T*>(e)) {
				return t;
			}
		}
		return nullptr;
	}
	const std::vector<Extension*>& extensions() const noexcept { return order_; }

	/*! Posts a plain lifecycle event. */
	Error dispatch(EventId id);
	/*! Posts any event. */
	Error dispatch(Event& event, bool reverse = false) { return host_.bus().post(event, reverse); }

private:
	EffectHost& host_;
	std::map<std::string, std::unique_ptr<Extension>> extensions_;
	std::vector<Extension*> order_;
	std::map<std::string, int> counts_;
};

OFXGRIP_NAMESPACE_END

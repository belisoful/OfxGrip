// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Plugin.hpp
	@brief The bundle's plug-in registry, the per-slot entry-point trampolines, and the macro that
	       exports `OfxGetNumberOfPlugins`, `OfxGetPlugin`, and `OfxSetHost`.

	Introduced in OfxGrip 0.1.0. OpenFX's `mainEntry` and `setHost` carry no plug-in identity, so
	each registered factory gets its own pair of trampolines generated from a bounded table. The
	table size is a compile-time constant; registering more factories fails at registration.
*/
#pragma once

#include <ofxgrip/ofx/Effect.hpp>

#include "ofxCore.h"

#include <memory>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#define OFXGRIP_EXPORT extern "C" __declspec(dllexport)
#else
#define OFXGRIP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

OFXGRIP_NAMESPACE_BEGIN

/*! The most plug-ins one bundle can register. */
inline constexpr int kMaxPluginsPerBundle = 32;

class PluginSlot;

/*! The registry. One per bundle, filled by the registration function the bundle macro names. */
class PluginRegistry {
public:
	static PluginRegistry& instance();

	/*! Registers a factory. Returns the slot index of the current major version, or -1 when the
	    table is full or the identifier is already registered. Each entry of the manifest's
	    `deprecatedMajors` takes a further slot that shares the factory, so a host loads the
	    plug-in record a saved project names (OpenFX keys plug-ins by identifier and major). */
	int registerFactory(std::unique_ptr<EffectFactory> factory);
	int count() const;
	/*! The `OfxPlugin` for a slot, or null. */
	OfxPlugin* plugin(int index);
	/*! Records the host handed to `OfxSetHost`, which precedes `OfxGetPlugin` on hosts that call it. */
	void setGlobalHost(const OfxHost* host);
	/*! Runs the registration function once. */
	void ensureRegistered(void (*registerFunction)());
	/*! Drops every slot. For tests. */
	void clear();

	/*! Trampoline targets. */
	void setHost(int index, OfxHost* host);
	OfxStatus mainEntry(int index, const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs);
	/*! The overlay interact's entry for a slot (phase 3). */
	OfxStatus interactEntry(int index, const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs);

private:
	PluginRegistry();
	~PluginRegistry();
	mutable std::mutex mutex_;
	std::vector<std::unique_ptr<PluginSlot>> slots_;
	const OfxHost* globalHost_{nullptr};
	bool registered_{false};
};

namespace detail {
template <int N>
void slotSetHost(OfxHost* host)
{
	PluginRegistry::instance().setHost(N, host);
}
template <int N>
OfxStatus slotMainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	return PluginRegistry::instance().mainEntry(N, action, handle, inArgs, outArgs);
}
using SetHostFn = void (*)(OfxHost*);
using MainEntryFn = OfxStatus (*)(const char*, const void*, OfxPropertySetHandle, OfxPropertySetHandle);
SetHostFn setHostTrampoline(int index) noexcept;
MainEntryFn mainEntryTrampoline(int index) noexcept;
}  // namespace detail

OFXGRIP_NAMESPACE_END

/*! Exports the three OpenFX symbols for a bundle. `registerFunction` is a `void()` that calls
    `PluginRegistry::instance().registerFactory(...)` once per effect. */
#define OFXGRIP_BUNDLE(registerFunction)                                                                      \
	OFXGRIP_EXPORT int OfxGetNumberOfPlugins(void)                                                            \
	{                                                                                                         \
		::ofxgrip::PluginRegistry::instance().ensureRegistered(registerFunction);                             \
		return ::ofxgrip::PluginRegistry::instance().count();                                                 \
	}                                                                                                         \
	OFXGRIP_EXPORT OfxPlugin* OfxGetPlugin(int nth)                                                           \
	{                                                                                                         \
		::ofxgrip::PluginRegistry::instance().ensureRegistered(registerFunction);                             \
		return ::ofxgrip::PluginRegistry::instance().plugin(nth);                                             \
	}                                                                                                         \
	OFXGRIP_EXPORT OfxStatus OfxSetHost(const OfxHost* host)                                                  \
	{                                                                                                         \
		::ofxgrip::PluginRegistry::instance().setGlobalHost(host);                                            \
		return kOfxStatOK;                                                                                    \
	}

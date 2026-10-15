// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  OfxHostServices.hpp
	@brief The `HostServices` implementation over the OpenFX suites, plus the file logger and the
	       per-OS directories the adapter uses before an instance exists.

	Introduced in OfxGrip 0.1.0. One `OfxHostServices` exists per effect instance. It binds the
	manifest's parameters to their OpenFX handles by key, reads and writes values by type, keeps
	the persistence blobs in the hidden state parameter, validates the message suites, and runs
	parallel work through the host's thread suite or its own pool, as the profile decides.
*/
#pragma once

#include <ofxgrip/core/Events.hpp>
#include <ofxgrip/core/HostProfile.hpp>
#include <ofxgrip/core/HostServices.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/ofx/PropertySet.hpp>
#include <ofxgrip/ofx/Suites.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! A log that writes to a file under the per-OS log directory and to stderr. Thread-safe. */
class FileLogger final : public LogService {
public:
	explicit FileLogger(std::string name);
	~FileLogger() override;
	void log(LogLevel level, std::string_view message) override;
	void setLevel(LogLevel level) noexcept { level_ = level; }
	LogLevel level() const noexcept { return level_; }
	const std::filesystem::path& path() const noexcept { return path_; }
	/*! Mirrors every line to stderr as well as the file. Off by default. */
	void setEcho(bool on) noexcept { echo_ = on; }

private:
	std::mutex mutex_;
	std::string name_;
	std::filesystem::path path_;
	LogLevel level_{LogLevel::Info};
	bool echo_{false};
	void* file_{nullptr};
};

/*! The per-OS log directory: `~/Library/Logs/OfxGrip` on macOS, `%LOCALAPPDATA%\OfxGrip\Logs`
    on Windows, `$XDG_STATE_HOME/ofxgrip/log` or `~/.local/state/ofxgrip/log` on Linux. The
    `OFXGRIP_LOG_DIR` environment variable, read once at load, overrides it for tests and tools. */
std::filesystem::path defaultLogDirectory();
/*! The per-OS cache directory for a plug-in identifier. */
std::filesystem::path defaultCacheDirectory(std::string_view pluginId);
/*! The per-OS user support directory for a plug-in identifier, where user presets live. */
std::filesystem::path defaultUserSupportDirectory(std::string_view pluginId);

/*! One bound parameter. */
struct ParameterBinding {
	ParameterSpec spec;
	OfxParamHandle handle{nullptr};
	PropertySet properties;
};

/*! The services. */
class OfxHostServices final : public HostServices {
public:
	OfxHostServices(const Suites& suites,
	                const HostProfile& profile,
	                const EffectManifest& manifest,
	                Identity identity,
	                EventBus& bus,
	                LogService& logger,
	                OfxImageEffectHandle instance);
	~OfxHostServices() override;

	const Identity& identity() const override { return identity_; }
	const HostProfile& profile() const override { return profile_; }
	/*! The phase of the calling thread. A host may call a main-thread action while render threads
	    are inside render actions, so the phase is tracked per thread. */
	Phase phase() const override { return threadPhase_; }
	ParameterService& parameters() override;
	PersistenceService& persistence() override;
	FeedbackService& feedback() override;
	ThreadingService& threading() override;
	LogService& log() override { return logger_; }
	TimeService& time() override;
	PathService& paths() override;

	/*! Fetches the parameter and clip handles. Called once after the instance exists. */
	Error bind();
	void setPhase(Phase phase) noexcept { threadPhase_ = phase; }
	/*! Restores the calling thread's previous phase on scope exit. */
	class PhaseScope {
	public:
		PhaseScope(OfxHostServices& services, Phase phase) : previous_(services.phase()) { services.setPhase(phase); }
		~PhaseScope() { threadPhase_ = previous_; }

	private:
		Phase previous_;
	};

	const Suites& suites() const noexcept { return suites_; }
	EventBus& bus() noexcept { return bus_; }
	const EffectManifest& manifest() const noexcept { return manifest_; }
	OfxImageEffectHandle instanceHandle() const noexcept { return instance_; }
	PropertySet instanceProperties() const;
	OfxParamSetHandle paramSet() const noexcept { return paramSet_; }
	OfxImageClipHandle clipHandle(std::string_view name) const noexcept;
	PropertySet clipProperties(std::string_view name) const;
	const ParameterBinding* binding(ParameterId id) const noexcept;
	const std::map<ParameterId, ParameterBinding>& bindings() const noexcept { return bindings_; }
	/*! The bound parameter IDs in declaration order. */
	const std::vector<ParameterId>& orderedIds() const noexcept { return orderedIds_; }
	/*! The Resolve page the instance was created on, or empty. */
	const std::string& resolvePage() const noexcept { return resolvePage_; }
	void setResolvePage(std::string page) { resolvePage_ = std::move(page); }

private:
	struct Impl;
	Suites suites_;
	const HostProfile& profile_;
	const EffectManifest& manifest_;
	Identity identity_;
	EventBus& bus_;
	LogService& logger_;
	OfxImageEffectHandle instance_;
	OfxParamSetHandle paramSet_{nullptr};
	static thread_local Phase threadPhase_;
	std::map<ParameterId, ParameterBinding> bindings_;
	std::vector<ParameterId> orderedIds_;
	std::map<std::string, ParameterId> idsByKey_;
	std::map<std::string, OfxImageClipHandle> clips_;
	std::string resolvePage_;
	std::unique_ptr<Impl> impl_;
};

OFXGRIP_NAMESPACE_END

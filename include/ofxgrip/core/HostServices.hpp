// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  HostServices.hpp
	@brief The host seam: the abstract services an effect, its parameters, and its extensions
	       reach the host through.

	Introduced in OfxGrip 0.1.0. This is FxGrip's `FxGripEffectHost` plus `FxGripAPIAccessing`
	retyped as the neutral interface `Research/Comparison.md` §5.5 proposes. The OFX adapter
	implements every service over the suites; the mini host and the tests implement them over
	fakes. Each service names the phases in which it is valid, and the adapter checks the phase in
	debug builds.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Flags.hpp>
#include <ofxgrip/core/HostProfile.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The host callback an effect is inside. Parameter writes are legal only in
    `CreateInstance`, `InstanceChanged`, `SyncPrivateData`, `Interact`, and `EditScope`
    (`ofxParameter.rst` §"Setting Parameters"). */
enum class Phase : std::uint8_t {
	None = 0,
	Load,
	Describe,
	DescribeInContext,
	CreateInstance,
	InstanceChanged,
	SyncPrivateData,
	Interact,
	EditScope,
	ClipPreferences,
	Geometry,        /*!< RoD, RoI, frames needed, identity, time domain. */
	Render,
	Destroy,
	Unload,
};

/*! The name of a phase. */
std::string_view toString(Phase phase) noexcept;
/*! Whether parameter writes are legal in a phase. */
bool phaseAllowsParameterWrites(Phase phase) noexcept;

/*! The plug-in's and the instance's identity. The plug-in identity comes from the manifest,
    which ends FxGrip's dependency on a private host member. */
struct Identity {
	std::string pluginId;         /*!< The OFX identifier. */
	std::string pluginUuid;       /*!< The manifest UUID. */
	Version pluginVersion;
	std::string instanceId;       /*!< Unique per instance in this process. */
	std::filesystem::path bundlePath;
};

/*! Typed parameter access at a time. Every call posts the matching API event, so observers can
    localize labels, mirror values, or veto writes. */
class ParameterService {
public:
	virtual ~ParameterService() = default;

	/*! The value at a time. */
	virtual Error get(ParameterId id, Time time, Value& out) = 0;
	/*! The value at the current time, where the host defines one. */
	virtual Error getCurrent(ParameterId id, Value& out) = 0;
	/*! Writes a keyframe at a time, or the value when the parameter does not animate. */
	virtual Error set(ParameterId id, Time time, const Value& value) = 0;
	/*! Writes the value at the current time. */
	virtual Error setCurrent(ParameterId id, const Value& value) = 0;
	virtual Error getFlags(ParameterId id, HostFlags& out) = 0;
	virtual Error setFlags(ParameterId id, HostFlags flags) = 0;
	virtual Error getLabel(ParameterId id, std::string& out) = 0;
	virtual Error setLabel(ParameterId id, std::string_view label) = 0;
	virtual Error setHint(ParameterId id, std::string_view hint) = 0;
	virtual Error setOptions(ParameterId id, const std::vector<ChoiceOption>& options) = 0;
	virtual Error setRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) = 0;
	virtual Error setDisplayRange(ParameterId id, const std::vector<double>& minimum, const std::vector<double>& maximum) = 0;
	virtual Error keyframeCount(ParameterId id, int& out) = 0;
	virtual Error keyframeTime(ParameterId id, int index, Time& out) = 0;
	virtual Error deleteAllKeyframes(ParameterId id) = 0;
	/*! Whether a write is legal right now. */
	virtual bool canWrite() const = 0;
	/*! The manifest record for an ID, or null. */
	virtual const ParameterSpec* spec(ParameterId id) const = 0;
	/*! The ID for a key, or `kParameterIdNone`. */
	virtual ParameterId idForKey(std::string_view key) const = 0;
	/*! Every parameter the host holds for this instance, in declaration order. */
	virtual std::vector<ParameterId> ids() const = 0;
};

/*! Keyed blobs that persist with the instance in the host document. The OFX adapter stores
    them in a hidden string parameter and writes them during `SyncPrivateData`. */
class PersistenceService {
public:
	virtual ~PersistenceService() = default;
	virtual Error store(std::string_view key, const Bytes& data) = 0;
	virtual Error load(std::string_view key, Bytes& out) = 0;
	virtual Error remove(std::string_view key) = 0;
	virtual std::vector<std::string> keys() = 0;
	/*! Writes every stored blob to the host. Called on `Flush` and `SyncPrivateData`. */
	virtual Error commit() = 0;
};

/*! The kind of a message. */
enum class MessageKind : std::uint8_t {
	Log = 0,
	Message,
	Warning,
	Error,
	Question,
};

/*! Messages, persistent messages, progress, and the abort check. */
class FeedbackService {
public:
	virtual ~FeedbackService() = default;
	/*! Shows a message. For `Question`, the result is `ErrorCode::None` for yes and `Value` for no. */
	virtual Error message(MessageKind kind, std::string_view id, std::string_view text) = 0;
	/*! Sets the persistent message on the instance where the host supports it. Falls back to a
	    plain message otherwise (Q-RESOLVE-007). */
	virtual Error setPersistentMessage(MessageKind kind, std::string_view id, std::string_view text) = 0;
	virtual Error clearPersistentMessage() = 0;
	/*! Whether the host asked the render to stop. Render loops poll this per band. */
	virtual bool abortRequested() = 0;
	virtual Error progressStart(std::string_view label) = 0;
	virtual Error progressUpdate(double fraction) = 0;
	virtual Error progressEnd() = 0;
};

/*! Parallel work. The OFX adapter uses the host's multithread suite unless the profile says it
    is a stub (Q-RESOLVE-003) or missing (Q-OFX-ANY-013), in which case it runs its own pool. */
class ThreadingService {
public:
	virtual ~ThreadingService() = default;
	virtual unsigned cpuCount() = 0;
	/*! Runs `work(index, count)` for `index` in `[0, count)` across threads and waits for all. */
	virtual Error parallelFor(unsigned count, const std::function<void(unsigned index, unsigned count)>& work) = 0;
	virtual bool isSpawnedThread() = 0;
};

/*! A log line's level. */
enum class LogLevel : std::uint8_t {
	Debug = 0,
	Info,
	Warning,
	Error,
};

/*! Structured logging with a per-host sink. */
class LogService {
public:
	virtual ~LogService() = default;
	virtual void log(LogLevel level, std::string_view message) = 0;
	void debug(std::string_view m) { log(LogLevel::Debug, m); }
	void info(std::string_view m) { log(LogLevel::Info, m); }
	void warning(std::string_view m) { log(LogLevel::Warning, m); }
	void error(std::string_view m) { log(LogLevel::Error, m); }
};

/*! The host's time and the project's geometry. */
class TimeService {
public:
	virtual ~TimeService() = default;
	/*! The host's current time, where the host defines one; 0 otherwise. */
	virtual Time currentTime() = 0;
	virtual std::pair<Time, Time> timeBounds() = 0;
	virtual double frameRate() = 0;
	virtual Point2D projectSize() = 0;
	virtual Point2D projectOffset() = 0;
	virtual double projectPixelAspect() = 0;
};

/*! Per-OS directories. */
class PathService {
public:
	virtual ~PathService() = default;
	/*! A machine-local cache directory for this plug-in, created on demand. */
	virtual std::filesystem::path cacheDirectory() = 0;
	/*! The per-user support directory for this plug-in, where user presets live. */
	virtual std::filesystem::path userSupportDirectory() = 0;
	/*! The directory for log files. */
	virtual std::filesystem::path logDirectory() = 0;
};

/*! The complete seam. */
class HostServices {
public:
	virtual ~HostServices() = default;
	virtual const Identity& identity() const = 0;
	virtual const HostProfile& profile() const = 0;
	virtual Phase phase() const = 0;
	virtual ParameterService& parameters() = 0;
	virtual PersistenceService& persistence() = 0;
	virtual FeedbackService& feedback() = 0;
	virtual ThreadingService& threading() = 0;
	virtual LogService& log() = 0;
	virtual TimeService& time() = 0;
	virtual PathService& paths() = 0;
};

/*! An RAII window for parameter writes outside a host callback: OfxGrip's form of FxGrip's
    out-of-band access. On OFX it brackets `paramEditBegin`/`paramEditEnd`, so the writes form one
    undo step. The destructor closes the window and posts `Flush` when `flush` was requested. */
class EditScope {
public:
	EditScope(HostServices& services, std::string_view name, bool flushOnClose = true);
	~EditScope();
	EditScope(const EditScope&) = delete;
	EditScope& operator=(const EditScope&) = delete;
	bool open() const noexcept { return open_; }
	void close();

	/*! The hook the adapter installs. */
	struct Hooks {
		std::function<Error(std::string_view)> begin;
		std::function<Error()> end;
		std::function<void()> flush;
	};
	static void setHooks(HostServices& services, Hooks hooks);

private:
	HostServices& services_;
	bool flush_;
	bool open_{false};
};

OFXGRIP_NAMESPACE_END

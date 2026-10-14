// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Events.hpp
	@brief The event bus: the closed event set, the payload types, and priority-ordered delivery
	       with veto and override.

	Introduced in OfxGrip 0.1.0. This is FxGrip's extension bus (`NSPriorityNotificationCenter`
	plus the `FxTileableEffect…Name` and `FxGripNotifyAPI_…Name` notifications) in C++. Each
	OpenFX action the adapter handles posts one lifecycle event, and every parameter read or
	write through `ParameterService` posts API-traffic events. Observers run in ascending priority
	(−20 first, 10 by default, 20 last). An observer vetoes by setting `event.error`, and a
	`…Pre` observer short-circuits a host call by setting `event.result`.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Flags.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The closed set of events. Lifecycle events follow the OpenFX action order; API events wrap
    parameter traffic. */
enum class EventId : std::uint8_t {
	Load = 0,
	Describe,
	DescribeInContext,
	CreateInstance,
	AddParameters,         /*!< Observers may append or edit `ManifestEvent::parameters` before lowering. */
	FinishSetup,
	AddedToDocument,
	BeginInstanceChanged,
	InstanceChanged,
	EndInstanceChanged,
	ParameterClicked,
	Flush,                 /*!< Observers persist dirty state through the persistence service. */
	SyncPrivateData,
	BuildSnapshot,         /*!< Observers add their render state to the snapshot builder. */
	RegionOfDefinition,
	RegionsOfInterest,
	FramesNeeded,
	ClipPreferences,
	IsIdentity,
	BeginSequenceRender,
	Render,
	EndSequenceRender,
	PurgeCaches,
	DestroyInstance,
	Unload,

	ApiParameterAddPre,
	ApiParameterAdd,
	ApiGetValue,
	ApiSetValuePre,
	ApiSetValue,
	ApiGetLabel,
	ApiSetLabelPre,
	ApiSetLabel,
	ApiGetFlags,
	ApiSetFlagsPre,
	ApiSetFlags,
	ApiSetOptionsPre,
	ApiSetOptions,

	Count,
};

/*! The name of an event. */
std::string_view toString(EventId id) noexcept;

/*! Delivery priorities. Lower runs first. */
inline constexpr int kPriorityFirst = -20;
inline constexpr int kPriorityDefault = 10;
inline constexpr int kPriorityLast = 20;
/*! The priorities the framework reserves for its own bookkeeping, as FxGrip reserves −18, −17,
    and −14. Extensions that must run after the parameter model is current use values above
    `kPriorityFrameworkFlush`; those that must precede capture use values below
    `kPriorityFrameworkCapture`. */
inline constexpr int kPriorityFrameworkCapture = -18;
inline constexpr int kPriorityFrameworkReconstruct = -17;
inline constexpr int kPriorityFrameworkFlush = -14;

/*! The base of every event. An observer sets `error` to veto, or `result` on a `…Pre` event to
    answer in place of the host. */
struct Event {
	explicit Event(EventId eventId) : id(eventId) {}
	virtual ~Event() = default;

	EventId id;
	Error error;
	std::optional<Value> result;
	bool vetoed() const noexcept { return !error.ok(); }
};

/*! A lifecycle event that carries no payload beyond the base. */
struct PlainEvent : Event {
	using Event::Event;
};

/*! `AddParameters`: the mutable parameter list before lowering. */
struct ManifestEvent : Event {
	ManifestEvent(EventId eventId, std::vector<ParameterSpec>& list) : Event(eventId), parameters(list) {}
	std::vector<ParameterSpec>& parameters;
};

/*! The reason a parameter changed. */
enum class ChangeReason : std::uint8_t {
	UserEdited = 0,
	PluginEdited,
	TimeChanged,
};

/*! `InstanceChanged`, `BeginInstanceChanged`, `EndInstanceChanged`, and `ParameterClicked`. For
    a clip change `parameter` is `kParameterIdNone` and `name` is the clip. */
struct ChangeEvent : Event {
	ChangeEvent(EventId eventId, ParameterId parameterId, std::string changedName, Time changeTime, ChangeReason why)
	: Event(eventId), parameter(parameterId), name(std::move(changedName)), time(changeTime), reason(why)
	{
	}
	ParameterId parameter;
	std::string name;
	Time time;
	ChangeReason reason;
	Point2D renderScale{1, 1};
};

/*! The API-traffic events. `parameter` is the affected parameter. For value events `time` is
    the read or write time and `value` the value; for label events `label`; for flag events
    `hostFlags`; for option events `options`. A `…Pre` observer rewrites the mutable members. */
struct ApiEvent : Event {
	ApiEvent(EventId eventId, ParameterId parameterId) : Event(eventId), parameter(parameterId) {}
	ParameterId parameter;
	Time time{0};
	Value value;
	std::string label;
	HostFlags hostFlags;
	std::vector<ChoiceOption> options;
	const ParameterSpec* spec{nullptr};
};

class RenderSnapshotBuilder;

/*! `BuildSnapshot`: observers add their render state to the builder. */
struct SnapshotEvent : Event {
	explicit SnapshotEvent(RenderSnapshotBuilder& b) : Event(EventId::BuildSnapshot), builder(b) {}
	RenderSnapshotBuilder& builder;
};

/*! A handler. Handlers run synchronously on the posting thread. */
using EventHandler = std::function<void(Event&)>;

/*! A subscription handle. Destroying it or calling `reset` unsubscribes. */
class Subscription {
public:
	Subscription() = default;
	Subscription(Subscription&& other) noexcept;
	Subscription& operator=(Subscription&& other) noexcept;
	Subscription(const Subscription&) = delete;
	Subscription& operator=(const Subscription&) = delete;
	~Subscription();
	void reset();
	bool active() const noexcept { return bus_ != nullptr; }

private:
	friend class EventBus;
	Subscription(class EventBus* bus, std::uint64_t token) : bus_(bus), token_(token) {}
	class EventBus* bus_{nullptr};
	std::uint64_t token_{0};
};

/*! The bus. One per effect instance. Thread-safe: subscriptions change under a mutex, and
    delivery iterates a snapshot of the handler list, so a handler may subscribe or unsubscribe
    during delivery. */
class EventBus {
public:
	EventBus();
	~EventBus();
	EventBus(const EventBus&) = delete;
	EventBus& operator=(const EventBus&) = delete;

	/*! Subscribes a handler at a priority. The handle keeps the subscription alive. */
	[[nodiscard]] Subscription subscribe(EventId id, int priority, EventHandler handler);
	/*! Delivers an event in ascending priority order, or descending when `reverse` is set.
	    Delivery stops at the first veto. Returns the event's error. */
	Error post(Event& event, bool reverse = false);
	/*! The number of handlers for an event. */
	std::size_t handlerCount(EventId id) const;

private:
	friend class Subscription;
	void unsubscribe(std::uint64_t token);

	struct Entry {
		std::uint64_t token;
		std::uint64_t order;
		int priority;
		EventHandler handler;
	};
	mutable std::mutex mutex_;
	std::array<std::vector<Entry>, static_cast<std::size_t>(EventId::Count)> handlers_;
	std::uint64_t nextToken_{1};
};

OFXGRIP_NAMESPACE_END

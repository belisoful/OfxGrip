// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Events.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

std::string_view toString(EventId id) noexcept
{
	switch (id) {
	case EventId::Load: return "Load";
	case EventId::Describe: return "Describe";
	case EventId::DescribeInContext: return "DescribeInContext";
	case EventId::CreateInstance: return "CreateInstance";
	case EventId::AddParameters: return "AddParameters";
	case EventId::FinishSetup: return "FinishSetup";
	case EventId::AddedToDocument: return "AddedToDocument";
	case EventId::BeginInstanceChanged: return "BeginInstanceChanged";
	case EventId::InstanceChanged: return "InstanceChanged";
	case EventId::EndInstanceChanged: return "EndInstanceChanged";
	case EventId::ParameterClicked: return "ParameterClicked";
	case EventId::Flush: return "Flush";
	case EventId::SyncPrivateData: return "SyncPrivateData";
	case EventId::BuildSnapshot: return "BuildSnapshot";
	case EventId::RegionOfDefinition: return "RegionOfDefinition";
	case EventId::RegionsOfInterest: return "RegionsOfInterest";
	case EventId::FramesNeeded: return "FramesNeeded";
	case EventId::ClipPreferences: return "ClipPreferences";
	case EventId::IsIdentity: return "IsIdentity";
	case EventId::BeginSequenceRender: return "BeginSequenceRender";
	case EventId::Render: return "Render";
	case EventId::EndSequenceRender: return "EndSequenceRender";
	case EventId::PurgeCaches: return "PurgeCaches";
	case EventId::DestroyInstance: return "DestroyInstance";
	case EventId::Unload: return "Unload";
	case EventId::ApiParameterAddPre: return "ApiParameterAddPre";
	case EventId::ApiParameterAdd: return "ApiParameterAdd";
	case EventId::ApiGetValue: return "ApiGetValue";
	case EventId::ApiSetValuePre: return "ApiSetValuePre";
	case EventId::ApiSetValue: return "ApiSetValue";
	case EventId::ApiGetLabel: return "ApiGetLabel";
	case EventId::ApiSetLabelPre: return "ApiSetLabelPre";
	case EventId::ApiSetLabel: return "ApiSetLabel";
	case EventId::ApiGetFlags: return "ApiGetFlags";
	case EventId::ApiSetFlagsPre: return "ApiSetFlagsPre";
	case EventId::ApiSetFlags: return "ApiSetFlags";
	case EventId::ApiSetOptionsPre: return "ApiSetOptionsPre";
	case EventId::ApiSetOptions: return "ApiSetOptions";
	case EventId::Count: break;
	}
	return "Unknown";
}

Subscription::Subscription(Subscription&& other) noexcept : bus_(other.bus_), token_(other.token_)
{
	other.bus_ = nullptr;
	other.token_ = 0;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept
{
	if (this != &other) {
		reset();
		bus_ = other.bus_;
		token_ = other.token_;
		other.bus_ = nullptr;
		other.token_ = 0;
	}
	return *this;
}

Subscription::~Subscription()
{
	reset();
}

void Subscription::reset()
{
	if (bus_ != nullptr) {
		bus_->unsubscribe(token_);
		bus_ = nullptr;
		token_ = 0;
	}
}

EventBus::EventBus() = default;
EventBus::~EventBus() = default;

Subscription EventBus::subscribe(EventId id, int priority, EventHandler handler)
{
	std::lock_guard<std::mutex> lock(mutex_);
	const std::uint64_t token = nextToken_++;
	auto& list = handlers_[static_cast<std::size_t>(id)];
	list.push_back(Entry{token, token, priority, std::move(handler)});
	std::stable_sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
		return a.priority < b.priority || (a.priority == b.priority && a.order < b.order);
	});
	return Subscription(this, token);
}

void EventBus::unsubscribe(std::uint64_t token)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& list : handlers_) {
		auto it = std::remove_if(list.begin(), list.end(), [token](const Entry& e) { return e.token == token; });
		if (it != list.end()) {
			list.erase(it, list.end());
			return;
		}
	}
}

Error EventBus::post(Event& event, bool reverse)
{
	std::vector<EventHandler> snapshot;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const auto& list = handlers_[static_cast<std::size_t>(event.id)];
		snapshot.reserve(list.size());
		for (const Entry& entry : list) {
			snapshot.push_back(entry.handler);
		}
	}
	if (reverse) {
		std::reverse(snapshot.begin(), snapshot.end());
	}
	for (const EventHandler& handler : snapshot) {
		handler(event);
		if (event.vetoed()) {
			break;
		}
	}
	return event.error;
}

std::size_t EventBus::handlerCount(EventId id) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return handlers_[static_cast<std::size_t>(id)].size();
}

OFXGRIP_NAMESPACE_END

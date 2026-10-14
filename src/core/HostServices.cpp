// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/HostServices.hpp>

#include <map>
#include <mutex>

OFXGRIP_NAMESPACE_BEGIN

std::string_view toString(Phase phase) noexcept
{
	switch (phase) {
	case Phase::None: return "None";
	case Phase::Load: return "Load";
	case Phase::Describe: return "Describe";
	case Phase::DescribeInContext: return "DescribeInContext";
	case Phase::CreateInstance: return "CreateInstance";
	case Phase::InstanceChanged: return "InstanceChanged";
	case Phase::SyncPrivateData: return "SyncPrivateData";
	case Phase::Interact: return "Interact";
	case Phase::EditScope: return "EditScope";
	case Phase::ClipPreferences: return "ClipPreferences";
	case Phase::Geometry: return "Geometry";
	case Phase::Render: return "Render";
	case Phase::Destroy: return "Destroy";
	case Phase::Unload: return "Unload";
	}
	return "None";
}

bool phaseAllowsParameterWrites(Phase phase) noexcept
{
	switch (phase) {
	case Phase::CreateInstance:
	case Phase::InstanceChanged:
	case Phase::SyncPrivateData:
	case Phase::Interact:
	case Phase::EditScope:
		return true;
	default:
		return false;
	}
}

namespace {

std::mutex& hooksMutex()
{
	static std::mutex m;
	return m;
}

std::map<HostServices*, EditScope::Hooks>& hooksTable()
{
	static std::map<HostServices*, EditScope::Hooks> table;
	return table;
}

EditScope::Hooks hooksFor(HostServices& services)
{
	std::lock_guard<std::mutex> lock(hooksMutex());
	auto it = hooksTable().find(&services);
	return it == hooksTable().end() ? EditScope::Hooks{} : it->second;
}

}  // namespace

void EditScope::setHooks(HostServices& services, Hooks hooks)
{
	std::lock_guard<std::mutex> lock(hooksMutex());
	if (!hooks.begin && !hooks.end && !hooks.flush) {
		hooksTable().erase(&services);
	} else {
		hooksTable()[&services] = std::move(hooks);
	}
}

EditScope::EditScope(HostServices& services, std::string_view name, bool flushOnClose)
: services_(services), flush_(flushOnClose)
{
	const Hooks hooks = hooksFor(services_);
	if (hooks.begin) {
		open_ = hooks.begin(name).ok();
	} else {
		open_ = true;
	}
}

EditScope::~EditScope()
{
	close();
}

void EditScope::close()
{
	if (!open_) {
		return;
	}
	open_ = false;
	const Hooks hooks = hooksFor(services_);
	if (flush_ && hooks.flush) {
		hooks.flush();
	}
	if (hooks.end) {
		hooks.end();
	}
}

OFXGRIP_NAMESPACE_END

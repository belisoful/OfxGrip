// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/AnalysisExtension.hpp>

#include <ofxgrip/core/Preset.hpp>

#include <cmath>

OFXGRIP_NAMESPACE_BEGIN

int AnalysisExtension::priority(EventId id) const
{
	switch (id) {
	case EventId::CreateInstance:
		return kPriorityFrameworkCapture;
	case EventId::Flush:
		return kPriorityFrameworkFlush;
	default:
		return defaultPriority();
	}
}

void AnalysisExtension::handle(Event& event)
{
	switch (event.id) {
	case EventId::CreateInstance:
		loadStored();
		break;
	case EventId::Flush:
		save();
		break;
	default:
		break;
	}
}

void AnalysisExtension::loadStored()
{
	HostServices& services = host()->services();
	const std::filesystem::path spill = services.paths().cacheDirectory() / "analysis";
	Bytes stored;
	if (services.persistence().load(kPersistenceKey, stored).ok()) {
		FrameData loaded;
		if (Error e = FrameData::decode(stored, spill, loaded)) {
			services.log().warning("The stored analysis is unreadable and was discarded: " + e.describe());
		} else {
			data_ = std::move(loaded);
			loadedFromDocument_ = true;
		}
	}
	data_.setSpillDirectory(spill);
	if (data_.instanceUuid.empty()) {
		data_.instanceUuid = Preset::makeUuid();
		dirtyOverride_ = true;
	}
	const double rate = services.time().frameRate();
	if (!loadedFromDocument_ && rate > 0.0) {
		data_.frameDuration = 1.0 / rate;
	}
}

std::optional<Bytes> AnalysisExtension::recordAt(Time time) const
{
	// OpenFX times are frames; the store keys by frame index.
	return data_.latestAtOrBefore(static_cast<long>(std::floor(time + 1e-9)));
}

void AnalysisExtension::save()
{
	if (!data_.dirty() && !dirtyOverride_) {
		return;
	}
	if (host()->services().persistence().store(kPersistenceKey, data_.encode()).ok()) {
		data_.clearDirty();
		dirtyOverride_ = false;
	}
}

OFXGRIP_NAMESPACE_END

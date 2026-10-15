// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  AnalysisExtension.hpp
	@brief The framework extension that owns an instance's per-frame analysis store: loading it
	       from the document, pointing its spill at the cache folder, and saving it on flush.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripAnalysis`. The effect base loads it when
	the manifest declares an analyzer control or sets the `analysis` property. The store persists
	through the persistence service under `ofxgrip.analysis`; large records spill to the
	plug-in's cache directory under a UUID the store carries, so the files are found again when
	the document reopens on the same machine.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/FrameData.hpp>

#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The extension. */
class AnalysisExtension final : public Extension {
public:
	/*! The persistence key the encoded store is stored under. */
	static constexpr std::string_view kPersistenceKey = "ofxgrip.analysis";

	std::string name() const override { return "Analysis"; }
	std::vector<EventId> interests() const override { return {EventId::CreateInstance, EventId::Flush}; }
	int priority(EventId id) const override;
	void handle(Event& event) override;

	FrameData& data() noexcept { return data_; }
	const FrameData& data() const noexcept { return data_; }
	/*! Whether a stored store was found on creation. */
	bool loadedFromDocument() const noexcept { return loadedFromDocument_; }
	/*! The record for a time: the latest at or before its frame index, or none. */
	std::optional<Bytes> recordAt(Time time) const;
	/*! Marks the store for saving on the next flush. */
	void markDirty() noexcept { dirtyOverride_ = true; }

private:
	void loadStored();
	void save();

	FrameData data_;
	bool loadedFromDocument_{false};
	bool dirtyOverride_{false};
};

OFXGRIP_NAMESPACE_END

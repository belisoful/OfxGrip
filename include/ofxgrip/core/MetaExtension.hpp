// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MetaExtension.hpp
	@brief The framework extension that owns an instance's meta manager: seeding from the
	       manifest, loading and saving through the persistence service, target presets, and
	       momentary reset values.

	Introduced in OfxGrip 0.2.0. This is FxGrip's `FxGripMeta`. The effect base loads it on every
	instance unless the manifest property `manageMeta` is false. Its priorities follow FxGrip's:
	the stored manager loads at `kPriorityFrameworkCapture` so every later observer reads it,
	target presets apply at −10 on a change, and the store flushes at `kPriorityFrameworkFlush`.
*/
#pragma once

#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/MetaManager.hpp>

#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The extension. */
class MetaExtension final : public Extension {
public:
	/*! The persistence key the encoded manager is stored under. */
	static constexpr std::string_view kPersistenceKey = "ofxgrip.meta";
	/*! The event priority target presets apply at. */
	static constexpr int kPriorityTargetPresets = -10;

	std::string name() const override { return "Meta"; }
	std::vector<EventId> interests() const override;
	int priority(EventId id) const override;
	void handle(Event& event) override;
	bool load(EffectHost& host, int index) override;

	MetaManager& manager() noexcept { return manager_; }
	const MetaManager& manager() const noexcept { return manager_; }
	/*! Whether a stored manager was found and merged on creation. */
	bool loadedFromDocument() const noexcept { return loadedFromDocument_; }

	/*! Applies one target preset to the instance in the fixed section order. Public so a presets
	    control or a test can apply one directly. */
	Error applyTargetPreset(const TargetPreset& preset);

private:
	void seedFromManifest();
	void loadStored();
	void onChange(const ChangeEvent& change);
	void save();

	MetaManager manager_;
	bool loadedFromDocument_{false};
};

OFXGRIP_NAMESPACE_END

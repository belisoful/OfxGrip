// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Presets.hpp
	@brief Capturing, applying, listing, and filing presets for one effect instance, and the
	       menu a presets control shows.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripPresetsAPI_v1` over the host services.
	Capture reads every state parameter's current value plus its tags and meta; apply writes the
	sections in FxGrip's order (values, tags, meta) inside the preset's tag boundary. Presets come
	from three places, listed plug-in then user: the manifest's presets table, `.fxpreset` files
	under the bundle's `Contents/Resources/Presets/<tag>/`, and `.fxpreset` files under the user
	support folder's `Presets/<tag>/`.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/MetaManager.hpp>
#include <ofxgrip/core/Preset.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! What an apply may relax. */
struct PresetApplyOptions {
	bool ignoreCompatibility{false};   /*!< Skip the plug-in UUID check. */
	bool ignoreTagBoundary{false};     /*!< Write a parameter that does not carry the preset's tag. */
	bool ignoreTags{false};            /*!< Skip the tags section. */
	bool ignoreMeta{false};            /*!< Skip the meta section. */
	/*! Rewrites a value before it is written, for host conventions that differ (color ranges,
	    point origins). Returning none keeps the value. */
	std::function<std::optional<Value>(const ParameterSpec&, const Value&)> remap;
};

/*! Resolves a manifest preset's keys to IDs. Keys the manifest does not declare are dropped. */
Preset resolveManifestPreset(const EffectManifest& manifest, const ManifestPreset& preset);
/*! Converts a preset value to the shape a parameter stores: an option key to a choice index,
    RGB to RGBA and back, numbers to the numeric kind, anything to a string for string
    parameters. Returns false when the parameter carries no value or no conversion exists. */
bool coercePresetValue(const ParameterSpec& spec, const Value& in, Value& out);
/*! The bundle directory for a path the host reports: the path itself for a `.bundle`, or the
    enclosing bundle of a binary inside one; otherwise the path's directory. */
std::filesystem::path bundleDirectoryFor(const std::filesystem::path& reported);

/*! One row of a presets menu. */
struct PresetMenuEntry {
	enum class Kind : std::uint8_t { Default = 0, Separator, User, Plugin, Reveal, Save };
	Kind kind{Kind::Default};
	std::string label;
	std::string name;  /*!< The preset's display name for User and Plugin rows. */
	bool operator==(const PresetMenuEntry&) const = default;
};

/*! Builds the menu: Default, the user presets, the plug-in presets, Reveal, Save, with
    separators between non-empty sections. The labels of the fixed rows are localizable. */
std::vector<PresetMenuEntry> presetMenuEntries(const std::vector<Preset>& user, const std::vector<Preset>& plugin);
/*! The choice options for a menu: the entry's label, with the key `kind:name`. */
std::vector<ChoiceOption> presetMenuOptions(const std::vector<PresetMenuEntry>& entries);
/*! The presets under a directory, sorted by name, tagged with `tag`. */
std::vector<Preset> presetsInDirectory(const std::filesystem::path& directory, std::string_view tag, PresetSource source);
/*! The describe-time menu for a tag from the manifest, the bundle, and the user folder, so a
    host that freezes choice options after describe (Q-RESOLVE-021) still lists every preset
    present at load. */
std::vector<PresetMenuEntry> presetMenuEntriesForTag(const EffectManifest& manifest, const std::filesystem::path& bundlePath, const std::filesystem::path& userSupportDirectory, std::string_view tag);

/*! The per-instance preset service. */
class PresetManager {
public:
	PresetManager(EffectHost& host, MetaManager* meta);

	/*! Captures the instance: every state parameter's current value, and its tags and meta,
	    unless the record's app flags opt a section out. Presets controls themselves are not
	    captured. */
	Preset capture(std::string_view name, std::string_view tag) const;
	/*! Whether the preset's plug-in UUID is this effect's, or one of its prior UUIDs. A preset
	    without a plug-in UUID is compatible. */
	bool compatible(const Preset& preset) const;
	/*! Applies the sections in order inside the tag boundary. Values are written at `time`, or
	    at the current time when none is given. The first failure is returned after the rest
	    applied. */
	Error apply(const Preset& preset, const PresetApplyOptions& options = {}, std::optional<Time> time = std::nullopt);

	std::vector<Preset> manifestPresets(std::string_view tag) const;
	std::vector<Preset> bundlePresets(std::string_view tag) const;
	/*! The manifest presets, then the bundle files. */
	std::vector<Preset> pluginPresets(std::string_view tag) const;
	std::vector<Preset> userPresets(std::string_view tag) const;
	/*! The plug-in presets, then the user presets. */
	std::vector<Preset> presetsForTag(std::string_view tag) const;
	/*! The first preset with a name under a tag, user presets first so a user preset shadows a
	    plug-in preset of the same name. */
	std::optional<Preset> find(std::string_view tag, std::string_view name) const;
	std::vector<PresetMenuEntry> menuEntries(std::string_view tag) const;

	std::filesystem::path bundlePresetDirectory(std::string_view tag) const;
	std::filesystem::path userPresetDirectory(std::string_view tag) const;
	/*! Writes a preset into the user folder for its tag, named after its display name. Sets
	    the preset's path and source. */
	Error saveUserPreset(Preset& preset) const;

private:
	EffectHost& host_;
	MetaManager* meta_;
};

/*! The framework extension behind a `Presets` control: lists the presets for the control's
    first tag, applies the one the user picks, records the selection by name in the meta store so
    it survives a reordered menu, and handles the Reveal and Save rows. Loaded on every instance
    whose manifest declares a presets control. */
class PresetsExtension final : public Extension {
public:
	/*! `meta` is the instance's meta manager, or null on an effect without one; selections are
	    then not recorded. */
	explicit PresetsExtension(MetaManager* meta = nullptr) : meta_(meta) {}
	std::string name() const override { return "Presets"; }
	std::vector<EventId> interests() const override;
	int priority(EventId id) const override;
	void handle(Event& event) override;
	bool load(EffectHost& host, int index) override;

	PresetManager& manager() noexcept { return *manager_; }
	/*! The controls the extension drives. */
	const std::vector<ParameterId>& controls() const noexcept { return controls_; }
	/*! The menu a control shows now. */
	std::vector<PresetMenuEntry> entries(ParameterId control) const;
	/*! The recorded selection name of a control, or empty. */
	std::string selectedName(ParameterId control) const;
	/*! The tag a control lists. */
	std::string tagOf(ParameterId control) const;

private:
	void restoreSelections();
	void onChange(const ChangeEvent& change);
	void select(ParameterId control, int index);
	void record(ParameterId control, std::string_view name);

	std::unique_ptr<PresetManager> manager_;
	MetaManager* meta_{nullptr};
	std::vector<ParameterId> controls_;
	std::map<ParameterId, std::string> tags_;
	std::map<ParameterId, int> lastIndex_;
};

OFXGRIP_NAMESPACE_END

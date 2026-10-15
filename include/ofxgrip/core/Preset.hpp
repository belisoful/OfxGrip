// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  Preset.hpp
	@brief The preset model: captured parameter values, tags, and meta plus the identity of the
	       plug-in that produced them, and its `.fxpreset` file form.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripPreset`. The file form is an XML property
	list in FxFactory's `.fxpreset` format: a written file carries FxFactory's seven keys under
	their exact names, so FxFactory reads an OfxGrip file and OfxGrip reads an FxFactory file,
	and FxGrip's seven flat sibling keys carry the framework, UUID, name, tag, time, tags, and
	meta. A reader ignores keys it does not know. Parameter-keyed dictionaries use string
	parameter-ID keys on disk, so a preset travels between FxGrip and OfxGrip effects that share
	the same numeric IDs.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Plist.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! Where a preset came from. Not written to the file. */
enum class PresetSource : std::uint8_t {
	Code = 0,   /*!< Built in code or captured from an instance. */
	Manifest,   /*!< The manifest's presets table. */
	Bundle,     /*!< A file under the bundle's `Resources/Presets/<tag>/`. */
	User,       /*!< A file in the user's presets folder. */
	File,       /*!< A file loaded from an explicit path. */
};

/*! The file keys. */
namespace presetkeys {
inline constexpr const char* kFxFactoryCreatedByParameterId = "FxFactoryPresetCreatedByParameterID";
inline constexpr const char* kFxFactoryParameterValues = "FxFactoryPresetParameterValues";
inline constexpr const char* kFxFactoryPluginAuthor = "FxFactoryPresetPlugInAuthor";
inline constexpr const char* kFxFactoryLocalizedName = "FxFactoryPresetPlugInLocalizedName";
inline constexpr const char* kFxFactoryPluginUuid = "FxFactoryPresetPlugInUUID";
inline constexpr const char* kFxFactoryPluginVersion = "FxFactoryPresetPlugInVersion";
inline constexpr const char* kFxFactoryProductId = "FxFactoryPresetProductID";
inline constexpr const char* kFxGripFramework = "FxGripPresetFramework";
inline constexpr const char* kFxGripUuid = "FxGripPresetUUID";
inline constexpr const char* kFxGripDisplayName = "FxGripPresetDisplayName";
inline constexpr const char* kFxGripTag = "FxGripPresetTag";
inline constexpr const char* kFxGripCreatedTime = "FxGripPresetCreatedTime";
inline constexpr const char* kFxGripParameterMeta = "FxGripPresetParameterMeta";
inline constexpr const char* kFxGripParameterTags = "FxGripPresetParameterTags";
inline constexpr const char* kExtension = ".fxpreset";
}  // namespace presetkeys

/*! A preset. */
struct Preset {
	/*! The parameter that created the preset, or `kParameterIdRoot` (0) when none. */
	ParameterId createdBy{kParameterIdRoot};
	std::map<ParameterId, Value> values;
	std::map<ParameterId, std::vector<std::string>> tags;
	std::map<ParameterId, std::map<std::string, Value>> meta;

	std::string framework{"OfxGrip"};
	std::string uuid;            /*!< The preset's own UUID. */
	std::string name;            /*!< The display name. */
	std::string tag;             /*!< The tag the preset applies under. */
	std::string createdTime;     /*!< ISO 8601. */

	std::string pluginAuthor;
	std::string pluginLocalizedName;                        /*!< A plain name. */
	std::map<std::string, std::string> pluginLocalizedNames; /*!< FxFactory's per-language names, kept verbatim. */
	std::string pluginUuid;
	std::string pluginVersion;
	std::string productId;

	PresetSource source{PresetSource::Code};
	std::filesystem::path path;  /*!< The file the preset came from or was saved to. */

	/*! The file-form dictionary. Empty fields are omitted. */
	PlistValue toPlist() const;
	/*! Reads a file-form dictionary. Unknown keys are ignored; a non-dict is an error. */
	static Error fromPlist(const PlistValue& plist, Preset& out);
	/*! The XML text of `toPlist`. */
	std::string serialize() const;
	/*! Parses XML plist text. */
	static Error parse(std::string_view xml, Preset& out);
	/*! Reads a `.fxpreset` file. */
	static Error load(const std::filesystem::path& file, Preset& out);
	/*! Writes a `.fxpreset` file, creating the directory. */
	Error save(const std::filesystem::path& file) const;

	/*! A new random UUID in the 8-4-4-4-12 form. */
	static std::string makeUuid();
	/*! The current time in ISO 8601 UTC. */
	static std::string nowIso8601();
	/*! A file name for a display name: the characters a file system refuses become `_`. */
	static std::string fileNameFor(std::string_view displayName);

	bool operator==(const Preset& other) const
	{
		return createdBy == other.createdBy && values == other.values && tags == other.tags && meta == other.meta && framework == other.framework && uuid == other.uuid && name == other.name
		       && tag == other.tag && createdTime == other.createdTime && pluginAuthor == other.pluginAuthor && pluginLocalizedName == other.pluginLocalizedName
		       && pluginLocalizedNames == other.pluginLocalizedNames && pluginUuid == other.pluginUuid && pluginVersion == other.pluginVersion && productId == other.productId;
	}
};

OFXGRIP_NAMESPACE_END

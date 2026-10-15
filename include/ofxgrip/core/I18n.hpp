// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  I18n.hpp
	@brief Localization: a string table read from the bundle's `.strings` files, applied to the
	       manifest at describe time and to parameter traffic at runtime.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripI18N`. The table maps a key to its
	display string. Describe-time localization rewrites the manifest's labels, hints, option
	labels, string defaults, and About rows before lowering. At runtime the extension localizes
	a label, a string value, or option labels on the way to the host and delocalizes them on the
	way back, so a round trip closes and effect code sees keys. The files are Apple `.strings`
	(`"key" = "value";`), shared with FxGrip's bundles, under
	`Contents/Resources/<language>.lproj/Localizable.strings`.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Extension.hpp>
#include <ofxgrip/core/Manifest.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! A key → display string table with its inverse. */
class StringTable {
public:
	/*! Parses `.strings` text: `"key" = "value";` lines, block and line comments, and the
	    backslash escapes for quotes, backslashes, newlines, and tabs. Later entries replace
	    earlier ones. */
	static Error parseStrings(std::string_view text, StringTable& out);
	/*! Reads a `.strings` file. */
	static Error loadFile(const std::filesystem::path& file, StringTable& out);

	void set(std::string key, std::string value);
	bool has(std::string_view key) const noexcept { return forward_.count(std::string(key)) != 0; }
	std::size_t size() const noexcept { return forward_.size(); }
	bool empty() const noexcept { return forward_.empty(); }
	/*! The display string for a key, or the key when the table has none. */
	std::string localize(std::string_view key) const;
	/*! The key for a display string, or the string when the table has none. */
	std::string delocalize(std::string_view text) const;
	const std::map<std::string, std::string>& entries() const noexcept { return forward_; }

private:
	std::map<std::string, std::string> forward_;
	std::map<std::string, std::string> reverse_;
};

/*! The languages to try, most preferred first: `OFXGRIP_LANGUAGE`, then the language part of
    `LC_ALL`, `LC_MESSAGES`, or `LANG`, then `en`. Read once at load. */
std::vector<std::string> preferredLanguages();
/*! The first table found under a bundle for the languages, then `Base.lproj`, then a bare
    `Localizable.strings` in `Contents/Resources`. None when the bundle has no table. */
std::optional<StringTable> findStringTable(const std::filesystem::path& bundlePath, const std::vector<std::string>& languages);
/*! Rewrites the manifest's user-visible text through the table: parameter labels and hints,
    option labels, string defaults, the effect name, the About menu rows, and preset names. */
void localizeManifest(EffectManifest& manifest, const StringTable& table);

/*! The runtime extension. */
class I18nExtension final : public Extension {
public:
	struct Switches {
		bool localizeNames{true};
		bool localizeValues{true};
		bool localizeMenus{true};
		bool delocalizeNames{true};
		bool delocalizeValues{true};
		bool delocalizeMenus{true};
	};
	explicit I18nExtension(StringTable table);
	I18nExtension(StringTable table, Switches switches);
	std::string name() const override { return "I18n"; }
	std::vector<EventId> interests() const override;
	void handle(Event& event) override;

	const StringTable& table() const noexcept { return table_; }
	const Switches& switches() const noexcept { return switches_; }

private:
	StringTable table_;
	Switches switches_;
};

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  MetaManager.hpp
	@brief The per-instance store of parameter tags and meta, with a reverse index from a tag to
	       the parameters that carry it.

	Introduced in OfxGrip 0.2.0. This is FxGrip's `FxGripMetaManager`: one record per parameter
	holding its tags and its meta entries, a tag → parameters index, a recursive lock for
	multi-step edits, and an unsaved flag. The `MetaExtension` owns one per effect instance,
	seeds it from the manifest, loads the stored copy from the persistence service, and writes it
	back on flush. The codec is a versioned little-endian stream, the same bytes on every
	platform.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Manifest.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! One parameter's record. */
struct MetaRecord {
	ParameterId id{kParameterIdNone};
	std::vector<std::string> tags;
	std::map<std::string, Value> meta;
	bool operator==(const MetaRecord&) const = default;
};

/*! The store. Every public method takes the manager's recursive lock; a caller composing a
    multi-step edit holds `lock()` across its own sequence. */
class MetaManager {
public:
	/*! The codec version this build writes. */
	static constexpr std::uint16_t kCodecVersion = 1;
	/*! The meta key under which a presets control records the selected preset name. */
	static constexpr std::string_view kSelectedPresetKey = "ofxgrip.selectedPreset";

	MetaManager() = default;
	MetaManager(const MetaManager& other);
	MetaManager& operator=(const MetaManager& other);

	/*! Records. */
	bool has(ParameterId id) const;
	std::optional<MetaRecord> record(ParameterId id) const;
	/*! Every parameter with a record, in ascending ID order. */
	std::vector<ParameterId> parameters() const;
	/*! Creates an empty record when none exists. */
	void add(ParameterId id);
	/*! Drops a record and scrubs its ID from the tag index. */
	void remove(ParameterId id);
	void clear();
	std::size_t size() const;

	/*! Tags. */
	std::vector<std::string> tags(ParameterId id) const;
	bool hasTag(ParameterId id, std::string_view tag) const;
	void setTags(ParameterId id, std::vector<std::string> tags);
	void addTag(ParameterId id, std::string_view tag);
	void removeTag(ParameterId id, std::string_view tag);
	void removeAllTags(ParameterId id);
	/*! The parameters carrying a tag, in ascending ID order. */
	std::vector<ParameterId> parametersWithTag(std::string_view tag) const;
	/*! Every tag in use, sorted. */
	std::vector<std::string> allTags() const;
	std::size_t tagCount() const;

	/*! Meta. */
	std::optional<Value> meta(ParameterId id, std::string_view key) const;
	bool hasMeta(ParameterId id, std::string_view key) const;
	std::map<std::string, Value> metaOf(ParameterId id) const;
	std::vector<std::string> metaKeys(ParameterId id) const;
	/*! The entry count, or -1 when the parameter has no record. */
	int metaCount(ParameterId id) const;
	void setMeta(ParameterId id, std::string_view key, Value value);
	/*! Replaces every entry. */
	void setMetaOf(ParameterId id, std::map<std::string, Value> entries);
	void removeMeta(ParameterId id, std::string_view key);
	void removeAllMeta(ParameterId id);

	/*! Transfers a manifest record's tags and meta additively: a tag already present stays once,
	    a meta key already present keeps its value. Creates the record when absent. */
	void seed(const ParameterSpec& spec);
	/*! Merges a stored manager: its records win, and this manager's records fill only the
	    entries the stored one lacks. */
	void mergeFrom(const MetaManager& stored);

	/*! The unsaved flag. Every mutation sets it; the owner clears it after writing. */
	bool dirty() const;
	void clearDirty();
	void markDirty();

	Bytes encode() const;
	/*! Decodes a stream written by `encode`. A truncated or newer-version stream is an error. */
	static Error decode(std::span<const std::uint8_t> bytes, MetaManager& out);

	/*! Holds the manager's lock for a multi-step edit. */
	std::unique_lock<std::recursive_mutex> lock() const { return std::unique_lock<std::recursive_mutex>(mutex_); }

	/*! Compares the records; the dirty flag does not take part. */
	bool operator==(const MetaManager& other) const;

private:
	MetaRecord& recordRef(ParameterId id);
	void index(ParameterId id, std::string_view tag);
	void unindex(ParameterId id, std::string_view tag);

	mutable std::recursive_mutex mutex_;
	std::map<ParameterId, MetaRecord> records_;
	std::map<std::string, std::set<ParameterId>> byTag_;
	bool dirty_{false};
};

OFXGRIP_NAMESPACE_END

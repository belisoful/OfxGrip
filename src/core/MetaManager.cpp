// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/MetaManager.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <algorithm>

OFXGRIP_NAMESPACE_BEGIN

namespace {

// Stream layout (little-endian):
//   magic "OGMT", u16 codec version, u16 reserved, u32 record count
//   per record: u32 id, u16 tag count, [str tag]..., u16 meta count, [str key, value]...
constexpr std::string_view kMagic = "OGMT";

}  // namespace

MetaManager::MetaManager(const MetaManager& other)
{
	std::lock_guard<std::recursive_mutex> lock(other.mutex_);
	records_ = other.records_;
	byTag_ = other.byTag_;
	dirty_ = other.dirty_;
}

MetaManager& MetaManager::operator=(const MetaManager& other)
{
	if (this == &other) {
		return *this;
	}
	std::map<ParameterId, MetaRecord> records;
	std::map<std::string, std::set<ParameterId>> byTag;
	bool dirty = false;
	{
		std::lock_guard<std::recursive_mutex> lock(other.mutex_);
		records = other.records_;
		byTag = other.byTag_;
		dirty = other.dirty_;
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	records_ = std::move(records);
	byTag_ = std::move(byTag);
	dirty_ = dirty;
	return *this;
}

// ---- records ------------------------------------------------------------------------------------

bool MetaManager::has(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return records_.count(id) != 0;
}

std::optional<MetaRecord> MetaManager::record(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return std::nullopt;
	}
	return it->second;
}

std::vector<ParameterId> MetaManager::parameters() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<ParameterId> out;
	out.reserve(records_.size());
	for (const auto& [id, record] : records_) {
		out.push_back(id);
	}
	return out;
}

MetaRecord& MetaManager::recordRef(ParameterId id)
{
	auto it = records_.find(id);
	if (it == records_.end()) {
		it = records_.emplace(id, MetaRecord{id, {}, {}}).first;
		dirty_ = true;
	}
	return it->second;
}

void MetaManager::add(ParameterId id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	recordRef(id);
}

void MetaManager::remove(ParameterId id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return;
	}
	for (const std::string& tag : it->second.tags) {
		unindex(id, tag);
	}
	records_.erase(it);
	dirty_ = true;
}

void MetaManager::clear()
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (records_.empty()) {
		return;
	}
	records_.clear();
	byTag_.clear();
	dirty_ = true;
}

std::size_t MetaManager::size() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return records_.size();
}

// ---- tags ---------------------------------------------------------------------------------------

void MetaManager::index(ParameterId id, std::string_view tag)
{
	byTag_[std::string(tag)].insert(id);
}

void MetaManager::unindex(ParameterId id, std::string_view tag)
{
	auto it = byTag_.find(std::string(tag));
	if (it == byTag_.end()) {
		return;
	}
	it->second.erase(id);
	if (it->second.empty()) {
		byTag_.erase(it);
	}
}

std::vector<std::string> MetaManager::tags(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	return it == records_.end() ? std::vector<std::string>{} : it->second.tags;
}

bool MetaManager::hasTag(ParameterId id, std::string_view tag) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return false;
	}
	return std::find(it->second.tags.begin(), it->second.tags.end(), tag) != it->second.tags.end();
}

void MetaManager::setTags(ParameterId id, std::vector<std::string> tags)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	MetaRecord& record = recordRef(id);
	for (const std::string& tag : record.tags) {
		unindex(id, tag);
	}
	record.tags.clear();
	for (std::string& tag : tags) {
		if (tag.empty() || std::find(record.tags.begin(), record.tags.end(), tag) != record.tags.end()) {
			continue;
		}
		index(id, tag);
		record.tags.push_back(std::move(tag));
	}
	dirty_ = true;
}

void MetaManager::addTag(ParameterId id, std::string_view tag)
{
	if (tag.empty()) {
		return;
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	MetaRecord& record = recordRef(id);
	if (std::find(record.tags.begin(), record.tags.end(), tag) != record.tags.end()) {
		return;
	}
	record.tags.emplace_back(tag);
	index(id, tag);
	dirty_ = true;
}

void MetaManager::removeTag(ParameterId id, std::string_view tag)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return;
	}
	auto pos = std::find(it->second.tags.begin(), it->second.tags.end(), tag);
	if (pos == it->second.tags.end()) {
		return;
	}
	it->second.tags.erase(pos);
	unindex(id, tag);
	dirty_ = true;
}

void MetaManager::removeAllTags(ParameterId id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end() || it->second.tags.empty()) {
		return;
	}
	for (const std::string& tag : it->second.tags) {
		unindex(id, tag);
	}
	it->second.tags.clear();
	dirty_ = true;
}

std::vector<ParameterId> MetaManager::parametersWithTag(std::string_view tag) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = byTag_.find(std::string(tag));
	if (it == byTag_.end()) {
		return {};
	}
	return std::vector<ParameterId>(it->second.begin(), it->second.end());
}

std::vector<std::string> MetaManager::allTags() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<std::string> out;
	out.reserve(byTag_.size());
	for (const auto& [tag, ids] : byTag_) {
		out.push_back(tag);
	}
	return out;
}

std::size_t MetaManager::tagCount() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return byTag_.size();
}

// ---- meta ---------------------------------------------------------------------------------------

std::optional<Value> MetaManager::meta(ParameterId id, std::string_view key) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return std::nullopt;
	}
	auto entry = it->second.meta.find(std::string(key));
	if (entry == it->second.meta.end()) {
		return std::nullopt;
	}
	return entry->second;
}

bool MetaManager::hasMeta(ParameterId id, std::string_view key) const
{
	return meta(id, key).has_value();
}

std::map<std::string, Value> MetaManager::metaOf(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	return it == records_.end() ? std::map<std::string, Value>{} : it->second.meta;
}

std::vector<std::string> MetaManager::metaKeys(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	std::vector<std::string> out;
	auto it = records_.find(id);
	if (it == records_.end()) {
		return out;
	}
	for (const auto& [key, value] : it->second.meta) {
		out.push_back(key);
	}
	return out;
}

int MetaManager::metaCount(ParameterId id) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	return it == records_.end() ? -1 : static_cast<int>(it->second.meta.size());
}

void MetaManager::setMeta(ParameterId id, std::string_view key, Value value)
{
	if (key.empty()) {
		return;
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	MetaRecord& record = recordRef(id);
	auto entry = record.meta.find(std::string(key));
	if (entry != record.meta.end() && entry->second == value) {
		return;
	}
	record.meta[std::string(key)] = std::move(value);
	dirty_ = true;
}

void MetaManager::setMetaOf(ParameterId id, std::map<std::string, Value> entries)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	MetaRecord& record = recordRef(id);
	entries.erase("");
	if (record.meta == entries) {
		return;
	}
	record.meta = std::move(entries);
	dirty_ = true;
}

void MetaManager::removeMeta(ParameterId id, std::string_view key)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end()) {
		return;
	}
	if (it->second.meta.erase(std::string(key)) > 0) {
		dirty_ = true;
	}
}

void MetaManager::removeAllMeta(ParameterId id)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto it = records_.find(id);
	if (it == records_.end() || it->second.meta.empty()) {
		return;
	}
	it->second.meta.clear();
	dirty_ = true;
}

// ---- seeding and merge --------------------------------------------------------------------------

void MetaManager::seed(const ParameterSpec& spec)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	MetaRecord& record = recordRef(spec.id);
	for (const std::string& tag : spec.tags) {
		if (tag.empty() || std::find(record.tags.begin(), record.tags.end(), tag) != record.tags.end()) {
			continue;
		}
		record.tags.push_back(tag);
		index(spec.id, tag);
		dirty_ = true;
	}
	for (const auto& [key, value] : spec.meta) {
		if (key.empty() || record.meta.count(key) != 0) {
			continue;
		}
		record.meta[key] = value;
		dirty_ = true;
	}
}

void MetaManager::mergeFrom(const MetaManager& stored)
{
	std::map<ParameterId, MetaRecord> theirs;
	{
		std::lock_guard<std::recursive_mutex> lock(stored.mutex_);
		theirs = stored.records_;
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto& [id, record] : theirs) {
		auto mine = records_.find(id);
		if (mine == records_.end()) {
			records_[id] = record;
			for (const std::string& tag : record.tags) {
				index(id, tag);
			}
			dirty_ = true;
			continue;
		}
		// The stored record wins; the seeded one fills only what the stored one lacks.
		MetaRecord merged = record;
		for (const std::string& tag : mine->second.tags) {
			if (std::find(merged.tags.begin(), merged.tags.end(), tag) == merged.tags.end()) {
				merged.tags.push_back(tag);
			}
		}
		for (const auto& [key, value] : mine->second.meta) {
			merged.meta.emplace(key, value);
		}
		if (merged != mine->second) {
			for (const std::string& tag : mine->second.tags) {
				unindex(id, tag);
			}
			mine->second = std::move(merged);
			for (const std::string& tag : mine->second.tags) {
				index(id, tag);
			}
			dirty_ = true;
		}
	}
}

// ---- dirty --------------------------------------------------------------------------------------

bool MetaManager::dirty() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return dirty_;
}

void MetaManager::clearDirty()
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	dirty_ = false;
}

void MetaManager::markDirty()
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	dirty_ = true;
}

// ---- codec --------------------------------------------------------------------------------------

Bytes MetaManager::encode() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u16(kCodecVersion);
	w.u16(0);
	w.u32(static_cast<std::uint32_t>(records_.size()));
	for (const auto& [id, record] : records_) {
		w.u32(id);
		w.u16(static_cast<std::uint16_t>(record.tags.size()));
		for (const std::string& tag : record.tags) {
			w.str(tag);
		}
		w.u16(static_cast<std::uint16_t>(record.meta.size()));
		for (const auto& [key, value] : record.meta) {
			w.str(key);
			writeValue(w, value);
		}
	}
	return out;
}

Error MetaManager::decode(std::span<const std::uint8_t> bytes, MetaManager& out)
{
	auto fail = [](const char* why) { return Error::make(ErrorCode::Codec, "The meta store is unreadable", why); };
	ByteReader r(bytes);
	if (!r.magic(kMagic)) {
		return fail("bad magic");
	}
	std::uint16_t version = 0;
	std::uint16_t reserved = 0;
	std::uint32_t count = 0;
	if (!r.u16(version) || !r.u16(reserved) || !r.u32(count)) {
		return fail("truncated header");
	}
	if (version > kCodecVersion) {
		return fail("newer codec version");
	}
	MetaManager result;
	for (std::uint32_t i = 0; i < count; ++i) {
		std::uint32_t id = 0;
		std::uint16_t tagCount = 0;
		if (!r.u32(id) || !r.u16(tagCount)) {
			return fail("truncated record");
		}
		MetaRecord record;
		record.id = id;
		for (std::uint16_t t = 0; t < tagCount; ++t) {
			std::string tag;
			if (!r.str(tag)) {
				return fail("truncated tag");
			}
			if (!tag.empty() && std::find(record.tags.begin(), record.tags.end(), tag) == record.tags.end()) {
				record.tags.push_back(std::move(tag));
			}
		}
		std::uint16_t metaCount = 0;
		if (!r.u16(metaCount)) {
			return fail("truncated record");
		}
		for (std::uint16_t m = 0; m < metaCount; ++m) {
			std::string key;
			Value value;
			if (!r.str(key) || !readValue(r, value)) {
				return fail("truncated meta entry");
			}
			record.meta[std::move(key)] = std::move(value);
		}
		for (const std::string& tag : record.tags) {
			result.index(id, tag);
		}
		result.records_[id] = std::move(record);
	}
	result.dirty_ = false;
	out = std::move(result);
	out.dirty_ = false;
	return Error::none();
}

bool MetaManager::operator==(const MetaManager& other) const
{
	if (this == &other) {
		return true;
	}
	std::map<ParameterId, MetaRecord> theirs;
	{
		std::lock_guard<std::recursive_mutex> lock(other.mutex_);
		theirs = other.records_;
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return records_ == theirs;
}

OFXGRIP_NAMESPACE_END

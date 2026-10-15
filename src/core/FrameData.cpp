// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/FrameData.hpp>

#include <ofxgrip/core/Codec.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

OFXGRIP_NAMESPACE_BEGIN

namespace {
// Stream layout (little-endian):
//   magic "OGFD", u16 codec version, u16 reserved
//   f64 frame duration, str instance uuid, u32 spill threshold (0xFFFFFFFF never),
//   u16 header count, [str key, value]...
//   u32 record count, [i32 frame, u8 spilled, (spilled ? u32 length : bytes data)]...
constexpr std::string_view kMagic = "OGFD";
}  // namespace

void FrameData::set(long frame, Bytes record)
{
	Record& r = records_[frame];
	r.data = std::move(record);
	r.spilled = false;
	r.loaded = true;
	r.length = r.data.size();
	dirty_ = true;
}

bool FrameData::has(long frame) const noexcept
{
	return records_.count(frame) != 0;
}

bool FrameData::loadSpilled(long frame, Record& record) const
{
	std::ifstream in(spillFileFor(frame), std::ios::binary);
	if (!in) {
		return false;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	const std::string text = buffer.str();
	record.data.assign(text.begin(), text.end());
	record.loaded = true;
	return true;
}

std::optional<Bytes> FrameData::at(long frame) const
{
	auto it = records_.find(frame);
	if (it == records_.end()) {
		return std::nullopt;
	}
	if (it->second.loaded) {
		return it->second.data;
	}
	Record copy = it->second;
	if (!loadSpilled(frame, copy)) {
		return std::nullopt;
	}
	return copy.data;
}

std::optional<Bytes> FrameData::latestAtOrBefore(long frame, long* foundFrame) const
{
	auto it = records_.upper_bound(frame);
	while (it != records_.begin()) {
		--it;
		std::optional<Bytes> data = at(it->first);
		if (data) {
			if (foundFrame != nullptr) {
				*foundFrame = it->first;
			}
			return data;
		}
	}
	return std::nullopt;
}

void FrameData::remove(long frame)
{
	auto it = records_.find(frame);
	if (it == records_.end()) {
		return;
	}
	if (it->second.spilled) {
		std::error_code ec;
		std::filesystem::remove(spillFileFor(frame), ec);
	}
	records_.erase(it);
	dirty_ = true;
}

void FrameData::clear()
{
	if (records_.empty()) {
		return;
	}
	removeSpillFiles();
	records_.clear();
	dirty_ = true;
}

std::vector<long> FrameData::frames() const
{
	std::vector<long> out;
	out.reserve(records_.size());
	for (const auto& [frame, record] : records_) {
		out.push_back(frame);
	}
	return out;
}

long FrameData::frameIndexFor(double timeInSeconds) const noexcept
{
	if (frameDuration <= 0.0) {
		return static_cast<long>(std::floor(timeInSeconds));
	}
	return static_cast<long>(std::floor(timeInSeconds / frameDuration + 1e-9));
}

std::filesystem::path FrameData::spillFileFor(long frame) const
{
	const std::string name = (instanceUuid.empty() ? std::string("frame") : instanceUuid) + "-" + std::to_string(frame) + ".ogfd";
	return spillDirectory_ / name;
}

bool FrameData::isSpilled(long frame) const noexcept
{
	auto it = records_.find(frame);
	return it != records_.end() && it->second.spilled;
}

void FrameData::removeSpillFiles()
{
	for (auto& [frame, record] : records_) {
		if (record.spilled) {
			std::error_code ec;
			std::filesystem::remove(spillFileFor(frame), ec);
		}
	}
}

bool FrameData::writeSpill(long frame, const Bytes& data) const
{
	std::error_code ec;
	std::filesystem::create_directories(spillDirectory_, ec);
	std::ofstream out(spillFileFor(frame), std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	return static_cast<bool>(out);
}

Bytes FrameData::encode()
{
	Bytes out;
	ByteWriter w(out);
	w.raw(kMagic);
	w.u16(kCodecVersion);
	w.u16(0);
	w.f64(frameDuration);
	w.str(instanceUuid);
	w.u32(spillThreshold_ == kNeverSpill ? 0xFFFFFFFFu : static_cast<std::uint32_t>(spillThreshold_));
	w.u16(static_cast<std::uint16_t>(header.size()));
	for (const auto& [key, value] : header) {
		w.str(key);
		writeValue(w, value);
	}
	w.u32(static_cast<std::uint32_t>(records_.size()));
	const bool canSpill = !spillDirectory_.empty() && spillThreshold_ != kNeverSpill;
	for (auto& [frame, record] : records_) {
		w.i32(static_cast<std::int32_t>(frame));
		const bool large = record.length > spillThreshold_;
		if (record.loaded && canSpill && large) {
			if (writeSpill(frame, record.data)) {
				record.spilled = true;
			}
		}
		if (record.spilled && (canSpill || !record.loaded)) {
			w.u8(1);
			w.u32(static_cast<std::uint32_t>(record.length));
		} else {
			w.u8(0);
			w.bytes(record.data);
		}
	}
	return out;
}

Error FrameData::decode(std::span<const std::uint8_t> bytes, const std::filesystem::path& spillDirectory, FrameData& out)
{
	auto fail = [](const char* why) { return Error::make(ErrorCode::Codec, "The frame data is unreadable", why); };
	ByteReader r(bytes);
	std::uint16_t version = 0;
	std::uint16_t reserved = 0;
	FrameData result;
	result.spillDirectory_ = spillDirectory;
	std::uint32_t threshold = 0;
	std::uint16_t headerCount = 0;
	if (!r.magic(kMagic) || !r.u16(version) || !r.u16(reserved) || !r.f64(result.frameDuration) || !r.str(result.instanceUuid) || !r.u32(threshold) || !r.u16(headerCount)) {
		return fail("truncated header");
	}
	if (version > kCodecVersion) {
		return fail("newer codec version");
	}
	result.spillThreshold_ = threshold == 0xFFFFFFFFu ? kNeverSpill : threshold;
	for (std::uint16_t i = 0; i < headerCount; ++i) {
		std::string key;
		Value value;
		if (!r.str(key) || !readValue(r, value)) {
			return fail("truncated header entry");
		}
		result.header[std::move(key)] = std::move(value);
	}
	std::uint32_t count = 0;
	if (!r.u32(count)) {
		return fail("truncated record count");
	}
	for (std::uint32_t i = 0; i < count; ++i) {
		std::int32_t frame = 0;
		std::uint8_t spilled = 0;
		if (!r.i32(frame) || !r.u8(spilled)) {
			return fail("truncated record");
		}
		Record record;
		if (spilled != 0) {
			std::uint32_t length = 0;
			if (!r.u32(length)) {
				return fail("truncated marker");
			}
			record.spilled = true;
			record.loaded = false;
			record.length = length;
		} else {
			if (!r.bytes(record.data)) {
				return fail("truncated record data");
			}
			record.length = record.data.size();
		}
		result.records_[frame] = std::move(record);
	}
	result.dirty_ = false;
	out = std::move(result);
	return Error::none();
}

OFXGRIP_NAMESPACE_END

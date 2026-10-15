// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  FrameData.hpp
	@brief Per-frame analysis records keyed by frame index, with header entries and a size-gated
	       spill of large records to a cache folder.

	Introduced in OfxGrip 0.3.0. This is FxGrip's `FxGripFrameData`. The store is sparse and does
	not interpolate; `latestAtOrBefore` serves a feedback-style seek so an effect whose frame
	derives from the previous one re-simulates forward from the nearest stored index. A record
	larger than the spill threshold is written to a file under the spill directory and a marker
	takes its place in the encoded store, so the host document carries only the manifest. A
	document opened where the spill files are absent reads such a record as missing, and the
	caller re-analyzes.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Types.hpp>
#include <ofxgrip/core/Value.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

/*! The store. */
class FrameData {
public:
	/*! The codec version this build writes. */
	static constexpr std::uint16_t kCodecVersion = 1;
	/*! The default spill threshold in bytes. */
	static constexpr std::size_t kDefaultSpillThreshold = 64 * 1024;
	/*! A threshold that never spills. */
	static constexpr std::size_t kNeverSpill = static_cast<std::size_t>(-1);

	/*! Header entries: the frame duration in seconds for arbitrary rates, the instance UUID the
	    spill files are named by, and free entries an effect adds. */
	double frameDuration{1.0 / 24.0};
	std::string instanceUuid;
	std::map<std::string, Value> header;

	/*! Records. */
	void set(long frame, Bytes record);
	std::optional<Bytes> at(long frame) const;
	bool has(long frame) const noexcept;
	/*! The record at the frame or the nearest earlier one. `foundFrame` receives its index. */
	std::optional<Bytes> latestAtOrBefore(long frame, long* foundFrame = nullptr) const;
	void remove(long frame);
	void clear();
	std::vector<long> frames() const;
	std::size_t size() const noexcept { return records_.size(); }
	/*! The frame index of a time at the stored frame duration. */
	long frameIndexFor(double timeInSeconds) const noexcept;

	/*! Spill. The directory is created on demand; an empty directory never spills. */
	void setSpillDirectory(std::filesystem::path directory) { spillDirectory_ = std::move(directory); }
	const std::filesystem::path& spillDirectory() const noexcept { return spillDirectory_; }
	void setSpillThreshold(std::size_t bytes) noexcept { spillThreshold_ = bytes; }
	std::size_t spillThreshold() const noexcept { return spillThreshold_; }
	/*! The spill file for a frame. */
	std::filesystem::path spillFileFor(long frame) const;
	/*! Whether a record lives in a spill file. */
	bool isSpilled(long frame) const noexcept;
	/*! Removes the spill files this store wrote. */
	void removeSpillFiles();

	/*! The unsaved flag. */
	bool dirty() const noexcept { return dirty_; }
	void clearDirty() noexcept { dirty_ = false; }

	/*! Encodes the header and the inline records, and a marker for each spilled record, writing
	    spill files for records over the threshold. */
	Bytes encode();
	/*! Decodes a store; markers are resolved against `spillDirectory` on `at`. */
	static Error decode(std::span<const std::uint8_t> bytes, const std::filesystem::path& spillDirectory, FrameData& out);

private:
	struct Record {
		Bytes data;        /*!< Inline, or the cached contents of a spill file. */
		bool spilled{false};
		bool loaded{true}; /*!< False for a marker whose file has not been read. */
		std::size_t length{0};
	};
	bool loadSpilled(long frame, Record& record) const;
	bool writeSpill(long frame, const Bytes& data) const;

	std::map<long, Record> records_;
	std::filesystem::path spillDirectory_;
	std::size_t spillThreshold_{kDefaultSpillThreshold};
	bool dirty_{false};
};

OFXGRIP_NAMESPACE_END

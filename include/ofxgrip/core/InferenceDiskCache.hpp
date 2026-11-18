// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
/*!
	@file  InferenceDiskCache.hpp
	@brief The disk tier of the inference cache: one file per output image, named by a digest of
	       everything the output depends on.

	Introduced in OfxGrip 0.5.0. `InferenceCache` keeps an instance's outputs in memory for the
	session. This tier keeps them in a folder, so a model that takes seconds per frame runs once
	per input and its output survives a restart of the host.

	A key is the SHA-256 of the cache signature and of the source image's description and
	pixels. The same model, parameters, and input pixels give the same key in any instance,
	document, or process, and a changed input gives a new one. No frame number and no instance
	identity take part, so a copied effect and a held still frame reuse a stored output, and a
	stored output is never served for other pixels.

	A file is written under a temporary name and renamed into place, so a reader sees a whole
	file or none. Several instances and several processes may share one folder. The least
	recently used files leave when the folder passes its byte limit.
*/
#pragma once

#include <ofxgrip/core/Error.hpp>
#include <ofxgrip/core/Image.hpp>
#include <ofxgrip/core/Inference.hpp>
#include <ofxgrip/core/Types.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

OFXGRIP_NAMESPACE_BEGIN

/*! The folder of stored outputs. Safe to use from several threads. */
class InferenceDiskCache {
public:
	/*! The default limit: 4 GiB. */
	static constexpr std::uint64_t kDefaultByteLimit = std::uint64_t{4} * 1024 * 1024 * 1024;

	/*! The folder is created by the first store. */
	explicit InferenceDiskCache(std::filesystem::path directory, std::uint64_t byteLimit = kDefaultByteLimit);
	~InferenceDiskCache();
	InferenceDiskCache(const InferenceDiskCache&) = delete;
	InferenceDiskCache& operator=(const InferenceDiskCache&) = delete;

	/*! The key of the output a signature gives for a source image: 64 hexadecimal digits. The
	    source's bounds, depth, layout, premultiplication, and every pixel take part. */
	static std::string key(std::string_view signature, const ImageView& source);

	/*! The image stored under a key, or null. A file that is missing, cut short, or written by
	    another codec version reads as null. A hit marks the file as recently used. */
	InferenceImage find(const std::string& key);
	/*! Stores an image under a key, then trims the folder to the limit. An image larger than
	    the limit is not kept. */
	Error store(const std::string& key, const ImageView& image);
	/*! Removes every file this cache wrote to the folder. */
	void clear();

	const std::filesystem::path& directory() const noexcept;
	std::uint64_t byteLimit() const noexcept;
	/*! The number of stored outputs in the folder. */
	std::size_t fileCount() const;
	/*! The bytes the stored outputs take. */
	std::uint64_t byteCount() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

OFXGRIP_NAMESPACE_END

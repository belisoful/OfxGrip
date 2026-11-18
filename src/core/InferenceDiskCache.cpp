// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/InferenceDiskCache.hpp>

#include <ofxgrip/core/Codec.hpp>
#include <ofxgrip/core/Digest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <span>
#include <system_error>
#include <vector>

OFXGRIP_NAMESPACE_BEGIN

namespace {

constexpr std::string_view kMagic = "OGIC";
constexpr std::uint16_t kCodecVersion = 1;
constexpr std::size_t kHeaderBytes = 80;
constexpr std::string_view kExtension = ".ogic";
constexpr std::string_view kTemporaryExtension = ".tmp";
constexpr int kMaxSide = 1 << 16;
// A temporary file this old belongs to a writer that died.
constexpr std::chrono::hours kStaleTemporaryAge{1};

bool isKey(std::string_view text) noexcept
{
	if (text.size() != 64) {
		return false;
	}
	return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool isStoredFile(const std::filesystem::path& path)
{
	return path.extension().string() == kExtension && isKey(path.stem().string());
}

bool isTemporaryFile(const std::filesystem::path& path)
{
	const std::string name = path.filename().string();
	return path.extension().string() == kTemporaryExtension && name.size() > 64 && isKey(std::string_view(name).substr(0, 64));
}

/*! The image's rows without padding: a view of the pixels when they are packed, a copy
    otherwise. */
std::span<const std::uint8_t> packedPixels(const ImageView& image, std::vector<std::uint8_t>& storage)
{
	const std::size_t rowBytes = image.desc.minimumRowBytes();
	const std::size_t height = static_cast<std::size_t>(image.desc.bounds.height());
	if (image.rowBytes == static_cast<std::ptrdiff_t>(rowBytes)) {
		return {image.data, rowBytes * height};
	}
	storage.resize(rowBytes * height);
	for (std::size_t row = 0; row < height; ++row) {
		const std::uint8_t* from = image.pixel(image.desc.bounds.x1, image.desc.bounds.y1 + static_cast<int>(row));
		std::memcpy(storage.data() + row * rowBytes, from, rowBytes);
	}
	return storage;
}

Bytes encodeHeader(const ImageDesc& desc, std::uint64_t payload)
{
	Bytes header;
	header.reserve(kHeaderBytes);
	ByteWriter out(header);
	out.raw(kMagic);
	out.u16(kCodecVersion);
	out.u16(0);
	for (const RectI& rect : {desc.bounds, desc.regionOfDefinition}) {
		out.i32(rect.x1);
		out.i32(rect.y1);
		out.i32(rect.x2);
		out.i32(rect.y2);
	}
	out.u8(static_cast<std::uint8_t>(desc.depth));
	out.u8(static_cast<std::uint8_t>(desc.components));
	out.u8(static_cast<std::uint8_t>(desc.premultiplication));
	out.u8(static_cast<std::uint8_t>(desc.field));
	out.f64(desc.pixelAspect);
	out.f64(desc.renderScale.x);
	out.f64(desc.renderScale.y);
	out.u32(static_cast<std::uint32_t>(desc.minimumRowBytes()));
	out.u32(static_cast<std::uint32_t>(payload & 0xFFFFFFFFu));
	out.u32(static_cast<std::uint32_t>(payload >> 32));
	return header;
}

bool decodeHeader(std::span<const std::uint8_t> header, ImageDesc& desc, std::uint64_t& payload)
{
	if (header.size() != kHeaderBytes || std::memcmp(header.data(), kMagic.data(), kMagic.size()) != 0) {
		return false;
	}
	ByteReader in(header.subspan(kMagic.size()));
	std::uint16_t version = 0;
	std::uint16_t reserved = 0;
	if (!in.u16(version) || !in.u16(reserved) || version != kCodecVersion) {
		return false;
	}
	for (RectI* rect : {&desc.bounds, &desc.regionOfDefinition}) {
		if (!in.i32(rect->x1) || !in.i32(rect->y1) || !in.i32(rect->x2) || !in.i32(rect->y2)) {
			return false;
		}
	}
	std::uint8_t depth = 0;
	std::uint8_t components = 0;
	std::uint8_t premultiplication = 0;
	std::uint8_t field = 0;
	std::uint32_t rowBytes = 0;
	std::uint32_t low = 0;
	std::uint32_t high = 0;
	if (!in.u8(depth) || !in.u8(components) || !in.u8(premultiplication) || !in.u8(field) || !in.f64(desc.pixelAspect) || !in.f64(desc.renderScale.x) || !in.f64(desc.renderScale.y) || !in.u32(rowBytes) || !in.u32(low) || !in.u32(high)) {
		return false;
	}
	if (depth == 0 || depth > static_cast<std::uint8_t>(BitDepth::Float) || components == 0 || components > static_cast<std::uint8_t>(Components::Alpha) || premultiplication > static_cast<std::uint8_t>(Premultiplication::Unpremultiplied) || field > static_cast<std::uint8_t>(Field::Doubled)) {
		return false;
	}
	desc.depth = static_cast<BitDepth>(depth);
	desc.components = static_cast<Components>(components);
	desc.premultiplication = static_cast<Premultiplication>(premultiplication);
	desc.field = static_cast<Field>(field);
	const std::int64_t width = static_cast<std::int64_t>(desc.bounds.x2) - desc.bounds.x1;
	const std::int64_t height = static_cast<std::int64_t>(desc.bounds.y2) - desc.bounds.y1;
	if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) {
		return false;
	}
	payload = (static_cast<std::uint64_t>(high) << 32) | low;
	return rowBytes == desc.minimumRowBytes() && payload == static_cast<std::uint64_t>(rowBytes) * static_cast<std::uint64_t>(height);
}

std::string temporarySuffix()
{
	static std::atomic<std::uint64_t> counter{0};
	std::random_device device;
	const std::uint64_t value = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ counter.fetch_add(1);
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string text(16, '0');
	for (std::size_t i = 0; i < text.size(); ++i) {
		text[i] = kDigits[(value >> (i * 4)) & 0xF];
	}
	return text;
}

struct StoredFile {
	std::filesystem::path path;
	std::uint64_t bytes{0};
	std::filesystem::file_time_type used;
};

}  // namespace

struct InferenceDiskCache::Impl {
	std::filesystem::path directory;
	std::uint64_t limit{0};
	mutable std::mutex mutex;

	std::filesystem::path fileFor(const std::string& key) const
	{
		return directory / (key + std::string(kExtension));
	}

	/*! The stored outputs in the folder. With `sweep`, temporary files of dead writers leave. */
	std::vector<StoredFile> scan(bool sweep) const
	{
		std::vector<StoredFile> files;
		std::error_code ec;
		std::filesystem::directory_iterator it(directory, ec);
		const auto now = std::filesystem::file_time_type::clock::now();
		for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const std::filesystem::path path = it->path();
			std::error_code fileError;
			if (isStoredFile(path)) {
				StoredFile file;
				file.path = path;
				file.bytes = static_cast<std::uint64_t>(std::filesystem::file_size(path, fileError));
				file.used = std::filesystem::last_write_time(path, fileError);
				if (!fileError) {
					files.push_back(std::move(file));
				}
			} else if (sweep && isTemporaryFile(path)) {
				const auto written = std::filesystem::last_write_time(path, fileError);
				if (!fileError && now - written > kStaleTemporaryAge) {
					std::filesystem::remove(path, fileError);
				}
			}
		}
		return files;
	}

	void trim()
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::vector<StoredFile> files = scan(true);
		std::uint64_t total = 0;
		for (const StoredFile& file : files) {
			total += file.bytes;
		}
		if (total <= limit) {
			return;
		}
		std::sort(files.begin(), files.end(), [](const StoredFile& a, const StoredFile& b) { return a.used < b.used; });
		for (const StoredFile& file : files) {
			if (total <= limit) {
				break;
			}
			std::error_code ec;
			if (std::filesystem::remove(file.path, ec)) {
				total -= file.bytes;
			}
		}
	}
};

InferenceDiskCache::InferenceDiskCache(std::filesystem::path directory, std::uint64_t byteLimit) : impl_(std::make_unique<Impl>())
{
	impl_->directory = std::move(directory);
	impl_->limit = byteLimit;
}

InferenceDiskCache::~InferenceDiskCache() = default;

std::string InferenceDiskCache::key(std::string_view signature, const ImageView& source)
{
	std::string text = "ofxgrip-inference-1\n";
	text += signature;
	text += '\n';
	const ImageDesc& desc = source.desc;
	for (int value : {desc.bounds.x1, desc.bounds.y1, desc.bounds.x2, desc.bounds.y2, static_cast<int>(desc.depth), static_cast<int>(desc.components), static_cast<int>(desc.premultiplication)}) {
		text += std::to_string(value);
		text += ' ';
	}
	if (source.valid()) {
		std::vector<std::uint8_t> storage;
		text += hexDigest(sha256(packedPixels(source, storage)));
	}
	return hexDigest(sha256(text));
}

InferenceImage InferenceDiskCache::find(const std::string& key)
{
	if (!isKey(key)) {
		return nullptr;
	}
	const std::filesystem::path path = impl_->fileFor(key);
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return nullptr;
	}
	std::uint8_t header[kHeaderBytes];
	in.read(reinterpret_cast<char*>(header), static_cast<std::streamsize>(kHeaderBytes));
	ImageDesc desc;
	std::uint64_t payload = 0;
	if (in.gcount() != static_cast<std::streamsize>(kHeaderBytes) || !decodeHeader(header, desc, payload)) {
		return nullptr;
	}
	std::error_code ec;
	if (std::filesystem::file_size(path, ec) != kHeaderBytes + payload || ec) {
		return nullptr;
	}
	auto image = std::make_shared<ImageBuffer>(desc);
	const ImageView view = image->view();
	const std::streamsize rowBytes = static_cast<std::streamsize>(desc.minimumRowBytes());
	for (int y = desc.bounds.y1; y < desc.bounds.y2; ++y) {
		in.read(reinterpret_cast<char*>(view.pixel(desc.bounds.x1, y)), rowBytes);
		if (in.gcount() != rowBytes) {
			return nullptr;
		}
	}
	std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
	return image;
}

Error InferenceDiskCache::store(const std::string& key, const ImageView& image)
{
	if (!isKey(key)) {
		return Error::make(ErrorCode::InvalidParameter, "The inference cache key is not a digest").with("key", key);
	}
	if (!image.valid()) {
		return Error::make(ErrorCode::InvalidParameter, "The inference cache stores no empty image");
	}
	std::vector<std::uint8_t> storage;
	const std::span<const std::uint8_t> pixels = packedPixels(image, storage);
	if (kHeaderBytes + pixels.size() > impl_->limit) {
		return Error::none();
	}
	std::error_code ec;
	std::filesystem::create_directories(impl_->directory, ec);
	const std::filesystem::path target = impl_->fileFor(key);
	const std::filesystem::path temporary = impl_->directory / (key + "." + temporarySuffix() + std::string(kTemporaryExtension));
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		const Bytes header = encodeHeader(image.desc, pixels.size());
		out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
		out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
		out.flush();
		if (!out) {
			out.close();
			std::filesystem::remove(temporary, ec);
			return Error::make(ErrorCode::Failed, "The inference cache could not write a file").with("path", temporary.string());
		}
	}
	std::filesystem::rename(temporary, target, ec);
	if (ec) {
		const std::string why = ec.message();
		std::filesystem::remove(temporary, ec);
		return Error::make(ErrorCode::Failed, "The inference cache could not place a file", why).with("path", target.string());
	}
	impl_->trim();
	return Error::none();
}

void InferenceDiskCache::clear()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	for (const StoredFile& file : impl_->scan(false)) {
		std::error_code ec;
		std::filesystem::remove(file.path, ec);
	}
}

const std::filesystem::path& InferenceDiskCache::directory() const noexcept
{
	return impl_->directory;
}

std::uint64_t InferenceDiskCache::byteLimit() const noexcept
{
	return impl_->limit;
}

std::size_t InferenceDiskCache::fileCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->scan(false).size();
}

std::uint64_t InferenceDiskCache::byteCount() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	std::uint64_t total = 0;
	for (const StoredFile& file : impl_->scan(false)) {
		total += file.bytes;
	}
	return total;
}

OFXGRIP_NAMESPACE_END

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The disk tier of the inference cache: keys, the file codec, files another object or a broken
// writer left, the byte limit, and what `clear` leaves alone.
#include <ofxgrip/core/InferenceDiskCache.hpp>

#include "../TestEnvironment.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ofxgrip;

namespace {

std::filesystem::path freshDirectory(const char* name)
{
	static int counter = 0;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ofxgrip-inferencedisk-" + ofxgrip::testing::currentTestTag() + "-" + std::to_string(++counter)) / name;
	std::error_code ec;
	std::filesystem::remove_all(dir.parent_path(), ec);
	return dir;
}

std::shared_ptr<ImageBuffer> image(int width, int height, BitDepth depth = BitDepth::Float, Components components = Components::RGBA, float seed = 0.0f, int originX = 0, int originY = 0)
{
	ImageDesc desc;
	desc.bounds = {originX, originY, originX + width, originY + height};
	desc.regionOfDefinition = {originX - 1, originY - 2, originX + width + 3, originY + height + 4};
	desc.depth = depth;
	desc.components = components;
	desc.premultiplication = Premultiplication::Unpremultiplied;
	desc.pixelAspect = 1.5;
	desc.field = Field::Lower;
	desc.renderScale = {0.5, 0.25};
	auto buffer = std::make_shared<ImageBuffer>(desc);
	const ImageView v = buffer->view();
	for (int y = desc.bounds.y1; y < desc.bounds.y2; ++y) {
		for (int x = desc.bounds.x1; x < desc.bounds.x2; ++x) {
			for (std::size_t c = 0; c < componentCount(components); ++c) {
				writeComponent(v.pixel(x, y), depth, c, seed + static_cast<float>(x - originX) / static_cast<float>(width) * 0.5f + static_cast<float>(y - originY) * 0.01f + static_cast<float>(c) * 0.1f);
			}
		}
	}
	return buffer;
}

std::string keyOf(char digit)
{
	return std::string(64, digit);
}

void setAge(const std::filesystem::path& file, std::chrono::seconds age)
{
	std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - age);
}

std::vector<char> readFile(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& file, const std::vector<char>& bytes)
{
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(InferenceDiskCache, KeysFollowTheSignatureTheDescriptionAndEveryPixel)
{
	const auto base = image(8, 4);
	const std::string key = InferenceDiskCache::key("model|1x1", base->view());
	EXPECT_EQ(key.size(), 64u);
	EXPECT_EQ(key.find_first_not_of("0123456789abcdef"), std::string::npos);
	EXPECT_EQ(InferenceDiskCache::key("model|1x1", image(8, 4)->view()), key);

	EXPECT_NE(InferenceDiskCache::key("model|2x2", base->view()), key);
	EXPECT_NE(InferenceDiskCache::key("model|1x1", image(8, 4, BitDepth::Float, Components::RGBA, 0.001f)->view()), key);
	EXPECT_NE(InferenceDiskCache::key("model|1x1", image(8, 4, BitDepth::Half)->view()), key);
	EXPECT_NE(InferenceDiskCache::key("model|1x1", image(8, 4, BitDepth::Float, Components::RGB)->view()), key);
	EXPECT_NE(InferenceDiskCache::key("model|1x1", image(8, 4, BitDepth::Float, Components::RGBA, 0.0f, 2, 0)->view()), key);
	auto opaque = image(8, 4);
	ImageView relabeled = opaque->view();
	relabeled.desc.premultiplication = Premultiplication::Opaque;
	EXPECT_NE(InferenceDiskCache::key("model|1x1", relabeled), key);
	auto onePixel = image(8, 4);
	writeComponent(onePixel->view().pixel(7, 3), BitDepth::Float, 2, 0.75f);
	EXPECT_NE(InferenceDiskCache::key("model|1x1", onePixel->view()), key);

	// A window of a wider image has padded rows; its key is the key of the same pixels packed.
	const auto wide = image(8, 4);
	const ImageView window = wide->view().subView({2, 1, 6, 3});
	ASSERT_TRUE(window.valid());
	ASSERT_NE(window.rowBytes, static_cast<std::ptrdiff_t>(window.desc.minimumRowBytes()));
	ImageBuffer packed(window.desc);
	ASSERT_TRUE(packed.view().copyFrom(window).ok());
	EXPECT_EQ(InferenceDiskCache::key("model", window), InferenceDiskCache::key("model", packed.view()));

	// An empty image still has a key, and the signature still tells two apart.
	EXPECT_EQ(InferenceDiskCache::key("a", ImageView{}).size(), 64u);
	EXPECT_NE(InferenceDiskCache::key("a", ImageView{}), InferenceDiskCache::key("b", ImageView{}));
}

TEST(InferenceDiskCache, AnotherObjectReadsWhatOneStoredInEveryLayout)
{
	const std::filesystem::path dir = freshDirectory("layouts");
	InferenceDiskCache writer(dir, 1 << 20);
	EXPECT_TRUE(writer.directory() == dir);
	EXPECT_EQ(writer.byteLimit(), std::uint64_t{1} << 20);
	EXPECT_EQ(writer.fileCount(), 0u);
	EXPECT_EQ(writer.byteCount(), 0u);
	std::size_t index = 0;
	std::uint64_t expectedBytes = 0;
	for (BitDepth depth : {BitDepth::UInt8, BitDepth::UInt16, BitDepth::Half, BitDepth::Float}) {
		for (Components components : {Components::RGBA, Components::RGB, Components::Alpha}) {
			const auto stored = image(9, 5, depth, components, 0.1f, -3, 7);
			const std::string key = keyOf("0123456789abcdef"[index++]);
			ASSERT_TRUE(writer.store(key, stored->view()).ok());
			expectedBytes += 80 + stored->desc().minimumRowBytes() * 5;
			InferenceDiskCache reader(dir, 1 << 20);
			const InferenceImage found = reader.find(key);
			ASSERT_NE(found, nullptr) << toString(depth);
			EXPECT_EQ(found->desc(), stored->desc());
			for (int y = 7; y < 12; ++y) {
				EXPECT_EQ(std::memcmp(found->view().pixel(-3, y), stored->view().pixel(-3, y), stored->desc().minimumRowBytes()), 0);
			}
		}
	}
	EXPECT_EQ(writer.fileCount(), 12u);
	EXPECT_EQ(writer.byteCount(), expectedBytes);

	// A padded window is stored packed.
	const auto wide = image(8, 4);
	const ImageView window = wide->view().subView({2, 1, 6, 3});
	ASSERT_TRUE(writer.store(keyOf('f'), window).ok());
	const InferenceImage found = writer.find(keyOf('f'));
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->desc().bounds, (RectI{2, 1, 6, 3}));
	EXPECT_EQ(std::memcmp(found->view().pixel(2, 2), wide->view().pixel(2, 2), window.desc.minimumRowBytes()), 0);

	// Storing a key again replaces its file.
	ASSERT_TRUE(writer.store(keyOf('f'), image(2, 2)->view()).ok());
	EXPECT_EQ(writer.find(keyOf('f'))->desc().bounds, (RectI{0, 0, 2, 2}));
	EXPECT_EQ(writer.fileCount(), 13u);
}

TEST(InferenceDiskCache, AMissingOrDamagedFileReadsAsNothing)
{
	const std::filesystem::path dir = freshDirectory("damaged");
	InferenceDiskCache cache(dir, 1 << 20);
	const std::string key = keyOf('a');
	EXPECT_EQ(cache.find(key), nullptr);
	EXPECT_EQ(cache.find("not-a-key"), nullptr);
	EXPECT_EQ(cache.find(std::string(64, 'G')), nullptr);
	EXPECT_EQ(cache.store("../escape", image(2, 2)->view()).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(cache.store(key, ImageView{}).code(), ErrorCode::InvalidParameter);
	EXPECT_EQ(cache.fileCount(), 0u);

	ASSERT_TRUE(cache.store(key, image(4, 4)->view()).ok());
	const std::filesystem::path file = dir / (key + ".ogic");
	ASSERT_TRUE(std::filesystem::exists(file));
	const std::vector<char> good = readFile(file);
	ASSERT_EQ(good.size(), 80u + 4u * 4u * 16u);
	ASSERT_NE(cache.find(key), nullptr);

	const auto damaged = [&](std::size_t offset, char value) {
		std::vector<char> bytes = good;
		bytes[offset] = value;
		writeFile(file, bytes);
		return cache.find(key);
	};
	EXPECT_EQ(damaged(0, 'X'), nullptr);                  // the magic
	EXPECT_EQ(damaged(4, 9), nullptr);                    // the codec version
	EXPECT_EQ(damaged(16, 0), nullptr);                   // x2 == x1: no width
	EXPECT_EQ(damaged(19, 0x7F), nullptr);                // a width past the largest side
	EXPECT_EQ(damaged(40, 0), nullptr);                   // no depth
	EXPECT_EQ(damaged(40, 9), nullptr);                   // an unknown depth
	EXPECT_EQ(damaged(41, 9), nullptr);                   // an unknown layout
	EXPECT_EQ(damaged(42, 9), nullptr);                   // an unknown premultiplication
	EXPECT_EQ(damaged(43, 9), nullptr);                   // an unknown field
	EXPECT_EQ(damaged(68, 1), nullptr);                   // row bytes that disagree with the layout
	EXPECT_EQ(damaged(72, 1), nullptr);                   // a payload length that disagrees

	std::vector<char> cut(good.begin(), good.begin() + 40);
	writeFile(file, cut);
	EXPECT_EQ(cache.find(key), nullptr);
	cut.assign(good.begin(), good.end() - 8);
	writeFile(file, cut);
	EXPECT_EQ(cache.find(key), nullptr);
	std::vector<char> longer = good;
	longer.push_back(0);
	writeFile(file, longer);
	EXPECT_EQ(cache.find(key), nullptr);

	writeFile(file, good);
	EXPECT_NE(cache.find(key), nullptr);
}

TEST(InferenceDiskCache, TheLeastRecentlyUsedFilesLeaveAtTheLimit)
{
	const std::filesystem::path dir = freshDirectory("limit");
	const std::uint64_t one = 80 + 4 * 4 * 16;
	InferenceDiskCache cache(dir, one * 2 + 10);
	ASSERT_TRUE(cache.store(keyOf('a'), image(4, 4)->view()).ok());
	ASSERT_TRUE(cache.store(keyOf('b'), image(4, 4)->view()).ok());
	EXPECT_EQ(cache.byteCount(), one * 2);
	setAge(dir / (keyOf('a') + ".ogic"), std::chrono::seconds(200));
	setAge(dir / (keyOf('b') + ".ogic"), std::chrono::seconds(100));
	// Reading `a` makes `b` the least recently used.
	ASSERT_NE(cache.find(keyOf('a')), nullptr);
	ASSERT_TRUE(cache.store(keyOf('c'), image(4, 4)->view()).ok());
	EXPECT_EQ(cache.fileCount(), 2u);
	EXPECT_NE(cache.find(keyOf('a')), nullptr);
	EXPECT_EQ(cache.find(keyOf('b')), nullptr);
	EXPECT_NE(cache.find(keyOf('c')), nullptr);

	// An image larger than the limit is not kept, and nothing else leaves for it.
	EXPECT_TRUE(cache.store(keyOf('d'), image(16, 16)->view()).ok());
	EXPECT_EQ(cache.find(keyOf('d')), nullptr);
	EXPECT_EQ(cache.fileCount(), 2u);
}

TEST(InferenceDiskCache, ClearAndTheSweepTouchOnlyTheCachesOwnFiles)
{
	const std::filesystem::path dir = freshDirectory("own");
	InferenceDiskCache cache(dir, 1 << 20);
	ASSERT_TRUE(cache.store(keyOf('a'), image(4, 4)->view()).ok());
	const std::filesystem::path notes = dir / "notes.txt";
	const std::filesystem::path otherCache = dir / "short.ogic";
	const std::filesystem::path staleWrite = dir / (keyOf('b') + ".0123456789abcdef.tmp");
	const std::filesystem::path liveWrite = dir / (keyOf('c') + ".0123456789abcdef.tmp");
	const std::filesystem::path otherTemporary = dir / "download.tmp";
	for (const std::filesystem::path& file : {notes, otherCache, staleWrite, liveWrite, otherTemporary}) {
		writeFile(file, {'x'});
	}
	setAge(staleWrite, std::chrono::hours(3));
	setAge(otherTemporary, std::chrono::hours(3));
	EXPECT_EQ(cache.fileCount(), 1u);

	// A store sweeps the temporary file a dead writer left an hour or more ago.
	ASSERT_TRUE(cache.store(keyOf('d'), image(4, 4)->view()).ok());
	EXPECT_FALSE(std::filesystem::exists(staleWrite));
	EXPECT_TRUE(std::filesystem::exists(liveWrite));
	EXPECT_TRUE(std::filesystem::exists(otherTemporary));
	EXPECT_EQ(cache.fileCount(), 2u);

	cache.clear();
	EXPECT_EQ(cache.fileCount(), 0u);
	EXPECT_EQ(cache.byteCount(), 0u);
	EXPECT_TRUE(std::filesystem::exists(notes));
	EXPECT_TRUE(std::filesystem::exists(otherCache));
	EXPECT_TRUE(std::filesystem::exists(liveWrite));
	EXPECT_TRUE(std::filesystem::exists(otherTemporary));
}

TEST(InferenceDiskCache, AFolderThatCannotBeMadeFailsTheStoreAndReadsAsEmpty)
{
	const std::filesystem::path dir = freshDirectory("blocked");
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	const std::filesystem::path blocker = dir / "file";
	writeFile(blocker, {'x'});
	// A folder under a regular file cannot be created on any platform.
	InferenceDiskCache cache(blocker / "cache", 1 << 20);
	EXPECT_EQ(cache.store(keyOf('a'), image(4, 4)->view()).code(), ErrorCode::Failed);
	EXPECT_EQ(cache.find(keyOf('a')), nullptr);
	EXPECT_EQ(cache.fileCount(), 0u);
	EXPECT_EQ(cache.byteCount(), 0u);
	cache.clear();
}

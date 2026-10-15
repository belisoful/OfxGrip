// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/AnalysisExtension.hpp>
#include <ofxgrip/core/FrameData.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

std::filesystem::path freshDirectory(const char* name)
{
	static int counter = 0;
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ofxgrip-framedata-" + std::to_string(++counter)) / name;
	std::error_code ec;
	std::filesystem::remove_all(dir.parent_path(), ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

Bytes bytesOf(std::size_t n, std::uint8_t fill)
{
	return Bytes(n, fill);
}

}  // namespace

TEST(FrameData, StoresSparseRecordsAndSeeksBackward)
{
	FrameData data;
	EXPECT_FALSE(data.dirty());
	EXPECT_EQ(data.size(), 0u);
	EXPECT_FALSE(data.at(0).has_value());
	EXPECT_FALSE(data.latestAtOrBefore(10).has_value());
	data.set(10, Bytes{1});
	data.set(20, Bytes{2});
	data.set(5, Bytes{3});
	EXPECT_TRUE(data.dirty());
	EXPECT_EQ(data.size(), 3u);
	EXPECT_EQ(data.frames(), (std::vector<long>{5, 10, 20}));
	EXPECT_TRUE(data.has(10));
	EXPECT_FALSE(data.has(11));
	EXPECT_EQ(*data.at(10), Bytes{1});
	long found = -1;
	EXPECT_EQ(*data.latestAtOrBefore(15, &found), Bytes{1});
	EXPECT_EQ(found, 10);
	EXPECT_EQ(*data.latestAtOrBefore(20, &found), Bytes{2});
	EXPECT_EQ(found, 20);
	EXPECT_EQ(*data.latestAtOrBefore(100), Bytes{2});
	EXPECT_FALSE(data.latestAtOrBefore(4).has_value());
	EXPECT_EQ(*data.latestAtOrBefore(5), Bytes{3});
	data.remove(10);
	data.remove(999);
	EXPECT_EQ(data.size(), 2u);
	data.clearDirty();
	data.clear();
	EXPECT_TRUE(data.dirty());
	data.clearDirty();
	data.clear();
	EXPECT_FALSE(data.dirty());
	data.frameDuration = 1.0 / 25.0;
	EXPECT_EQ(data.frameIndexFor(1.0), 25);
	EXPECT_EQ(data.frameIndexFor(0.039), 0);
	data.frameDuration = 0.0;
	EXPECT_EQ(data.frameIndexFor(3.7), 3);
}

TEST(FrameData, CodecRoundTripsInlineRecords)
{
	FrameData data;
	data.frameDuration = 1.0 / 30.0;
	data.instanceUuid = "ABC";
	data.header["note"] = Value("hi");
	data.header["scale"] = Value(2.5);
	data.set(1, Bytes{1, 2, 3});
	data.set(-4, Bytes{});
	data.setSpillThreshold(FrameData::kNeverSpill);
	const Bytes encoded = data.encode();
	FrameData back;
	ASSERT_TRUE(FrameData::decode(encoded, {}, back).ok());
	EXPECT_DOUBLE_EQ(back.frameDuration, 1.0 / 30.0);
	EXPECT_EQ(back.instanceUuid, "ABC");
	EXPECT_EQ(back.header.at("note"), Value("hi"));
	EXPECT_EQ(back.header.at("scale"), Value(2.5));
	EXPECT_EQ(back.spillThreshold(), FrameData::kNeverSpill);
	EXPECT_EQ(*back.at(1), (Bytes{1, 2, 3}));
	EXPECT_EQ(*back.at(-4), Bytes{});
	EXPECT_FALSE(back.dirty());
	EXPECT_FALSE(back.isSpilled(1));
	for (std::size_t cut = 0; cut < encoded.size(); ++cut) {
		FrameData partial;
		EXPECT_FALSE(FrameData::decode(std::span<const std::uint8_t>(encoded.data(), cut), {}, partial).ok()) << cut;
	}
	Bytes newer = encoded;
	newer[4] = 9;
	FrameData out;
	EXPECT_EQ(FrameData::decode(newer, {}, out).code(), ErrorCode::Codec);
}

TEST(FrameData, SpillsLargeRecordsToTheCacheFolder)
{
	const std::filesystem::path dir = freshDirectory("spill");
	FrameData data;
	data.instanceUuid = "INST";
	data.setSpillDirectory(dir / "sub");
	data.setSpillThreshold(16);
	EXPECT_EQ(data.spillThreshold(), 16u);
	EXPECT_EQ(data.spillDirectory(), dir / "sub");
	data.set(3, bytesOf(100, 7));
	data.set(4, bytesOf(8, 1));
	EXPECT_EQ(data.spillFileFor(3), dir / "sub" / "INST-3.ogfd");
	const Bytes encoded = data.encode();
	EXPECT_TRUE(data.isSpilled(3));
	EXPECT_FALSE(data.isSpilled(4));
	EXPECT_TRUE(std::filesystem::exists(data.spillFileFor(3)));
	EXPECT_LT(encoded.size(), 100u);
	// The record stays readable from the writer and from a decoded copy.
	EXPECT_EQ(data.at(3)->size(), 100u);
	FrameData back;
	ASSERT_TRUE(FrameData::decode(encoded, dir / "sub", back).ok());
	EXPECT_TRUE(back.isSpilled(3));
	ASSERT_TRUE(back.at(3).has_value());
	EXPECT_EQ(back.at(3)->size(), 100u);
	EXPECT_EQ((*back.at(3))[99], 7);
	EXPECT_EQ(*back.at(4), bytesOf(8, 1));
	long found = 0;
	EXPECT_EQ(back.latestAtOrBefore(3, &found)->size(), 100u);
	// Re-encoding a decoded store keeps the marker without re-reading the file.
	const Bytes again = back.encode();
	EXPECT_EQ(again.size(), encoded.size());
	// Without the spill files, the record reads as missing and the seek skips it.
	FrameData elsewhere;
	ASSERT_TRUE(FrameData::decode(encoded, dir / "nowhere", elsewhere).ok());
	EXPECT_FALSE(elsewhere.at(3).has_value());
	EXPECT_EQ(*elsewhere.latestAtOrBefore(5, &found), bytesOf(8, 1));
	EXPECT_EQ(found, 4);
	EXPECT_FALSE(elsewhere.latestAtOrBefore(3).has_value());
	// Removing a spilled record deletes its file; clearing deletes the rest.
	data.set(5, bytesOf(50, 2));
	data.encode();
	EXPECT_TRUE(std::filesystem::exists(data.spillFileFor(5)));
	data.remove(5);
	EXPECT_FALSE(std::filesystem::exists(data.spillFileFor(5)));
	data.clear();
	EXPECT_FALSE(std::filesystem::exists(data.spillFileFor(3)));
	// No spill directory, no spill, however large the record.
	FrameData inline_;
	inline_.setSpillThreshold(1);
	inline_.set(1, bytesOf(100, 3));
	const Bytes whole = inline_.encode();
	EXPECT_GT(whole.size(), 100u);
	EXPECT_FALSE(inline_.isSpilled(1));
	// An unwritable spill directory keeps the record inline.
	FrameData blocked;
	blocked.setSpillDirectory("/dev/null/nowhere");
	blocked.setSpillThreshold(1);
	blocked.set(1, bytesOf(100, 3));
	EXPECT_GT(blocked.encode().size(), 100u);
	EXPECT_FALSE(blocked.isSpilled(1));
	EXPECT_EQ(FrameData().spillFileFor(2).filename(), "frame-2.ogfd");
}

TEST(AnalysisExtension, LoadsSavesAndAnswersRecordsByTime)
{
	EffectManifest m;
	m.identity.ofxId = "com.test.analysis";
	m.identity.name = "Analysis";
	const std::filesystem::path cache = freshDirectory("cache");
	FakeEffectHost host(m);
	host.fakes().pathService.cache = cache;
	ExtensionSystem system(host);
	auto* ext = static_cast<AnalysisExtension*>(system.load(std::make_unique<AnalysisExtension>()));
	ASSERT_NE(ext, nullptr);
	EXPECT_EQ(ext->priority(EventId::CreateInstance), kPriorityFrameworkCapture);
	EXPECT_EQ(ext->priority(EventId::Flush), kPriorityFrameworkFlush);
	EXPECT_EQ(ext->priority(EventId::Render), kPriorityDefault);
	host.post(EventId::CreateInstance);
	EXPECT_FALSE(ext->loadedFromDocument());
	EXPECT_EQ(ext->data().spillDirectory(), cache / "analysis");
	EXPECT_FALSE(ext->data().instanceUuid.empty());
	EXPECT_DOUBLE_EQ(ext->data().frameDuration, 1.0 / 24.0);
	EXPECT_FALSE(ext->recordAt(0).has_value());
	host.post(EventId::Flush);  // the fresh UUID is saved
	ASSERT_EQ(host.fakes().persist.blobs.count(std::string(AnalysisExtension::kPersistenceKey)), 1u);
	ext->data().set(2, Bytes{9});
	ext->data().set(7, Bytes{8});
	host.post(EventId::Flush);
	host.post(EventId::Flush);
	EXPECT_EQ(*ext->recordAt(2.0), Bytes{9});
	EXPECT_EQ(*ext->recordAt(6.9), Bytes{9});
	EXPECT_EQ(*ext->recordAt(7.0), Bytes{8});
	EXPECT_FALSE(ext->recordAt(1.5).has_value());
	const std::string uuid = ext->data().instanceUuid;

	// A second instance over the same document loads the store and keeps the UUID.
	FakeEffectHost again(m);
	again.fakes().pathService.cache = cache;
	again.fakes().persist.blobs = host.fakes().persist.blobs;
	ExtensionSystem system2(again);
	auto* ext2 = static_cast<AnalysisExtension*>(system2.load(std::make_unique<AnalysisExtension>()));
	again.post(EventId::CreateInstance);
	EXPECT_TRUE(ext2->loadedFromDocument());
	EXPECT_EQ(ext2->data().instanceUuid, uuid);
	EXPECT_EQ(*ext2->recordAt(7), Bytes{8});
	// An unreadable blob is discarded with a warning.
	FakeEffectHost broken(m);
	broken.fakes().persist.blobs[std::string(AnalysisExtension::kPersistenceKey)] = Bytes{1, 2};
	ExtensionSystem system3(broken);
	auto* ext3 = static_cast<AnalysisExtension*>(system3.load(std::make_unique<AnalysisExtension>()));
	broken.post(EventId::CreateInstance);
	EXPECT_FALSE(ext3->loadedFromDocument());
	EXPECT_TRUE(broken.fakes().logService.contains("unreadable"));
	ext3->markDirty();
	broken.post(EventId::Flush);
	PlainEvent other(EventId::Render);
	broken.bus().post(other);
}

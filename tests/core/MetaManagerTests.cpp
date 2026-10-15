// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/MetaManager.hpp>

#include <gtest/gtest.h>

#include <thread>

using namespace ofxgrip;

TEST(MetaManager, RecordsTagsAndTheReverseIndex)
{
	MetaManager m;
	EXPECT_FALSE(m.dirty());
	EXPECT_FALSE(m.has(1));
	EXPECT_EQ(m.metaCount(1), -1);
	m.add(1);
	EXPECT_TRUE(m.has(1));
	EXPECT_TRUE(m.dirty());
	EXPECT_EQ(m.metaCount(1), 0);
	m.addTag(1, "color");
	m.addTag(1, "color");
	m.addTag(1, "");
	m.addTag(2, "color");
	m.addTag(2, "shape");
	EXPECT_EQ(m.tags(1), std::vector<std::string>{"color"});
	EXPECT_TRUE(m.hasTag(2, "shape"));
	EXPECT_FALSE(m.hasTag(2, "size"));
	EXPECT_FALSE(m.hasTag(9, "shape"));
	EXPECT_EQ(m.parametersWithTag("color"), (std::vector<ParameterId>{1, 2}));
	EXPECT_EQ(m.allTags(), (std::vector<std::string>{"color", "shape"}));
	EXPECT_EQ(m.tagCount(), 2u);
	EXPECT_EQ(m.parameters(), (std::vector<ParameterId>{1, 2}));
	EXPECT_EQ(m.size(), 2u);

	m.removeTag(1, "color");
	m.removeTag(1, "missing");
	m.removeTag(9, "color");
	EXPECT_EQ(m.parametersWithTag("color"), std::vector<ParameterId>{2});
	m.setTags(2, {"a", "b", "a", ""});
	EXPECT_EQ(m.tags(2), (std::vector<std::string>{"a", "b"}));
	EXPECT_TRUE(m.parametersWithTag("shape").empty());
	m.removeAllTags(2);
	m.removeAllTags(2);
	m.removeAllTags(9);
	EXPECT_EQ(m.tagCount(), 0u);
	EXPECT_TRUE(m.tags(9).empty());

	m.addTag(2, "x");
	m.remove(2);
	m.remove(2);
	EXPECT_FALSE(m.has(2));
	EXPECT_TRUE(m.parametersWithTag("x").empty());
	EXPECT_TRUE(m.record(2) == std::nullopt);
	ASSERT_TRUE(m.record(1).has_value());
	EXPECT_EQ(m.record(1)->id, 1u);
	m.clear();
	m.clear();
	EXPECT_EQ(m.size(), 0u);
}

TEST(MetaManager, MetaEntries)
{
	MetaManager m;
	m.setMeta(5, "unit", Value("px"));
	m.setMeta(5, "", Value(1));
	EXPECT_TRUE(m.has(5));
	EXPECT_TRUE(m.hasMeta(5, "unit"));
	EXPECT_FALSE(m.hasMeta(5, "none"));
	EXPECT_FALSE(m.hasMeta(6, "unit"));
	EXPECT_EQ(m.meta(5, "unit")->as<std::string>(), "px");
	EXPECT_EQ(m.metaCount(5), 1);
	EXPECT_EQ(m.metaKeys(5), std::vector<std::string>{"unit"});
	EXPECT_TRUE(m.metaKeys(6).empty());
	EXPECT_TRUE(m.metaOf(6).empty());
	m.clearDirty();
	m.setMeta(5, "unit", Value("px"));
	EXPECT_FALSE(m.dirty());
	m.setMeta(5, "unit", Value("cm"));
	EXPECT_TRUE(m.dirty());
	m.setMetaOf(5, {{"a", Value(1)}, {"", Value(2)}});
	EXPECT_EQ(m.metaOf(5).size(), 1u);
	m.clearDirty();
	m.setMetaOf(5, {{"a", Value(1)}});
	EXPECT_FALSE(m.dirty());
	m.removeMeta(5, "zzz");
	m.removeMeta(6, "a");
	EXPECT_FALSE(m.dirty());
	m.removeMeta(5, "a");
	EXPECT_TRUE(m.dirty());
	m.setMeta(5, "b", Value(2));
	m.removeAllMeta(5);
	m.removeAllMeta(5);
	m.removeAllMeta(6);
	EXPECT_EQ(m.metaCount(5), 0);
	m.markDirty();
	EXPECT_TRUE(m.dirty());
}

TEST(MetaManager, SeedIsAdditiveAndMergePrefersTheStoredRecord)
{
	ParameterSpec spec = ParameterSpec::floatParam(7, "amount", "Amount", 1, 0, 2).withTags({"color", ""}).withMeta("unit", Value("px")).withMeta("", Value(0));
	MetaManager seeded;
	seeded.setMeta(7, "unit", Value("cm"));
	seeded.addTag(7, "user");
	seeded.seed(spec);
	seeded.seed(spec);
	EXPECT_EQ(seeded.tags(7), (std::vector<std::string>{"user", "color"}));
	EXPECT_EQ(seeded.meta(7, "unit")->as<std::string>(), "cm");

	MetaManager stored;
	stored.setTags(7, {"doc"});
	stored.setMeta(7, "unit", Value("mm"));
	stored.setTags(8, {"other"});
	MetaManager target;
	target.seed(spec);
	target.setMeta(7, "extra", Value(1));
	target.clearDirty();
	target.mergeFrom(stored);
	EXPECT_TRUE(target.dirty());
	EXPECT_EQ(target.tags(7), (std::vector<std::string>{"doc", "color"}));
	EXPECT_EQ(target.meta(7, "unit")->as<std::string>(), "mm");
	EXPECT_EQ(target.meta(7, "extra")->toInt(), 1);
	EXPECT_EQ(target.tags(8), std::vector<std::string>{"other"});
	EXPECT_EQ(target.parametersWithTag("doc"), std::vector<ParameterId>{7});
	EXPECT_EQ(target.parametersWithTag("color"), std::vector<ParameterId>{7});
	target.clearDirty();
	target.mergeFrom(stored);
	EXPECT_FALSE(target.dirty());
}

TEST(MetaManager, CodecRoundTripsAndRejectsBadStreams)
{
	MetaManager m;
	m.setTags(1, {"a", "b"});
	m.setMeta(1, "k", Value(Point2D{1, 2}));
	m.setMeta(1, "s", Value("text"));
	m.add(2);
	m.setMeta(3, "bytes", Value(Bytes{1, 2}));
	const Bytes encoded = m.encode();
	MetaManager back;
	ASSERT_TRUE(MetaManager::decode(encoded, back).ok());
	EXPECT_TRUE(back == m);
	EXPECT_FALSE(back.dirty());
	EXPECT_EQ(back.parametersWithTag("b"), std::vector<ParameterId>{1});
	EXPECT_EQ(back.encode(), encoded);

	for (std::size_t cut = 0; cut < encoded.size(); ++cut) {
		MetaManager partial;
		EXPECT_FALSE(MetaManager::decode(std::span<const std::uint8_t>(encoded.data(), cut), partial).ok()) << cut;
	}
	Bytes newer = encoded;
	newer[4] = 0xFF;
	MetaManager out;
	EXPECT_EQ(MetaManager::decode(newer, out).code(), ErrorCode::Codec);
	Bytes wrongMagic = encoded;
	wrongMagic[0] = 'X';
	EXPECT_FALSE(MetaManager::decode(wrongMagic, out).ok());

	MetaManager copy(m);
	EXPECT_TRUE(copy == m);
	MetaManager assigned;
	assigned = m;
	MetaManager& self = assigned;
	assigned = self;
	EXPECT_TRUE(assigned == m);
	EXPECT_TRUE(m == m);
	assigned.addTag(1, "c");
	EXPECT_FALSE(assigned == m);
}

TEST(MetaManager, LockComposesMultiStepEditsAcrossThreads)
{
	MetaManager m;
	std::thread writer([&m]() {
		for (int i = 0; i < 200; ++i) {
			auto lock = m.lock();
			m.addTag(1, "t" + std::to_string(i % 5));
			m.setMeta(1, "n", Value(i));
		}
	});
	for (int i = 0; i < 200; ++i) {
		auto lock = m.lock();
		(void)m.tags(1);
		(void)m.metaOf(1);
	}
	writer.join();
	EXPECT_EQ(m.tagCount(), 5u);
	EXPECT_EQ(m.meta(1, "n")->toInt(), 199);
}

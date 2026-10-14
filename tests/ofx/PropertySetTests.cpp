// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/PropertySetImpl.hpp>
#include <ofxgrip/ofx/PropertySet.hpp>
#include <ofxgrip/ofx/Suites.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(PropertySet, TypedReadsAndWrites)
{
	minihost::PropSet backing;
	PropertySet p(minihost::PropSet::suite(), backing.handle());
	ASSERT_TRUE(p.valid());
	EXPECT_TRUE(p.setInt("i", 3).ok());
	EXPECT_TRUE(p.setDouble("d", 2.5).ok());
	EXPECT_TRUE(p.setString("s", "hello").ok());
	int marker = 0;
	EXPECT_TRUE(p.setPointer("p", &marker).ok());
	EXPECT_EQ(p.getInt("i").value_or(-1), 3);
	EXPECT_EQ(p.getDouble("d").value_or(-1), 2.5);
	EXPECT_EQ(p.getString("s").value_or(""), "hello");
	EXPECT_EQ(p.getPointer("p").value_or(nullptr), &marker);
	EXPECT_EQ(p.getInt("missing", 9, 0), 9);
	EXPECT_FALSE(p.getString("missing").has_value());
	EXPECT_EQ(p.dimension("missing"), -1);
	EXPECT_TRUE(p.has("i"));
	EXPECT_FALSE(p.has("missing"));
}

TEST(PropertySet, VectorsAndDimensions)
{
	minihost::PropSet backing;
	PropertySet p(minihost::PropSet::suite(), backing.handle());
	EXPECT_TRUE(p.setDoubles("v", {1, 2, 3, 4}).ok());
	EXPECT_EQ(p.dimension("v"), 4);
	EXPECT_EQ(p.getDoubles("v"), (std::vector<double>{1, 2, 3, 4}));
	EXPECT_TRUE(p.setInts("n", {7, 8}).ok());
	EXPECT_EQ(p.getInts("n"), (std::vector<int>{7, 8}));
	EXPECT_TRUE(p.setStrings("names", {"a", "b"}).ok());
	EXPECT_EQ(p.getStrings("names"), (std::vector<std::string>{"a", "b"}));
}

TEST(PropertySet, TolerantWritesRecordRejections)
{
	minihost::PropSet backing;
	backing.makeReadOnly("locked");
	PropertySet p(minihost::PropSet::suite(), backing.handle());
	std::vector<std::string> rejected;
	EXPECT_FALSE(p.trySetString("locked", "x", &rejected));
	EXPECT_FALSE(p.trySetInt("locked", 1, &rejected));
	EXPECT_FALSE(p.trySetDouble("locked", 1.0, &rejected));
	EXPECT_TRUE(p.trySetInt("free", 1, &rejected));
	EXPECT_EQ(rejected.size(), 3u);
	EXPECT_EQ(p.setInt("locked", 2).code(), ErrorCode::Value);
}

TEST(PropertySet, InvalidHandleFailsSoftly)
{
	PropertySet p;
	EXPECT_FALSE(p.valid());
	EXPECT_FALSE(p.getInt("x").has_value());
	EXPECT_EQ(p.setInt("x", 1).code(), ErrorCode::BadHandle);
	EXPECT_EQ(p.dimension("x"), -1);
}

TEST(PropertySet, StatusMapping)
{
	EXPECT_TRUE(statusToError(kOfxStatOK, "x").ok());
	EXPECT_TRUE(statusToError(kOfxStatReplyDefault, "x").ok());
	EXPECT_EQ(statusToError(kOfxStatErrBadHandle, "x").code(), ErrorCode::BadHandle);
	EXPECT_EQ(statusToError(kOfxStatErrMemory, "x").code(), ErrorCode::Memory);
	EXPECT_EQ(statusToError(999, "x").code(), ErrorCode::Unknown);
	EXPECT_EQ(errorToStatus(Error::none()), kOfxStatOK);
	EXPECT_EQ(errorToStatus(Error::cancelled()), kOfxStatOK);
	EXPECT_EQ(errorToStatus(Error::make(ErrorCode::Render, "r")), kOfxStatFailed);
	EXPECT_EQ(errorToStatus(Error::make(ErrorCode::MissingHostFeature, "m")), kOfxStatErrMissingHostFeature);
	EXPECT_EQ(std::string(statusName(kOfxStatReplyYes)), "kOfxStatReplyYes");
}

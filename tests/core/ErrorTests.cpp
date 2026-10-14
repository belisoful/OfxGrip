// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Error.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Error, DefaultIsSuccess)
{
	Error e;
	EXPECT_TRUE(e.ok());
	EXPECT_FALSE(e);
	EXPECT_EQ(e.code(), ErrorCode::None);
	EXPECT_TRUE(Error::none().ok());
}

TEST(Error, CarriesCodeMessagesAndContext)
{
	Error e = Error::make(ErrorCode::InvalidManifest, "Bad manifest", "key missing").with("parameter", "gain");
	EXPECT_FALSE(e.ok());
	EXPECT_TRUE(e);
	EXPECT_EQ(e.message(), "Bad manifest");
	EXPECT_EQ(e.developerMessage(), "key missing");
	ASSERT_EQ(e.context().size(), 1u);
	EXPECT_EQ(e.context()[0].first, "parameter");
	const std::string text = e.describe();
	EXPECT_NE(text.find("InvalidManifest"), std::string::npos);
	EXPECT_NE(text.find("[parameter=gain]"), std::string::npos);
	EXPECT_NE(text.find("(key missing)"), std::string::npos);
}

TEST(Error, CancellationIsNotAFailureCode)
{
	Error c = Error::cancelled();
	EXPECT_TRUE(c.isCancellation());
	EXPECT_FALSE(c.ok());
	EXPECT_EQ(toString(c.code()), "Cancelled");
}

TEST(Error, ExceptionCarriesTheError)
{
	try {
		throw OfxGripException(Error::make(ErrorCode::Render, "boom"));
	} catch (const OfxGripException& e) {
		EXPECT_EQ(e.error().code(), ErrorCode::Render);
		EXPECT_NE(std::string(e.what()).find("boom"), std::string::npos);
	}
}

TEST(Error, ResultHoldsValueOrError)
{
	Result<int> ok(42);
	EXPECT_TRUE(ok);
	EXPECT_EQ(ok.value(), 42);
	EXPECT_EQ(ok.valueOr(0), 42);
	Result<int> bad(Error::make(ErrorCode::Failed, "no"));
	EXPECT_FALSE(bad);
	EXPECT_EQ(bad.valueOr(7), 7);
	EXPECT_EQ(bad.error().code(), ErrorCode::Failed);
}

TEST(Error, EveryCodeHasAName)
{
	for (ErrorCode code : {ErrorCode::None, ErrorCode::Failed, ErrorCode::Fatal, ErrorCode::Unknown, ErrorCode::MissingHostFeature, ErrorCode::Unsupported, ErrorCode::Exists, ErrorCode::Format,
	                       ErrorCode::Memory, ErrorCode::BadHandle, ErrorCode::BadIndex, ErrorCode::Value, ErrorCode::Cancelled, ErrorCode::InvalidManifest, ErrorCode::InvalidParameter,
	                       ErrorCode::NotFound, ErrorCode::WrongPhase, ErrorCode::Codec, ErrorCode::HostCall, ErrorCode::Render}) {
		EXPECT_FALSE(toString(code).empty());
	}
}

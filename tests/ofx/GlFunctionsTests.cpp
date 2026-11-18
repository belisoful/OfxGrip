// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/GlFunctions.hpp>
#include <ofxgrip/ofx/Interact.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

GlFunctions filledTable()
{
	GlFunctions table;
	table.begin = [](unsigned) {};
	table.end = []() {};
	table.vertex2d = [](double, double) {};
	table.color4f = [](float, float, float, float) {};
	table.lineWidth = [](float) {};
	table.enable = [](unsigned) {};
	table.disable = [](unsigned) {};
	table.lineStipple = [](int, unsigned short) {};
	table.matrixMode = [](unsigned) {};
	table.hasCurrentContext = []() { return true; };
	return table;
}

}  // namespace

TEST(GlFunctions, IsCompleteOnlyWithEveryFunction)
{
	EXPECT_FALSE(GlFunctions().complete());
	EXPECT_TRUE(filledTable().complete());
	GlFunctions table = filledTable();
	table.begin = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.end = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.vertex2d = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.color4f = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.lineWidth = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.enable = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.disable = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.lineStipple = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.matrixMode = nullptr;
	EXPECT_FALSE(table.complete());
	table = filledTable();
	table.hasCurrentContext = nullptr;
	EXPECT_FALSE(table.complete());
}

TEST(GlFunctions, TheSystemTableIsCompleteAndSeesNoContextInATest)
{
	const GlFunctions* system = GlFunctions::system();
	EXPECT_EQ(GlFunctions::system(), system);
	EXPECT_EQ(GlDrawKit::available(), system != nullptr);
#if defined(__APPLE__) || defined(_WIN32)
	ASSERT_NE(system, nullptr);
#endif
	if (system == nullptr) {
		GTEST_SKIP() << "No OpenGL library on this machine";
	}
	EXPECT_TRUE(system->complete());
	EXPECT_FALSE(system->hasCurrentContext());
	EXPECT_FALSE(GlDrawKit().active());
}

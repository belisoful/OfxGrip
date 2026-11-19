// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Portable environment-variable helpers for tests: POSIX has setenv and unsetenv, MSVC has
// _putenv_s. `useBuildTreeFolders` keeps a test program out of the developer's own folders.
#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ofxgrip::testing {

inline void setTestEnv(const char* name, const char* value)
{
#if defined(_WIN32)
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

inline void unsetTestEnv(const char* name)
{
#if defined(_WIN32)
	_putenv_s(name, "");
#else
	unsetenv(name);
#endif
}

/*! Points the log, support, and cache folders at `<root>/test-logs`, `<root>/test-support`, and
    `<root>/test-cache`. A variable the caller's environment already sets is kept. A test
    program calls this first in `main`, before any OfxGrip code reads the variables. */
inline void useBuildTreeFolders(const char* root)
{
	const auto point = [root](const char* name, const char* folder) {
		const char* current = std::getenv(name);
		if (current == nullptr || *current == '\0') {
			setTestEnv(name, (std::string(root) + "/" + folder).c_str());
		}
	};
	point("OFXGRIP_LOG_DIR", "test-logs");
	point("OFXGRIP_SUPPORT_DIR", "test-support");
	point("OFXGRIP_CACHE_DIR", "test-cache");
}

/*! Eight hex digits that are the same on every run of the running test and differ between
    tests. ctest runs each test in its own process and runs several at once, so a temporary
    folder named with the tag belongs to one test and no other test removes it. */
inline std::string currentTestTag()
{
	const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
	const std::string name = info != nullptr ? std::string(info->test_suite_name()) + "." + info->name() : std::string("none");
	std::uint32_t hash = 2166136261u;
	for (const char c : name) {
		hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
	}
	char text[9];
	std::snprintf(text, sizeof text, "%08x", static_cast<unsigned>(hash));
	return text;
}

}  // namespace ofxgrip::testing

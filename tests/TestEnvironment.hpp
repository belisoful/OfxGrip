// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// Portable environment-variable helpers for tests: POSIX has setenv and unsetenv, MSVC has
// _putenv_s.
#pragma once

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

}  // namespace ofxgrip::testing

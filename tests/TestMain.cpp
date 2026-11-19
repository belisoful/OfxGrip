// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The `main` of every test program: it points the OfxGrip folder variables into the build tree
// and then runs the tests.
#include "TestEnvironment.hpp"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
	ofxgrip::testing::useBuildTreeFolders(OFXGRIP_TEST_ROOT);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/ofx/Descriptor.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Descriptor, TypeTableCoversTheVocabulary)
{
	HostProfile profile;
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Float, profile), kOfxParamTypeDouble);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Angle, profile), kOfxParamTypeDouble);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Int, profile), kOfxParamTypeInteger);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Toggle, profile), kOfxParamTypeBoolean);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Switch, profile), kOfxParamTypeBoolean);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Point, profile), kOfxParamTypeDouble2D);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Point3D, profile), kOfxParamTypeDouble3D);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::RGB, profile), kOfxParamTypeRGB);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::RGBA, profile), kOfxParamTypeRGBA);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Menu, profile), kOfxParamTypeChoice);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Presets, profile), kOfxParamTypeChoice);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::PushButton, profile), kOfxParamTypePushButton);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Group, profile), kOfxParamTypeGroup);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Section, profile), kOfxParamTypeGroup);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::String, profile), kOfxParamTypeString);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Status, profile), kOfxParamTypeString);
	EXPECT_STREQ(ofxParameterTypeFor(ParameterType::Custom, profile), kOfxParamTypeCustom);
	EXPECT_EQ(ofxParameterTypeFor(ParameterType::WebView, profile), nullptr);
	EXPECT_EQ(ofxParameterTypeFor(ParameterType::LiveImage, profile), nullptr);
	EXPECT_EQ(ofxParameterTypeFor(ParameterType::Gradient, profile), nullptr);
	EXPECT_EQ(ofxParameterTypeFor(ParameterType::None, profile), nullptr);
}

TEST(Descriptor, SanitizesLabels)
{
	EXPECT_EQ(sanitizeLabel("a<b>&c", true), "a b  c");
	EXPECT_EQ(sanitizeLabel("tab\there", true), "tab here");
	EXPECT_EQ(sanitizeLabel("caf\xC3\xA9", true), "caf??");
	EXPECT_EQ(sanitizeLabel("caf\xC3\xA9", false), "caf\xC3\xA9");
}

TEST(Descriptor, FrameworkParametersAreHiddenState)
{
	const std::vector<ParameterSpec> extras = frameworkParameters(EffectManifest{});
	ASSERT_EQ(extras.size(), 1u);
	EXPECT_EQ(extras[0].id, kParameterIdInstanceState);
	EXPECT_EQ(extras[0].key, kInstanceStateParameterKey);
	EXPECT_TRUE(extras[0].hostFlags.has(HostFlag::Hidden));
	EXPECT_TRUE(extras[0].hostFlags.has(HostFlag::NotAnimatable));
	EXPECT_TRUE(extras[0].appFlags.has(AppFlag::NoState));
	EXPECT_FALSE(extras[0].hasState());
}

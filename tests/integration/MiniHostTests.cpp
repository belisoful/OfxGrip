// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/minihost/BundleLoader.hpp>
#include <ofxgrip/minihost/MiniHost.hpp>

#include "ofxParam.h"
#include "ofxParametricParam.h"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

TEST(MiniHostParams, KeyframesInterpolateDoublesAndHoldIntegers)
{
	Param d("d", kOfxParamTypeDouble);
	d.props().setInt(kOfxParamPropAnimates, 1);
	d.setNumericValueAt(0, {0.0});
	d.setNumericValueAt(10, {10.0});
	EXPECT_DOUBLE_EQ(d.numericValueAt(5)[0], 5.0);
	EXPECT_DOUBLE_EQ(d.numericValueAt(-5)[0], 0.0);
	EXPECT_DOUBLE_EQ(d.numericValueAt(50)[0], 10.0);
	EXPECT_EQ(d.keyCount(), 2);
	d.deleteKey(10);
	EXPECT_EQ(d.keyCount(), 1);
	d.deleteAllKeys();
	EXPECT_EQ(d.keyCount(), 0);
	Param i("i", kOfxParamTypeInteger);
	i.props().setInt(kOfxParamPropAnimates, 1);
	i.setNumericValueAt(0, {1});
	i.setNumericValueAt(10, {3});
	EXPECT_EQ(i.numericValueAt(9)[0], 1.0);
	EXPECT_EQ(i.numericValueAt(10)[0], 3.0);
	Param s("s", kOfxParamTypeString);
	s.setStringValue("x");
	s.setStringValueAt(5, "y");
	EXPECT_EQ(s.stringValueAt(3), "y");
	EXPECT_EQ(s.keyCount(), 0);
}

TEST(MiniHostParams, NonAnimatingParamsKeepOneValue)
{
	Param d("d", kOfxParamTypeDouble);
	d.props().setInt(kOfxParamPropAnimates, 0);
	d.setNumericValueAt(0, {1.0});
	d.setNumericValueAt(10, {2.0});
	EXPECT_DOUBLE_EQ(d.numericValueAt(0)[0], 2.0);
	EXPECT_EQ(d.keyCount(), 0);
}

TEST(MiniHostParams, InstantiateCopiesPropertiesAndDefaults)
{
	ParamSet set;
	Param* p = set.define(kOfxParamTypeDouble2D, "pt");
	ASSERT_NE(p, nullptr);
	p->props().setDoubles(kOfxParamPropDefault, {3.0, 4.0});
	p->props().setString(kOfxPropLabel, "Point");
	p->props().setInt(kOfxParamPropSecret, 1);
	EXPECT_EQ(set.define(kOfxParamTypeDouble2D, "pt"), nullptr);
	std::unique_ptr<ParamSet> instance = set.instantiate();
	Param* ip = instance->find("pt");
	ASSERT_NE(ip, nullptr);
	EXPECT_EQ(ip->numericValueAt(0), (std::vector<double>{3.0, 4.0}));
	EXPECT_EQ(ip->props().getString(kOfxPropLabel), "Point");
	EXPECT_EQ(ip->props().getInt(kOfxParamPropSecret), 1);
	EXPECT_EQ(ip->props().getString(kOfxPropType), kOfxTypeParameterInstance);
	Param* s = set.define(kOfxParamTypeString, "s");
	s->props().setString(kOfxParamPropDefault, "text");
	EXPECT_EQ(set.instantiate()->find("s")->stringValueAt(0), "text");
}

TEST(MiniHostParams, SuiteVarargsReadAndWrite)
{
	ParamSet set;
	set.define(kOfxParamTypeRGBA, "c");
	set.define(kOfxParamTypeInteger, "i");
	set.define(kOfxParamTypeString, "s");
	std::unique_ptr<ParamSet> instance = set.instantiate();
	const OfxParameterSuiteV1* suite = ParamSet::suite();
	OfxParamHandle c = nullptr;
	ASSERT_EQ(suite->paramGetHandle(instance->handle(), "c", &c, nullptr), kOfxStatOK);
	EXPECT_EQ(suite->paramSetValue(c, 0.1, 0.2, 0.3, 0.4), kOfxStatOK);
	double r = 0;
	double g = 0;
	double b = 0;
	double a = 0;
	EXPECT_EQ(suite->paramGetValue(c, &r, &g, &b, &a), kOfxStatOK);
	EXPECT_DOUBLE_EQ(b, 0.3);
	OfxParamHandle i = nullptr;
	ASSERT_EQ(suite->paramGetHandle(instance->handle(), "i", &i, nullptr), kOfxStatOK);
	EXPECT_EQ(suite->paramSetValueAtTime(i, 2.0, 7), kOfxStatOK);
	int iv = 0;
	EXPECT_EQ(suite->paramGetValueAtTime(i, 2.0, &iv), kOfxStatOK);
	EXPECT_EQ(iv, 7);
	OfxParamHandle s = nullptr;
	ASSERT_EQ(suite->paramGetHandle(instance->handle(), "s", &s, nullptr), kOfxStatOK);
	EXPECT_EQ(suite->paramSetValue(s, "hello"), kOfxStatOK);
	char* sv = nullptr;
	EXPECT_EQ(suite->paramGetValue(s, &sv), kOfxStatOK);
	EXPECT_STREQ(sv, "hello");
	OfxParamHandle missing = nullptr;
	EXPECT_EQ(suite->paramGetHandle(instance->handle(), "nope", &missing, nullptr), kOfxStatErrUnknown);
	unsigned int n = 9;
	EXPECT_EQ(suite->paramGetNumKeys(i, &n), kOfxStatOK);
	EXPECT_EQ(n, 1u);
	EXPECT_EQ(suite->paramEditBegin(instance->handle(), "edit"), kOfxStatOK);
	EXPECT_EQ(instance->editDepth, 1);
	EXPECT_EQ(suite->paramEditEnd(instance->handle()), kOfxStatOK);
	EXPECT_EQ(instance->editDepth, 0);
}

TEST(MiniHostParams, ParamCopyHonorsTheNoOpSwitch)
{
	ParamSet set;
	set.define(kOfxParamTypeDouble, "a");
	set.define(kOfxParamTypeDouble, "b");
	std::unique_ptr<ParamSet> instance = set.instantiate();
	instance->find("a")->setNumericValue({5.0});
	const OfxParameterSuiteV1* suite = ParamSet::suite();
	ParamSet::copyIsNoOp = true;
	EXPECT_EQ(suite->paramCopy(instance->find("b")->handle(), instance->find("a")->handle(), 0, nullptr), kOfxStatOK);
	EXPECT_EQ(instance->find("b")->numericValueAt(0)[0], 0.0);
	ParamSet::copyIsNoOp = false;
	EXPECT_EQ(suite->paramCopy(instance->find("b")->handle(), instance->find("a")->handle(), 0, nullptr), kOfxStatOK);
	EXPECT_EQ(instance->find("b")->numericValueAt(0)[0], 5.0);
}

TEST(MiniHostProps, WithheldAndReadOnlyProperties)
{
	PropSet p;
	p.setInt("a", 1);
	p.withhold("a");
	EXPECT_FALSE(p.has("a"));
	EXPECT_EQ(p.dimension("a"), -1);
	p.makeReadOnly("b");
	const OfxPropertySuiteV1* suite = PropSet::suite();
	EXPECT_EQ(suite->propSetInt(p.handle(), "b", 0, 1), kOfxStatErrValue);
	EXPECT_EQ(p.rejectedWrites.size(), 1u);
	EXPECT_EQ(suite->propSetInt(nullptr, "b", 0, 1), kOfxStatErrBadHandle);
	int v = 0;
	EXPECT_EQ(suite->propGetInt(p.handle(), "zzz", 0, &v), kOfxStatErrUnknown);
	EXPECT_EQ(suite->propGetInt(p.handle(), "b", 3, &v), kOfxStatErrUnknown);
	p.setDoubles("d", {1, 2});
	EXPECT_EQ(suite->propGetDouble(p.handle(), "d", 5, nullptr) == kOfxStatErrBadIndex, true);
	int count = 0;
	EXPECT_EQ(suite->propGetDimension(p.handle(), "d", &count), kOfxStatOK);
	EXPECT_EQ(count, 2);
	EXPECT_EQ(suite->propReset(p.handle(), "d"), kOfxStatOK);
	EXPECT_EQ(p.dimension("d"), -1);
	const char* strings[] = {"x", "y"};
	EXPECT_EQ(suite->propSetStringN(p.handle(), "s", 2, strings), kOfxStatOK);
	char* out[2] = {nullptr, nullptr};
	EXPECT_EQ(suite->propGetStringN(p.handle(), "s", 2, out), kOfxStatOK);
	EXPECT_STREQ(out[1], "y");
	EXPECT_EQ(p.getInt("d", 7), 7);
}

TEST(MiniHostOptions, PresetsDescribeTheirHosts)
{
	const HostOptions resolve = HostOptions::resolve21();
	EXPECT_EQ(resolve.name, "DaVinciResolveLite");
	EXPECT_FALSE(resolve.supportsTiles);
	EXPECT_TRUE(resolve.clipsAlwaysConnected);
	EXPECT_TRUE(resolve.threadSuiteDummy);
	EXPECT_NE(resolve.withheldSuites.count("OfxParametricParameterSuite"), 0u);
	EXPECT_EQ(HostOptions::nuke().depths.size(), 1u);
	EXPECT_TRUE(HostOptions::natron().supportsCustomInteract);
	EXPECT_TRUE(HostOptions::spec().supportsTiles);
}

TEST(MiniHostHost, HostPropertiesFollowTheOptions)
{
	MiniHost host(HostOptions::resolve21());
	PropSet& p = host.hostProps();
	EXPECT_EQ(p.getString(kOfxPropName), "DaVinciResolveLite");
	EXPECT_EQ(p.getInt(kOfxImageEffectPropSupportsTiles, -1), 0);
	EXPECT_FALSE(p.has(kOfxPropHostOSHandle));
	EXPECT_EQ(p.getInts(kOfxPropVersion), (std::vector<int>{21, 1, 1}));
	EXPECT_EQ(host.ofxHost()->fetchSuite(host.ofxHost()->host, kOfxParametricParameterSuite, 1), nullptr);
	EXPECT_NE(host.ofxHost()->fetchSuite(host.ofxHost()->host, kOfxPropertySuite, 1), nullptr);
	EXPECT_EQ(host.ofxHost()->fetchSuite(host.ofxHost()->host, "NoSuchSuite", 1), nullptr);
	const auto* v2 = static_cast<const OfxMessageSuiteV2*>(host.ofxHost()->fetchSuite(host.ofxHost()->host, kOfxMessageSuite, 2));
	ASSERT_NE(v2, nullptr);
	EXPECT_EQ(v2->setPersistentMessage, nullptr);
	EXPECT_EQ(host.load(nullptr), kOfxStatErrBadHandle);
	EXPECT_EQ(host.describe(), kOfxStatErrBadHandle);
	EXPECT_EQ(host.unload(), kOfxStatOK);
}

TEST(MiniHostHost, RampImageHasRampsAndOpaqueAlpha)
{
	std::shared_ptr<ImageBuffer> image = makeRampImage(4, 2, BitDepth::UInt8);
	const ImageView v = image->view();
	EXPECT_EQ(readComponent(v.pixel(0, 0), BitDepth::UInt8, 0), 0.0f);
	EXPECT_EQ(readComponent(v.pixel(3, 0), BitDepth::UInt8, 0), 1.0f);
	EXPECT_EQ(readComponent(v.pixel(0, 1), BitDepth::UInt8, 1), 1.0f);
	EXPECT_EQ(readComponent(v.pixel(0, 0), BitDepth::UInt8, 3), 1.0f);
	std::shared_ptr<ImageBuffer> alpha = makeRampImage(2, 2, BitDepth::Float, Components::Alpha);
	EXPECT_EQ(readComponent(alpha->view().pixel(1, 1), BitDepth::Float, 0), 1.0f);
}

TEST(BundleLoader, ReportsMissingAndInvalidBinaries)
{
	Bundle bundle;
	EXPECT_EQ(bundle.open("/nonexistent/thing.ofx.bundle").code(), ErrorCode::NotFound);
	EXPECT_FALSE(bundle.isOpen());
	EXPECT_EQ(bundle.pluginCount(), 0);
	EXPECT_EQ(bundle.plugin(0), nullptr);
	EXPECT_EQ(bundle.setHost(nullptr), kOfxStatOK);
	const std::filesystem::path binary = binaryPathInBundle("/tmp/Example.ofx.bundle");
	EXPECT_EQ(binary.filename().string(), "Example.ofx");
	EXPECT_EQ(binaryPathInBundle("/tmp/plain.ofx").string(), "/tmp/plain.ofx");
}

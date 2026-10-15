// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Parameters.hpp>

#include "FakeServices.hpp"

#include <gtest/gtest.h>

using namespace ofxgrip;
using namespace ofxgrip::testing;

namespace {

enum : ParameterId { kGain = 1, kMode = 2, kOn = 3, kCenter = 4, kColor = 5, kTint = 6, kName = 7, kGroup = 10, kInner = 11, kDeep = 12, kButton = 20, kSpace = 21 };

EffectManifest manifest()
{
	EffectManifest m;
	m.identity.ofxId = "com.test.params";
	m.identity.name = "Params";
	m.parameters.push_back(ParameterSpec::floatParam(kGain, "gain", "Gain", 1.0, 0, 10).withTags({"a"}).withMeta("unit", Value("x")).withResetValue(Value(2.0)));
	m.parameters.push_back(ParameterSpec::menuParam(kMode, "mode", "Mode", {{"soft", "Soft"}, {"hard", "Hard"}}, 1));
	m.parameters.push_back(ParameterSpec::boolParam(kOn, "on", "On", true).withTags({"a", "b"}));
	m.parameters.push_back(ParameterSpec::pointParam(kCenter, "center", "Center", {1, 2}));
	m.parameters.push_back(ParameterSpec::rgbParam(kColor, "color", "Color", {0.1, 0.2, 0.3}));
	m.parameters.push_back(ParameterSpec::rgbaParam(kTint, "tint", "Tint", {0.4, 0.5, 0.6, 0.7}));
	m.parameters.push_back(ParameterSpec::stringParam(kName, "name", "Name", "hello"));
	m.parameters.push_back(ParameterSpec::groupParam(kGroup, "group", "Group", {ParameterSpec::intParam(kInner, "inner", "Inner", 5, 0, 10), ParameterSpec::groupParam(kDeep, "deep", "Deep", {ParameterSpec::buttonParam(kButton, "button", "Button", "go")})}));
	ParameterSpec space;
	space.id = kSpace;
	space.key = "space";
	space.type = ParameterType::Point3D;
	space.label = "Space";
	space.defaultValue = Point3D{1, 2, 3};
	m.parameters.push_back(space);
	return m;
}

}  // namespace

TEST(Parameters, ModelBuildsTheTreeAndLooksUpByIdAndKey)
{
	FakeEffectHost host(manifest());
	ParameterModel model(host.params());
	EXPECT_EQ(model.size(), 12u);
	ASSERT_NE(model.find(kGain), nullptr);
	EXPECT_EQ(model.find("gain"), model[kGain]);
	EXPECT_EQ(model[std::string_view("gain")], model[kGain]);
	EXPECT_EQ(model.find(999), nullptr);
	EXPECT_EQ(model.find("nope"), nullptr);
	EXPECT_EQ(model.roots().size(), 9u);
	Parameter* group = model[kGroup];
	ASSERT_NE(group, nullptr);
	EXPECT_TRUE(group->isGroup());
	EXPECT_FALSE(group->hasState());
	ASSERT_EQ(group->children().size(), 2u);
	EXPECT_EQ(group->children()[0]->key(), "inner");
	EXPECT_EQ(group->children()[0]->parent(), group);
	EXPECT_EQ(group->descendants().size(), 3u);
	EXPECT_EQ(group->descendants()[2]->key(), "button");
	EXPECT_EQ(model[kButton]->selector(), "go");
	EXPECT_EQ(model[kGain]->parent(), nullptr);
	EXPECT_EQ(model[kGain]->dimensions(), 1);
	EXPECT_EQ(model[kGain]->type(), ParameterType::Float);
	EXPECT_EQ(model[kGain]->spec().key, "gain");
	EXPECT_TRUE(model[kGain]->appFlags().none());
	EXPECT_EQ(&model.service(), &host.params());
	EXPECT_EQ(model.metaManager(), nullptr);
	EXPECT_EQ(model.all().size(), 12u);
	model.rebuild();
	EXPECT_EQ(model.size(), 12u);
}

TEST(Parameters, TypedReadsAndWritesGoThroughTheService)
{
	FakeEffectHost host(manifest());
	ParameterModel model(host.params());
	EXPECT_DOUBLE_EQ(model[kGain]->number(0), 1.0);
	EXPECT_EQ(model[kMode]->integer(0), 1);
	EXPECT_EQ(model[kMode]->optionKey(0), "hard");
	EXPECT_EQ(model[kMode]->currentOptionKey(), "hard");
	EXPECT_TRUE(model[kOn]->boolean(0));
	EXPECT_EQ(model[kCenter]->point(0).y, 2.0);
	EXPECT_EQ(model[kSpace]->point3D(0).z, 3.0);
	EXPECT_DOUBLE_EQ(model[kColor]->rgb(0).b, 0.3);
	EXPECT_DOUBLE_EQ(model[kColor]->rgba(0).a, 1.0);
	EXPECT_DOUBLE_EQ(model[kTint]->rgba(0).a, 0.7);
	EXPECT_DOUBLE_EQ(model[kTint]->rgb(0).g, 0.5);
	EXPECT_EQ(model[kName]->string(0), "hello");
	EXPECT_EQ(model[kGain]->string(0, "fallback"), "fallback");
	EXPECT_EQ(model[kName]->point(0, {9, 9}).x, 9.0);
	EXPECT_EQ(model[kName]->point3D(0, {9, 9, 9}).x, 9.0);
	EXPECT_EQ(model[kName]->rgb(0, {9, 9, 9}).r, 9.0);
	EXPECT_EQ(model[kName]->rgba(0, {9, 9, 9, 9}).r, 9.0);
	EXPECT_EQ(model[kName]->optionKey(0), "");
	EXPECT_TRUE(model[kGain]->current().is<double>());

	EXPECT_TRUE(model[kGain]->set(0, Value(3.0)).ok());
	EXPECT_DOUBLE_EQ(model[kGain]->value(0).toDouble(), 3.0);
	EXPECT_TRUE(model[kGain]->setCurrent(Value(4.0)).ok());
	EXPECT_DOUBLE_EQ(model[kGain]->number(0), 4.0);
	EXPECT_TRUE(model[kMode]->setOption(0, "soft").ok());
	EXPECT_EQ(model[kMode]->integer(0), 0);
	EXPECT_TRUE(model[kMode]->setCurrentOption("hard").ok());
	EXPECT_EQ(model[kMode]->integer(0), 1);
	EXPECT_EQ(model[kMode]->setOption(0, "zzz").code(), ErrorCode::NotFound);
	EXPECT_EQ(model[kMode]->setCurrentOption("zzz").code(), ErrorCode::NotFound);
	EXPECT_TRUE(model[kGain]->reset(0).ok());
	EXPECT_DOUBLE_EQ(model[kGain]->number(0), 2.0);
	model[kGain]->setCurrent(Value(7.0));
	EXPECT_TRUE(model[kGain]->resetCurrent().ok());
	EXPECT_DOUBLE_EQ(model[kGain]->number(0), 2.0);
	model[kMode]->setCurrent(Value(0));
	EXPECT_TRUE(model[kMode]->reset(0).ok());
	EXPECT_EQ(model[kMode]->integer(0), 1);
	Value v;
	EXPECT_TRUE(model[kGain]->get(0, v).ok());
	EXPECT_TRUE(model[kGain]->getCurrent(v).ok());

	host.params().writable = false;
	EXPECT_EQ(model[kGain]->set(0, Value(1.0)).code(), ErrorCode::WrongPhase);
	EXPECT_EQ(host.params().writes.size(), 9u);
}

TEST(Parameters, FlagsLabelsOptionsRangesAndKeyframes)
{
	FakeEffectHost host(manifest());
	ParameterModel model(host.params());
	Parameter* gain = model[kGain];
	EXPECT_FALSE(gain->hidden());
	EXPECT_TRUE(gain->enabled());
	EXPECT_TRUE(gain->setHidden(true).ok());
	EXPECT_TRUE(gain->setHidden(true).ok());
	EXPECT_TRUE(gain->hidden());
	EXPECT_TRUE(gain->setEnabled(false).ok());
	EXPECT_TRUE(gain->setEnabled(false).ok());
	EXPECT_FALSE(gain->enabled());
	EXPECT_EQ(host.params().flagWrites.size(), 2u);
	EXPECT_TRUE(gain->setFlags(HostFlags()).ok());
	EXPECT_FALSE(gain->hidden());
	EXPECT_EQ(gain->label(), "Gain");
	EXPECT_TRUE(gain->setLabel("Loudness").ok());
	EXPECT_EQ(gain->label(), "Loudness");
	EXPECT_TRUE(gain->setHint("hint").ok());
	EXPECT_EQ(host.params().hints[kGain], "hint");
	EXPECT_TRUE(model[kMode]->setOptions({{"a", "A"}}).ok());
	EXPECT_EQ(host.params().optionWrites[kMode].size(), 1u);
	EXPECT_TRUE(gain->setRange({0}, {5}).ok());
	EXPECT_TRUE(gain->setDisplayRange({0}, {2}).ok());
	EXPECT_EQ(host.params().ranges[kGain].second[0], 5.0);
	EXPECT_EQ(host.params().displayRanges[kGain].second[0], 2.0);
	host.params().keyframes = 3;
	EXPECT_EQ(gain->keyframeCount(), 3);
	EXPECT_TRUE(gain->deleteAllKeyframes().ok());
	EXPECT_EQ(gain->keyframeCount(), 0);
	EXPECT_EQ(&gain->service(), &host.params());
}

TEST(Parameters, TagsAndMetaFallBackToTheRecordWithoutAManager)
{
	FakeEffectHost host(manifest());
	ParameterModel model(host.params());
	Parameter* gain = model[kGain];
	EXPECT_EQ(gain->tags(), std::vector<std::string>{"a"});
	EXPECT_TRUE(gain->hasTag("a"));
	EXPECT_FALSE(gain->hasTag("b"));
	EXPECT_EQ(gain->meta("unit")->as<std::string>(), "x");
	EXPECT_FALSE(gain->meta("none").has_value());
	EXPECT_EQ(gain->allMeta().size(), 1u);
	EXPECT_EQ(gain->addTag("z").code(), ErrorCode::Unsupported);
	EXPECT_EQ(gain->removeTag("a").code(), ErrorCode::Unsupported);
	EXPECT_EQ(gain->setTags({"q"}).code(), ErrorCode::Unsupported);
	EXPECT_EQ(gain->setMeta("k", Value(1)).code(), ErrorCode::Unsupported);
	EXPECT_EQ(gain->removeMeta("unit").code(), ErrorCode::Unsupported);
	EXPECT_EQ(model.withTag("a").size(), 2u);
	EXPECT_EQ(model.withTag("b").size(), 1u);
	EXPECT_EQ(gain->metaManager(), nullptr);
}

TEST(Parameters, TagsAndMetaUseTheManagerWhenBound)
{
	FakeEffectHost host(manifest());
	MetaManager meta;
	ParameterModel model(host.params(), &meta);
	Parameter* gain = model[kGain];
	// No record yet: the manifest record still answers.
	EXPECT_EQ(gain->tags(), std::vector<std::string>{"a"});
	EXPECT_TRUE(gain->addTag("b").ok());
	EXPECT_EQ(gain->tags(), std::vector<std::string>{"b"});
	EXPECT_TRUE(gain->setTags({"c", "d"}).ok());
	EXPECT_TRUE(gain->removeTag("c").ok());
	EXPECT_EQ(gain->tags(), std::vector<std::string>{"d"});
	EXPECT_FALSE(gain->meta("unit").has_value());
	EXPECT_TRUE(gain->setMeta("unit", Value("cm")).ok());
	EXPECT_EQ(gain->meta("unit")->as<std::string>(), "cm");
	EXPECT_EQ(gain->allMeta().size(), 1u);
	EXPECT_TRUE(gain->removeMeta("unit").ok());
	EXPECT_TRUE(gain->allMeta().empty());
	EXPECT_EQ(model.withTag("d").size(), 1u);
	EXPECT_EQ(model.metaManager(), &meta);
	model.setMetaManager(nullptr);
	EXPECT_EQ(gain->metaManager(), nullptr);
	EXPECT_EQ(gain->tags(), std::vector<std::string>{"a"});
}

TEST(Parameters, ReadsOfUnboundParametersFallBack)
{
	FakeEffectHost host(manifest());
	ParameterModel model(host.params());
	ParameterSpec orphan = ParameterSpec::floatParam(500, "orphan", "Orphan", 0.5, 0, 1).withFlags(HostFlags(HostFlag::Hidden));
	Parameter p(orphan, host.params(), nullptr);
	EXPECT_TRUE(p.value(0).empty());
	EXPECT_DOUBLE_EQ(p.number(0, 9.0), 9.0);
	EXPECT_EQ(p.integer(0, 9), 9);
	EXPECT_TRUE(p.boolean(0, true));
	EXPECT_EQ(p.optionKey(0), "");
	EXPECT_EQ(p.currentOptionKey(), "");
	EXPECT_TRUE(p.hidden());
	EXPECT_EQ(p.label(), "Orphan");
	EXPECT_EQ(p.keyframeCount(), 0);
	EXPECT_EQ(p.set(0, Value(1.0)).code(), ErrorCode::NotFound);
	EXPECT_EQ(p.setHidden(false).code(), ErrorCode::NotFound);
	EXPECT_EQ(p.setEnabled(false).code(), ErrorCode::NotFound);
}

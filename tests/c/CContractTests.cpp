// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
// The C contract: a manifest built through the C API, an effect whose behavior is a table of C
// callbacks, driven through the mini host as a host would.
#include <ofxgrip/c/ofxgrip_c.h>
#include <ofxgrip/minihost/MiniHost.hpp>
#include <ofxgrip/ofx/Effect.hpp>

#include "ofxParam.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace ofxgrip;
using namespace ofxgrip::minihost;

namespace {

enum : uint32_t { kGain = 1, kCount = 2, kOn = 3, kName = 4, kMode = 5, kCenter = 6, kTint = 7, kColor = 8, kButton = 9, kGroup = 10, kInner = 11 };

struct State {
	std::atomic<int> creates{0};
	std::atomic<int> destroys{0};
	std::atomic<int> changes{0};
	std::atomic<int> clicks{0};
	std::atomic<int> renders{0};
	std::atomic<int> identities{0};
	std::string lastName;
	std::atomic<double> lastGain{-1};
	std::mutex mutex;        /*!< Guards `page`: tiles render on several threads. */
	std::string page;
	std::string hostName;
	int quirk{-1};
	int paramErrors{0};
	std::atomic<ofxgrip_status> renderStatus{OFXGRIP_OK};
	std::atomic<bool> identityWhenOn{false};
	char stringValue[64]{};
	void reset()
	{
		creates = destroys = changes = clicks = renders = identities = 0;
		lastName.clear();
		lastGain = -1;
		page.clear();
		hostName.clear();
		quirk = -1;
		paramErrors = 0;
		renderStatus = OFXGRIP_OK;
		identityWhenOn = false;
		stringValue[0] = '\0';
	}
};

State& state()
{
	static State s;
	return s;
}

void* onCreate(ofxgrip_effect* effect, void* user)
{
	auto* s = static_cast<State*>(user);
	s->creates++;
	s->hostName = ofxgrip_host_name(effect);
	s->quirk = ofxgrip_host_has_quirk(effect, "MaskAlwaysConnected");
	// Services work inside a host callback.
	double gain = 0;
	if (ofxgrip_param_get_number(effect, kGain, &gain) != OFXGRIP_OK) {
		s->paramErrors++;
	}
	if (ofxgrip_param_set_number(effect, kGain, gain + 1.0) != OFXGRIP_OK) {
		s->paramErrors++;
	}
	if (ofxgrip_param_set_int(effect, kCount, 7) != OFXGRIP_OK || ofxgrip_param_set_bool(effect, kOn, 1) != OFXGRIP_OK || ofxgrip_param_set_string(effect, kName, "renamed") != OFXGRIP_OK) {
		s->paramErrors++;
	}
	if (ofxgrip_param_set_hidden(effect, kInner, 1) != OFXGRIP_OK || ofxgrip_param_set_enabled(effect, kCount, 0) != OFXGRIP_OK || ofxgrip_param_set_label(effect, kGain, "Loudness") != OFXGRIP_OK) {
		s->paramErrors++;
	}
	size_t needed = 0;
	if (ofxgrip_param_get_string(effect, kName, s->stringValue, sizeof s->stringValue, &needed) != OFXGRIP_OK || needed != 7) {
		s->paramErrors++;
	}
	if (ofxgrip_param_id(effect, "gain") != kGain || ofxgrip_param_id(effect, "nope") != UINT32_MAX) {
		s->paramErrors++;
	}
	if (ofxgrip_param_get_number(effect, 999, &gain) != OFXGRIP_NOT_FOUND) {
		s->paramErrors++;
	}
	ofxgrip_log(effect, OFXGRIP_LOG_INFO, "created through the C contract");
	return s;
}

void onDestroy(ofxgrip_effect*, void* user, void* instance)
{
	EXPECT_EQ(user, instance);
	static_cast<State*>(user)->destroys++;
}

ofxgrip_status onChanged(ofxgrip_effect*, void*, void* instance, uint32_t, const char* name, double, int32_t reason)
{
	auto* s = static_cast<State*>(instance);
	s->changes++;
	s->lastName = name;
	EXPECT_EQ(reason, OFXGRIP_CHANGE_USER);
	return OFXGRIP_OK;
}

ofxgrip_status onClicked(ofxgrip_effect* effect, void*, void* instance, uint32_t parameter)
{
	auto* s = static_cast<State*>(instance);
	s->clicks++;
	EXPECT_EQ(parameter, kButton);
	return ofxgrip_message(effect, OFXGRIP_MESSAGE_MESSAGE, "c.click", "clicked from C");
}

ofxgrip_status onIdentity(ofxgrip_effect*, void*, void* instance, const ofxgrip_snapshot* snapshot, double, int32_t* identity)
{
	auto* s = static_cast<State*>(instance);
	s->identities++;
	*identity = s->identityWhenOn && ofxgrip_snapshot_bool(snapshot, kOn, 0) ? 1 : 0;
	return OFXGRIP_OK;
}

ofxgrip_status onRender(ofxgrip_effect* effect, void*, void* instance, ofxgrip_render* render)
{
	auto* s = static_cast<State*>(instance);
	s->renders++;
	if (s->renderStatus != OFXGRIP_OK) {
		return s->renderStatus;
	}
	EXPECT_EQ(ofxgrip_render_effect(render), effect);
	const ofxgrip_snapshot* snapshot = ofxgrip_render_snapshot(render);
	s->lastGain = ofxgrip_snapshot_number(snapshot, kGain, -1);
	char page[16];
	ofxgrip_snapshot_page(snapshot, page, sizeof page);
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		s->page = page;
	}
	ofxgrip_image out{};
	ofxgrip_render_output(render, &out);
	ofxgrip_render_args args{};
	ofxgrip_render_get_args(render, &args);
	int32_t connected = 0;
	ofxgrip_image in{};
	const char* clip = nullptr;
	if (ofxgrip_render_input(render, 0, &clip, &connected, &in) != OFXGRIP_OK) {
		return OFXGRIP_FAILED;
	}
	EXPECT_STREQ(clip, "Source");
	const float gain = static_cast<float>(s->lastGain.load());
	const int components = out.components == OFXGRIP_COMPONENTS_RGBA ? 4 : (out.components == OFXGRIP_COMPONENTS_RGB ? 3 : 1);
	const int bytes = ofxgrip_bytes_per_component(out.depth);
	for (int y = args.window_y1; y < args.window_y2; ++y) {
		if (ofxgrip_render_abort_requested(render)) {
			return OFXGRIP_CANCELLED;
		}
		for (int x = args.window_x1; x < args.window_x2; ++x) {
			auto* dst = static_cast<uint8_t*>(out.data) + (y - out.y1) * out.row_bytes + (x - out.x1) * components * bytes;
			const uint8_t* src = connected ? static_cast<const uint8_t*>(in.data) + (y - in.y1) * in.row_bytes + (x - in.x1) * components * ofxgrip_bytes_per_component(in.depth) : nullptr;
			for (int c = 0; c < components; ++c) {
				const float v = src != nullptr ? ofxgrip_read_component(src, in.depth, c) * gain : 0.5f;
				ofxgrip_write_component(dst, out.depth, c, v);
			}
		}
	}
	return OFXGRIP_OK;
}

ofxgrip_manifest* buildManifest()
{
	ofxgrip_manifest* m = ofxgrip_manifest_create("com.belisoful.ofxgrip.ctest", "C Test");
	EXPECT_EQ(ofxgrip_manifest_set_identity(m, "33333333-3333-4333-8333-333333333333", "OfxGrip Tests", "An effect from C", OFXGRIP_KIND_FILTER), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_author(m, "Belisoful", "ofxgrip"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_version(m, 1, 2, 3), OFXGRIP_OK);
	ofxgrip_capabilities caps{};
	caps.supports_tiles = 1;
	caps.supports_multi_resolution = 1;
	caps.depth_mask = (1 << OFXGRIP_DEPTH_UINT8) | (1 << OFXGRIP_DEPTH_FLOAT);
	EXPECT_EQ(ofxgrip_manifest_set_capabilities(m, &caps), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_property(m, "regression", 1), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_float(m, kGain, "gain", "Gain", 1.0, 0.0, 10.0), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_display_range(m, kGain, 0.0, 2.0), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_hint(m, kGain, "Multiplies"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_tags(m, kGain, "look, gain;x"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_meta_string(m, kGain, "unit", "x"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_int(m, kCount, "count", "Count", 3, 0, 10), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_bool(m, kOn, "on", "On", 0), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_string(m, kName, "name", "Name", "hello"), OFXGRIP_OK);
	const char* keys[] = {"soft", "hard"};
	const char* labels[] = {"Soft", "Hard"};
	EXPECT_EQ(ofxgrip_manifest_add_choice(m, kMode, "mode", "Mode", keys, labels, 2, 1), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_point(m, kCenter, "center", "Center", 0.5, 0.25), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_rgba(m, kTint, "tint", "Tint", 0.1, 0.2, 0.3, 0.4), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_rgb(m, kColor, "color", "Color", 0.5, 0.6, 0.7), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_button(m, kButton, "button", "Press"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_group(m, kGroup, "group", "Group", 1), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_add_float(m, kInner, "inner", "Inner", 0.5, 0, 1), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_parent(m, kInner, kGroup), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_flags(m, kInner, "notanimatable,+nostate"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_visible_when(m, kInner, "on"), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_enabled_when(m, kCount, "mode == 'hard'"), OFXGRIP_OK);
	return m;
}

ofxgrip_callbacks callbacks()
{
	ofxgrip_callbacks c{};
	c.user = &state();
	c.create = &onCreate;
	c.destroy = &onDestroy;
	c.instance_changed = &onChanged;
	c.parameter_clicked = &onClicked;
	c.is_identity = &onIdentity;
	c.render = &onRender;
	return c;
}

void registerTestEffect()
{
	ofxgrip_callbacks c = callbacks();
	EXPECT_GE(ofxgrip_register(buildManifest(), &c), 0);
}

class CContract : public ::testing::Test {
protected:
	void SetUp() override
	{
		state().reset();
		ofxgrip_registry_clear();
	}
	void TearDown() override
	{
		instance_.reset();
		host_.reset();
		ofxgrip_registry_clear();
	}
	void start(HostOptions options = HostOptions::spec(), const std::string& page = {})
	{
		ASSERT_EQ(ofxgrip_bundle_plugin_count(&registerTestEffect), 1);
		auto* plugin = static_cast<OfxPlugin*>(ofxgrip_bundle_plugin(&registerTestEffect, 0));
		ASSERT_NE(plugin, nullptr);
		host_ = std::make_unique<MiniHost>(options);
		ASSERT_EQ(ofxgrip_bundle_set_host(host_->ofxHost()), 0);
		ASSERT_EQ(host_->load(plugin, "/tmp/ctest.ofx.bundle"), kOfxStatOK);
		ASSERT_EQ(host_->describe(), kOfxStatOK);
		Project project;
		project.width = 8;
		project.height = 4;
		instance_ = host_->createInstance(kOfxImageEffectContextFilter, project, page);
		ASSERT_NE(instance_, nullptr);
	}
	std::unique_ptr<MiniHost> host_;
	std::unique_ptr<Instance> instance_;
};

}  // namespace

TEST(CContractManifest, BuildsValidatesAndSerializes)
{
	int32_t major = 0;
	int32_t minor = -1;
	ofxgrip_c_version(&major, &minor);
	EXPECT_EQ(major, OFXGRIP_C_VERSION_MAJOR);
	EXPECT_EQ(minor, OFXGRIP_C_VERSION_MINOR);
	ofxgrip_c_version(nullptr, nullptr);
	EXPECT_STRNE(ofxgrip_version(), "");
	ofxgrip_manifest* m = buildManifest();
	EXPECT_EQ(ofxgrip_manifest_validate(m), OFXGRIP_OK);
	const size_t needed = ofxgrip_manifest_to_json(m, nullptr, 0);
	EXPECT_GT(needed, 100u);
	std::vector<char> json(needed + 1);
	EXPECT_EQ(ofxgrip_manifest_to_json(m, json.data(), json.size()), needed);
	EXPECT_NE(std::string(json.data()).find("\"gain\""), std::string::npos);
	char small[8];
	EXPECT_EQ(ofxgrip_manifest_to_json(m, small, sizeof small), needed);
	EXPECT_EQ(small[7], '\0');
	// A JSON manifest round-trips through the C entry point.
	ofxgrip_manifest* parsed = nullptr;
	ASSERT_EQ(ofxgrip_manifest_from_json(json.data(), &parsed), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_validate(parsed), OFXGRIP_OK);
	ofxgrip_manifest_destroy(parsed);
	// Errors: duplicates, unknown ids, bad flags, bad kinds, null handles.
	EXPECT_EQ(ofxgrip_manifest_add_float(m, kGain, "dup", "Dup", 0, 0, 1), OFXGRIP_INVALID_MANIFEST);
	EXPECT_STRNE(ofxgrip_last_error_message(), "");
	EXPECT_EQ(ofxgrip_manifest_set_hint(m, 999, "x"), OFXGRIP_NOT_FOUND);
	EXPECT_EQ(ofxgrip_manifest_set_flags(m, kGain, "sparkly"), OFXGRIP_VALUE);
	EXPECT_EQ(ofxgrip_manifest_set_identity(m, nullptr, nullptr, nullptr, 9), OFXGRIP_VALUE);
	EXPECT_EQ(ofxgrip_manifest_set_meta_string(m, kGain, nullptr, "x"), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_add_choice(m, 77, "c", "C", nullptr, nullptr, 2, 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_add_choice(m, 78, "c2", "C2", nullptr, nullptr, 0, 0), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_set_visible_when(m, kGain, "mode =="), OFXGRIP_OK);
	EXPECT_EQ(ofxgrip_manifest_validate(m), OFXGRIP_INVALID_MANIFEST);
	ofxgrip_manifest_destroy(m);
	EXPECT_EQ(ofxgrip_manifest_validate(nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_identity(nullptr, "u", "g", "d", 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_author(nullptr, "a", "p"), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_version(nullptr, 1, 0, 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_capabilities(nullptr, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_property(nullptr, "x", 1), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_add_float(nullptr, 1, "k", "l", 0, 0, 1), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_set_parent(nullptr, 1, 2), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_manifest_to_json(nullptr, small, sizeof small), 0u);
	ofxgrip_manifest* none = nullptr;
	EXPECT_EQ(ofxgrip_manifest_from_json("{ not json", &none), OFXGRIP_INVALID_MANIFEST);
	EXPECT_EQ(ofxgrip_manifest_from_json(nullptr, &none), OFXGRIP_BAD_HANDLE);
	ofxgrip_manifest_destroy(nullptr);
	// Registration refuses a null manifest, missing callbacks, and an invalid manifest.
	ofxgrip_callbacks c = callbacks();
	EXPECT_EQ(ofxgrip_register(nullptr, &c), -1);
	EXPECT_EQ(ofxgrip_register(buildManifest(), nullptr), -1);
	ofxgrip_callbacks noRender = c;
	noRender.render = nullptr;
	EXPECT_EQ(ofxgrip_register(buildManifest(), &noRender), -1);
	ofxgrip_manifest* invalid = ofxgrip_manifest_create("", "");
	EXPECT_EQ(ofxgrip_register(invalid, &c), -1);
	EXPECT_EQ(ofxgrip_register(buildManifest(), &c), 0);
	EXPECT_EQ(ofxgrip_register(buildManifest(), &c), -1);  // the identifier is registered
	ofxgrip_registry_clear();
}

TEST_F(CContract, DrivesAnEffectThroughTheHost)
{
	start(HostOptions::resolve21(), "Fusion");
	State& s = state();
	EXPECT_EQ(s.creates.load(), 1);
	EXPECT_EQ(s.paramErrors, 0);
	EXPECT_EQ(s.hostName, "DaVinciResolveLite");
	EXPECT_EQ(s.quirk, 1);
	EXPECT_STREQ(s.stringValue, "renamed");
	EXPECT_EQ(numericValue(*instance_, "gain")[0], 2.0);
	EXPECT_EQ(numericValue(*instance_, "count")[0], 7.0);
	EXPECT_EQ(instance_->param("inner")->props().getInt(kOfxParamPropSecret, -1), 1);
	EXPECT_EQ(instance_->param("count")->props().getInt(kOfxParamPropEnabled, -1), 0);
	EXPECT_EQ(instance_->param("gain")->props().getString(kOfxPropLabel), "Loudness");
	EXPECT_EQ(instance_->param("group")->props().getInt(kOfxParamPropGroupOpen, -1), 0);
	EXPECT_EQ(instance_->param("mode")->props().getStrings(kOfxParamPropChoiceOption)[1], "Hard");

	instance_->setParam("gain", {0.5});
	ASSERT_EQ(instance_->instanceChanged("gain"), kOfxStatOK);
	EXPECT_EQ(s.changes.load(), 1);
	EXPECT_EQ(s.lastName, "gain");
	ASSERT_EQ(instance_->instanceChanged("button"), kOfxStatOK);
	EXPECT_EQ(s.clicks.load(), 1);
	ASSERT_EQ(instance_->messages().size(), 1u);
	EXPECT_NE(instance_->messages()[0].find("clicked from C"), std::string::npos);

	instance_->setInput("Source", makeRampImage(8, 4, BitDepth::Float));
	RenderRequest request;
	request.tileSize = 3;
	request.threads = 2;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_FALSE(result.wasIdentity);
	EXPECT_GE(s.renders.load(), 1);
	EXPECT_EQ(s.lastGain.load(), 0.5);
	EXPECT_EQ(s.page, "Fusion");
	const ImageView v = result.image.view();
	EXPECT_NEAR(readComponent(v.pixel(7, 0), BitDepth::Float, 0), 0.5f, 0.01f);
	EXPECT_NEAR(readComponent(v.pixel(0, 0), BitDepth::Float, 3), 0.5f, 0.01f);
	// Identity through the snapshot.
	s.identityWhenOn = true;
	instance_->setParam("on", {1.0});
	RenderRequest plain;
	RenderResult identity = instance_->render(plain);
	ASSERT_TRUE(identity.ok());
	EXPECT_GE(s.identities.load(), 1);
	// A disconnected source renders the fallback value; a failing callback fails the action.
	instance_->disconnect("Source");
	RenderResult fallback = instance_->render(plain);
	ASSERT_TRUE(fallback.ok());
	EXPECT_NEAR(readComponent(fallback.image.view().pixel(1, 1), BitDepth::Float, 1), 0.5f, 0.01f);
	s.renderStatus = OFXGRIP_FAILED;
	instance_->setParam("on", {0.0});
	EXPECT_FALSE(instance_->render(plain).ok());
	s.renderStatus = OFXGRIP_CANCELLED;
	EXPECT_TRUE(instance_->render(plain).ok());  // cancellation is never a failure
	s.renderStatus = OFXGRIP_OK;
	// An edit scope allows writes outside a callback.
	Effect* effect = static_cast<Effect*>(instance_->props().getPointer(kOfxPropInstanceData));
	auto* cEffect = reinterpret_cast<ofxgrip_effect*>(effect);
	EXPECT_EQ(ofxgrip_param_set_number(cEffect, kGain, 3.0), OFXGRIP_WRONG_PHASE);
	void* edit = ofxgrip_edit_begin(cEffect, "c edit");
	ASSERT_NE(edit, nullptr);
	EXPECT_EQ(ofxgrip_param_set_number(cEffect, kGain, 3.0), OFXGRIP_OK);
	ofxgrip_edit_end(edit);
	ofxgrip_edit_end(nullptr);
	EXPECT_EQ(numericValue(*instance_, "gain")[0], 3.0);
	int32_t flag = -1;
	EXPECT_EQ(ofxgrip_param_get_hidden(cEffect, kInner, &flag), OFXGRIP_OK);
	EXPECT_EQ(flag, 1);
	EXPECT_EQ(ofxgrip_param_get_enabled(cEffect, kCount, &flag), OFXGRIP_OK);
	EXPECT_EQ(flag, 0);
	EXPECT_EQ(ofxgrip_param_get_int(cEffect, kCount, &flag), OFXGRIP_OK);
	EXPECT_EQ(flag, 7);
	EXPECT_EQ(ofxgrip_param_get_bool(cEffect, kOn, &flag), OFXGRIP_OK);
	EXPECT_EQ(flag, 0);
	EXPECT_EQ(ofxgrip_message(cEffect, 9, "x", "y"), OFXGRIP_VALUE);
	EXPECT_EQ(ofxgrip_host_has_quirk(cEffect, "Nonesuch"), 0);
	ofxgrip_log(cEffect, 42, "clamped level");
	ofxgrip_log(cEffect, -1, "clamped level");
	ofxgrip_log(cEffect, 1, nullptr);
	instance_.reset();
	EXPECT_EQ(s.destroys.load(), 1);
}

TEST_F(CContract, NullHandlesAreSafe)
{
	EXPECT_EQ(ofxgrip_param_get_number(nullptr, 1, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_number(nullptr, 1, 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_get_int(nullptr, 1, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_int(nullptr, 1, 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_get_bool(nullptr, 1, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_bool(nullptr, 1, 0), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_get_string(nullptr, 1, nullptr, 0, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_string(nullptr, 1, "x"), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_get_hidden(nullptr, 1, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_hidden(nullptr, 1, 1), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_get_enabled(nullptr, 1, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_enabled(nullptr, 1, 1), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_set_label(nullptr, 1, "x"), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_param_id(nullptr, "x"), UINT32_MAX);
	EXPECT_EQ(ofxgrip_message(nullptr, 1, "x", "y"), OFXGRIP_BAD_HANDLE);
	ofxgrip_log(nullptr, 1, "x");
	EXPECT_STREQ(ofxgrip_host_name(nullptr), "");
	EXPECT_EQ(ofxgrip_host_has_quirk(nullptr, "x"), 0);
	EXPECT_EQ(ofxgrip_edit_begin(nullptr, "x"), nullptr);
	ofxgrip_image image{};
	image.x2 = 5;
	ofxgrip_render_output(nullptr, &image);
	EXPECT_EQ(image.x2, 0);
	ofxgrip_render_output(nullptr, nullptr);
	EXPECT_EQ(ofxgrip_render_input_count(nullptr), 0);
	EXPECT_EQ(ofxgrip_render_input(nullptr, 0, nullptr, nullptr, nullptr), OFXGRIP_BAD_HANDLE);
	EXPECT_EQ(ofxgrip_render_input_named(nullptr, "Source", nullptr, nullptr), OFXGRIP_BAD_HANDLE);
	ofxgrip_render_args args{};
	args.draft = 5;
	ofxgrip_render_get_args(nullptr, &args);
	EXPECT_EQ(args.draft, 0);
	ofxgrip_render_get_args(nullptr, nullptr);
	EXPECT_EQ(ofxgrip_render_snapshot(nullptr), nullptr);
	EXPECT_EQ(ofxgrip_render_abort_requested(nullptr), 0);
	EXPECT_EQ(ofxgrip_render_effect(nullptr), nullptr);
	EXPECT_EQ(ofxgrip_snapshot_number(nullptr, 1, 4.0), 4.0);
	EXPECT_EQ(ofxgrip_snapshot_int(nullptr, 1, 4), 4);
	EXPECT_EQ(ofxgrip_snapshot_bool(nullptr, 1, 1), 1);
	EXPECT_EQ(ofxgrip_snapshot_point(nullptr, 1, nullptr, nullptr), 0);
	EXPECT_EQ(ofxgrip_snapshot_rgba(nullptr, 1, nullptr), 0);
	char buffer[4];
	EXPECT_EQ(ofxgrip_snapshot_string(nullptr, 1, buffer, sizeof buffer), 0u);
	EXPECT_EQ(ofxgrip_snapshot_has(nullptr, 1), 0);
	EXPECT_EQ(ofxgrip_snapshot_time(nullptr), 0.0);
	EXPECT_EQ(ofxgrip_snapshot_frame_rate(nullptr), 0.0);
	EXPECT_EQ(ofxgrip_snapshot_page(nullptr, buffer, sizeof buffer), 0u);
	EXPECT_EQ(ofxgrip_bytes_per_component(OFXGRIP_DEPTH_UINT16), 2);
	EXPECT_EQ(ofxgrip_bytes_per_component(99), 0);
	EXPECT_EQ(ofxgrip_read_component(nullptr, OFXGRIP_DEPTH_FLOAT, 0), 0.0f);
	ofxgrip_write_component(nullptr, OFXGRIP_DEPTH_FLOAT, 0, 1.0f);
	uint8_t pixel[4] = {0, 0, 0, 0};
	ofxgrip_write_component(pixel, OFXGRIP_DEPTH_UINT8, 1, 1.0f);
	EXPECT_EQ(pixel[1], 255);
	EXPECT_EQ(ofxgrip_read_component(pixel, OFXGRIP_DEPTH_UINT8, 1), 1.0f);
	ofxgrip_write_component(pixel, 99, 0, 1.0f);
	EXPECT_EQ(ofxgrip_read_component(pixel, OFXGRIP_DEPTH_UINT8, -1), 0.0f);
	EXPECT_EQ(ofxgrip_bundle_plugin(&registerTestEffect, 99), nullptr);
}

TEST_F(CContract, SnapshotAccessorsCoverEveryKind)
{
	struct Probe {
		static ofxgrip_status render(ofxgrip_effect*, void*, void* instance, ofxgrip_render* render)
		{
			auto* s = static_cast<State*>(instance);
			const ofxgrip_snapshot* snapshot = ofxgrip_render_snapshot(render);
			double x = 0;
			double y = 0;
			EXPECT_EQ(ofxgrip_snapshot_point(snapshot, kCenter, &x, &y), 1);
			EXPECT_EQ(x, 0.5);
			EXPECT_EQ(y, 0.25);
			EXPECT_EQ(ofxgrip_snapshot_point(snapshot, 999, &x, &y), 0);
			double rgba[4] = {0, 0, 0, 0};
			EXPECT_EQ(ofxgrip_snapshot_rgba(snapshot, kTint, rgba), 1);
			EXPECT_EQ(rgba[3], 0.4);
			EXPECT_EQ(ofxgrip_snapshot_rgba(snapshot, kColor, rgba), 1);
			EXPECT_EQ(rgba[3], 1.0);
			EXPECT_EQ(ofxgrip_snapshot_rgba(snapshot, kTint, nullptr), 0);
			char name[16];
			EXPECT_EQ(ofxgrip_snapshot_string(snapshot, kName, name, sizeof name), 7u);
			EXPECT_STREQ(name, "renamed");
			EXPECT_EQ(ofxgrip_snapshot_int(snapshot, kMode, -1), 1);
			EXPECT_EQ(ofxgrip_snapshot_bool(snapshot, kOn, 0), 1);
			EXPECT_EQ(ofxgrip_snapshot_has(snapshot, kGain), 1);
			EXPECT_EQ(ofxgrip_snapshot_has(snapshot, kButton), 0);
			EXPECT_EQ(ofxgrip_snapshot_frame_rate(snapshot), 24.0);
			EXPECT_EQ(ofxgrip_snapshot_time(snapshot), 2.0);
			int32_t connected = 1;
			ofxgrip_image in{};
			EXPECT_EQ(ofxgrip_render_input_named(render, "Source", &connected, &in), OFXGRIP_OK);
			EXPECT_EQ(connected, 0);
			EXPECT_EQ(in.data, nullptr);
			EXPECT_EQ(ofxgrip_render_input_named(render, "Nope", &connected, &in), OFXGRIP_NOT_FOUND);
			EXPECT_EQ(ofxgrip_render_input(render, 5, nullptr, nullptr, nullptr), OFXGRIP_BAD_INDEX);
			EXPECT_EQ(ofxgrip_render_input_count(render), 1);
			s->renders++;
			return OFXGRIP_OK;
		}
	};
	ofxgrip_registry_clear();
	ofxgrip_callbacks c{};
	c.user = &state();
	c.create = &onCreate;
	c.render = &Probe::render;
	ASSERT_GE(ofxgrip_register(buildManifest(), &c), 0);
	auto* plugin = static_cast<OfxPlugin*>(ofxgrip_bundle_plugin(nullptr, 0));
	ASSERT_NE(plugin, nullptr);
	host_ = std::make_unique<MiniHost>();
	ASSERT_EQ(host_->load(plugin, "/tmp/ctest.ofx.bundle"), kOfxStatOK);
	ASSERT_EQ(host_->describe(), kOfxStatOK);
	Project project;
	project.lastFrame = 5;
	instance_ = host_->createInstance(kOfxImageEffectContextFilter, project);
	ASSERT_NE(instance_, nullptr);
	RenderRequest request;
	request.time = 2.0;
	RenderResult result = instance_->render(request);
	ASSERT_TRUE(result.ok());
	EXPECT_EQ(state().renders.load(), 1);
}

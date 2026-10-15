// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/c/ofxgrip_c.h>

#include <ofxgrip/core/ManifestJson.hpp>
#include <ofxgrip/ofx/Plugin.hpp>

#include <cstring>
#include <string>

using namespace ofxgrip;

// ---- the opaque objects -------------------------------------------------------------------------

struct ofxgrip_manifest {
	EffectManifest manifest;
};

struct ofxgrip_snapshot {
	const RenderSnapshot* snapshot{nullptr};
};

struct ofxgrip_render {
	RenderContext* context{nullptr};
	ofxgrip_effect* effect{nullptr};
	ofxgrip_snapshot snapshot;
};

/*! The effect that forwards to the C callbacks. */
struct ofxgrip_effect final : Effect {
	explicit ofxgrip_effect(ofxgrip_callbacks callbacks) : callbacks_(callbacks) {}

	Error onCreate() override
	{
		if (callbacks_.create != nullptr) {
			instance_ = callbacks_.create(this, callbacks_.user);
		}
		return Error::none();
	}
	~ofxgrip_effect() override
	{
		if (callbacks_.destroy != nullptr && created()) {
			callbacks_.destroy(this, callbacks_.user, instance_);
		}
	}
	Error onInstanceChanged(const ChangeEvent& change) override
	{
		if (callbacks_.instance_changed == nullptr) {
			return Error::none();
		}
		return fromStatus(callbacks_.instance_changed(this, callbacks_.user, instance_, change.parameter, change.name.c_str(), change.time, static_cast<int32_t>(change.reason)));
	}
	Error onParameterClicked(ParameterId id) override
	{
		if (callbacks_.parameter_clicked == nullptr) {
			return Error::none();
		}
		return fromStatus(callbacks_.parameter_clicked(this, callbacks_.user, instance_, id));
	}
	Error onIsIdentity(const IsIdentityArgs& args, const RenderSnapshot& snapshot, std::string& clip, Time&, bool& identity) override
	{
		identity = false;
		if (callbacks_.is_identity == nullptr) {
			return Error::none();
		}
		ofxgrip_snapshot wrapper{&snapshot};
		int32_t flag = 0;
		const Error e = fromStatus(callbacks_.is_identity(this, callbacks_.user, instance_, &wrapper, args.time, &flag));
		identity = flag != 0;
		clip = "Source";
		return e;
	}
	Error render(RenderContext& context) override
	{
		if (callbacks_.render == nullptr) {
			return Error::make(ErrorCode::Unsupported, "The effect has no render callback");
		}
		ofxgrip_render wrapper;
		wrapper.context = &context;
		wrapper.effect = this;
		wrapper.snapshot.snapshot = &context.snapshot;
		return fromStatus(callbacks_.render(this, callbacks_.user, instance_, &wrapper));
	}
	bool created() const noexcept { return callbacks_.create != nullptr; }
	static Error fromStatus(ofxgrip_status status);

	ofxgrip_callbacks callbacks_;
	void* instance_{nullptr};
};

namespace {

thread_local std::string lastError;

ofxgrip_status fail(ofxgrip_status status, std::string message)
{
	lastError = std::move(message);
	return status;
}

ofxgrip_status fromError(const Error& e)
{
	if (e.ok()) {
		return OFXGRIP_OK;
	}
	lastError = e.describe();
	return static_cast<ofxgrip_status>(e.code());
}

size_t copyOut(const std::string& text, char* buffer, size_t length)
{
	if (buffer != nullptr && length > 0) {
		const size_t n = text.size() < length - 1 ? text.size() : length - 1;
		std::memcpy(buffer, text.data(), n);
		buffer[n] = '\0';
	}
	return text.size();
}

std::string text(const char* s)
{
	return s == nullptr ? std::string() : std::string(s);
}

ParameterSpec* findSpec(ofxgrip_manifest* manifest, uint32_t id)
{
	if (manifest == nullptr) {
		return nullptr;
	}
	for (ParameterSpec& spec : manifest->manifest.parameters) {
		if (spec.id == id) {
			return &spec;
		}
	}
	return nullptr;
}

ofxgrip_status addSpec(ofxgrip_manifest* manifest, ParameterSpec spec)
{
	if (manifest == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null manifest");
	}
	if (findSpec(manifest, spec.id) != nullptr) {
		return fail(OFXGRIP_INVALID_MANIFEST, "a parameter with id " + std::to_string(spec.id) + " exists");
	}
	manifest->manifest.parameters.push_back(std::move(spec));
	return OFXGRIP_OK;
}

ofxgrip_status withSpec(ofxgrip_manifest* manifest, uint32_t id, const std::function<void(ParameterSpec&)>& edit)
{
	ParameterSpec* spec = findSpec(manifest, id);
	if (spec == nullptr) {
		return fail(manifest == nullptr ? OFXGRIP_BAD_HANDLE : OFXGRIP_NOT_FOUND, "no parameter with id " + std::to_string(id));
	}
	edit(*spec);
	return OFXGRIP_OK;
}

void fillImage(const ImageView& view, const GpuImage& gpu, ofxgrip_image* out)
{
	if (out == nullptr) {
		return;
	}
	const bool isGpu = gpu.api != GpuApi::None;
	const ImageDesc& desc = isGpu ? gpu.desc : view.desc;
	out->data = isGpu ? nullptr : view.data;
	out->row_bytes = isGpu ? gpu.rowBytes : view.rowBytes;
	out->x1 = desc.bounds.x1;
	out->y1 = desc.bounds.y1;
	out->x2 = desc.bounds.x2;
	out->y2 = desc.bounds.y2;
	out->depth = static_cast<int32_t>(desc.depth);
	out->components = static_cast<int32_t>(desc.components);
	out->premultiplied = desc.premultiplication == Premultiplication::Premultiplied ? 1 : 0;
	out->pixel_aspect = desc.pixelAspect;
	out->gpu_api = static_cast<int32_t>(gpu.api);
	out->gpu_handle = gpu.handle;
	out->gpu_queue = gpu.queue;
	out->needs_intermediate_copy = gpu.needsIntermediateCopy ? 1 : 0;
}

class CFactory final : public EffectFactory {
public:
	CFactory(EffectManifest manifest, ofxgrip_callbacks callbacks) : manifest_(std::move(manifest)), callbacks_(callbacks) {}
	const EffectManifest& manifest() const override { return manifest_; }
	std::unique_ptr<Effect> create() override { return std::make_unique<ofxgrip_effect>(callbacks_); }

private:
	EffectManifest manifest_;
	ofxgrip_callbacks callbacks_;
};

}  // namespace

Error ofxgrip_effect::fromStatus(ofxgrip_status status)
{
	if (status == OFXGRIP_OK) {
		return Error::none();
	}
	return Error::make(static_cast<ErrorCode>(status), lastError.empty() ? std::string("The effect reported status ") + std::to_string(status) : lastError);
}

// ---- versions and status ------------------------------------------------------------------------

extern "C" {

const char* ofxgrip_version(void)
{
	return OFXGRIP_VERSION_STRING;
}

void ofxgrip_c_version(int32_t* major, int32_t* minor)
{
	if (major != nullptr) {
		*major = OFXGRIP_C_VERSION_MAJOR;
	}
	if (minor != nullptr) {
		*minor = OFXGRIP_C_VERSION_MINOR;
	}
}

const char* ofxgrip_last_error_message(void)
{
	return lastError.c_str();
}

// ---- manifest -----------------------------------------------------------------------------------

ofxgrip_manifest* ofxgrip_manifest_create(const char* ofx_id, const char* name)
{
	auto* m = new ofxgrip_manifest();
	m->manifest.identity.ofxId = text(ofx_id);
	m->manifest.identity.name = text(name);
	m->manifest.identity.grouping = "OfxGrip";
	return m;
}

ofxgrip_status ofxgrip_manifest_from_json(const char* json, ofxgrip_manifest** out)
{
	if (json == nullptr || out == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null argument");
	}
	auto m = std::make_unique<ofxgrip_manifest>();
	if (Error e = parseManifestJson(json, m->manifest)) {
		return fromError(e);
	}
	*out = m.release();
	return OFXGRIP_OK;
}

void ofxgrip_manifest_destroy(ofxgrip_manifest* manifest)
{
	delete manifest;
}

ofxgrip_status ofxgrip_manifest_set_identity(ofxgrip_manifest* manifest, const char* uuid, const char* grouping, const char* description, int32_t kind)
{
	if (manifest == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null manifest");
	}
	if (uuid != nullptr) {
		manifest->manifest.identity.uuid = uuid;
	}
	if (grouping != nullptr) {
		manifest->manifest.identity.grouping = grouping;
	}
	if (description != nullptr) {
		manifest->manifest.identity.description = description;
	}
	if (kind < 0 || kind > OFXGRIP_KIND_GENERAL) {
		return fail(OFXGRIP_VALUE, "unknown effect kind");
	}
	manifest->manifest.identity.kind = static_cast<EffectKind>(kind);
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_manifest_set_author(ofxgrip_manifest* manifest, const char* author, const char* product_id)
{
	if (manifest == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null manifest");
	}
	manifest->manifest.identity.author = text(author);
	manifest->manifest.identity.productId = text(product_id);
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_manifest_set_version(ofxgrip_manifest* manifest, uint32_t major, uint32_t minor, uint32_t patch)
{
	if (manifest == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null manifest");
	}
	manifest->manifest.version = Version{major, minor, patch};
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_manifest_set_capabilities(ofxgrip_manifest* manifest, const ofxgrip_capabilities* capabilities)
{
	if (manifest == nullptr || capabilities == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null argument");
	}
	Capabilities& c = manifest->manifest.capabilities;
	c.supportsTiles = capabilities->supports_tiles != 0;
	c.supportsMultiResolution = capabilities->supports_multi_resolution != 0;
	c.temporalClipAccess = capabilities->temporal_clip_access != 0;
	c.noSpatialAwareness = capabilities->no_spatial_awareness != 0;
	c.metalRender = capabilities->metal_render != 0;
	c.cudaRender = capabilities->cuda_render != 0;
	c.openCLRender = capabilities->opencl_render != 0;
	if (capabilities->depth_mask != 0) {
		c.depths.clear();
		for (int32_t depth : {OFXGRIP_DEPTH_UINT8, OFXGRIP_DEPTH_UINT16, OFXGRIP_DEPTH_HALF, OFXGRIP_DEPTH_FLOAT}) {
			if ((capabilities->depth_mask & (1 << depth)) != 0) {
				c.depths.push_back(static_cast<BitDepth>(depth));
			}
		}
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_manifest_set_property(ofxgrip_manifest* manifest, const char* name, int32_t value)
{
	if (manifest == nullptr || name == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null argument");
	}
	manifest->manifest.properties[name] = Value(value != 0);
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_manifest_add_float(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double default_value, double minimum, double maximum)
{
	return addSpec(manifest, ParameterSpec::floatParam(id, text(key), text(label), default_value, minimum, maximum));
}

ofxgrip_status ofxgrip_manifest_add_int(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t default_value, int32_t minimum, int32_t maximum)
{
	return addSpec(manifest, ParameterSpec::intParam(id, text(key), text(label), default_value, minimum, maximum));
}

ofxgrip_status ofxgrip_manifest_add_bool(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t default_value)
{
	return addSpec(manifest, ParameterSpec::boolParam(id, text(key), text(label), default_value != 0));
}

ofxgrip_status ofxgrip_manifest_add_string(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, const char* default_value)
{
	return addSpec(manifest, ParameterSpec::stringParam(id, text(key), text(label), text(default_value)));
}

ofxgrip_status ofxgrip_manifest_add_choice(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, const char* const* option_keys, const char* const* option_labels, int32_t option_count, int32_t default_index)
{
	if (option_keys == nullptr && option_count > 0) {
		return fail(OFXGRIP_BAD_HANDLE, "null option keys");
	}
	std::vector<ChoiceOption> options;
	for (int32_t i = 0; i < option_count; ++i) {
		ChoiceOption option;
		option.key = text(option_keys[i]);
		option.label = option_labels != nullptr ? text(option_labels[i]) : option.key;
		options.push_back(std::move(option));
	}
	return addSpec(manifest, ParameterSpec::menuParam(id, text(key), text(label), std::move(options), default_index));
}

ofxgrip_status ofxgrip_manifest_add_point(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double x, double y)
{
	return addSpec(manifest, ParameterSpec::pointParam(id, text(key), text(label), Point2D{x, y}));
}

ofxgrip_status ofxgrip_manifest_add_rgb(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double r, double g, double b)
{
	return addSpec(manifest, ParameterSpec::rgbParam(id, text(key), text(label), RGB{r, g, b}));
}

ofxgrip_status ofxgrip_manifest_add_rgba(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double r, double g, double b, double a)
{
	return addSpec(manifest, ParameterSpec::rgbaParam(id, text(key), text(label), RGBA{r, g, b, a}));
}

ofxgrip_status ofxgrip_manifest_add_button(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label)
{
	return addSpec(manifest, ParameterSpec::buttonParam(id, text(key), text(label)));
}

ofxgrip_status ofxgrip_manifest_add_group(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t collapsed)
{
	ParameterSpec group = ParameterSpec::groupParam(id, text(key), text(label));
	if (collapsed != 0) {
		group.hostFlags.set(HostFlag::Collapsed);
	}
	return addSpec(manifest, std::move(group));
}

ofxgrip_status ofxgrip_manifest_set_parent(ofxgrip_manifest* manifest, uint32_t id, uint32_t parent_id)
{
	return withSpec(manifest, id, [parent_id](ParameterSpec& spec) { spec.parent = parent_id; });
}

ofxgrip_status ofxgrip_manifest_set_hint(ofxgrip_manifest* manifest, uint32_t id, const char* hint)
{
	return withSpec(manifest, id, [hint](ParameterSpec& spec) { spec.hint = text(hint); });
}

ofxgrip_status ofxgrip_manifest_set_display_range(ofxgrip_manifest* manifest, uint32_t id, double minimum, double maximum)
{
	return withSpec(manifest, id, [minimum, maximum](ParameterSpec& spec) { spec.withDisplayRange(minimum, maximum); });
}

ofxgrip_status ofxgrip_manifest_set_flags(ofxgrip_manifest* manifest, uint32_t id, const char* flags)
{
	ParsedFlags parsed = parseFlags(text(flags));
	if (!parsed.unknown.empty()) {
		return fail(OFXGRIP_VALUE, "unknown flag " + parsed.unknown.front());
	}
	return withSpec(manifest, id, [&parsed](ParameterSpec& spec) {
		spec.hostFlags = parsed.host;
		spec.appFlags = parsed.app;
	});
}

ofxgrip_status ofxgrip_manifest_set_tags(ofxgrip_manifest* manifest, uint32_t id, const char* tags)
{
	std::vector<std::string> list;
	std::string current;
	for (char c : text(tags)) {
		if (c == ',' || c == ' ' || c == ';') {
			if (!current.empty()) {
				list.push_back(current);
				current.clear();
			}
			continue;
		}
		current.push_back(c);
	}
	if (!current.empty()) {
		list.push_back(current);
	}
	return withSpec(manifest, id, [&list](ParameterSpec& spec) { spec.tags = list; });
}

ofxgrip_status ofxgrip_manifest_set_meta_string(ofxgrip_manifest* manifest, uint32_t id, const char* name, const char* value)
{
	if (name == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null meta name");
	}
	return withSpec(manifest, id, [name, value](ParameterSpec& spec) { spec.meta[name] = Value(text(value)); });
}

ofxgrip_status ofxgrip_manifest_set_visible_when(ofxgrip_manifest* manifest, uint32_t id, const char* rule)
{
	return withSpec(manifest, id, [rule](ParameterSpec& spec) { spec.visibleWhen = text(rule); });
}

ofxgrip_status ofxgrip_manifest_set_enabled_when(ofxgrip_manifest* manifest, uint32_t id, const char* rule)
{
	return withSpec(manifest, id, [rule](ParameterSpec& spec) { spec.enabledWhen = text(rule); });
}

ofxgrip_status ofxgrip_manifest_validate(const ofxgrip_manifest* manifest)
{
	if (manifest == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null manifest");
	}
	return fromError(manifest->manifest.validate());
}

size_t ofxgrip_manifest_to_json(const ofxgrip_manifest* manifest, char* buffer, size_t length)
{
	if (manifest == nullptr) {
		return copyOut("", buffer, length);
	}
	return copyOut(writeManifestJson(manifest->manifest), buffer, length);
}

// ---- registration -------------------------------------------------------------------------------

int32_t ofxgrip_register(ofxgrip_manifest* manifest, const ofxgrip_callbacks* callbacks)
{
	std::unique_ptr<ofxgrip_manifest> owned(manifest);
	if (!owned || callbacks == nullptr) {
		fail(OFXGRIP_BAD_HANDLE, "null manifest or callbacks");
		return -1;
	}
	if (callbacks->render == nullptr) {
		fail(OFXGRIP_VALUE, "the render callback is required");
		return -1;
	}
	if (Error e = owned->manifest.validate()) {
		fromError(e);
		return -1;
	}
	const int index = PluginRegistry::instance().registerFactory(std::make_unique<CFactory>(owned->manifest, *callbacks));
	if (index < 0) {
		fail(OFXGRIP_FAILED, "registration failed: the table is full or the identifier is registered");
	}
	return index;
}

int32_t ofxgrip_bundle_plugin_count(void (*register_fn)(void))
{
	PluginRegistry::instance().ensureRegistered(register_fn);
	return PluginRegistry::instance().count();
}

void* ofxgrip_bundle_plugin(void (*register_fn)(void), int32_t index)
{
	PluginRegistry::instance().ensureRegistered(register_fn);
	return PluginRegistry::instance().plugin(index);
}

int32_t ofxgrip_bundle_set_host(const void* host)
{
	PluginRegistry::instance().setGlobalHost(static_cast<const OfxHost*>(host));
	return 0;
}

void ofxgrip_registry_clear(void)
{
	PluginRegistry::instance().clear();
}

// ---- services -----------------------------------------------------------------------------------

#define OFXGRIP_C_EFFECT_OR_FAIL(effect)                   \
	if ((effect) == nullptr || !(effect)->attached()) {   \
		return fail(OFXGRIP_BAD_HANDLE, "null effect");   \
	}

ofxgrip_status ofxgrip_param_get_number(ofxgrip_effect* effect, uint32_t id, double* out)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	Value v;
	if (Error e = effect->services().parameters().getCurrent(id, v)) {
		return fromError(e);
	}
	if (out != nullptr) {
		*out = v.toDouble(0.0);
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_number(ofxgrip_effect* effect, uint32_t id, double value)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	return fromError(effect->services().parameters().setCurrent(id, Value(value)));
}

ofxgrip_status ofxgrip_param_get_int(ofxgrip_effect* effect, uint32_t id, int32_t* out)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	Value v;
	if (Error e = effect->services().parameters().getCurrent(id, v)) {
		return fromError(e);
	}
	if (out != nullptr) {
		*out = v.toInt(0);
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_int(ofxgrip_effect* effect, uint32_t id, int32_t value)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	return fromError(effect->services().parameters().setCurrent(id, Value(static_cast<int>(value))));
}

ofxgrip_status ofxgrip_param_get_bool(ofxgrip_effect* effect, uint32_t id, int32_t* out)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	Value v;
	if (Error e = effect->services().parameters().getCurrent(id, v)) {
		return fromError(e);
	}
	if (out != nullptr) {
		*out = v.toBool(false) ? 1 : 0;
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_bool(ofxgrip_effect* effect, uint32_t id, int32_t value)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	return fromError(effect->services().parameters().setCurrent(id, Value(value != 0)));
}

ofxgrip_status ofxgrip_param_get_string(ofxgrip_effect* effect, uint32_t id, char* buffer, size_t length, size_t* needed)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	Value v;
	if (Error e = effect->services().parameters().getCurrent(id, v)) {
		return fromError(e);
	}
	const std::string s = v.is<std::string>() ? v.as<std::string>() : v.toString();
	const size_t n = copyOut(s, buffer, length);
	if (needed != nullptr) {
		*needed = n;
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_string(ofxgrip_effect* effect, uint32_t id, const char* value)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	return fromError(effect->services().parameters().setCurrent(id, Value(text(value))));
}

ofxgrip_status ofxgrip_param_get_hidden(ofxgrip_effect* effect, uint32_t id, int32_t* out)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	HostFlags flags;
	if (Error e = effect->services().parameters().getFlags(id, flags)) {
		return fromError(e);
	}
	if (out != nullptr) {
		*out = flags.has(HostFlag::Hidden) ? 1 : 0;
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_hidden(ofxgrip_effect* effect, uint32_t id, int32_t hidden)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	HostFlags flags;
	if (Error e = effect->services().parameters().getFlags(id, flags)) {
		return fromError(e);
	}
	return fromError(effect->services().parameters().setFlags(id, flags.set(HostFlag::Hidden, hidden != 0)));
}

ofxgrip_status ofxgrip_param_get_enabled(ofxgrip_effect* effect, uint32_t id, int32_t* out)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	HostFlags flags;
	if (Error e = effect->services().parameters().getFlags(id, flags)) {
		return fromError(e);
	}
	if (out != nullptr) {
		*out = flags.has(HostFlag::Disabled) ? 0 : 1;
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_param_set_enabled(ofxgrip_effect* effect, uint32_t id, int32_t enabled)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	HostFlags flags;
	if (Error e = effect->services().parameters().getFlags(id, flags)) {
		return fromError(e);
	}
	return fromError(effect->services().parameters().setFlags(id, flags.set(HostFlag::Disabled, enabled == 0)));
}

ofxgrip_status ofxgrip_param_set_label(ofxgrip_effect* effect, uint32_t id, const char* label)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	return fromError(effect->services().parameters().setLabel(id, text(label)));
}

uint32_t ofxgrip_param_id(ofxgrip_effect* effect, const char* key)
{
	if (effect == nullptr || !effect->attached() || key == nullptr) {
		return UINT32_MAX;
	}
	return effect->services().parameters().idForKey(key);
}

ofxgrip_status ofxgrip_message(ofxgrip_effect* effect, int32_t kind, const char* id, const char* text_)
{
	OFXGRIP_C_EFFECT_OR_FAIL(effect);
	if (kind < 0 || kind > OFXGRIP_MESSAGE_QUESTION) {
		return fail(OFXGRIP_VALUE, "unknown message kind");
	}
	return fromError(effect->services().feedback().message(static_cast<MessageKind>(kind), text(id), text(text_)));
}

void ofxgrip_log(ofxgrip_effect* effect, int32_t level, const char* text_)
{
	if (effect == nullptr || !effect->attached() || text_ == nullptr) {
		return;
	}
	const LogLevel l = level < 0 ? LogLevel::Debug : (level > 3 ? LogLevel::Error : static_cast<LogLevel>(level));
	effect->services().log().log(l, text_);
}

const char* ofxgrip_host_name(ofxgrip_effect* effect)
{
	if (effect == nullptr || !effect->attached()) {
		return "";
	}
	return effect->services().profile().hostName.c_str();
}

int32_t ofxgrip_host_has_quirk(ofxgrip_effect* effect, const char* quirk)
{
	if (effect == nullptr || !effect->attached() || quirk == nullptr) {
		return 0;
	}
	Quirk key = Quirk::Count;
	if (!parseQuirk(quirk, key)) {
		return 0;
	}
	return effect->services().profile().has(key) ? 1 : 0;
}

void* ofxgrip_edit_begin(ofxgrip_effect* effect, const char* name)
{
	if (effect == nullptr || !effect->attached()) {
		return nullptr;
	}
	return new EditScope(effect->services(), text(name));
}

void ofxgrip_edit_end(void* edit)
{
	delete static_cast<EditScope*>(edit);
}

// ---- render -------------------------------------------------------------------------------------

void ofxgrip_render_output(const ofxgrip_render* render, ofxgrip_image* out)
{
	if (render == nullptr || render->context == nullptr) {
		if (out != nullptr) {
			*out = ofxgrip_image{};
		}
		return;
	}
	fillImage(render->context->output, render->context->outputGpu, out);
}

int32_t ofxgrip_render_input_count(const ofxgrip_render* render)
{
	return render == nullptr || render->context == nullptr ? 0 : static_cast<int32_t>(render->context->inputs.size());
}

ofxgrip_status ofxgrip_render_input(const ofxgrip_render* render, int32_t index, const char** clip, int32_t* connected, ofxgrip_image* out)
{
	if (render == nullptr || render->context == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null render");
	}
	if (index < 0 || static_cast<size_t>(index) >= render->context->inputs.size()) {
		return fail(OFXGRIP_BAD_INDEX, "no input at index " + std::to_string(index));
	}
	const InputImage& input = render->context->inputs[static_cast<size_t>(index)];
	if (clip != nullptr) {
		*clip = input.clip.c_str();
	}
	if (connected != nullptr) {
		*connected = input.connected ? 1 : 0;
	}
	if (out != nullptr) {
		if (input.connected) {
			fillImage(input.view, input.gpu, out);
		} else {
			*out = ofxgrip_image{};
		}
	}
	return OFXGRIP_OK;
}

ofxgrip_status ofxgrip_render_input_named(const ofxgrip_render* render, const char* clip, int32_t* connected, ofxgrip_image* out)
{
	if (render == nullptr || render->context == nullptr || clip == nullptr) {
		return fail(OFXGRIP_BAD_HANDLE, "null render or clip");
	}
	for (size_t i = 0; i < render->context->inputs.size(); ++i) {
		if (render->context->inputs[i].clip == clip) {
			return ofxgrip_render_input(render, static_cast<int32_t>(i), nullptr, connected, out);
		}
	}
	return fail(OFXGRIP_NOT_FOUND, std::string("no input clip ") + clip);
}

void ofxgrip_render_get_args(const ofxgrip_render* render, ofxgrip_render_args* out)
{
	if (out == nullptr) {
		return;
	}
	*out = ofxgrip_render_args{};
	if (render == nullptr || render->context == nullptr) {
		return;
	}
	const RenderArgs& a = render->context->args;
	out->time = a.time;
	out->render_scale_x = a.renderScale.x;
	out->render_scale_y = a.renderScale.y;
	const RectI window = a.renderWindow.empty() ? render->context->output.desc.bounds : a.renderWindow;
	out->window_x1 = window.x1;
	out->window_y1 = window.y1;
	out->window_x2 = window.x2;
	out->window_y2 = window.y2;
	out->draft = a.draft ? 1 : 0;
	out->interactive = a.interactive ? 1 : 0;
	out->gpu_api = static_cast<int32_t>(a.gpu);
	out->gpu_queue = a.gpuQueue;
}

const ofxgrip_snapshot* ofxgrip_render_snapshot(const ofxgrip_render* render)
{
	return render == nullptr ? nullptr : &render->snapshot;
}

int32_t ofxgrip_render_abort_requested(ofxgrip_render* render)
{
	return render == nullptr || render->context == nullptr ? 0 : (render->context->abortRequested() ? 1 : 0);
}

ofxgrip_effect* ofxgrip_render_effect(ofxgrip_render* render)
{
	return render == nullptr ? nullptr : render->effect;
}

// ---- snapshot -----------------------------------------------------------------------------------

double ofxgrip_snapshot_number(const ofxgrip_snapshot* snapshot, uint32_t id, double fallback)
{
	return snapshot == nullptr || snapshot->snapshot == nullptr ? fallback : snapshot->snapshot->number(id, fallback);
}

int32_t ofxgrip_snapshot_int(const ofxgrip_snapshot* snapshot, uint32_t id, int32_t fallback)
{
	return snapshot == nullptr || snapshot->snapshot == nullptr ? fallback : snapshot->snapshot->integer(id, fallback);
}

int32_t ofxgrip_snapshot_bool(const ofxgrip_snapshot* snapshot, uint32_t id, int32_t fallback)
{
	return snapshot == nullptr || snapshot->snapshot == nullptr ? fallback : (snapshot->snapshot->boolean(id, fallback != 0) ? 1 : 0);
}

int32_t ofxgrip_snapshot_point(const ofxgrip_snapshot* snapshot, uint32_t id, double* x, double* y)
{
	if (snapshot == nullptr || snapshot->snapshot == nullptr || !snapshot->snapshot->has(id)) {
		return 0;
	}
	const Point2D p = snapshot->snapshot->point(id);
	if (x != nullptr) {
		*x = p.x;
	}
	if (y != nullptr) {
		*y = p.y;
	}
	return 1;
}

int32_t ofxgrip_snapshot_rgba(const ofxgrip_snapshot* snapshot, uint32_t id, double* rgba)
{
	if (snapshot == nullptr || snapshot->snapshot == nullptr || !snapshot->snapshot->has(id) || rgba == nullptr) {
		return 0;
	}
	const RGBA c = snapshot->snapshot->rgba(id, RGBA{0, 0, 0, 1});
	rgba[0] = c.r;
	rgba[1] = c.g;
	rgba[2] = c.b;
	rgba[3] = c.a;
	return 1;
}

size_t ofxgrip_snapshot_string(const ofxgrip_snapshot* snapshot, uint32_t id, char* buffer, size_t length)
{
	if (snapshot == nullptr || snapshot->snapshot == nullptr) {
		return copyOut("", buffer, length);
	}
	return copyOut(snapshot->snapshot->string(id), buffer, length);
}

int32_t ofxgrip_snapshot_has(const ofxgrip_snapshot* snapshot, uint32_t id)
{
	return snapshot != nullptr && snapshot->snapshot != nullptr && snapshot->snapshot->has(id) ? 1 : 0;
}

double ofxgrip_snapshot_time(const ofxgrip_snapshot* snapshot)
{
	return snapshot == nullptr || snapshot->snapshot == nullptr ? 0.0 : snapshot->snapshot->info().time;
}

double ofxgrip_snapshot_frame_rate(const ofxgrip_snapshot* snapshot)
{
	return snapshot == nullptr || snapshot->snapshot == nullptr ? 0.0 : snapshot->snapshot->info().frameRate;
}

size_t ofxgrip_snapshot_page(const ofxgrip_snapshot* snapshot, char* buffer, size_t length)
{
	if (snapshot == nullptr || snapshot->snapshot == nullptr) {
		return copyOut("", buffer, length);
	}
	return copyOut(snapshot->snapshot->info().resolvePage, buffer, length);
}

// ---- pixels -------------------------------------------------------------------------------------

int32_t ofxgrip_bytes_per_component(int32_t depth)
{
	if (depth < 0 || depth > OFXGRIP_DEPTH_FLOAT) {
		return 0;
	}
	return static_cast<int32_t>(bytesPerComponent(static_cast<BitDepth>(depth)));
}

float ofxgrip_read_component(const void* pixel, int32_t depth, int32_t component)
{
	if (pixel == nullptr || depth <= 0 || depth > OFXGRIP_DEPTH_FLOAT || component < 0) {
		return 0.0f;
	}
	return readComponent(static_cast<const std::uint8_t*>(pixel), static_cast<BitDepth>(depth), static_cast<size_t>(component));
}

void ofxgrip_write_component(void* pixel, int32_t depth, int32_t component, float value)
{
	if (pixel == nullptr || depth <= 0 || depth > OFXGRIP_DEPTH_FLOAT || component < 0) {
		return;
	}
	writeComponent(static_cast<std::uint8_t*>(pixel), static_cast<BitDepth>(depth), static_cast<size_t>(component), value);
}

}  // extern "C"

/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Belisoful */
/*!
	@file  ofxgrip_c.h
	@brief The C contract over OfxGrip: build a manifest, register an effect whose behavior is
	       a table of C callbacks, and read the render context, the snapshot, and the host
	       services through plain functions.

	Introduced in OfxGrip 0.3.0. The contract exists so effects and libraries written in
	another language (the Rust crate first) take part without a C++ ABI. Every object is an
	opaque pointer; every string is UTF-8 and NUL-terminated; every function that can fail
	returns an `ofxgrip_status` (0 is success) and leaves a message for
	`ofxgrip_last_error_message`. Strings a function returns through a `char*` buffer are
	truncated to the buffer and the full length is returned, so a caller can size a second
	call. The contract is additive: a point release adds functions and never changes or removes
	one.

	A bundle written against this contract exports the three OpenFX symbols by forwarding to
	`ofxgrip_bundle_plugin_count`, `ofxgrip_bundle_plugin`, and `ofxgrip_bundle_set_host`, with
	a registration function that calls `ofxgrip_register` once per effect.
*/
#ifndef OFXGRIP_C_H
#define OFXGRIP_C_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define OFXGRIP_C_API __declspec(dllexport)
#else
#define OFXGRIP_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- versions and status -------------------------------------------------------------------- */

/*! The contract version this header describes. */
#define OFXGRIP_C_VERSION_MAJOR 1
#define OFXGRIP_C_VERSION_MINOR 0

/*! A status code. 0 is success; the others mirror `ofxgrip::ErrorCode`. */
typedef int32_t ofxgrip_status;
#define OFXGRIP_OK 0
#define OFXGRIP_FAILED 1
#define OFXGRIP_UNSUPPORTED 5
#define OFXGRIP_BAD_HANDLE 9
#define OFXGRIP_BAD_INDEX 10
#define OFXGRIP_VALUE 11
#define OFXGRIP_CANCELLED 12
#define OFXGRIP_INVALID_MANIFEST 100
#define OFXGRIP_NOT_FOUND 102
#define OFXGRIP_WRONG_PHASE 103

/*! The library version as "major.minor.patch". */
OFXGRIP_C_API const char* ofxgrip_version(void);
/*! The contract version. */
OFXGRIP_C_API void ofxgrip_c_version(int32_t* major, int32_t* minor);
/*! The message of the last failing call on this thread, or an empty string. */
OFXGRIP_C_API const char* ofxgrip_last_error_message(void);

/* ---- enums ---------------------------------------------------------------------------------- */

/*! `ofxgrip::EffectKind`. */
enum ofxgrip_effect_kind { OFXGRIP_KIND_FILTER = 0, OFXGRIP_KIND_GENERATOR = 1, OFXGRIP_KIND_TRANSITION = 2, OFXGRIP_KIND_GENERAL = 3 };
/*! `ofxgrip::BitDepth`. */
enum ofxgrip_bit_depth { OFXGRIP_DEPTH_NONE = 0, OFXGRIP_DEPTH_UINT8 = 1, OFXGRIP_DEPTH_UINT16 = 2, OFXGRIP_DEPTH_HALF = 3, OFXGRIP_DEPTH_FLOAT = 4 };
/*! `ofxgrip::Components`. */
enum ofxgrip_components { OFXGRIP_COMPONENTS_NONE = 0, OFXGRIP_COMPONENTS_RGBA = 1, OFXGRIP_COMPONENTS_RGB = 2, OFXGRIP_COMPONENTS_ALPHA = 3 };
/*! `ofxgrip::GpuApi`. */
enum ofxgrip_gpu_api { OFXGRIP_GPU_NONE = 0, OFXGRIP_GPU_METAL = 1, OFXGRIP_GPU_CUDA = 2, OFXGRIP_GPU_OPENCL = 3, OFXGRIP_GPU_OPENGL = 4, OFXGRIP_GPU_VULKAN = 5 };
/*! `ofxgrip::MessageKind`. */
enum ofxgrip_message_kind { OFXGRIP_MESSAGE_LOG = 0, OFXGRIP_MESSAGE_MESSAGE = 1, OFXGRIP_MESSAGE_WARNING = 2, OFXGRIP_MESSAGE_ERROR = 3, OFXGRIP_MESSAGE_QUESTION = 4 };
/*! `ofxgrip::LogLevel`. */
enum ofxgrip_log_level { OFXGRIP_LOG_DEBUG = 0, OFXGRIP_LOG_INFO = 1, OFXGRIP_LOG_WARNING = 2, OFXGRIP_LOG_ERROR = 3 };
/*! `ofxgrip::ChangeReason`. */
enum ofxgrip_change_reason { OFXGRIP_CHANGE_USER = 0, OFXGRIP_CHANGE_PLUGIN = 1, OFXGRIP_CHANGE_TIME = 2 };

/* ---- opaque objects ------------------------------------------------------------------------- */

typedef struct ofxgrip_manifest ofxgrip_manifest;   /*!< `ofxgrip::EffectManifest` under construction. */
typedef struct ofxgrip_effect ofxgrip_effect;       /*!< A live effect instance. */
typedef struct ofxgrip_render ofxgrip_render;       /*!< The render context of one render action. */
typedef struct ofxgrip_snapshot ofxgrip_snapshot;   /*!< An immutable render snapshot. */

/* ---- manifest ------------------------------------------------------------------------------- */

/*! The capabilities an effect declares; every field is 0 or 1 except the depth mask. */
typedef struct ofxgrip_capabilities {
	int32_t supports_tiles;
	int32_t supports_multi_resolution;
	int32_t temporal_clip_access;
	int32_t no_spatial_awareness;
	int32_t depth_mask;      /*!< Bits: 1 << OFXGRIP_DEPTH_UINT8, 1 << OFXGRIP_DEPTH_UINT16, 1 << OFXGRIP_DEPTH_HALF, 1 << OFXGRIP_DEPTH_FLOAT. */
	int32_t metal_render;
	int32_t cuda_render;
	int32_t opencl_render;
} ofxgrip_capabilities;

OFXGRIP_C_API ofxgrip_manifest* ofxgrip_manifest_create(const char* ofx_id, const char* name);
/*! Parses a JSON manifest (`ManifestJson.hpp`). */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_from_json(const char* json, ofxgrip_manifest** out);
OFXGRIP_C_API void ofxgrip_manifest_destroy(ofxgrip_manifest* manifest);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_identity(ofxgrip_manifest* manifest, const char* uuid, const char* grouping, const char* description, int32_t kind);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_author(ofxgrip_manifest* manifest, const char* author, const char* product_id);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_version(ofxgrip_manifest* manifest, uint32_t major, uint32_t minor, uint32_t patch);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_capabilities(ofxgrip_manifest* manifest, const ofxgrip_capabilities* capabilities);
/*! Sets a boolean effect property such as `manageMeta` or `debugMenu`. */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_property(ofxgrip_manifest* manifest, const char* name, int32_t value);

/*! Parameters. Each `add` declares one record at the root; `set_parent` moves it under a group.
    IDs are the author's stable numbers; keys are the OpenFX names. */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_float(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double default_value, double minimum, double maximum);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_int(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t default_value, int32_t minimum, int32_t maximum);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_bool(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t default_value);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_string(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, const char* default_value);
/*! `option_keys` and `option_labels` are arrays of `option_count` strings; a null labels array
    uses the keys as labels. */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_choice(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, const char* const* option_keys, const char* const* option_labels, int32_t option_count, int32_t default_index);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_point(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double x, double y);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_rgb(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double r, double g, double b);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_rgba(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, double r, double g, double b, double a);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_button(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_add_group(ofxgrip_manifest* manifest, uint32_t id, const char* key, const char* label, int32_t collapsed);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_parent(ofxgrip_manifest* manifest, uint32_t id, uint32_t parent_id);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_hint(ofxgrip_manifest* manifest, uint32_t id, const char* hint);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_display_range(ofxgrip_manifest* manifest, uint32_t id, double minimum, double maximum);
/*! A `parseFlags` list, for example `"hidden,notanimatable,+nostate"`. */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_flags(ofxgrip_manifest* manifest, uint32_t id, const char* flags);
/*! A comma-separated tag list. */
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_tags(ofxgrip_manifest* manifest, uint32_t id, const char* tags);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_meta_string(ofxgrip_manifest* manifest, uint32_t id, const char* name, const char* value);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_visible_when(ofxgrip_manifest* manifest, uint32_t id, const char* rule);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_set_enabled_when(ofxgrip_manifest* manifest, uint32_t id, const char* rule);
OFXGRIP_C_API ofxgrip_status ofxgrip_manifest_validate(const ofxgrip_manifest* manifest);
/*! The manifest as JSON. Returns the full length; copies at most `length - 1` bytes. */
OFXGRIP_C_API size_t ofxgrip_manifest_to_json(const ofxgrip_manifest* manifest, char* buffer, size_t length);

/* ---- effects -------------------------------------------------------------------------------- */

/*! The callbacks an effect implements. Any may be null except `render`. `instance` is what
    `create` returned for the effect instance (null when `create` is null). */
typedef struct ofxgrip_callbacks {
	void* user;                                                                                   /*!< Passed to every callback. */
	void* (*create)(ofxgrip_effect* effect, void* user);                                          /*!< Returns per-instance data. */
	void (*destroy)(ofxgrip_effect* effect, void* user, void* instance);
	ofxgrip_status (*instance_changed)(ofxgrip_effect* effect, void* user, void* instance, uint32_t parameter, const char* name, double time, int32_t reason);
	ofxgrip_status (*parameter_clicked)(ofxgrip_effect* effect, void* user, void* instance, uint32_t parameter);
	/*! Sets `*identity` to 1 to pass the source clip through. */
	ofxgrip_status (*is_identity)(ofxgrip_effect* effect, void* user, void* instance, const ofxgrip_snapshot* snapshot, double time, int32_t* identity);
	ofxgrip_status (*render)(ofxgrip_effect* effect, void* user, void* instance, ofxgrip_render* render);
} ofxgrip_callbacks;

/*! Registers an effect. The manifest is consumed (destroyed by the library). Returns the slot
    index, or -1 and leaves a message. Call from the bundle's registration function. */
OFXGRIP_C_API int32_t ofxgrip_register(ofxgrip_manifest* manifest, const ofxgrip_callbacks* callbacks);

/*! The three OpenFX exports forward here. `register_fn` runs once. */
OFXGRIP_C_API int32_t ofxgrip_bundle_plugin_count(void (*register_fn)(void));
OFXGRIP_C_API void* ofxgrip_bundle_plugin(void (*register_fn)(void), int32_t index);
OFXGRIP_C_API int32_t ofxgrip_bundle_set_host(const void* host);
/*! Drops every registered effect. For tests. */
OFXGRIP_C_API void ofxgrip_registry_clear(void);

/*! Per-instance services. */
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_number(ofxgrip_effect* effect, uint32_t id, double* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_number(ofxgrip_effect* effect, uint32_t id, double value);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_int(ofxgrip_effect* effect, uint32_t id, int32_t* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_int(ofxgrip_effect* effect, uint32_t id, int32_t value);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_bool(ofxgrip_effect* effect, uint32_t id, int32_t* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_bool(ofxgrip_effect* effect, uint32_t id, int32_t value);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_string(ofxgrip_effect* effect, uint32_t id, char* buffer, size_t length, size_t* needed);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_string(ofxgrip_effect* effect, uint32_t id, const char* value);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_hidden(ofxgrip_effect* effect, uint32_t id, int32_t* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_hidden(ofxgrip_effect* effect, uint32_t id, int32_t hidden);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_get_enabled(ofxgrip_effect* effect, uint32_t id, int32_t* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_enabled(ofxgrip_effect* effect, uint32_t id, int32_t enabled);
OFXGRIP_C_API ofxgrip_status ofxgrip_param_set_label(ofxgrip_effect* effect, uint32_t id, const char* label);
/*! The ID for a key, or UINT32_MAX. */
OFXGRIP_C_API uint32_t ofxgrip_param_id(ofxgrip_effect* effect, const char* key);
OFXGRIP_C_API ofxgrip_status ofxgrip_message(ofxgrip_effect* effect, int32_t kind, const char* id, const char* text);
OFXGRIP_C_API void ofxgrip_log(ofxgrip_effect* effect, int32_t level, const char* text);
OFXGRIP_C_API const char* ofxgrip_host_name(ofxgrip_effect* effect);
/*! Whether a registry quirk applies, by key name such as `MaskAlwaysConnected`. */
OFXGRIP_C_API int32_t ofxgrip_host_has_quirk(ofxgrip_effect* effect, const char* quirk);
/*! Opens an edit window for parameter writes outside a host callback. `ofxgrip_edit_end` closes
    it and flushes. */
OFXGRIP_C_API void* ofxgrip_edit_begin(ofxgrip_effect* effect, const char* name);
OFXGRIP_C_API void ofxgrip_edit_end(void* edit);

/* ---- render --------------------------------------------------------------------------------- */

/*! A frame. For a CPU frame `data` addresses the pixel at `x1, y1` and `row_bytes` steps rows;
    for a GPU frame `gpu_handle` is the API's buffer and `data` is null. */
typedef struct ofxgrip_image {
	void* data;
	int64_t row_bytes;
	int32_t x1, y1, x2, y2;
	int32_t depth;        /*!< ofxgrip_bit_depth */
	int32_t components;   /*!< ofxgrip_components */
	int32_t premultiplied;
	double pixel_aspect;
	int32_t gpu_api;      /*!< ofxgrip_gpu_api */
	void* gpu_handle;
	void* gpu_queue;
	int32_t needs_intermediate_copy;
} ofxgrip_image;

/*! The render arguments. */
typedef struct ofxgrip_render_args {
	double time;
	double render_scale_x, render_scale_y;
	int32_t window_x1, window_y1, window_x2, window_y2;
	int32_t draft;
	int32_t interactive;
	int32_t gpu_api;
	void* gpu_queue;
} ofxgrip_render_args;

OFXGRIP_C_API void ofxgrip_render_output(const ofxgrip_render* render, ofxgrip_image* out);
OFXGRIP_C_API int32_t ofxgrip_render_input_count(const ofxgrip_render* render);
/*! `connected` receives 0 when the clip returned no image; `out` is then empty. */
OFXGRIP_C_API ofxgrip_status ofxgrip_render_input(const ofxgrip_render* render, int32_t index, const char** clip, int32_t* connected, ofxgrip_image* out);
OFXGRIP_C_API ofxgrip_status ofxgrip_render_input_named(const ofxgrip_render* render, const char* clip, int32_t* connected, ofxgrip_image* out);
OFXGRIP_C_API void ofxgrip_render_get_args(const ofxgrip_render* render, ofxgrip_render_args* out);
OFXGRIP_C_API const ofxgrip_snapshot* ofxgrip_render_snapshot(const ofxgrip_render* render);
OFXGRIP_C_API int32_t ofxgrip_render_abort_requested(ofxgrip_render* render);
OFXGRIP_C_API ofxgrip_effect* ofxgrip_render_effect(ofxgrip_render* render);

/* ---- snapshot ------------------------------------------------------------------------------- */

OFXGRIP_C_API double ofxgrip_snapshot_number(const ofxgrip_snapshot* snapshot, uint32_t id, double fallback);
OFXGRIP_C_API int32_t ofxgrip_snapshot_int(const ofxgrip_snapshot* snapshot, uint32_t id, int32_t fallback);
OFXGRIP_C_API int32_t ofxgrip_snapshot_bool(const ofxgrip_snapshot* snapshot, uint32_t id, int32_t fallback);
OFXGRIP_C_API int32_t ofxgrip_snapshot_point(const ofxgrip_snapshot* snapshot, uint32_t id, double* x, double* y);
OFXGRIP_C_API int32_t ofxgrip_snapshot_rgba(const ofxgrip_snapshot* snapshot, uint32_t id, double* rgba);
OFXGRIP_C_API size_t ofxgrip_snapshot_string(const ofxgrip_snapshot* snapshot, uint32_t id, char* buffer, size_t length);
OFXGRIP_C_API int32_t ofxgrip_snapshot_has(const ofxgrip_snapshot* snapshot, uint32_t id);
OFXGRIP_C_API double ofxgrip_snapshot_time(const ofxgrip_snapshot* snapshot);
OFXGRIP_C_API double ofxgrip_snapshot_frame_rate(const ofxgrip_snapshot* snapshot);
OFXGRIP_C_API size_t ofxgrip_snapshot_page(const ofxgrip_snapshot* snapshot, char* buffer, size_t length);
/*! The license state the snapshot was built under. Introduced in OfxGrip 0.4.0. */
enum ofxgrip_license_state { OFXGRIP_LICENSE_UNKNOWN = 0, OFXGRIP_LICENSE_LICENSED = 1, OFXGRIP_LICENSE_TRIAL = 2, OFXGRIP_LICENSE_UNLICENSED = 3 };
OFXGRIP_C_API int32_t ofxgrip_snapshot_license_state(const ofxgrip_snapshot* snapshot);

/* ---- pixels --------------------------------------------------------------------------------- */

/*! The bytes one component occupies for a depth. */
OFXGRIP_C_API int32_t ofxgrip_bytes_per_component(int32_t depth);
/*! Reads one component as a float in [0, 1] for integer depths. */
OFXGRIP_C_API float ofxgrip_read_component(const void* pixel, int32_t depth, int32_t component);
/*! Writes one component from a float, clamping and rounding for integer depths. */
OFXGRIP_C_API void ofxgrip_write_component(void* pixel, int32_t depth, int32_t component, float value);

#ifdef __cplusplus
}
#endif

#endif /* OFXGRIP_C_H */

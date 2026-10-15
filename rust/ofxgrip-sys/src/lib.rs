// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
//! Raw bindings to the OfxGrip C contract, `include/ofxgrip/c/ofxgrip_c.h`.
//!
//! Every declaration mirrors the header one to one. The safe API is the `ofxgrip` crate.

#![allow(non_camel_case_types)]
#![allow(clippy::missing_safety_doc)]

use std::os::raw::{c_char, c_int, c_void};

pub type ofxgrip_status = i32;
pub const OFXGRIP_OK: ofxgrip_status = 0;
pub const OFXGRIP_FAILED: ofxgrip_status = 1;
pub const OFXGRIP_UNSUPPORTED: ofxgrip_status = 5;
pub const OFXGRIP_BAD_HANDLE: ofxgrip_status = 9;
pub const OFXGRIP_BAD_INDEX: ofxgrip_status = 10;
pub const OFXGRIP_VALUE: ofxgrip_status = 11;
pub const OFXGRIP_CANCELLED: ofxgrip_status = 12;
pub const OFXGRIP_INVALID_MANIFEST: ofxgrip_status = 100;
pub const OFXGRIP_NOT_FOUND: ofxgrip_status = 102;
pub const OFXGRIP_WRONG_PHASE: ofxgrip_status = 103;

pub const OFXGRIP_KIND_FILTER: i32 = 0;
pub const OFXGRIP_KIND_GENERATOR: i32 = 1;
pub const OFXGRIP_KIND_TRANSITION: i32 = 2;
pub const OFXGRIP_KIND_GENERAL: i32 = 3;

pub const OFXGRIP_DEPTH_NONE: i32 = 0;
pub const OFXGRIP_DEPTH_UINT8: i32 = 1;
pub const OFXGRIP_DEPTH_UINT16: i32 = 2;
pub const OFXGRIP_DEPTH_HALF: i32 = 3;
pub const OFXGRIP_DEPTH_FLOAT: i32 = 4;

pub const OFXGRIP_COMPONENTS_NONE: i32 = 0;
pub const OFXGRIP_COMPONENTS_RGBA: i32 = 1;
pub const OFXGRIP_COMPONENTS_RGB: i32 = 2;
pub const OFXGRIP_COMPONENTS_ALPHA: i32 = 3;

pub const OFXGRIP_GPU_NONE: i32 = 0;
pub const OFXGRIP_GPU_METAL: i32 = 1;
pub const OFXGRIP_GPU_CUDA: i32 = 2;
pub const OFXGRIP_GPU_OPENCL: i32 = 3;
pub const OFXGRIP_GPU_OPENGL: i32 = 4;
pub const OFXGRIP_GPU_VULKAN: i32 = 5;
pub const OFXGRIP_LICENSE_UNKNOWN: i32 = 0;
pub const OFXGRIP_LICENSE_LICENSED: i32 = 1;
pub const OFXGRIP_LICENSE_TRIAL: i32 = 2;
pub const OFXGRIP_LICENSE_UNLICENSED: i32 = 3;

pub const OFXGRIP_MESSAGE_LOG: i32 = 0;
pub const OFXGRIP_MESSAGE_MESSAGE: i32 = 1;
pub const OFXGRIP_MESSAGE_WARNING: i32 = 2;
pub const OFXGRIP_MESSAGE_ERROR: i32 = 3;
pub const OFXGRIP_MESSAGE_QUESTION: i32 = 4;

pub const OFXGRIP_LOG_DEBUG: i32 = 0;
pub const OFXGRIP_LOG_INFO: i32 = 1;
pub const OFXGRIP_LOG_WARNING: i32 = 2;
pub const OFXGRIP_LOG_ERROR: i32 = 3;

pub const OFXGRIP_CHANGE_USER: i32 = 0;
pub const OFXGRIP_CHANGE_PLUGIN: i32 = 1;
pub const OFXGRIP_CHANGE_TIME: i32 = 2;

#[repr(C)]
pub struct ofxgrip_manifest {
    _private: [u8; 0],
}
#[repr(C)]
pub struct ofxgrip_effect {
    _private: [u8; 0],
}
#[repr(C)]
pub struct ofxgrip_render {
    _private: [u8; 0],
}
#[repr(C)]
pub struct ofxgrip_snapshot {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct ofxgrip_capabilities {
    pub supports_tiles: i32,
    pub supports_multi_resolution: i32,
    pub temporal_clip_access: i32,
    pub no_spatial_awareness: i32,
    pub depth_mask: i32,
    pub metal_render: i32,
    pub cuda_render: i32,
    pub opencl_render: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ofxgrip_callbacks {
    pub user: *mut c_void,
    pub create: Option<unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void) -> *mut c_void>,
    pub destroy: Option<unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void, *mut c_void)>,
    pub instance_changed: Option<
        unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void, *mut c_void, u32, *const c_char, f64, i32) -> ofxgrip_status,
    >,
    pub parameter_clicked: Option<unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void, *mut c_void, u32) -> ofxgrip_status>,
    pub is_identity: Option<
        unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void, *mut c_void, *const ofxgrip_snapshot, f64, *mut i32) -> ofxgrip_status,
    >,
    pub render: Option<unsafe extern "C" fn(*mut ofxgrip_effect, *mut c_void, *mut c_void, *mut ofxgrip_render) -> ofxgrip_status>,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ofxgrip_image {
    pub data: *mut c_void,
    pub row_bytes: i64,
    pub x1: i32,
    pub y1: i32,
    pub x2: i32,
    pub y2: i32,
    pub depth: i32,
    pub components: i32,
    pub premultiplied: i32,
    pub pixel_aspect: f64,
    pub gpu_api: i32,
    pub gpu_handle: *mut c_void,
    pub gpu_queue: *mut c_void,
    pub needs_intermediate_copy: i32,
}

impl Default for ofxgrip_image {
    fn default() -> Self {
        Self {
            data: std::ptr::null_mut(),
            row_bytes: 0,
            x1: 0,
            y1: 0,
            x2: 0,
            y2: 0,
            depth: 0,
            components: 0,
            premultiplied: 0,
            pixel_aspect: 1.0,
            gpu_api: 0,
            gpu_handle: std::ptr::null_mut(),
            gpu_queue: std::ptr::null_mut(),
            needs_intermediate_copy: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ofxgrip_render_args {
    pub time: f64,
    pub render_scale_x: f64,
    pub render_scale_y: f64,
    pub window_x1: i32,
    pub window_y1: i32,
    pub window_x2: i32,
    pub window_y2: i32,
    pub draft: i32,
    pub interactive: i32,
    pub gpu_api: i32,
    pub gpu_queue: *mut c_void,
}

impl Default for ofxgrip_render_args {
    fn default() -> Self {
        Self {
            time: 0.0,
            render_scale_x: 1.0,
            render_scale_y: 1.0,
            window_x1: 0,
            window_y1: 0,
            window_x2: 0,
            window_y2: 0,
            draft: 0,
            interactive: 0,
            gpu_api: 0,
            gpu_queue: std::ptr::null_mut(),
        }
    }
}

extern "C" {
    pub fn ofxgrip_version() -> *const c_char;
    pub fn ofxgrip_c_version(major: *mut i32, minor: *mut i32);
    pub fn ofxgrip_last_error_message() -> *const c_char;

    pub fn ofxgrip_manifest_create(ofx_id: *const c_char, name: *const c_char) -> *mut ofxgrip_manifest;
    pub fn ofxgrip_manifest_from_json(json: *const c_char, out: *mut *mut ofxgrip_manifest) -> ofxgrip_status;
    pub fn ofxgrip_manifest_destroy(manifest: *mut ofxgrip_manifest);
    pub fn ofxgrip_manifest_set_identity(manifest: *mut ofxgrip_manifest, uuid: *const c_char, grouping: *const c_char, description: *const c_char, kind: i32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_author(manifest: *mut ofxgrip_manifest, author: *const c_char, product_id: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_version(manifest: *mut ofxgrip_manifest, major: u32, minor: u32, patch: u32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_capabilities(manifest: *mut ofxgrip_manifest, capabilities: *const ofxgrip_capabilities) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_property(manifest: *mut ofxgrip_manifest, name: *const c_char, value: i32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_float(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, default_value: f64, minimum: f64, maximum: f64) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_int(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, default_value: i32, minimum: i32, maximum: i32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_bool(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, default_value: i32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_string(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, default_value: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_choice(
        manifest: *mut ofxgrip_manifest,
        id: u32,
        key: *const c_char,
        label: *const c_char,
        option_keys: *const *const c_char,
        option_labels: *const *const c_char,
        option_count: i32,
        default_index: i32,
    ) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_point(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, x: f64, y: f64) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_rgb(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, r: f64, g: f64, b: f64) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_rgba(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, r: f64, g: f64, b: f64, a: f64) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_button(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_add_group(manifest: *mut ofxgrip_manifest, id: u32, key: *const c_char, label: *const c_char, collapsed: i32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_parent(manifest: *mut ofxgrip_manifest, id: u32, parent_id: u32) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_hint(manifest: *mut ofxgrip_manifest, id: u32, hint: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_display_range(manifest: *mut ofxgrip_manifest, id: u32, minimum: f64, maximum: f64) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_flags(manifest: *mut ofxgrip_manifest, id: u32, flags: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_tags(manifest: *mut ofxgrip_manifest, id: u32, tags: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_meta_string(manifest: *mut ofxgrip_manifest, id: u32, name: *const c_char, value: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_visible_when(manifest: *mut ofxgrip_manifest, id: u32, rule: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_set_enabled_when(manifest: *mut ofxgrip_manifest, id: u32, rule: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_manifest_validate(manifest: *const ofxgrip_manifest) -> ofxgrip_status;
    pub fn ofxgrip_manifest_to_json(manifest: *const ofxgrip_manifest, buffer: *mut c_char, length: usize) -> usize;

    pub fn ofxgrip_register(manifest: *mut ofxgrip_manifest, callbacks: *const ofxgrip_callbacks) -> i32;
    pub fn ofxgrip_bundle_plugin_count(register_fn: Option<unsafe extern "C" fn()>) -> i32;
    pub fn ofxgrip_bundle_plugin(register_fn: Option<unsafe extern "C" fn()>, index: i32) -> *mut c_void;
    pub fn ofxgrip_bundle_set_host(host: *const c_void) -> i32;
    pub fn ofxgrip_registry_clear();

    pub fn ofxgrip_param_get_number(effect: *mut ofxgrip_effect, id: u32, out: *mut f64) -> ofxgrip_status;
    pub fn ofxgrip_param_set_number(effect: *mut ofxgrip_effect, id: u32, value: f64) -> ofxgrip_status;
    pub fn ofxgrip_param_get_int(effect: *mut ofxgrip_effect, id: u32, out: *mut i32) -> ofxgrip_status;
    pub fn ofxgrip_param_set_int(effect: *mut ofxgrip_effect, id: u32, value: i32) -> ofxgrip_status;
    pub fn ofxgrip_param_get_bool(effect: *mut ofxgrip_effect, id: u32, out: *mut i32) -> ofxgrip_status;
    pub fn ofxgrip_param_set_bool(effect: *mut ofxgrip_effect, id: u32, value: i32) -> ofxgrip_status;
    pub fn ofxgrip_param_get_string(effect: *mut ofxgrip_effect, id: u32, buffer: *mut c_char, length: usize, needed: *mut usize) -> ofxgrip_status;
    pub fn ofxgrip_param_set_string(effect: *mut ofxgrip_effect, id: u32, value: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_param_get_hidden(effect: *mut ofxgrip_effect, id: u32, out: *mut i32) -> ofxgrip_status;
    pub fn ofxgrip_param_set_hidden(effect: *mut ofxgrip_effect, id: u32, hidden: i32) -> ofxgrip_status;
    pub fn ofxgrip_param_get_enabled(effect: *mut ofxgrip_effect, id: u32, out: *mut i32) -> ofxgrip_status;
    pub fn ofxgrip_param_set_enabled(effect: *mut ofxgrip_effect, id: u32, enabled: i32) -> ofxgrip_status;
    pub fn ofxgrip_param_set_label(effect: *mut ofxgrip_effect, id: u32, label: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_param_id(effect: *mut ofxgrip_effect, key: *const c_char) -> u32;
    pub fn ofxgrip_message(effect: *mut ofxgrip_effect, kind: i32, id: *const c_char, text: *const c_char) -> ofxgrip_status;
    pub fn ofxgrip_log(effect: *mut ofxgrip_effect, level: i32, text: *const c_char);
    pub fn ofxgrip_host_name(effect: *mut ofxgrip_effect) -> *const c_char;
    pub fn ofxgrip_host_has_quirk(effect: *mut ofxgrip_effect, quirk: *const c_char) -> i32;
    pub fn ofxgrip_edit_begin(effect: *mut ofxgrip_effect, name: *const c_char) -> *mut c_void;
    pub fn ofxgrip_edit_end(edit: *mut c_void);

    pub fn ofxgrip_render_output(render: *const ofxgrip_render, out: *mut ofxgrip_image);
    pub fn ofxgrip_render_input_count(render: *const ofxgrip_render) -> i32;
    pub fn ofxgrip_render_input(render: *const ofxgrip_render, index: i32, clip: *mut *const c_char, connected: *mut i32, out: *mut ofxgrip_image) -> ofxgrip_status;
    pub fn ofxgrip_render_input_named(render: *const ofxgrip_render, clip: *const c_char, connected: *mut i32, out: *mut ofxgrip_image) -> ofxgrip_status;
    pub fn ofxgrip_render_get_args(render: *const ofxgrip_render, out: *mut ofxgrip_render_args);
    pub fn ofxgrip_render_snapshot(render: *const ofxgrip_render) -> *const ofxgrip_snapshot;
    pub fn ofxgrip_render_abort_requested(render: *mut ofxgrip_render) -> i32;
    pub fn ofxgrip_render_effect(render: *mut ofxgrip_render) -> *mut ofxgrip_effect;

    pub fn ofxgrip_snapshot_number(snapshot: *const ofxgrip_snapshot, id: u32, fallback: f64) -> f64;
    pub fn ofxgrip_snapshot_int(snapshot: *const ofxgrip_snapshot, id: u32, fallback: i32) -> i32;
    pub fn ofxgrip_snapshot_bool(snapshot: *const ofxgrip_snapshot, id: u32, fallback: i32) -> i32;
    pub fn ofxgrip_snapshot_point(snapshot: *const ofxgrip_snapshot, id: u32, x: *mut f64, y: *mut f64) -> i32;
    pub fn ofxgrip_snapshot_rgba(snapshot: *const ofxgrip_snapshot, id: u32, rgba: *mut f64) -> i32;
    pub fn ofxgrip_snapshot_string(snapshot: *const ofxgrip_snapshot, id: u32, buffer: *mut c_char, length: usize) -> usize;
    pub fn ofxgrip_snapshot_has(snapshot: *const ofxgrip_snapshot, id: u32) -> i32;
    pub fn ofxgrip_snapshot_time(snapshot: *const ofxgrip_snapshot) -> f64;
    pub fn ofxgrip_snapshot_frame_rate(snapshot: *const ofxgrip_snapshot) -> f64;
    pub fn ofxgrip_snapshot_page(snapshot: *const ofxgrip_snapshot, buffer: *mut c_char, length: usize) -> usize;
    pub fn ofxgrip_snapshot_license_state(snapshot: *const ofxgrip_snapshot) -> i32;

    pub fn ofxgrip_bytes_per_component(depth: i32) -> i32;
    pub fn ofxgrip_read_component(pixel: *const c_void, depth: i32, component: i32) -> f32;
    pub fn ofxgrip_write_component(pixel: *mut c_void, depth: i32, component: i32, value: f32);
}

/// The C type of the OpenFX exports a bundle forwards to the contract.
pub type c_int_t = c_int;

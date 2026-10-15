// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
//! Write OpenFX image-effect plug-ins in Rust on the OfxGrip framework.
//!
//! An effect is a type that implements [`Effect`]. A bundle registers one or more effects from a
//! registration function and exports the OpenFX entry points with [`bundle!`]:
//!
//! ```ignore
//! use ofxgrip::{bundle, Effect, Manifest, RenderContext, Result, Services};
//!
//! struct Gain;
//! impl Effect for Gain {
//!     fn render(&mut self, ctx: &mut RenderContext) -> Result<()> { /* ... */ Ok(()) }
//! }
//!
//! fn register() {
//!     let manifest = Manifest::new("com.example.gain", "Gain")
//!         .float(1, "gain", "Gain", 1.0, 0.0, 10.0).unwrap();
//!     ofxgrip::register(manifest, |_services: &mut Services| Gain).unwrap();
//! }
//! bundle!(register);
//! ```
//!
//! Everything the framework does for a C++ effect (the manifest lowering, the render snapshot,
//! meta and tags, visibility rules, presets, versioning, the quirk registry, the Metal backend
//! for frames the host passes) applies to a Rust effect, because the Rust effect is an OfxGrip
//! effect whose behavior is a table of callbacks.

use std::ffi::{c_void, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

pub use ofxgrip_sys as sys;

// ---- errors ---------------------------------------------------------------------------------------

/// A failure: a status code from the contract and a message.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub status: i32,
    pub message: String,
}

impl Error {
    /// An error with the contract's generic failure status.
    pub fn new(message: impl Into<String>) -> Self {
        Self { status: sys::OFXGRIP_FAILED, message: message.into() }
    }
    /// An error with a specific status.
    pub fn with_status(status: i32, message: impl Into<String>) -> Self {
        Self { status, message: message.into() }
    }
    /// The cancellation outcome, which a host never reports as a failure.
    pub fn cancelled() -> Self {
        Self { status: sys::OFXGRIP_CANCELLED, message: "Cancelled".into() }
    }
    pub fn is_cancellation(&self) -> bool {
        self.status == sys::OFXGRIP_CANCELLED
    }
    fn last(status: i32) -> Self {
        Self { status, message: last_error_message() }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} (status {})", self.message, self.status)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

fn check(status: i32) -> Result<()> {
    if status == sys::OFXGRIP_OK {
        Ok(())
    } else {
        Err(Error::last(status))
    }
}

/// The message of the last failing contract call on this thread.
pub fn last_error_message() -> String {
    unsafe { cstr_to_string(sys::ofxgrip_last_error_message()) }
}

/// The OfxGrip library version.
pub fn version() -> String {
    unsafe { cstr_to_string(sys::ofxgrip_version()) }
}

unsafe fn cstr_to_string(ptr: *const std::os::raw::c_char) -> String {
    if ptr.is_null() {
        String::new()
    } else {
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}

fn cstring(text: &str) -> CString {
    CString::new(text.replace('\0', "")).expect("no interior NUL")
}

// ---- enums ----------------------------------------------------------------------------------------

/// The kind of effect, which maps to an OpenFX context.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Filter,
    Generator,
    Transition,
    General,
}

/// A pixel component's storage type.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Depth {
    None,
    UInt8,
    UInt16,
    Half,
    Float,
}

impl Depth {
    fn from_raw(raw: i32) -> Self {
        match raw {
            sys::OFXGRIP_DEPTH_UINT8 => Depth::UInt8,
            sys::OFXGRIP_DEPTH_UINT16 => Depth::UInt16,
            sys::OFXGRIP_DEPTH_HALF => Depth::Half,
            sys::OFXGRIP_DEPTH_FLOAT => Depth::Float,
            _ => Depth::None,
        }
    }
    fn raw(self) -> i32 {
        match self {
            Depth::None => sys::OFXGRIP_DEPTH_NONE,
            Depth::UInt8 => sys::OFXGRIP_DEPTH_UINT8,
            Depth::UInt16 => sys::OFXGRIP_DEPTH_UINT16,
            Depth::Half => sys::OFXGRIP_DEPTH_HALF,
            Depth::Float => sys::OFXGRIP_DEPTH_FLOAT,
        }
    }
    /// The bytes one component occupies.
    pub fn bytes(self) -> usize {
        unsafe { sys::ofxgrip_bytes_per_component(self.raw()) as usize }
    }
}

/// The components an image carries.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Components {
    None,
    Rgba,
    Rgb,
    Alpha,
}

impl Components {
    fn from_raw(raw: i32) -> Self {
        match raw {
            sys::OFXGRIP_COMPONENTS_RGBA => Components::Rgba,
            sys::OFXGRIP_COMPONENTS_RGB => Components::Rgb,
            sys::OFXGRIP_COMPONENTS_ALPHA => Components::Alpha,
            _ => Components::None,
        }
    }
    /// The number of components.
    pub fn count(self) -> usize {
        match self {
            Components::Rgba => 4,
            Components::Rgb => 3,
            Components::Alpha => 1,
            Components::None => 0,
        }
    }
}

/// The GPU API a frame lives in. `None` is a CPU frame.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GpuApi {
    None,
    Metal,
    Cuda,
    OpenCl,
    OpenGl,
    Vulkan,
}

impl GpuApi {
    fn from_raw(raw: i32) -> Self {
        match raw {
            sys::OFXGRIP_GPU_METAL => GpuApi::Metal,
            sys::OFXGRIP_GPU_CUDA => GpuApi::Cuda,
            sys::OFXGRIP_GPU_OPENCL => GpuApi::OpenCl,
            sys::OFXGRIP_GPU_OPENGL => GpuApi::OpenGl,
            sys::OFXGRIP_GPU_VULKAN => GpuApi::Vulkan,
            _ => GpuApi::None,
        }
    }
}

/// The license state a render snapshot was built under.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LicenseState {
    Unknown,
    Licensed,
    Trial,
    Unlicensed,
}

impl LicenseState {
    fn from_raw(raw: i32) -> Self {
        match raw {
            sys::OFXGRIP_LICENSE_LICENSED => LicenseState::Licensed,
            sys::OFXGRIP_LICENSE_TRIAL => LicenseState::Trial,
            sys::OFXGRIP_LICENSE_UNLICENSED => LicenseState::Unlicensed,
            _ => LicenseState::Unknown,
        }
    }
    /// Whether the state unlocks the effect.
    pub fn unlocks(self) -> bool {
        matches!(self, LicenseState::Licensed | LicenseState::Trial)
    }
}

/// The kind of a message shown through the host.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MessageKind {
    Log,
    Message,
    Warning,
    Error,
    Question,
}

/// A log line's level.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel {
    Debug,
    Info,
    Warning,
    Error,
}

/// Why a parameter changed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ChangeReason {
    UserEdited,
    PluginEdited,
    TimeChanged,
}

// ---- manifest -------------------------------------------------------------------------------------

/// What the effect can do.
#[derive(Clone, Copy, Debug)]
pub struct Capabilities {
    pub supports_tiles: bool,
    pub supports_multi_resolution: bool,
    pub temporal_clip_access: bool,
    pub no_spatial_awareness: bool,
    pub depths: &'static [Depth],
    pub metal_render: bool,
    pub cuda_render: bool,
    pub opencl_render: bool,
}

impl Default for Capabilities {
    fn default() -> Self {
        Self {
            supports_tiles: true,
            supports_multi_resolution: true,
            temporal_clip_access: false,
            no_spatial_awareness: false,
            depths: &[Depth::UInt8, Depth::UInt16, Depth::Float],
            metal_render: false,
            cuda_render: false,
            opencl_render: false,
        }
    }
}

/// An effect manifest under construction. Consumed by [`register`].
pub struct Manifest {
    raw: *mut sys::ofxgrip_manifest,
}

impl Manifest {
    /// A manifest for an effect with an OpenFX identifier and a display name.
    pub fn new(ofx_id: &str, name: &str) -> Self {
        let id = cstring(ofx_id);
        let label = cstring(name);
        Self { raw: unsafe { sys::ofxgrip_manifest_create(id.as_ptr(), label.as_ptr()) } }
    }
    /// A manifest parsed from OfxGrip's JSON form.
    pub fn from_json(json: &str) -> Result<Self> {
        let text = cstring(json);
        let mut raw: *mut sys::ofxgrip_manifest = ptr::null_mut();
        check(unsafe { sys::ofxgrip_manifest_from_json(text.as_ptr(), &mut raw) })?;
        Ok(Self { raw })
    }
    fn apply(self, status: i32) -> Result<Self> {
        check(status)?;
        Ok(self)
    }
    pub fn identity(self, uuid: &str, grouping: &str, description: &str, kind: Kind) -> Result<Self> {
        let (u, g, d) = (cstring(uuid), cstring(grouping), cstring(description));
        let k = match kind {
            Kind::Filter => sys::OFXGRIP_KIND_FILTER,
            Kind::Generator => sys::OFXGRIP_KIND_GENERATOR,
            Kind::Transition => sys::OFXGRIP_KIND_TRANSITION,
            Kind::General => sys::OFXGRIP_KIND_GENERAL,
        };
        let status = unsafe { sys::ofxgrip_manifest_set_identity(self.raw, u.as_ptr(), g.as_ptr(), d.as_ptr(), k) };
        self.apply(status)
    }
    pub fn author(self, author: &str, product_id: &str) -> Result<Self> {
        let (a, p) = (cstring(author), cstring(product_id));
        let status = unsafe { sys::ofxgrip_manifest_set_author(self.raw, a.as_ptr(), p.as_ptr()) };
        self.apply(status)
    }
    pub fn version(self, major: u32, minor: u32, patch: u32) -> Result<Self> {
        let status = unsafe { sys::ofxgrip_manifest_set_version(self.raw, major, minor, patch) };
        self.apply(status)
    }
    pub fn capabilities(self, capabilities: Capabilities) -> Result<Self> {
        let mut raw = sys::ofxgrip_capabilities {
            supports_tiles: capabilities.supports_tiles as i32,
            supports_multi_resolution: capabilities.supports_multi_resolution as i32,
            temporal_clip_access: capabilities.temporal_clip_access as i32,
            no_spatial_awareness: capabilities.no_spatial_awareness as i32,
            depth_mask: 0,
            metal_render: capabilities.metal_render as i32,
            cuda_render: capabilities.cuda_render as i32,
            opencl_render: capabilities.opencl_render as i32,
        };
        for depth in capabilities.depths {
            raw.depth_mask |= 1 << depth.raw();
        }
        let status = unsafe { sys::ofxgrip_manifest_set_capabilities(self.raw, &raw) };
        self.apply(status)
    }
    /// A boolean effect property such as `manageMeta`, `debugMenu`, or `internationalize`.
    pub fn property(self, name: &str, value: bool) -> Result<Self> {
        let n = cstring(name);
        let status = unsafe { sys::ofxgrip_manifest_set_property(self.raw, n.as_ptr(), value as i32) };
        self.apply(status)
    }
    pub fn float(self, id: u32, key: &str, label: &str, default: f64, minimum: f64, maximum: f64) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_float(self.raw, id, k.as_ptr(), l.as_ptr(), default, minimum, maximum) };
        self.apply(status)
    }
    pub fn int(self, id: u32, key: &str, label: &str, default: i32, minimum: i32, maximum: i32) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_int(self.raw, id, k.as_ptr(), l.as_ptr(), default, minimum, maximum) };
        self.apply(status)
    }
    pub fn boolean(self, id: u32, key: &str, label: &str, default: bool) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_bool(self.raw, id, k.as_ptr(), l.as_ptr(), default as i32) };
        self.apply(status)
    }
    pub fn string(self, id: u32, key: &str, label: &str, default: &str) -> Result<Self> {
        let (k, l, d) = (cstring(key), cstring(label), cstring(default));
        let status = unsafe { sys::ofxgrip_manifest_add_string(self.raw, id, k.as_ptr(), l.as_ptr(), d.as_ptr()) };
        self.apply(status)
    }
    /// A choice whose options are `(key, label)` pairs with stable keys.
    pub fn choice(self, id: u32, key: &str, label: &str, options: &[(&str, &str)], default_index: i32) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let keys: Vec<CString> = options.iter().map(|(o, _)| cstring(o)).collect();
        let labels: Vec<CString> = options.iter().map(|(_, o)| cstring(o)).collect();
        let key_ptrs: Vec<*const std::os::raw::c_char> = keys.iter().map(|c| c.as_ptr()).collect();
        let label_ptrs: Vec<*const std::os::raw::c_char> = labels.iter().map(|c| c.as_ptr()).collect();
        let status = unsafe {
            sys::ofxgrip_manifest_add_choice(self.raw, id, k.as_ptr(), l.as_ptr(), key_ptrs.as_ptr(), label_ptrs.as_ptr(), options.len() as i32, default_index)
        };
        self.apply(status)
    }
    pub fn point(self, id: u32, key: &str, label: &str, x: f64, y: f64) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_point(self.raw, id, k.as_ptr(), l.as_ptr(), x, y) };
        self.apply(status)
    }
    pub fn rgb(self, id: u32, key: &str, label: &str, r: f64, g: f64, b: f64) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_rgb(self.raw, id, k.as_ptr(), l.as_ptr(), r, g, b) };
        self.apply(status)
    }
    pub fn rgba(self, id: u32, key: &str, label: &str, r: f64, g: f64, b: f64, a: f64) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_rgba(self.raw, id, k.as_ptr(), l.as_ptr(), r, g, b, a) };
        self.apply(status)
    }
    pub fn button(self, id: u32, key: &str, label: &str) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_button(self.raw, id, k.as_ptr(), l.as_ptr()) };
        self.apply(status)
    }
    pub fn group(self, id: u32, key: &str, label: &str, collapsed: bool) -> Result<Self> {
        let (k, l) = (cstring(key), cstring(label));
        let status = unsafe { sys::ofxgrip_manifest_add_group(self.raw, id, k.as_ptr(), l.as_ptr(), collapsed as i32) };
        self.apply(status)
    }
    pub fn parent(self, id: u32, parent_id: u32) -> Result<Self> {
        let status = unsafe { sys::ofxgrip_manifest_set_parent(self.raw, id, parent_id) };
        self.apply(status)
    }
    pub fn hint(self, id: u32, hint: &str) -> Result<Self> {
        let h = cstring(hint);
        let status = unsafe { sys::ofxgrip_manifest_set_hint(self.raw, id, h.as_ptr()) };
        self.apply(status)
    }
    pub fn display_range(self, id: u32, minimum: f64, maximum: f64) -> Result<Self> {
        let status = unsafe { sys::ofxgrip_manifest_set_display_range(self.raw, id, minimum, maximum) };
        self.apply(status)
    }
    /// A flag list such as `"hidden,notanimatable,+nostate"`.
    pub fn flags(self, id: u32, flags: &str) -> Result<Self> {
        let f = cstring(flags);
        let status = unsafe { sys::ofxgrip_manifest_set_flags(self.raw, id, f.as_ptr()) };
        self.apply(status)
    }
    /// A comma-separated tag list.
    pub fn tags(self, id: u32, tags: &str) -> Result<Self> {
        let t = cstring(tags);
        let status = unsafe { sys::ofxgrip_manifest_set_tags(self.raw, id, t.as_ptr()) };
        self.apply(status)
    }
    pub fn meta(self, id: u32, name: &str, value: &str) -> Result<Self> {
        let (n, v) = (cstring(name), cstring(value));
        let status = unsafe { sys::ofxgrip_manifest_set_meta_string(self.raw, id, n.as_ptr(), v.as_ptr()) };
        self.apply(status)
    }
    pub fn visible_when(self, id: u32, rule: &str) -> Result<Self> {
        let r = cstring(rule);
        let status = unsafe { sys::ofxgrip_manifest_set_visible_when(self.raw, id, r.as_ptr()) };
        self.apply(status)
    }
    pub fn enabled_when(self, id: u32, rule: &str) -> Result<Self> {
        let r = cstring(rule);
        let status = unsafe { sys::ofxgrip_manifest_set_enabled_when(self.raw, id, r.as_ptr()) };
        self.apply(status)
    }
    /// Checks the manifest as describe would.
    pub fn validate(&self) -> Result<()> {
        check(unsafe { sys::ofxgrip_manifest_validate(self.raw) })
    }
    /// The manifest as JSON.
    pub fn to_json(&self) -> String {
        let needed = unsafe { sys::ofxgrip_manifest_to_json(self.raw, ptr::null_mut(), 0) };
        let mut buffer = vec![0u8; needed + 1];
        unsafe { sys::ofxgrip_manifest_to_json(self.raw, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        buffer.truncate(needed);
        String::from_utf8_lossy(&buffer).into_owned()
    }
    fn into_raw(self) -> *mut sys::ofxgrip_manifest {
        let raw = self.raw;
        std::mem::forget(self);
        raw
    }
}

impl Drop for Manifest {
    fn drop(&mut self) {
        unsafe { sys::ofxgrip_manifest_destroy(self.raw) };
    }
}

impl std::fmt::Debug for Manifest {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Manifest(")?;
        f.write_str(&self.to_json())?;
        f.write_str(")")
    }
}

// ---- services -------------------------------------------------------------------------------------

/// The host services of one effect instance: parameter reads and writes at the current time,
/// messages, logging, and the host profile. Writes outside a host callback need an [`EditScope`].
pub struct Services {
    raw: *mut sys::ofxgrip_effect,
}

impl Services {
    pub fn number(&self, id: u32) -> Result<f64> {
        let mut out = 0.0;
        check(unsafe { sys::ofxgrip_param_get_number(self.raw, id, &mut out) })?;
        Ok(out)
    }
    pub fn set_number(&mut self, id: u32, value: f64) -> Result<()> {
        check(unsafe { sys::ofxgrip_param_set_number(self.raw, id, value) })
    }
    pub fn int(&self, id: u32) -> Result<i32> {
        let mut out = 0;
        check(unsafe { sys::ofxgrip_param_get_int(self.raw, id, &mut out) })?;
        Ok(out)
    }
    pub fn set_int(&mut self, id: u32, value: i32) -> Result<()> {
        check(unsafe { sys::ofxgrip_param_set_int(self.raw, id, value) })
    }
    pub fn boolean(&self, id: u32) -> Result<bool> {
        let mut out = 0;
        check(unsafe { sys::ofxgrip_param_get_bool(self.raw, id, &mut out) })?;
        Ok(out != 0)
    }
    pub fn set_boolean(&mut self, id: u32, value: bool) -> Result<()> {
        check(unsafe { sys::ofxgrip_param_set_bool(self.raw, id, value as i32) })
    }
    pub fn string(&self, id: u32) -> Result<String> {
        let mut needed = 0usize;
        check(unsafe { sys::ofxgrip_param_get_string(self.raw, id, ptr::null_mut(), 0, &mut needed) })?;
        let mut buffer = vec![0u8; needed + 1];
        check(unsafe { sys::ofxgrip_param_get_string(self.raw, id, buffer.as_mut_ptr() as *mut _, buffer.len(), &mut needed) })?;
        buffer.truncate(needed);
        Ok(String::from_utf8_lossy(&buffer).into_owned())
    }
    pub fn set_string(&mut self, id: u32, value: &str) -> Result<()> {
        let v = cstring(value);
        check(unsafe { sys::ofxgrip_param_set_string(self.raw, id, v.as_ptr()) })
    }
    pub fn hidden(&self, id: u32) -> Result<bool> {
        let mut out = 0;
        check(unsafe { sys::ofxgrip_param_get_hidden(self.raw, id, &mut out) })?;
        Ok(out != 0)
    }
    pub fn set_hidden(&mut self, id: u32, hidden: bool) -> Result<()> {
        check(unsafe { sys::ofxgrip_param_set_hidden(self.raw, id, hidden as i32) })
    }
    pub fn enabled(&self, id: u32) -> Result<bool> {
        let mut out = 0;
        check(unsafe { sys::ofxgrip_param_get_enabled(self.raw, id, &mut out) })?;
        Ok(out != 0)
    }
    pub fn set_enabled(&mut self, id: u32, enabled: bool) -> Result<()> {
        check(unsafe { sys::ofxgrip_param_set_enabled(self.raw, id, enabled as i32) })
    }
    pub fn set_label(&mut self, id: u32, label: &str) -> Result<()> {
        let l = cstring(label);
        check(unsafe { sys::ofxgrip_param_set_label(self.raw, id, l.as_ptr()) })
    }
    /// The ID of a parameter key, or none.
    pub fn id(&self, key: &str) -> Option<u32> {
        let k = cstring(key);
        let id = unsafe { sys::ofxgrip_param_id(self.raw, k.as_ptr()) };
        if id == u32::MAX {
            None
        } else {
            Some(id)
        }
    }
    pub fn message(&mut self, kind: MessageKind, id: &str, text: &str) -> Result<()> {
        let kind = match kind {
            MessageKind::Log => sys::OFXGRIP_MESSAGE_LOG,
            MessageKind::Message => sys::OFXGRIP_MESSAGE_MESSAGE,
            MessageKind::Warning => sys::OFXGRIP_MESSAGE_WARNING,
            MessageKind::Error => sys::OFXGRIP_MESSAGE_ERROR,
            MessageKind::Question => sys::OFXGRIP_MESSAGE_QUESTION,
        };
        let (i, t) = (cstring(id), cstring(text));
        check(unsafe { sys::ofxgrip_message(self.raw, kind, i.as_ptr(), t.as_ptr()) })
    }
    pub fn log(&self, level: LogLevel, text: &str) {
        let level = match level {
            LogLevel::Debug => sys::OFXGRIP_LOG_DEBUG,
            LogLevel::Info => sys::OFXGRIP_LOG_INFO,
            LogLevel::Warning => sys::OFXGRIP_LOG_WARNING,
            LogLevel::Error => sys::OFXGRIP_LOG_ERROR,
        };
        let t = cstring(text);
        unsafe { sys::ofxgrip_log(self.raw, level, t.as_ptr()) };
    }
    pub fn host_name(&self) -> String {
        unsafe { cstr_to_string(sys::ofxgrip_host_name(self.raw)) }
    }
    /// Whether a registry quirk applies, by key name such as `MaskAlwaysConnected`.
    pub fn has_quirk(&self, quirk: &str) -> bool {
        let q = cstring(quirk);
        unsafe { sys::ofxgrip_host_has_quirk(self.raw, q.as_ptr()) != 0 }
    }
    /// Opens a window for parameter writes outside a host callback.
    pub fn edit(&mut self, name: &str) -> EditScope {
        let n = cstring(name);
        EditScope { raw: unsafe { sys::ofxgrip_edit_begin(self.raw, n.as_ptr()) } }
    }
}

/// An open edit window. Closing it flushes the effect's state.
pub struct EditScope {
    raw: *mut c_void,
}

impl Drop for EditScope {
    fn drop(&mut self) {
        unsafe { sys::ofxgrip_edit_end(self.raw) };
    }
}

// ---- render ---------------------------------------------------------------------------------------

/// A frame. For a CPU frame the pixels are addressable; for a GPU frame `gpu_handle` is the API's
/// buffer and the CPU accessors return nothing.
#[derive(Clone, Copy, Debug)]
pub struct Image {
    raw: sys::ofxgrip_image,
}

impl Image {
    pub fn x1(&self) -> i32 {
        self.raw.x1
    }
    pub fn y1(&self) -> i32 {
        self.raw.y1
    }
    pub fn x2(&self) -> i32 {
        self.raw.x2
    }
    pub fn y2(&self) -> i32 {
        self.raw.y2
    }
    pub fn width(&self) -> i32 {
        (self.raw.x2 - self.raw.x1).max(0)
    }
    pub fn height(&self) -> i32 {
        (self.raw.y2 - self.raw.y1).max(0)
    }
    pub fn depth(&self) -> Depth {
        Depth::from_raw(self.raw.depth)
    }
    pub fn components(&self) -> Components {
        Components::from_raw(self.raw.components)
    }
    pub fn premultiplied(&self) -> bool {
        self.raw.premultiplied != 0
    }
    pub fn pixel_aspect(&self) -> f64 {
        self.raw.pixel_aspect
    }
    pub fn gpu_api(&self) -> GpuApi {
        GpuApi::from_raw(self.raw.gpu_api)
    }
    pub fn is_gpu(&self) -> bool {
        self.gpu_api() != GpuApi::None
    }
    pub fn gpu_handle(&self) -> *mut c_void {
        self.raw.gpu_handle
    }
    pub fn gpu_queue(&self) -> *mut c_void {
        self.raw.gpu_queue
    }
    pub fn needs_intermediate_copy(&self) -> bool {
        self.raw.needs_intermediate_copy != 0
    }
    pub fn row_bytes(&self) -> i64 {
        self.raw.row_bytes
    }
    /// The address of a pixel inside the bounds of a CPU frame, or none.
    pub fn pixel(&self, x: i32, y: i32) -> Option<*mut u8> {
        if self.raw.data.is_null() || x < self.raw.x1 || x >= self.raw.x2 || y < self.raw.y1 || y >= self.raw.y2 {
            return None;
        }
        let stride = self.components().count() * self.depth().bytes();
        let offset = (y - self.raw.y1) as i64 * self.raw.row_bytes + (x - self.raw.x1) as i64 * stride as i64;
        Some(unsafe { (self.raw.data as *mut u8).offset(offset as isize) })
    }
    /// Reads a component in [0, 1] (for integer depths) at a pixel, or 0 outside the bounds.
    pub fn read(&self, x: i32, y: i32, component: usize) -> f32 {
        match self.pixel(x, y) {
            Some(p) => unsafe { sys::ofxgrip_read_component(p as *const c_void, self.raw.depth, component as i32) },
            None => 0.0,
        }
    }
    /// Writes a component from a float, clamping and rounding for integer depths. Outside the
    /// bounds nothing happens.
    pub fn write(&self, x: i32, y: i32, component: usize, value: f32) {
        if let Some(p) = self.pixel(x, y) {
            unsafe { sys::ofxgrip_write_component(p as *mut c_void, self.raw.depth, component as i32, value) };
        }
    }
}

/// An input clip's frame for one render.
#[derive(Clone, Debug)]
pub struct Input {
    pub clip: String,
    pub connected: bool,
    pub image: Image,
}

/// The render action's arguments.
#[derive(Clone, Copy, Debug)]
pub struct RenderArgs {
    pub time: f64,
    pub render_scale: (f64, f64),
    pub window: (i32, i32, i32, i32),
    pub draft: bool,
    pub interactive: bool,
    pub gpu_api: GpuApi,
    pub gpu_queue: *mut c_void,
}

/// The immutable snapshot a render reads.
pub struct Snapshot {
    raw: *const sys::ofxgrip_snapshot,
}

impl Snapshot {
    pub fn number(&self, id: u32, fallback: f64) -> f64 {
        unsafe { sys::ofxgrip_snapshot_number(self.raw, id, fallback) }
    }
    pub fn int(&self, id: u32, fallback: i32) -> i32 {
        unsafe { sys::ofxgrip_snapshot_int(self.raw, id, fallback) }
    }
    pub fn boolean(&self, id: u32, fallback: bool) -> bool {
        unsafe { sys::ofxgrip_snapshot_bool(self.raw, id, fallback as i32) != 0 }
    }
    pub fn point(&self, id: u32) -> Option<(f64, f64)> {
        let (mut x, mut y) = (0.0, 0.0);
        if unsafe { sys::ofxgrip_snapshot_point(self.raw, id, &mut x, &mut y) } != 0 {
            Some((x, y))
        } else {
            None
        }
    }
    pub fn rgba(&self, id: u32) -> Option<[f64; 4]> {
        let mut rgba = [0.0; 4];
        if unsafe { sys::ofxgrip_snapshot_rgba(self.raw, id, rgba.as_mut_ptr()) } != 0 {
            Some(rgba)
        } else {
            None
        }
    }
    pub fn string(&self, id: u32) -> String {
        let needed = unsafe { sys::ofxgrip_snapshot_string(self.raw, id, ptr::null_mut(), 0) };
        let mut buffer = vec![0u8; needed + 1];
        unsafe { sys::ofxgrip_snapshot_string(self.raw, id, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        buffer.truncate(needed);
        String::from_utf8_lossy(&buffer).into_owned()
    }
    pub fn has(&self, id: u32) -> bool {
        unsafe { sys::ofxgrip_snapshot_has(self.raw, id) != 0 }
    }
    pub fn time(&self) -> f64 {
        unsafe { sys::ofxgrip_snapshot_time(self.raw) }
    }
    pub fn frame_rate(&self) -> f64 {
        unsafe { sys::ofxgrip_snapshot_frame_rate(self.raw) }
    }
    /// The license state the snapshot was built under.
    pub fn license_state(&self) -> LicenseState {
        LicenseState::from_raw(unsafe { sys::ofxgrip_snapshot_license_state(self.raw) })
    }
    /// The Resolve page the instance lives on, or empty.
    pub fn page(&self) -> String {
        let needed = unsafe { sys::ofxgrip_snapshot_page(self.raw, ptr::null_mut(), 0) };
        let mut buffer = vec![0u8; needed + 1];
        unsafe { sys::ofxgrip_snapshot_page(self.raw, buffer.as_mut_ptr() as *mut _, buffer.len()) };
        buffer.truncate(needed);
        String::from_utf8_lossy(&buffer).into_owned()
    }
}

/// What a render reads and writes.
pub struct RenderContext {
    raw: *mut sys::ofxgrip_render,
}

impl RenderContext {
    pub fn output(&self) -> Image {
        let mut raw = sys::ofxgrip_image::default();
        unsafe { sys::ofxgrip_render_output(self.raw, &mut raw) };
        Image { raw }
    }
    pub fn inputs(&self) -> Vec<Input> {
        let count = unsafe { sys::ofxgrip_render_input_count(self.raw) };
        (0..count)
            .filter_map(|i| {
                let mut clip: *const std::os::raw::c_char = ptr::null();
                let mut connected = 0;
                let mut raw = sys::ofxgrip_image::default();
                if unsafe { sys::ofxgrip_render_input(self.raw, i, &mut clip, &mut connected, &mut raw) } != sys::OFXGRIP_OK {
                    return None;
                }
                Some(Input { clip: unsafe { cstr_to_string(clip) }, connected: connected != 0, image: Image { raw } })
            })
            .collect()
    }
    /// The input for a clip name, or none when the clip is not declared.
    pub fn input(&self, clip: &str) -> Option<Input> {
        let c = cstring(clip);
        let mut connected = 0;
        let mut raw = sys::ofxgrip_image::default();
        if unsafe { sys::ofxgrip_render_input_named(self.raw, c.as_ptr(), &mut connected, &mut raw) } != sys::OFXGRIP_OK {
            return None;
        }
        Some(Input { clip: clip.to_string(), connected: connected != 0, image: Image { raw } })
    }
    /// The first input, or none for a generator.
    pub fn source(&self) -> Option<Input> {
        self.inputs().into_iter().next()
    }
    pub fn args(&self) -> RenderArgs {
        let mut raw = sys::ofxgrip_render_args::default();
        unsafe { sys::ofxgrip_render_get_args(self.raw, &mut raw) };
        RenderArgs {
            time: raw.time,
            render_scale: (raw.render_scale_x, raw.render_scale_y),
            window: (raw.window_x1, raw.window_y1, raw.window_x2, raw.window_y2),
            draft: raw.draft != 0,
            interactive: raw.interactive != 0,
            gpu_api: GpuApi::from_raw(raw.gpu_api),
            gpu_queue: raw.gpu_queue,
        }
    }
    pub fn snapshot(&self) -> Snapshot {
        Snapshot { raw: unsafe { sys::ofxgrip_render_snapshot(self.raw) } }
    }
    pub fn abort_requested(&self) -> bool {
        unsafe { sys::ofxgrip_render_abort_requested(self.raw) != 0 }
    }
    /// The read-only services of the rendering instance.
    pub fn services(&self) -> Services {
        Services { raw: unsafe { sys::ofxgrip_render_effect(self.raw) } }
    }
}

// ---- effects --------------------------------------------------------------------------------------

/// One parameter change.
#[derive(Clone, Debug)]
pub struct Change {
    pub parameter: u32,
    pub name: String,
    pub time: f64,
    pub reason: ChangeReason,
}

/// An effect. Only `render` is required.
pub trait Effect: 'static {
    fn render(&mut self, context: &mut RenderContext) -> Result<()>;
    fn instance_changed(&mut self, _services: &mut Services, _change: &Change) -> Result<()> {
        Ok(())
    }
    fn parameter_clicked(&mut self, _services: &mut Services, _parameter: u32) -> Result<()> {
        Ok(())
    }
    /// Whether the source passes through unchanged at a time.
    fn is_identity(&mut self, _snapshot: &Snapshot, _time: f64) -> Result<bool> {
        Ok(false)
    }
}

struct Factory<E: Effect> {
    make: Box<dyn Fn(&mut Services) -> E>,
}

fn status_of<T>(result: std::thread::Result<Result<T>>) -> i32 {
    match result {
        Ok(Ok(_)) => sys::OFXGRIP_OK,
        Ok(Err(e)) => e.status,
        Err(_) => sys::OFXGRIP_FAILED,
    }
}

unsafe extern "C" fn create_cb<E: Effect>(effect: *mut sys::ofxgrip_effect, user: *mut c_void) -> *mut c_void {
    let factory = &*(user as *const Factory<E>);
    let mut services = Services { raw: effect };
    match catch_unwind(AssertUnwindSafe(|| (factory.make)(&mut services))) {
        Ok(instance) => Box::into_raw(Box::new(instance)) as *mut c_void,
        Err(_) => ptr::null_mut(),
    }
}

unsafe extern "C" fn destroy_cb<E: Effect>(_effect: *mut sys::ofxgrip_effect, _user: *mut c_void, instance: *mut c_void) {
    if !instance.is_null() {
        drop(Box::from_raw(instance as *mut E));
    }
}

unsafe extern "C" fn changed_cb<E: Effect>(
    effect: *mut sys::ofxgrip_effect,
    _user: *mut c_void,
    instance: *mut c_void,
    parameter: u32,
    name: *const std::os::raw::c_char,
    time: f64,
    reason: i32,
) -> i32 {
    if instance.is_null() {
        return sys::OFXGRIP_OK;
    }
    let this = &mut *(instance as *mut E);
    let mut services = Services { raw: effect };
    let change = Change {
        parameter,
        name: cstr_to_string(name),
        time,
        reason: match reason {
            sys::OFXGRIP_CHANGE_PLUGIN => ChangeReason::PluginEdited,
            sys::OFXGRIP_CHANGE_TIME => ChangeReason::TimeChanged,
            _ => ChangeReason::UserEdited,
        },
    };
    status_of(catch_unwind(AssertUnwindSafe(|| this.instance_changed(&mut services, &change))))
}

unsafe extern "C" fn clicked_cb<E: Effect>(effect: *mut sys::ofxgrip_effect, _user: *mut c_void, instance: *mut c_void, parameter: u32) -> i32 {
    if instance.is_null() {
        return sys::OFXGRIP_OK;
    }
    let this = &mut *(instance as *mut E);
    let mut services = Services { raw: effect };
    status_of(catch_unwind(AssertUnwindSafe(|| this.parameter_clicked(&mut services, parameter))))
}

unsafe extern "C" fn identity_cb<E: Effect>(
    _effect: *mut sys::ofxgrip_effect,
    _user: *mut c_void,
    instance: *mut c_void,
    snapshot: *const sys::ofxgrip_snapshot,
    time: f64,
    identity: *mut i32,
) -> i32 {
    if instance.is_null() {
        return sys::OFXGRIP_OK;
    }
    let this = &mut *(instance as *mut E);
    let snap = Snapshot { raw: snapshot };
    match catch_unwind(AssertUnwindSafe(|| this.is_identity(&snap, time))) {
        Ok(Ok(flag)) => {
            *identity = flag as i32;
            sys::OFXGRIP_OK
        }
        Ok(Err(e)) => e.status,
        Err(_) => sys::OFXGRIP_FAILED,
    }
}

unsafe extern "C" fn render_cb<E: Effect>(_effect: *mut sys::ofxgrip_effect, _user: *mut c_void, instance: *mut c_void, render: *mut sys::ofxgrip_render) -> i32 {
    if instance.is_null() {
        return sys::OFXGRIP_BAD_HANDLE;
    }
    let this = &mut *(instance as *mut E);
    let mut context = RenderContext { raw: render };
    status_of(catch_unwind(AssertUnwindSafe(|| this.render(&mut context))))
}

/// Registers an effect. `make` builds an instance from its services on every create. Returns
/// the plug-in slot index. Call from the bundle's registration function.
pub fn register<E: Effect>(manifest: Manifest, make: impl Fn(&mut Services) -> E + 'static) -> Result<i32> {
    let factory: *mut Factory<E> = Box::into_raw(Box::new(Factory { make: Box::new(make) }));
    let callbacks = sys::ofxgrip_callbacks {
        user: factory as *mut c_void,
        create: Some(create_cb::<E>),
        destroy: Some(destroy_cb::<E>),
        instance_changed: Some(changed_cb::<E>),
        parameter_clicked: Some(clicked_cb::<E>),
        is_identity: Some(identity_cb::<E>),
        render: Some(render_cb::<E>),
    };
    let index = unsafe { sys::ofxgrip_register(manifest.into_raw(), &callbacks) };
    if index < 0 {
        // The factory lives for the process on success; on failure it is freed.
        unsafe { drop(Box::from_raw(factory)) };
        return Err(Error::last(sys::OFXGRIP_FAILED));
    }
    Ok(index)
}

/// Exports the three OpenFX entry points for a bundle. `$register` is a `fn()` that calls
/// [`register`] once per effect.
#[macro_export]
macro_rules! bundle {
    ($register:path) => {
        unsafe extern "C" fn __ofxgrip_register_trampoline() {
            let _ = ::std::panic::catch_unwind(|| $register());
        }
        #[no_mangle]
        pub extern "C" fn OfxGetNumberOfPlugins() -> ::std::os::raw::c_int {
            unsafe { $crate::sys::ofxgrip_bundle_plugin_count(Some(__ofxgrip_register_trampoline)) as ::std::os::raw::c_int }
        }
        #[no_mangle]
        pub extern "C" fn OfxGetPlugin(nth: ::std::os::raw::c_int) -> *mut ::std::ffi::c_void {
            unsafe { $crate::sys::ofxgrip_bundle_plugin(Some(__ofxgrip_register_trampoline), nth as i32) }
        }
        #[no_mangle]
        pub extern "C" fn OfxSetHost(host: *const ::std::ffi::c_void) -> ::std::os::raw::c_int {
            unsafe { $crate::sys::ofxgrip_bundle_set_host(host) as ::std::os::raw::c_int }
        }
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn manifest_builds_validates_and_serializes() {
        let manifest = Manifest::new("com.example.rust", "Rust Test")
            .identity("44444444-4444-4444-8444-444444444444", "OfxGrip Tests", "A Rust effect", Kind::Filter)
            .unwrap()
            .author("Belisoful", "ofxgrip")
            .unwrap()
            .version(1, 0, 0)
            .unwrap()
            .capabilities(Capabilities { depths: &[Depth::UInt8, Depth::Float], ..Capabilities::default() })
            .unwrap()
            .property("regression", true)
            .unwrap()
            .float(1, "gain", "Gain", 1.0, 0.0, 10.0)
            .unwrap()
            .display_range(1, 0.0, 2.0)
            .unwrap()
            .hint(1, "Multiplies")
            .unwrap()
            .tags(1, "look")
            .unwrap()
            .meta(1, "unit", "x")
            .unwrap()
            .int(2, "count", "Count", 3, 0, 10)
            .unwrap()
            .boolean(3, "on", "On", false)
            .unwrap()
            .string(4, "name", "Name", "hello")
            .unwrap()
            .choice(5, "mode", "Mode", &[("soft", "Soft"), ("hard", "Hard")], 1)
            .unwrap()
            .point(6, "center", "Center", 0.5, 0.5)
            .unwrap()
            .rgb(7, "color", "Color", 1.0, 0.5, 0.25)
            .unwrap()
            .rgba(8, "tint", "Tint", 1.0, 0.5, 0.25, 1.0)
            .unwrap()
            .button(9, "go", "Go")
            .unwrap()
            .group(10, "group", "Group", true)
            .unwrap()
            .float(11, "inner", "Inner", 0.5, 0.0, 1.0)
            .unwrap()
            .parent(11, 10)
            .unwrap()
            .flags(11, "notanimatable")
            .unwrap()
            .visible_when(11, "on")
            .unwrap()
            .enabled_when(2, "mode == 'hard'")
            .unwrap();
        manifest.validate().unwrap();
        let json = manifest.to_json();
        assert!(json.contains("\"gain\""));
        let parsed = Manifest::from_json(&json).unwrap();
        parsed.validate().unwrap();
        assert!(Manifest::from_json("{ nope").is_err());
        assert!(!last_error_message().is_empty());
        assert!(!version().is_empty());
        // A duplicate ID is refused with a message.
        let err = Manifest::new("com.example.dup", "Dup").float(1, "a", "A", 0.0, 0.0, 1.0).unwrap().float(1, "b", "B", 0.0, 0.0, 1.0).unwrap_err();
        assert_eq!(err.status, sys::OFXGRIP_INVALID_MANIFEST);
        assert!(!err.to_string().is_empty());
        assert!(Error::cancelled().is_cancellation());
        assert_eq!(Error::new("x").status, sys::OFXGRIP_FAILED);
        assert_eq!(Error::with_status(7, "y").status, 7);
    }

    struct Noop;
    impl Effect for Noop {
        fn render(&mut self, _context: &mut RenderContext) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn registration_refuses_an_invalid_manifest_and_accepts_a_valid_one() {
        unsafe { sys::ofxgrip_registry_clear() };
        let bad = Manifest::new("", "");
        assert!(register(bad, |_| Noop).is_err());
        let good = Manifest::new("com.example.rust.noop", "Noop").float(1, "gain", "Gain", 1.0, 0.0, 10.0).unwrap();
        assert_eq!(register(good, |_| Noop).unwrap(), 0);
        let again = Manifest::new("com.example.rust.noop", "Noop");
        assert!(register(again, |_| Noop).is_err());
        assert_eq!(unsafe { sys::ofxgrip_bundle_plugin_count(None) }, 1);
        assert!(!unsafe { sys::ofxgrip_bundle_plugin(None, 0) }.is_null());
        unsafe { sys::ofxgrip_registry_clear() };
    }

    #[test]
    fn enums_convert() {
        assert_eq!(Depth::from_raw(sys::OFXGRIP_DEPTH_HALF), Depth::Half);
        assert_eq!(Depth::from_raw(99), Depth::None);
        assert_eq!(Depth::Float.bytes(), 4);
        assert_eq!(Components::from_raw(sys::OFXGRIP_COMPONENTS_RGB).count(), 3);
        assert_eq!(Components::from_raw(77), Components::None);
        assert_eq!(GpuApi::from_raw(sys::OFXGRIP_GPU_METAL), GpuApi::Metal);
        assert_eq!(GpuApi::from_raw(77), GpuApi::None);
        assert_eq!(LicenseState::from_raw(sys::OFXGRIP_LICENSE_TRIAL), LicenseState::Trial);
        assert_eq!(LicenseState::from_raw(77), LicenseState::Unknown);
        assert!(LicenseState::Licensed.unlocks());
        assert!(!LicenseState::Unlicensed.unlocks());
        let snapshot = Snapshot { raw: ptr::null() };
        assert_eq!(snapshot.license_state(), LicenseState::Unknown);
        let image = Image { raw: sys::ofxgrip_image::default() };
        assert_eq!(image.pixel(0, 0), None);
        assert_eq!(image.read(0, 0, 0), 0.0);
        image.write(0, 0, 0, 1.0);
        assert!(!image.is_gpu());
        assert_eq!(image.width(), 0);
    }
}

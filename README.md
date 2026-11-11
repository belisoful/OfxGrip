# OfxGrip

![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)
![OpenFX 1.5](https://img.shields.io/badge/OpenFX-1.5-orange.svg)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

OfxGrip is a C++20 framework for OpenFX image-effect plug-ins. It is the OpenFX counterpart of [FxGrip](https://github.com/belisoful/FxGrip), and it gives DaVinci Resolve, Fusion, Nuke, and Natron plug-ins the same foundation FxGrip gives Final Cut Pro and Motion plug-ins.

## Features

- **A declarative effect manifest.** Identity, version, capabilities, clips, and parameters are data, authored in C++ or JSON, and lowered to OpenFX descriptors at describe time. Parameters have stable numeric IDs and string keys, separate host and app flags, tags, meta, and stable choice-option keys.
- **An immutable render snapshot.** Every render reads one snapshot of the parameter values at the render time plus the host's render information, built from timed reads. Render paths are thread-safe.
- **An extension bus.** Lifecycle events for every OpenFX action and API-traffic events around every parameter read and write, delivered in priority order, with veto and override.
- **A host quirks registry.** Host behavior is asked for by key (`profile.has(Quirk::MaskAlwaysConnected)`), never by host name. Probed capabilities override the table.
- **A parameter model with tags, meta, rules, and target presets.** `effect["gain"]->number(t)` reads through the host; tags and meta persist with the document; `visibleWhen` and `enabledWhen` rules and menu-driven target presets rig the inspector without code; a stored schema version drives migrations.
- **Presets, analysis, and inspector controls.** `.fxpreset` files interoperate with FxFactory and FxGrip; a presets control lists built-in, bundled, and user presets; an analysis pass stores a record per frame with disk spill; sections, banners, status lines, and the About and Debug menus lower to standard OpenFX parameters.
- **A C contract and a Rust crate.** `ofxgrip_c.h` exposes the manifest, the callbacks, the render context, and the services to any language; the `ofxgrip` crate wraps it so a Rust effect is an OfxGrip effect (`rust/examples/gain-rs`).
- **Slang kernels with a CPU fallback.** One compute kernel over the `GripKernel` module runs on Metal, CUDA, Vulkan, and the CPU; the generated sources are checked in, so the build never needs `slangc`. The catalog resolves native, universal, and CPU builds per backend, host, and OS, and the effect declares only the GPU paths where every kernel resolves.
- **GPU backends that load without their runtime.** Kernels run on the host's command queue over the host's frames: Metal through metal-cpp on macOS, OpenCL where it exists, CUDA through the driver API where an NVIDIA driver is installed. A Vulkan device of OfxGrip's own accelerates CPU frames on machines no other path covers. The Vulkan loader and the CUDA driver are opened at run time. No Objective-C in the plug-in.
- **An inference seam.** A request of named inputs and parameters, a backend interface with a passthrough default, a per-frame result cache, a runner that keeps the engine off the render thread, an effect template, and an adapter over InferKit's C interface.
- **On-screen controls.** Points, lines, rectangles, circles, angle dials, polylines, boxes, and HUD text as parts over a parameters seam, drawn through the OpenFX draw suite or OpenGL, with Shift and Alt modifiers.
- **Licensing.** A store-neutral provider interface with `static`, `trial`, and signed-`file` providers built in, the license state in every snapshot, a status toggle and a licensing menu, and a watermark on unlicensed renders over CPU, Metal, OpenCL, and CUDA frames.
- **A headless mini host.** Loads real bundles, runs the real action order, tiles, varies render scale and bit depth, renders concurrently, hands out CPU, Metal, or OpenCL frames, drives the overlay interact, and imitates a named host's quirk profile. CI runs it with sanitizers and a coverage gate.
- **Probe and sample plug-ins.** `HostDump` records what a host really does; `Gain` shows an effect built on the framework; `Spot` shows a Slang kernel effect with an overlay and a trial license.

## Status

Phase 4 is in progress. Built: the Linux build (GCC, tested on x86_64 in CI and on aarch64), the Windows x64 build (MSVC, tested in CI), the Vulkan kernel backend (tested on MoltenVK and on Mesa's software driver), the CUDA kernel backend (tested against a fake driver; it has not yet run on an NVIDIA GPU), and the inference seam. Open: CUDA golden images, a Windows on Arm build, and live Windows and Linux hosts. See `Docs/Plan.md` for the phases and what each ports from FxGrip.

## Requirements

- CMake 3.24 or later
- A C++20 compiler: Xcode 15 or later on macOS, Clang 16 or GCC 12 on Linux, Visual Studio 2022 on Windows
- The OpenFX 1.5.1 headers are vendored under `external/openfx/`. No other SDK is required.
- Optional: the Vulkan headers (Homebrew `vulkan-headers`, `libvulkan-dev`, or the Vulkan SDK) build the Vulkan backend; OpenCL and OpenGL development packages build the OpenCL backend and the OpenGL overlay fallback on Linux and Windows. The CUDA backend needs no toolkit to build.

## Building

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

`cmake --list-presets` shows the sanitizer, coverage, x86_64, and release presets. Add
`-DOFXGRIP_BUILD_RUST=ON` to build the Rust crates and the Rust sample bundle with cargo.

## Writing an effect in Rust

```rust
use ofxgrip::{bundle, Effect, Manifest, RenderContext, Result, Services};

struct Invert;
impl Effect for Invert {
    fn render(&mut self, ctx: &mut RenderContext) -> Result<()> {
        let out = ctx.output();
        let src = ctx.source().filter(|i| i.connected);
        let (x1, y1, x2, y2) = ctx.args().window;
        for y in y1..y2 { for x in x1..x2 { for c in 0..out.components().count() {
            let v = src.as_ref().map(|i| i.image.read(x, y, c)).unwrap_or(0.0);
            out.write(x, y, c, if c == 3 { v } else { 1.0 - v });
        }}}
        Ok(())
    }
}

fn register() {
    let manifest = Manifest::new("com.example.invert", "Invert");
    let _ = ofxgrip::register(manifest, |_: &mut Services| Invert);
}
bundle!(register);
```

## Testing in DaVinci Resolve

```bash
scripts/install-bundle.sh build/debug/plugins/Gain/OfxGripGain.ofx.bundle
```

The script copies the bundle into `/Library/OFX/Plugins`. Restart Resolve and look for the effect under the OpenFX library's "OfxGrip" group.

## Documentation

- `Docs/Plan.md`: phases, the FxGrip-to-OfxGrip mapping, open items.
- `Docs/Architecture.md`: the layering and the main interfaces.
- `Docs/Testing.md`: the test layers and the mini host.
- `AGENTS.md`: the coding standard and the Full Check.

## License

OfxGrip is available under the MIT License. See [LICENSE](LICENSE). The OpenFX headers under `external/openfx/` are BSD-3-Clause.

OpenFX is a trademark of the OpenFX Association. DaVinci Resolve is a trademark of Blackmagic Design. OfxGrip is not affiliated with either.

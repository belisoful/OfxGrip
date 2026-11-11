# Testing OfxGrip

Date: 2026-10-15.

Four layers, as `Research/Comparison.md` §3.16 and D-004 lay out.

| Layer | Where | What it covers |
| --- | --- | --- |
| Host-free unit tests | `tests/core/` | Every `core/` header: types, errors, values, the byte codec, the plist codec, flags, manifest and JSON (target presets, rules, presets, About), the event bus, extensions, the host profile and quirk table, the snapshot codec, images and bands, the meta manager, the rule language, the parameter model, presets and the presets control, frame data and analysis, I18N, the menus, the instance tracker, the regression checks, the kernel contract (layout JSON, metadata block, tile maps, value packing, the catalog's resolution), the overlay parts and composites over a recording draw kit, the digests against the published vectors, the watermark's glyphs, placement, and composite, licensing (statuses, entitlements, the registry, the built-in providers, the extension's status, snapshot, menu, version gate, and update window), and the framework extensions over `FakeServices.hpp` |
| C contract tests | `tests/c/` | A manifest built through the C API and an effect whose behavior is a C callback table, driven through the mini host |
| Rust tests | `rust/` (`cargo test -p ofxgrip`, run by ctest as `rust.cargo_test` when `OFXGRIP_BUILD_RUST` is on) | The safe crate: manifest building and serialization, registration, enum conversions |
| GPU backend tests | `tests/gpu/` | The Metal backend on the system default device: compile, cache, dispatch, blit copy and fill, the scratch pool, error paths (macOS). The kernel engine (`ofxgrip_kernel_tests`): the CPU backend over the embedded fixture kernels, every binding mode of `GripInspect`, and the Metal backend compared against the CPU backend per depth and mode (the conformance suite). The Vulkan backend and the CUDA backend in the same binary: the Vulkan device, jobs, and conformance against the CPU backend; the CUDA backend against a fake driver. The OpenCL backend (`ofxgrip_opencl_tests`): programs, buffers, fills, dispatch. Each is skipped without a device |
| Inference tests | `tests/core/InferenceTests.cpp`, `tests/ofx/InferenceEffectTests.cpp`, `tests/inference/` | The seam (passthrough, cache, runner, conversion), the effect template through the mini host, and the InferKit adapter against a stand-in library |
| Adapter contract tests | `tests/ofx/` | Action decoding, property sets, descriptor lowering, and the whole adapter driven in process: a test effect registered with the `PluginRegistry` and run through the mini host (lifecycle events, parameter service and phases, persistence, snapshots, tiled and threaded render, identity, the Resolve profile's quirk paths, the overlay interact through the draw suite and the OpenGL fallback, licensing through the framework parameters and the watermark on CPU, Metal, and OpenCL frames) |
| Artifact and bundle tests | `tests/integration/`, `scripts/validate-bundle.py` | The built `OfxGripGain`, `OfxGripHostDump`, and `OfxGripSpot` bundles loaded with `dlopen`, described, and rendered across depths, tiles, threads, and profiles, on CPU frames and on Metal and OpenCL frames (compared per pixel); the Rust `OfxGripGainRs` bundle when `OFXGRIP_BUILD_RUST` is on; bundle layout, `Info.plist`, exports, architectures |
| Live probes | `plugins/HostDump`, `scripts/install-bundle.sh` | Run in DaVinci Resolve; results go into `Research/Host Quirks Registry.md` with host version, OS, and date |

## The mini host

`ofxgrip_minihost` implements the property, parameter, image-effect, memory, multithread, message
(V1 and V2), progress (V1 and V2), timeline, interact, and draw suites. `HostOptions` selects what the host
description says, which suites are withheld, and which deviations it imitates:

| Option | Registry entry |
| --- | --- |
| `clipsAlwaysConnected` | Q-RESOLVE-002 |
| `threadSuiteDummy` | Q-RESOLVE-003 |
| `paramCopyNoOp` | Q-RESOLVE-006 |
| `messageV2Broken` | Q-RESOLVE-007 |
| `imageScaleAlwaysOne` | Q-RESOLVE-010 |
| `nonConformantInstanceType`, `noPluginHandleOnInstance`, `withheldHostProps` | Q-RESOLVE-014 |
| `renderDespiteIdentity` | Q-RESOLVE-027 |
| `withheldSuites` | Q-OFX-ANY-002, Q-OFX-ANY-013 |

Presets: `HostOptions::spec()`, `resolve21()`, `nuke()`, `natron()`. The Resolve preset carries
what the registry reports today; the live probes refine it.

A render through the mini host runs clip preferences, region of definition, is-identity, begin
sequence render, one render action per tile on the requested number of threads, and end sequence
render. Tiles really tile: the output image handed to each render covers only that tile.

With `RenderRequest::gpu = GpuApi::Metal` on macOS, the mini host uploads the inputs to
`MTLBuffer`s, allocates the output buffer, passes its own command queue in the render arguments,
and reads the output back after the queue drains, as `ofxGPURender.h` describes. `GpuApi::OpenCL`
does the same with `cl_mem` buffers and a `cl_command_queue` on the system default device. GPU
frames cover the whole window and the render window selects the tile. The request falls back to
CPU frames when the host options or the plug-in descriptor lack the API, or no device exists;
`RenderResult::gpu` says which happened.

When the descriptor declares an overlay interact, `createInstance` creates the interact instance
beside the effect. `Instance::overlayDraw`, `overlayPenDown`, `overlayPenMotion`, `overlayPenUp`,
`overlayKeyDown`, `overlayKeyUp`, and the focus calls dispatch the interact actions; the draw
suite records every call (`drawCalls()`), and `HostOptions::drawSuite = false` (the Nuke and
Natron presets) withholds it so the plug-in takes the OpenGL path.

Tests run with `OFXGRIP_LOG_DIR`, `OFXGRIP_SUPPORT_DIR`, and `OFXGRIP_CACHE_DIR` pointing into the
build directory, so user presets and analysis spill files never touch the real folders.

`MiniHost::createInstance` takes a `restore` callback that runs over the instantiated parameters
before the create action, so a test can imitate a host restoring a saved document (the hidden
state parameter, renamed parameters) and exercise migrations.

```bash
build/debug/minihost/ofxgrip-minihost --bundle build/debug/plugins/Gain/OfxGripGain.ofx.bundle \
    --profile resolve21 --width 128 --height 64 --tile 32 --threads 4 --set gain=2 --out /tmp/gain.ppm --dump
```

## Sanitizers and coverage

The `asan-ubsan` and `tsan` presets build everything with the sanitizers on and run the same
tests; the tiled multi-threaded renders are the concurrency stress for ThreadSanitizer. The
`coverage` preset instruments with clang's source-based coverage; `scripts/coverage.sh` merges
the profiles and fails below `OFXGRIP_COVERAGE_MIN`.

## Linux

The Linux build runs in CI on `ubuntu-24.04` and locally in a Lima virtual machine
(`limactl start --name=ofxgrip template://ubuntu-24.04`), which mounts the checkout read-only.
The build tree lives inside the machine:

```bash
limactl shell ofxgrip -- bash -lc 'cmake -S <checkout> -B ~/b/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build ~/b/debug && cd ~/b/debug && ctest'
```

With `libvulkan-dev`, `mesa-vulkan-drivers`, `ocl-icd-opencl-dev`, `pocl-opencl-icd`, and
`libgl-dev` installed, the Vulkan tests run on Mesa's software driver and the OpenCL tests on
pocl's CPU device, so neither needs a GPU. macOS takes a GPU OpenCL device or none: Apple's CPU
device, which an x86_64 process sees under Rosetta, refuses a kernel with a struct argument.

The Windows build runs only in CI (`windows-2025`, MSVC). No Windows machine is available
locally, so a push is the first time MSVC compiles a change.

## Backends without their hardware

- **Vulkan.** `VulkanBackendTests` and the Vulkan cases of `KernelEngineTests` render the fixture
  kernels on whatever device opens (MoltenVK on the development Mac, Mesa's software driver on
  Linux) and compare against the CPU backend in every depth. They skip where no device opens.
  The Spot bundle renders through the accelerator when `OFXGRIP_KERNEL_ACCELERATOR=vulkan`.
- **CUDA.** `CudaBackendTests` drives the backend through a fake driver table that keeps device
  memory, arrays, and texture and surface objects on the host and runs a hook in place of the
  kernel. The tests prove what the backend binds, launches, stores, and releases, on every
  failure too. They do not prove the pixels a real kernel writes: that needs PTX from `nvcc`
  and an NVIDIA GPU.
- **InferKit.** `InferKitBackendTests` loads a stand-in library that exports InferKit's C
  interface (`tests/fixtures/inferkit`), so the request, every image form of a result, and
  each failure are tested without the engine.

## Live runs

1. Build, then `scripts/install-bundle.sh build/debug/plugins/HostDump/OfxGripHostDump.ofx.bundle`.
2. Start Resolve, add "OfxGrip Host Dump" to a clip on the Edit page, press "Dump instance now",
   scrub, then repeat on the Color and Fusion pages.
3. Read `~/Library/Logs/OfxGrip/com.belisoful.ofxgrip.hostdump.log` and record the results in the
   registry (`Verified on: 21.1.1 free, macOS <version>, <date>`).

# OfxGrip Plan

Date: 2026-10-14. Status: phase 0 built and verified (131 tests under debug, ASan+UBSan, TSan, and the x86_64 slice; 84% product line coverage; both bundles load in DaVinci Resolve 21.1.1 free). Nothing is committed yet.

OfxGrip is the OpenFX counterpart of FxGrip: a C++20 framework for OpenFX image-effect plug-ins
that gives Resolve, Fusion, Nuke, and Natron plug-ins what FxGrip gives Final Cut Pro and Motion
plug-ins. It ships on macOS, Windows, and Linux. DaVinci Resolve 21.1.1 free is the live test host.

This plan applies the accepted EveryGrip decisions (`Research/Decisions.md`, D-001 to D-007), the
design synthesis (`Research/Comparison.md`), the host libraries plan (`FxGrip/Local/Cross-Host
Analysis/06 Host Libraries Plan.md`), and the host quirks registry (`Research/Host Quirks
Registry.md`). Where this plan departs from an earlier decision, the departure is listed in
section 1 and proposed in `Research/Decisions.md`.

## 1. Decisions this plan rests on

| # | Decision | Source |
| --- | --- | --- |
| 1 | OfxGrip 1.x is self-contained C++20 in its own repository, with no shared-library dependency. GripTruck is extracted later from the working host libraries, and OpenGrip 1.0 targets the 2.x host libraries. | Host Libraries Plan, decisions 2, 4, 14 |
| 2 | **OfxGrip is built before AEGrip.** Resolve free is installed; the Adobe hosts need a subscription. This supersedes the Host Libraries Plan's build order (decision 16, AEGrip first). AEGrip is developed as far as the SDK allows while the subscription waits. | D-008, accepted 2026-10-14 |
| 3 | The author API is C++. A C contract (`ofxgrip_c.h`) over the same objects and a Rust crate over the C contract follow, so Rust effects and Rust libraries (InferKit's planned C API) can take part. | Host Libraries Plan, decision 15; D-008, accepted 2026-10-14 |
| 4 | One GPU engine with Slang kernels plus native backends (Metal first on macOS; CUDA, OpenCL, and Vulkan on Windows and Linux; a CPU backend everywhere). The effect declares to the host only the GPU paths where every kernel resolves. | Host Libraries Plan, decisions 11, 13; Slang confirmed 2026-10-14 |
| 5 | Static linking, hidden visibility, a versioned inline namespace, no Objective-C classes, Metal through metal-cpp. Resolve loads plug-ins in process. | Host Libraries Plan, section 5 |
| 6 | Every render reads one immutable render snapshot built at render start from timed parameter reads. Render paths are thread-safe. | D-002, D-003 |
| 7 | A headless mini OFX host runs the real action order, really tiles, varies render scale and bit depth, renders concurrently, and can imitate a named host's quirk profile. CI runs it on every platform with sanitizers and a coverage gate. | D-004 |
| 8 | Host behavior is asked for by quirk key, never by host name. The registry entries compile into a `HostProfile` table; probed capabilities override the table. | D-005 |
| 9 | The reference repositories under `OfxGrip/local/` are concept-only. The official OpenFX 1.5.1 headers (BSD-3) are vendored under `external/openfx/`. Resolve's extension property names are declared in OfxGrip's own header. | D-001 |

## 2. What OfxGrip ports from FxGrip

The FxGrip profile (`Research/Analysis/FxGrip.md` §13) divides FxGrip into what is neutral in design and what is FxPlug-bound. OfxGrip ports the first group and writes OFX-native replacements for the second.

| FxGrip subsystem | OfxGrip counterpart | Phase |
| --- | --- | --- |
| Registration record (`FxGripTypes.h` keys, `FxGripPluginData`) | `EffectManifest`: identity, kind, version, deprecated majors, capabilities, clips, parameters, host inclusion. Authored in C++ or JSON. Lowered to OFX descriptors at describe time. | 0 |
| Parameter configuration vocabulary and `FxParameterType` | `ParameterSpec` with a numeric ID, a string key, a neutral type, host flags, app flags, tags, meta, options with stable keys, and visibility rules. | 0 |
| `FxGripParameterFlags` with app bits packed into the host word | `HostFlags` and `AppFlags` as two separate sets. Nothing is packed into a host property. | 0 |
| Extension bus (`NSPriorityNotificationCenter`, `FxGripExtension`) | `EventBus` with priorities, veto and override, and `Extension` observers keyed by `key()`. Lifecycle events cover the OFX action set; API-traffic events wrap every parameter read and write. | 0 |
| Host seam (`FxGripEffectHost`, `FxGripAPIAccessing`) | `HostServices` interfaces (identity, profile, parameters, edit scope, time, images, persistence, feedback, threading, logging) implemented by the OFX adapter and by the mini host. | 0 |
| Plugin state (`pluginState:` coder) | `RenderSnapshot`: every state parameter's value at the render time, the host render information, and the license state, with a versioned codec. Built at render start from timed reads. | 0 |
| Parameter object model (`FxGripParameterBase`, typed classes, groups, subscripts) | `Parameter` objects over the parameter service; `effect[id]` and `effect["key"]` lookups; groups with children. | 1 |
| `FxGripParameterData` mirror | Not needed on OFX for type, flags, or menus (OFX answers them). Kept only for app flags and the click selector. | 1 |
| Meta and tags (`FxGripMetaManager`, `MetaAPI_v1`, `TagsAPI_v1`) | `MetaManager` persisted in a hidden string parameter through `syncPrivateData`. Same record shape, same reverse index, same locking. | 1 |
| Target presets and declarative visibility | `visibleWhen` and `enabledWhen` rules plus target presets, applied in `instanceChanged` by setting `Secret` and `Enabled`. | 1 |
| Presets (`FxGripPreset`, `.fxpreset`) | `Preset` model and apply path in the fixed section order, `.fxpreset` XML plist read and write on every platform, user folder per OS. | 2 |
| Versioning (`checkVersion:`, upgrade hook) | Stored schema version in the hidden state parameter; a migration table (renames, value transforms); deprecated majors registered beside the current major. | 1 |
| Out-of-band access (`FxGripOOBParameterAccess`) | `EditScope` over `paramEditBegin`/`paramEditEnd`; writes allowed only in the actions OFX permits, checked per phase. | 1 |
| Analysis pass (`FxAnalyzer`, `FxGripFrameData`) | `AnalysisJob` driven from a push button in `instanceChanged`, fetching clip images with the progress suite; `FrameData` with disk spill in the per-OS cache directory. | 2 |
| Custom inspector controls (Section, Banner, Status, …) | Resolve has no inspector views (Q-RESOLVE-019). Each control lowers to standard parameters: Section → group, Banner/Status → label string, Switch → boolean, Presets → choice, Divider → label. The control vocabulary stays so one manifest serves every host. | 2 |
| On-screen controls (`FxGripOSCPart`) | Overlay interact with the OFX draw suite (1.5) and a GL 2.1 fallback; part model shared by geometry, hit test, and drag routing. | 3 |
| `FxGripMTLDeviceCache`, `FxGripImageBuffer` | `GpuImage` descriptor (API, device, queue, handle, layout) and a per-queue resource pool; CPU `ImageView` with pixel-format dispatch. | 0 (CPU), 1 (Metal) |
| Licensing (`FxGripLicensing`, provider protocol) | Provider interface and entitlement in the core; license state in the render snapshot; watermark on CPU and Metal. Providers per store are a separate decision. | 3 |
| Shader metadata (`FxGripShaderMetadata`, JSON5 in shader source) | Kernel metadata in Slang or MSL source emitting `ParameterSpec`s. | 3 |
| Inference seam (`FxGripInferenceBackend`) | InferKit through its planned C API, or a Wraz server over HTTP. Never on the render thread. | 4 |
| I18N, About and Debug menus, instance tracker, regression | Extensions over the event bus. Menus become choice parameters. | 2 |
| Registrars (`PROPlugInRegistering`) | Bounded plug-in table with one generated `mainEntry` trampoline per plug-in; `OfxGetNumberOfPlugins`/`OfxGetPlugin` from one macro. | 0 |
| FCPXML, workflow extensions, Space, RealityKit, StoreKit | Not applicable to OFX. The host scripting bridge (D-007) covers Resolve's Lua and Python later. | — |

## 3. Phases

Each phase ends with the Full Check green (section 6) and a live run in Resolve where it applies.

### Phase 0: walking skeleton (this session)

- Repository, CMake, presets, vendored OpenFX headers, CI workflow.
- Core: `Types`, `Error`, `Value`, `Flags`, `Manifest` (+ JSON), `Events` and `Extension`, `HostServices`, `HostProfile` with the quirk table, `RenderSnapshot` and codec, `Image` (CPU).
- OFX adapter: suites with pointer validation, typed property access, action decoding into a closed enum with typed arguments, descriptor lowering from the manifest (always both ranges, explicit `animates`, no Integer2D/3D on hosts that lack it), effect instance lifecycle, render path with timed reads, single catch boundary per entry point, plug-in table and bundle macro.
- Plug-ins: `HostDump` probe (host description, suites, extension-property acceptance, action log) and `OfxGripGain` (CPU filter built on the framework).
- Mini host: loads a bundle, implements the suites, runs the real action order, tiles, renders concurrently, injects quirk profiles. First tests.
- Bundle packaging on macOS (`X.ofx.bundle/Contents/MacOS/X.ofx`, `Info.plist`), an install script that copies (never links, Q-RESOLVE-025), a validator.
- Live: install both plug-ins in Resolve, record Q-RESOLVE-001 and the capability-dump entries in the registry. Done 2026-10-14 for the app-start scan (registry section 3.1); the per-page instance probes remain.

### Phase 1: parameter model, state, Metal

- Runtime `Parameter` objects, groups, lookups. Meta and tags with hidden-parameter persistence. Visibility rules and target presets. Versioning and migrations. `EditScope`.
- Metal backend through metal-cpp with the host's command queue; `GpuImage` for `MTLBuffer` frames; resource pool per queue; Fusion-page intermediate copy (Q-RESOLVE-031).
- Mini host variants: Metal frames on macOS; quirk profiles for Resolve, Nuke, Natron.
- Resolve behavior probes (registry section 5, step 2).

### Phase 2: presets, analysis, controls, extensions

- `.fxpreset` round trip shared with FxGrip's test vectors. Presets choice parameter. User folder per OS.
- `AnalysisJob` and `FrameData` with spill. Progress suite.
- Lowering of the custom-control vocabulary to standard parameters. I18N, About, Debug, instance tracker, regression extensions.
- C contract (`ofxgrip_c.h`) and the Rust crate (`ofxgrip-sys`, `ofxgrip`). Needs the Rust toolchain on the dev Mac and a decision on the crate's repository.

### Phase 3: overlays, GPU engine, licensing

- Overlay interact over the draw suite with GL fallback; OSC part model.
- Slang kernel pipeline (slangc at build time) with the resolution rules; CUDA, OpenCL, and Vulkan backends; conformance suite with golden images per backend.
- Licensing provider interface, entitlement, watermark.

### Phase 4: Windows and Linux

- CMake toolchains, MSVC and GCC builds, `Win64` and `Linux-x86-64` bundle folders, Authenticode and old-glibc builds, CI runners. Live hosts are an open question (no Windows or Linux machine is known).
- Inference through InferKit's C API or a Wraz server.

## 4. Layout

```
OfxGrip/
  CMakeLists.txt  CMakePresets.json  version.txt
  AGENTS.md (CLAUDE.md includes it)  README.md  CONTRIBUTING.md  SECURITY.md  LICENSE
  Docs/            Plan.md, Architecture.md, Testing.md
  cmake/           OfxGripPlugin.cmake (bundle layout, Info.plist), toolchain helpers
  external/openfx/ vendored OpenFX 1.5.1 C headers (BSD-3) and LICENSE.md
  include/ofxgrip/ public headers
    core/          host-neutral: Types, Error, Value, Flags, Manifest, Events, Extension,
                   HostServices, HostProfile, Quirks, RenderSnapshot, Image, Parameters,
                   MetaManager, Preset  (GripTruck candidates, kept free of OFX headers)
    ofx/           the OpenFX adapter: Suites, PropertySet, Actions, Descriptor, Effect,
                   Plugin, HostServices, ResolveExtensions
  src/             implementation, same split
  minihost/        the headless OFX host library and `ofxgrip-minihost` CLI
  plugins/         HostDump (probe), Gain (sample)
  tests/           GoogleTest: unit tests per header, mini-host integration tests, artifact tests
  scripts/         install-bundle.sh, validate-bundle.py, dump-registry.py
  .github/         CI workflows
  local/           reference clones (ignored)
```

`core/` never includes an OpenFX header. `ofx/` never contains effect logic. The author touches the
`Effect` base, the manifest, and the kernels.

## 5. Interfaces that shape the port

- **Manifest**: `EffectManifest { identity (ofxId, uuid, name, grouping, kind), version {major, minor}, deprecatedMajors, capabilities (tiles, multiResolution, renderScale, depths, gpu, cpuFallback, threadSafety, temporalAccess), clips, parameters, migrations }`. `ParameterSpec { id, key, type, label, hint, parent, defaults, range, displayRange, hostFlags, appFlags, options[{key,label}], tags, meta, visibleWhen, enabledWhen, hosts }`. Validation checks ID and key uniqueness, the reserved range 9991 to 9999, option-key stability, and host limits.
- **Events**: lifecycle `Load, Describe, DescribeInContext, CreateInstance, AddedToDocument, Properties, AddParameters, FinishSetup, BeginInstanceChanged, InstanceChanged, EndInstanceChanged, ParameterClicked, Flush, SyncPrivateData, BuildSnapshot, RegionOfDefinition, RegionsOfInterest, FramesNeeded, ClipPreferences, IsIdentity, BeginSequenceRender, Render, EndSequenceRender, PurgeCaches, DestroyInstance, Unload`; API traffic `ParameterAddPre, ParameterAdd, GetValue, SetValuePre, SetValue, GetLabel, SetLabelPre, SetLabel, GetFlags, SetFlagsPre, SetFlags, SetOptionsPre, SetOptions`. Payloads are mutable structs; an observer sets `error` to veto or `result` to override.
- **HostServices**: `Identity`, `HostProfile`, `ParameterService` (typed get and set at time, flags, label, options, keyframes, `canWrite(phase)`), `EditScope`, `TimeService`, `ImageService`, `PersistenceService` (keyed blobs), `FeedbackService` (message, persistent message, progress, abort), `ThreadingService`, `LogService`. Each service carries a phase-validity table checked in debug builds.
- **RenderSnapshot**: immutable; `values` by parameter ID, `time`, `frameRate`, `renderScale`, `field`, `draft`, `projectSize`, `pixelAspect`, `colour`, `license`. Codec: a versioned little-endian tag-length-value stream, the same bytes on every platform.
- **Quirks**: `enum class Quirk` with one case per registry key; `HostProfile::has(Quirk)` resolves name prefix and version range from a compiled table, then probed capabilities override.

## 6. Full Check

The Full Check must pass before a commit.

1. Configure and build with the `debug` preset (macOS arm64).
2. `ctest` in that build: unit tests, mini-host integration tests, artifact tests.
3. Build and test the `asan-ubsan` preset. Build and test the `tsan` preset (the concurrent-render stress tests run here).
4. Build the x86_64 slice (`-DCMAKE_OSX_ARCHITECTURES=x86_64`) and run its tests under Rosetta.
5. Coverage with the `coverage` preset at or above the gate in `CMakeLists.txt` (`OFXGRIP_COVERAGE_MIN`). The gate covers the product code (core, adapter, plug-ins); the mini host is reported separately. Phase 0 sets the gate at 80% (measured 84% on 2026-10-14); it rises to 90% as the tests grow, toward the developer's goal of complete coverage.
6. `scripts/validate-bundle.py` on every built bundle.

CI runs the same steps on `macos-26`, and the build-and-test subset on `ubuntu-24.04` and
`windows-2025` once phase 4 lands.

## 7. Open items for the developer

| Item | Needed |
| --- | --- |
| The Rust crate's repository (the toolchain is installed; the bridge is confirmed) | Phase 2 |
| metal-cpp: approved and vendored under `external/metal-cpp` | Done |
| Slang compiler (`slangc`) as a build dependency | Phase 3 |
| Windows and Linux machines with Resolve for live tests | Phase 4 |
| Licensing provider for Windows and Linux | Phase 3 |
| Natron as a second free OFX host | Phase 1 |

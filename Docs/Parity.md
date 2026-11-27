# OfxGrip Parity With FxGrip on macOS

Date: 2026-11-22, updated 2026-11-26. FxGrip `main` at `518e6d9`.

The goal is parity on macOS: every FxGrip subsystem that is not specific to Final Cut Pro has a
counterpart in OfxGrip on the Mac. This document lists each FxGrip subsystem, its OfxGrip
counterpart, and the work that remains. `Docs/Plan.md` section 2 holds the original port mapping.

The inventory compares public headers and their summaries. It does not audit behavior class by
class. Line counts are FxGrip source lines (headers and implementation).

## 1. Scope decisions

The developer set these on 2026-11-22.

| # | Decision |
| --- | --- |
| 1 | The Apple-framework subsystems that are not specific to Final Cut Pro are in scope: Space, StoreKit, the window, and the others listed in section 4. |
| 2 | Mac-only code is Objective-C. Swift is used where a framework publishes no Objective-C interface (RealityKit, StoreKit 2), and each Swift part has an Objective-C access point. |
| 3 | Inspector views live in a native window. OpenFX has no inspector views (Q-RESOLVE-019). |
| 4 | Space needs a plan of its own before work starts: the scene source, its parameters, and the split between platforms (section 6). |
| 5 | `FxGripShaderEffect` is not complete in FxGrip. Its port waits for the FxGrip side to settle. |

Decision 2 amends decision 5 of `Docs/Plan.md` ("no Objective-C classes"). The static libraries
(`core`, `ofx`, `gpu`, `inference`) stay free of Objective-C. The Mac code lives in one companion
module (section 5).

## 2. Status words

| Word | Meaning |
| --- | --- |
| Parity | The counterpart exists and is tested through the mini host. |
| Partial | The counterpart exists and lacks the listed parts. |
| Missing | No counterpart exists. The work is host-neutral C++. |
| Companion | No counterpart exists. The work belongs in the Mac companion module. |
| Replaced | OpenFX answers the need another way. No port is planned. |
| Excluded | The subsystem is specific to Final Cut Pro. |

## 3. Matrix

### 3.1 Registration, lifecycle, host seam

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| Registrars (`Static`, `Config`, `Class`, `Dynamic`), `FxGripPluginData` | Plug-in table, `mainEntry` trampolines, `EffectManifest` in C++ or JSON | Parity | — |
| `FxGripTileableEffect` and its categories | `Effect`, `EffectFactory` | Parity | — |
| `FxGripTileableGenerator` | `EffectKind::Generator` in the manifest and the descriptor | Partial | No sample generator. No generator bundle test. |
| Host seam (`FxGripEffectHost`, `FxGripPluginHost`) | `HostServices`, the C contract, the Rust crate | Parity | — |
| Extension bus (`FxGripExtension`, `FxGripExtensionSystem`), API-traffic events | `EventBus`, `Extension`, `ExtensionSystem` | Parity | — |
| Versioned API wrappers (`*API_v3` to `*API_v7`) | `Suites` with pointer validation and capability probes | Replaced | — |
| Plugin state coder | `RenderSnapshot` and its codec | Parity | — |
| Out-of-band access (`FxGripOOBParameterAccess`) | `EditScope`, `Effect::edit` | Parity | — |
| Versioning (`checkVersion:`, upgrade hook, `priorUuids`) | `VersioningExtension`, `Effect::onMigrate`, deprecated majors, `priorUuids` | Parity | — |
| Project properties (document ID, media folder, aspect ratio) | `TimeService` project size, offset, pixel aspect; `PathService` folders | Partial | No project document identity. No project media folder. |
| Timing (`FxGripTimingAPI_v4`, the `Timing` category, `FxGripTimecode`) | `TimeService`: current time, time bounds, frame rate. `Timecode`: exact rates, drop-frame counting, fields, text | Partial | `TimeService` reports no start timecode, no drop-frame flag, no input start and duration, and no retiming speed. |
| Temporal access (`scheduleInputs`, other-time image requests) | `temporalAccess` manifest flag | Partial | `Effect` has no frames-needed hook. A render receives its inputs at the render time only. |
| Errors (`FxGripErrors.h`) | `Error`, `ErrorCode` | Parity | — |

### 3.2 Parameters

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| Configuration vocabulary, type strings | `ParameterSpec`, `ParameterType` with the same numbers and strings | Parity | — |
| Flags (`FxGripParameterFlags`) | `HostFlags`, `AppFlags` | Parity | — |
| Object model, groups, subscripts | `Parameter`, `ParameterModel` | Parity | — |
| `FxGripParameterData` mirror (hidden 9998) | The manifest answers type, flags, parent, options, and selector | Replaced | — |
| Meta and tags (`FxGripMetaManager`, `MetaAPI_v1`, `TagsAPI_v1`) | `MetaManager`, `MetaExtension` | Parity | — |
| Target presets, declarative visibility | `MetaExtension`, `Rules`, `VisibilityExtension` | Parity | — |
| Info, Bounds, Grouping, Dynamic parameter APIs | `Parameter` accessors, `ParameterService` | Parity | — |
| Float, Int, Percent, Angle, Toggle, Menu, String, Point, RGB, RGBA, Group, PushButton, Help, Custom | Lowered to the OpenFX parameter types | Parity | — |
| FontMenu | Accepted by the manifest, omitted at describe | Missing | Needs an OpenFX form (a choice filled from the system fonts) and font access on each OS. |
| Gradient | Accepted, omitted | Missing | Needs a value type, an OpenFX form, and a kernel texture binding. |
| Histogram | Accepted, omitted | Missing | Needs an OpenFX form. A window view is the likely carrier. |
| ImageRef | A clip inside `KernelEffect` only | Partial | The manifest clip list covers other effects. No parameter-style access. |
| PathID, `FxGripPathData`, `FxGripPathGeometry` | `Path`: the vertex list, its edits, cubic segments, and a codec. The parameter type is accepted and omitted | Partial | Needs an OpenFX form and an overlay editor (section 3.5). |

### 3.3 Inspector controls

OpenFX has no inspector views. A control lowers to standard parameters today. Decision 3 adds a
native window that hosts the real view on macOS.

| FxGrip control | OfxGrip today | Status | Remaining |
| --- | --- | --- | --- |
| Section, Divider | Group; label string | Parity | — |
| Banner, Capsule, Status, Progress | Label string or disabled standard parameter | Parity | — |
| Switch | Boolean | Parity | — |
| Presets | Choice with Default, user, plug-in, Reveal, Save | Parity | — |
| Analyzer | Push button that starts the analysis pass | Parity | — |
| TrackingOpacity | Double | Partial | Nothing drives it to zero during analysis. |
| Random | Integer | Partial | No reroll control. |
| Curve, CurveSet (`FxGripCurveData`, `FxGripCurveLUT`, `FxGripCurveSetData`, editor views) | `Curve`: the curve value, the monotone cubic tables, and the keyed set with its keyframe blend | Partial, Companion | Needs an OpenFX form and a kernel binding for the table. The editor view belongs in the window. |
| LiveImage | Accepted, omitted | Companion | Render publishes frames in memory. OpenFX renders in the host process, so the same route works. |
| WebView, VideoView, `FxGripURLWhitelist` | `UrlWhitelist`. The parameter types are accepted and omitted | Partial, Companion | The views belong in the window. |
| ObjectTracker parameter, data, and view | Accepted, omitted | Missing, Companion | See tracking in section 3.8. |
| `FxGripWindow` extension | None | Companion | A window the plug-in opens from a button. Needs a live probe in Resolve. |

### 3.4 Custom values

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| `FxGripFrameData` | `FrameData` with disk spill | Parity | — |
| `FxGripDictionary` | `ValueDictionary`: scalars, arrays, nested maps, typed access on the same well-known keys, a lock, a codec | Parity | — |
| `FxGripInterpolatingDictionary` | `ValueDictionary::interpolated` with exempt paths and a custom hook | Partial | The adapter does not handle the OpenFX custom-parameter interpolation action, so a host does not call the blend yet. |
| `FxGripMutableParameter`, custom view data delegates | None | Missing | The typed parameter reads and writes do not route into a custom value's dictionary. |

### 3.5 On-screen controls

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| `FxGripOnScreenControl`, hit testing, drag routing | `OverlayControl`, `OverlayInteract` | Parity | — |
| Metal drawing (`FxGripOSC.metal`) | OpenFX draw suite, OpenGL 2.1 fallback | Replaced | — |
| Point, line, rect, rect corner, box, box corner, circle, radius handle, angle dial, rotation handle, polyline, HUD parts | The same twelve parts | Parity | — |
| Curve part, rect rotation handle part | None | Missing | — |
| Editable path part (`FxGripOSCPathPart`, 957 lines) | None | Missing | Follows the path value. |
| Rich point parts, `FxGripPointOSC` (705 lines), `FxGripPointOptions` | None | Missing | — |
| `FxGripObjectTrackerOSC` | None | Missing | Follows tracking. |
| `FxGripEventModifiers` | `OverlayModifiers` with four keys | Partial | The Command-delete and Control-menu conventions are not applied. |

### 3.6 Rendering, GPU, color

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| `FxGripRect`, tile geometry | `RectI`, `RectD`, `KernelTile` | Parity | — |
| `FxGripMTLDeviceCache` | `MetalBackend` pipeline cache and scratch pool | Parity | — |
| Kernel path (`FxGripKernelEffect`, `FxGripKernelPipeline`, `FxGripKernelLayout`) | `KernelEffect`, `KernelEngine`, `KernelLayout`, `KernelCatalog` | Parity | OfxGrip adds the CPU, OpenCL, Vulkan, and CUDA backends. |
| `FxGripShaderMetadata` | `KernelMetadata` | Parity | The `colorSpace` block also accepts every `ColorGamut` transfer and gamut name. |
| Primaries conversion (`FxGripShaderConverter`) | `KernelColorPass`, `KernelColor`, `GripColorConvert.slang` on the CPU, Metal, and CUDA backends | Parity | The CUDA build needs its PTX from the NVIDIA toolchain. |
| Fragment shader path (`FxGripShaderEffect`, `FxGripShaderPipeline`, 2,151 lines) | None | Missing | Waits for FxGrip (decision 5). |
| `FxGripColorGamut`, the `ColorGamut` category | `ColorGamut`: five gamuts, nine transfer functions, conversion matrices with Bradford adaptation, and the OpenFX colourspace names. `capabilities.colorManagement` fills `RenderInfo::colourspace` from the host or the Color Space menu, and `RenderSnapshot::colorSpace` resolves it | Partial | No linear or gamma color parameters. |
| `FxGripImageBuffer`, `FxGripImageCompression` (1,350 lines) | `ImageView`, `GpuImage` | Missing | The compressed in-memory pixel container. |
| `FxGripWatermark` | `Watermark` on CPU, Metal, OpenCL, and CUDA frames | Parity | — |
| `FxGripTextImage` | A 5×7 glyph font inside the watermark | Partial, Companion | Real font text needs CoreText on macOS. |
| `FxMatrix44` helpers | None | Missing | Arrives with Space. |

### 3.7 Extensions and state

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| Presets, `.fxpreset`, `FxGripPresetsAPI_v1` | `Preset`, `PresetManager`, `Plist` | Partial | A capture holds values at one time. Keyframes are not captured. User folders are not watched for changes. |
| Analysis pass (`FxGripAnalysis`) | `AnalysisExtension`, `runAnalysis`, `Effect::onAnalyzeFrame` | Parity | — |
| `FxGripI18N` | `I18n` | Parity | — |
| `FxGripAboutMenu`, `FxGripDebugMenu` | `About`, `Debug` | Parity | — |
| `FxGripInstanceTracker` | `InstanceTracker` | Parity | — |
| `FxGripRegression` | `Regression` | Parity | — |
| `FxGripGoogleAnalytics` (446 lines) | None | Missing | The capture rules and idle latch are host-neutral. The transport needs a decision. |
| Licensing extension, provider protocol, entitlement | `Licensing`, `LicensingProviders` (`static`, `trial`, `file`) | Parity | — |
| `FxGripFxFactoryProvider` (694 lines) | None | Companion | Depends on the store decision (D-010) and on FxFactory's support for OpenFX products. |
| `FxGripStoreKit` (864 lines, Swift) | None | Companion | Assumes an app wrapper and an App Group. An OpenFX bundle has neither. Needs a design. |

### 3.8 Inference, tracking, Space

| FxGrip subsystem | OfxGrip counterpart | Status | Remaining |
| --- | --- | --- | --- |
| Inference seam (backend, request, result, passthrough) | `Inference.hpp` | Parity | — |
| `FxGripInferenceBridge` | `InferKitBackend` | Parity | — |
| `FxGripMLCache` (document tier) | `InferenceCache` (memory), `InferenceDiskCache` (disk) | Replaced | — |
| `FxGripMLImageEffect` | `InferenceEffect` | Parity | — |
| `FxGripMLImageGenerator`, `FxGripMLVideoEffect`, `FxGripMLVideoGenerator` | None | Missing | The video templates need temporal access (section 3.1). |
| `FxGripObjectTracker` (Vision) | None | Companion | Runs inside the analysis pass, which exists. |
| Space physics: FMM, particle variation, simulation store, `FxGripSpaceMotion` (pure math) | None | Missing | Host-neutral C++. Section 6. |
| `FxGripPhysicsBake` | None | Missing | Persists through `FrameData` and the state parameter. Section 6. |
| `FxGripSpaceEffect`, SceneKit engine (6,684 lines with the physics) | None | Companion | Section 6. |
| `FxGripRealityKit` (3,001 lines, Swift) | None | Companion | Section 6. |

### 3.9 Final Cut Pro integration

| FxGrip subsystem | Lines | Status | Note |
| --- | --- | --- | --- |
| FCPXML document model, `FxGripFinalCutPro`, pasteboard | 6,173 | Excluded | Section 7. |
| `FxGripFCPXMLAPI_v1` capture and apply | — | Missing (function) | The function is host-neutral. Section 7. |
| `FxGripWorkflowMailbox`, `FxGripWorkflowRelay` | 1,382 | Missing (function) | The channel is host-neutral. Section 7. |
| `FxGripWorkflow` framework, `FxGripFinalCutHost` | 1,558 | Excluded | The Resolve counterpart is the scripting bridge (D-007). |

## 4. Totals

| Group | FxGrip lines (about) |
| --- | --- |
| Parity, Replaced, or Partial | 53,000 |
| Missing, host-neutral C++ | 13,000 |
| Companion (Mac only) | 13,000 |
| Excluded | 8,000 |

FxGrip is about 87,000 source lines with its three companion frameworks. OfxGrip is about 32,000
lines of product code. The three lower rows are summed from the files named in section 3. The
first row is the remainder. The 2026-11-26 ports (color gamut, timecode, curves, path, URL
whitelist, dictionary) moved about 4,000 lines from the second row to the first.

## 5. The Mac companion module

The companion holds every part that needs an Apple framework: the window and its views, Space with
SceneKit and RealityKit, Vision tracking, CoreText, StoreKit, and the FxFactory provider.

Open design points:

- **Class names in a shared process.** Resolve loads every OpenFX bundle into one process. The
  Objective-C runtime holds one class per name for the whole process. Two bundles that each carry
  the companion's classes collide. FxPlug gives each plug-in its own process, so FxGrip never met
  this. Candidates: a class-name prefix generated per bundle and version, or one shared framework
  that every bundle loads. The same holds for Swift module names.
- **The window on the host's main thread.** The plug-in opens an AppKit window inside the host
  process. Resolve and Natron need a live probe: which actions arrive on the main thread, whether a
  panel stays above the host window, and how it closes when the instance is destroyed.
- **The seam.** The C++ side reaches the companion through an interface in `core/`. Windows and
  Linux build with a null implementation until they get a native one.
- **Swift.** RealityKit and StoreKit 2 parts are Swift behind an Objective-C access point, as
  FxGrip does with `FxGripStoreKitRegistration.m`.

## 6. Space

Space needs a plan before work starts. The plan answers:

- **Scene source.** FxPlug supplies the camera and lights (`Fx3DAPI_v5`, `FxLightingAPI_v3`).
  OpenFX has no standard for either. The source on an OpenFX host is a parameter set (camera
  position, target, field of view, lights), or a host-specific route where one exists.
- **Snapshot.** The captured scene joins the render snapshot, with the samples at t−dt, t, and
  t+dt that `FxGripSpaceMotion` uses.
- **Platform split.** The physics (FMM, particle variation, simulation store, motion) is pure math
  and ports to C++ for every platform. The SceneKit and RealityKit engines are Mac only. The
  renderer on Windows and Linux is undecided.
- **Determinism.** A bake made on one platform must replay on another, or the plan states that it
  does not.
- **Persistence.** The physics bake uses `FrameData` and the state parameter.

## 7. Final Cut Pro integration on an OpenFX host

The FxGrip subsystems do three things.

| Function | FxGrip | OpenFX counterpart |
| --- | --- | --- |
| Capture the effect's state as a document and apply one, with keyframes | `FxGripFCPXMLAPI_v1` | Host-neutral. Extend `Preset` with keyframes. |
| A channel between a companion process and the live instances, with a link status | `FxGripWorkflowMailbox`, `FxGripWorkflowRelay` | Host-neutral. OpenFX plug-ins are not sandboxed, so the channel is files in the user support folder keyed by instance. |
| Reach the host: library, timeline, playhead | `FxGripFinalCutHost`, `FxGripFinalCutPro`, pasteboard | Host-specific. On Resolve this is the scripting bridge (D-007). |

Recommendation: do not port the FCPXML document model. Final Cut Pro is its only reader, and it
names an effect by its FxPlug UUID. Build the first two functions as small host-neutral pieces and
the third as the Resolve scripting bridge. The channel is the handoff D-007 already calls for.

## 8. Verification on the Mac

- Resolve 21.1.1: the app-start scan is done (2026-10-14). The 0.5.0 bundles ran on the Edit and
  Fusion pages on 2026-11-26: HostDump and Spot render on Metal through Resolve's queue, Spot
  shows its trial watermark, an Inspector edit re-renders, and the overlay draws on the Fusion
  page. The run found Q-RESOLVE-039 (a hang, fixed) and that Resolve states the page on the
  instance. The same day the 0.5.0 Spot converted around its kernel on the Fusion page with
  its Color Space menu set to Rec. 709, on Metal through Resolve's queue. Resolve names an
  Edit-page clip `Raw` in both colour science modes (Q-RESOLVE-028). Open: a node-level effect
  on the Color page, the Presets control, the analysis pass, label strings, an overlay drag,
  a licensed render, and preferred colourspaces in clip preferences.
- Tests: OfxGrip has 400. FxGrip has about 3,800.
- Documentation: OfxGrip has four documents. FxGrip has 63 DocC articles.

## 9. Order

1. Run the live Resolve probes on the Mac for what exists. Done for the Edit and Fusion pages;
   section 8 lists what is open.
2. Port the host-neutral C++ parts. Done: color gamut math, timecode, curve data, path data,
   dictionary values with interpolation, the URL whitelist, kernel color conversion. Open: the
   timing fields of `TimeService`, temporal access, the image buffer, the missing overlay parts,
   keyframed capture, the instance channel, the custom-parameter interpolation action.
3. Give each omitted parameter type an OpenFX form.
4. Add the generator sample and the remaining inference templates.
5. Settle the companion's open design points, then build the window and its views.
6. Write the Space plan.
7. Port the fragment shader path when FxGrip's is complete.

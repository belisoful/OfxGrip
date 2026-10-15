# OfxGrip Architecture

Date: 2026-10-15. Phase 3.

## Layers

```
 Plug-in author (C++, or Rust through the C contract)
   manifest (EffectManifest, C++, JSON, or the C/Rust builders) · Effect subclass or callback
   table · Slang kernels (GripKernel module; slangc outputs checked in) or hand-written MSL and OpenCL
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip_c (include/ofxgrip/c) · rust/ofxgrip-sys · rust/ofxgrip
   the C contract over the adapter: manifest building, one callback table per effect, the render
   context, the snapshot, the services, the bundle exports; the Rust crates wrap it
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip (the OpenFX adapter, include/ofxgrip/ofx)
   Plugin     bounded registry, one slot per major (current and deprecated), per-slot
              setHost/mainEntry trampolines, OFXGRIP_BUNDLE exports
   PluginSlot action dispatch with one catch boundary, host profile at load, lifecycle events
   Descriptor manifest → OFX descriptor, clips, parameters (registry handling rules applied)
   Effect     the author base: hooks, parameter objects, framework extensions, snapshot
              building, RenderContext, EditScope, presets, analysis, createOverlay
   KernelEffect  an effect whose render dispatches a resolved kernel through the engine
   Interact   OverlayInteract over the draw suite or OpenGL; the interact trampolines
   RenderWatermark  the licensing watermark after render, on CPU, Metal, and OpenCL frames
   Analysis   the analysis pass over clipGetImage inside a parameter action
   OfxHostServices  HostServices over the suites: parameters, persistence, feedback, threads,
                    time, paths, log
   Actions · PropertySet · Suites · ResolveExtensions
 ──────────────────────────────── HostServices (abstract) ──────────────────────────────────────
 ofxgrip_kernels · ofxgrip_gpu_metal (macOS) · ofxgrip_gpu_opencl (include/ofxgrip/gpu)
   KernelEngine  the backends by API; a dispatch carries the layout, the plan, the packed
                 uniforms and constants, the window, and the images
   CpuKernelBackend  the Slang C++ prelude: ITexture views over CPU frames, bands on the
                 threading service
   MetalBackend  a context per host command queue: pipeline cache, scratch pool, command
                 buffers over MTLBuffer frames and buffer-backed textures (metal-cpp)
   MetalKernelBackend  the engine's Metal path: function constants, textures, samplers
   OpenCLBackend  a context per cl_command_queue: program cache, buffers, blocking fills
   MetalWatermark · OpenCLWatermark  the blend kernels behind RenderWatermark
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip_core (host-neutral, include/ofxgrip/core; no OpenFX include)
   Types · Error · Value · Codec · Plist · Flags · Manifest (+ JSON, target presets, presets
   table, About menu) · Rules · Events (bus) · Extension (system) · Parameters · MetaManager
   Preset (.fxpreset) · Presets (manager, control) · FrameData · I18n · About · Debug
   InstanceTracker · Regression · the framework extensions (Meta, Visibility, Versioning,
   Analysis, Presets, About, Debug, I18n, InstanceTracker, Regression, Licensing)
   KernelLayout · KernelMetadata · KernelTile · KernelValues · KernelCatalog (the D-009 contract)
   Overlay (parts, composites, the parameters seam, draw kits)
   Licensing (statuses, entitlement, provider, registry, extension) · LicensingProviders
   (static, trial, file) · Watermark · Digest (SHA-256, HMAC)
   HostProfile + Quirks (registry table) · RenderSnapshot (+ codec) · Image (CPU, GpuImage)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 Hosts: DaVinci Resolve (live), the mini host (CI, CPU, Metal, and OpenCL frames; the interact
        and draw suites), Nuke and Natron (profiles only so far)
```

`ofxgrip_core` is the GripTruck candidate: it is what AEGrip will share once the extraction in
the host libraries plan happens. `ofxgrip` is the OpenFX adapter and contains no effect logic.

## Lifecycle

| OpenFX action | What the adapter does | Events |
| --- | --- | --- |
| `setHost`, Load | Fetch and validate suites, build the `HostProfile`, log it, `EffectFactory::onLoad` | — |
| Describe | Validate the manifest, lower the effect-level properties | — |
| DescribeInContext | Lower clips and parameters for the context, append the hidden state parameter | — |
| CreateInstance | Create the `Effect`, bind parameters and clips, load persistence, build the parameter model, load the framework extensions (versioning, meta, visibility), load the author's extensions, `onCreate`, flush | CreateInstance (versioning checks and migrates at −19, meta loads at −18, rules apply at −17), FinishSetup, AddedToDocument, Flush |
| InstanceChanged | Map the name to an ID (unknown names ignored), `onInstanceChanged`, button clicks, flush | BeginInstanceChanged, InstanceChanged (target presets at −10, rules at 0), ParameterClicked, Flush (meta saves at −14), EndInstanceChanged |
| SyncPrivateData | Re-apply the rules, commit the persistence blobs into the hidden state parameter | SyncPrivateData |
| RoD, RoI, IsIdentity, ClipPreferences | Build a snapshot at the action time, call the hook, answer or reply default | — |
| Begin/EndSequenceRender | Hooks | BeginSequenceRender, EndSequenceRender |
| Render | Build the snapshot (timed reads + `onBuildSnapshot` + `BuildSnapshot`), fetch output and inputs (RAII; `MTLBuffer` handles when the host enabled Metal), validate depth, `render(RenderContext&)` | BuildSnapshot, Render |
| DestroyInstance | Unload extensions, delete the effect | DestroyInstance (reverse order) |
| Anything else | `kOfxStatReplyDefault` | — |

## Rules the code enforces

- Render reads only the `RenderSnapshot`, the images handed to it, and read-only services.
- Render scale and field come from the action arguments, never from an image (Q-OFX-ANY-016).
- Every parameter read and write goes through `ParameterService`, so the `Api…` events fire.
- Parameter writes outside `CreateInstance`, `InstanceChanged`, `SyncPrivateData`, an interact, or
  an `EditScope` fail with `WrongPhase`.
- Both the hard range and the display range are written for every numeric parameter
  (Q-RESOLVE-004), and `animates` is written for every parameter (Q-RESOLVE-015).
- Message suite V2 is used only when all three pointers are non-null (Q-RESOLVE-007).
- The thread suite is replaced by the plug-in's own pool when the profile says the host's is a
  stub or missing (Q-RESOLVE-003, Q-OFX-ANY-013).
- A connected clip that returns no image is treated as unconnected (Q-RESOLVE-002).
- Host behavior is asked for through `HostProfile::has(Quirk)`, never a host name.
- A Metal render encodes on the host's queue and commits without waiting (`ofxGPURender.h`); on
  the Fusion page the source is copied to a scratch buffer first (Q-RESOLVE-031). The backend's
  process-wide cache holds only immutable pipelines and reusable scratch buffers under a mutex.
- Where the secret flag is permanent after describe (Q-NUKE-010), a visibility rule lowers to the
  enabled flag.

## State tiers (D-002)

| Tier | Where |
| --- | --- |
| Host parameters | The host's parameter store, the source of truth |
| Persistent framework state | `PersistenceService` blobs, committed into the hidden `ofxgrip_state` string parameter on flush and `SyncPrivateData`. The framework keeps its version record (`ofxgrip.version`) and the meta store (`ofxgrip.meta`) there |
| Shared runtime | Per-instance objects owned by the `Effect` (extensions, caches); none on render paths yet |
| Per render | The `RenderSnapshot` |

## The parameter model (phase 1)

`ParameterModel` builds one `Parameter` per bound host parameter from `ParameterService::ids()`,
in declaration order, with groups as a tree. A parameter reads and writes through the service, so
the API-traffic events fire for every access. `effect[id]`, `effect["key"]`, and `param(...)`
look one up; `parameters().roots()`, `children()`, `descendants()`, and `withTag()` walk the tree.
Tags and meta read from the instance's `MetaManager` when the meta extension is loaded and from the
manifest record otherwise; writes need the manager.

## The framework extensions (phase 1)

| Extension | Owns | Events and priorities |
| --- | --- | --- |
| `VersioningExtension` | The stored schema and plug-in version (`ofxgrip.version`). Runs the manifest's renames and `Effect::onMigrate` when the document is older; records the new version only on success. A deprecated major registers its own plug-in slot. | CreateInstance −19 |
| `MetaExtension` | The `MetaManager`: tags, meta, the tag index; seeds from the manifest additively, merges the stored copy (stored wins), applies target presets in the order values, flags, tags, meta, labels, snaps momentary controls back to their reset value, saves on flush. Off with the manifest property `manageMeta` = false. | CreateInstance −18, InstanceChanged −10, Flush −14 |
| `VisibilityExtension` | Parsed `visibleWhen` and `enabledWhen` rules and their dependents; sets `Secret` and `Enabled`. Off with `manageVisibility` = false. | CreateInstance −17, InstanceChanged 0, SyncPrivateData 0 |
| `PresetsExtension` | The presets controls: lists the presets for the control's first tag, applies the picked one, records the selection by name (`ofxgrip.selectedPreset` in the meta store), handles Reveal and Save. Loaded when the manifest declares a `Presets` control. | CreateInstance −16, InstanceChanged −12 |
| `AnalysisExtension` | The `FrameData` store (`ofxgrip.analysis`), its spill directory under the cache folder, and its save on flush. Loaded for an `Analyzer` control or the `analysis` property. | CreateInstance −18, Flush −14 |
| `AboutExtension`, `DebugExtension` | The momentary About and Debug menus (parameters 9993 and 9994, appended by `frameworkParameters`). The debug menu needs `debugMenu` and `EffectFactory::allowsDebugFeatures()`. | InstanceChanged −12 |
| `I18nExtension` | Runtime localization through the bundle's `.strings` table; the plug-in slot localizes a copy of the manifest at describe. Needs `internationalize` and a table in the bundle. | API label, value, and option events |
| `InstanceTrackerExtension`, `RegressionExtension` | The process-wide instance registry (`trackInstances`) and the development-time manifest checks (`regression`). | load, DestroyInstance |

The author's extensions load after these, so they observe a current parameter model.

## Presets (phase 2)

`Preset` is the model and the `.fxpreset` file form: FxFactory's seven keys under their exact
names plus FxGrip's seven sibling keys, parameter-keyed dictionaries with string IDs, values in
FxFactory's encodings (colors and points as dictionaries, toggles as integers). `PresetManager`
captures the instance (state parameters, tags, meta; `PresetNo*` flags opt a section out),
applies inside the tag boundary in the order values, tags, meta with an optional remap, and lists
presets from the manifest's table, the bundle's `Contents/Resources/Presets/<tag>/`, and the user
folder `<support>/Presets/<tag>/`. A `Presets` control lowers to a choice whose options are
computed at describe, so a host that freezes options (Q-RESOLVE-021) still lists what exists.

## Analysis (phase 2)

`runAnalysis` fetches a clip's frames over a range with `clipGetImage` inside the parameter
action that started it, hands each to `Effect::onAnalyzeFrame`, stores the records by frame
index in `FrameData`, reports through the progress suite, and stops on abort. Records larger
than the spill threshold go to files under the cache folder named by a UUID the store carries,
so a reopened document finds them again; a missing file reads as a missing record.

## The C contract and Rust (phase 2)

`ofxgrip_c.h` is additive: a point release adds functions and never changes one. Every object is
opaque, every string UTF-8, every failing call returns a status and leaves a thread-local
message. An effect is a table of callbacks owned by a `CFactory`; the `ofxgrip_effect` behind
the handle is a real `Effect`, so every framework extension applies. The Rust crate wraps the
contract with a `Manifest` builder, an `Effect` trait, and a `bundle!` macro that exports the
three OpenFX symbols; panics never cross the boundary.

## Kernels (phase 3)

A kernel is a Slang compute entry point over the `GripKernel` module (D-009). `slangc` runs when
the author asks (`scripts/ofxgrip-slang.sh`, or `OFXGRIP_SLANGC` at configure time) and its
outputs are checked in beside the `.slang`: the Metal source, the C++ source, and one reflection
JSON per target. `ofxgrip_add_kernels` embeds them into the plug-in through
`cmake/KernelAdapter.cpp.in`, which compiles the C++ output against the vendored Slang prelude
with the entry symbols renamed per kernel. `KernelLayout` reads the JSON: Metal binds by slot
(buffers, textures, samplers, function constants), the C++ target by byte offset into one globals
block. `KernelMetadata` reads the `FxGripShader` JSON5 block into parameters. `KernelValues`
resolves a binding plan (which parameter feeds which member, texture, or constant) and packs the
uniforms and the specialization constants for a render. `KernelCatalog` holds every
implementation of a kernel and resolves one per API, host, and OS: native for the backend, host,
and OS; native for the backend; the universal Slang build; the CPU build. A `KernelEffect`
declares to the host only the GPU paths where every kernel resolves. `KernelEngine` dispatches:
the CPU backend runs the prelude's entry over `ITexture` views in bands; the Metal backend makes
textures over the host's buffers (or blits through an intermediate when the row stride is not
aligned), sets the function constants, and commits without waiting. OpenFX frames are bottom-up,
so the tile carries `kGripTileRowsAscend` and `gripImagePixel` reads rows ascending.

## Overlays (phase 3)

`OverlayControl` owns `OverlayPart`s and routes pen and key events to them: hit test last-added
first, drag with the Shift (constrain, snap 45°) and Alt (fine, anchor center) modifiers, draw
through a `DrawKit`. Parts read and write parameters through the `OverlayParameters` seam, so
the core tests them without a host. The adapter's `OverlayInteract` creates the effect's control
over `EffectOverlayParameters`, brackets a drag in one `EditScope`, and draws through the draw
suite when the host passes a draw context or through OpenGL otherwise; the OpenGL kit is a no-op
without a current context. Hosts that draw their own point handles (`Quirk::NativeXYHandles`)
make the point parts stand down.

## Licensing (phase 3)

The manifest's `licensing` block names a provider from the `LicensingProviders` registry. The
`LicensingExtension` caches the provider's status so a render never waits on a store, mirrors it
into a hidden toggle, puts the license state and the entitlement into every snapshot, and offers
the provider's actions through one momentary menu. After `render()` the adapter applies the
watermark the status calls for (`RenderWatermark`): the CPU composite in the core, or the Metal
and OpenCL blend kernels over the host's frame, placed on the whole frame so tiles line up. The
built-in providers are `static` (a status set in code), `trial` (days from first run, recorded in
the support folder), and `file` (an HMAC-SHA256-signed license file). A store's provider
registers beside them under its own name (D-010).

## Rules

`Rule` parses the expression language of `Rules.hpp` (keys, literals, `==` `!=` `<` `<=` `>` `>=`,
`in`, `&&` `||` `!`, parentheses). A choice compared with a string literal compares through the
option key. The manifest's `validate()` parses every rule and rejects an unknown key, so a broken
rule fails at describe, never in the inspector.

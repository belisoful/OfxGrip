# OfxGrip Architecture

Date: 2026-10-14. Phase 1.

## Layers

```
 Plug-in author
   manifest (EffectManifest, C++ or JSON) · Effect subclass · kernels (CPU and MSL now; Slang later)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip (the OpenFX adapter, include/ofxgrip/ofx)
   Plugin     bounded registry, one slot per major (current and deprecated), per-slot
              setHost/mainEntry trampolines, OFXGRIP_BUNDLE exports
   PluginSlot action dispatch with one catch boundary, host profile at load, lifecycle events
   Descriptor manifest → OFX descriptor, clips, parameters (registry handling rules applied)
   Effect     the author base: hooks, parameter objects, framework extensions, snapshot
              building, RenderContext, EditScope
   OfxHostServices  HostServices over the suites: parameters, persistence, feedback, threads,
                    time, paths, log
   Actions · PropertySet · Suites · ResolveExtensions
 ──────────────────────────────── HostServices (abstract) ──────────────────────────────────────
 ofxgrip_gpu_metal (macOS, include/ofxgrip/gpu)
   MetalBackend  a context per host command queue: pipeline cache, scratch pool, command
                 buffers over MTLBuffer frames (metal-cpp; no Objective-C class)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip_core (host-neutral, include/ofxgrip/core; no OpenFX include)
   Types · Error · Value · Codec · Flags · Manifest (+ JSON, target presets) · Rules
   Events (bus) · Extension (system) · Parameters (objects, model) · MetaManager
   MetaExtension · VisibilityExtension · VersioningExtension
   HostProfile + Quirks (registry table) · RenderSnapshot (+ codec) · Image (CPU, GpuImage)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 Hosts: DaVinci Resolve (live), the mini host (CI, CPU and Metal frames), Nuke and Natron
        (profiles only so far)
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
- Where the secret flag is permanent after describe (Q-NUKE-003), a visibility rule lowers to the
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

The author's extensions load after these, so they observe a current parameter model.

## Rules

`Rule` parses the expression language of `Rules.hpp` (keys, literals, `==` `!=` `<` `<=` `>` `>=`,
`in`, `&&` `||` `!`, parentheses). A choice compared with a string literal compares through the
option key. The manifest's `validate()` parses every rule and rejects an unknown key, so a broken
rule fails at describe, never in the inspector.

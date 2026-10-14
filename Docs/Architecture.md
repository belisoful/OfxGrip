# OfxGrip Architecture

Date: 2026-10-14. Phase 0.

## Layers

```
 Plug-in author
   manifest (EffectManifest, C++ or JSON) · Effect subclass · kernels (CPU now; Slang + native later)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 ofxgrip (the OpenFX adapter, include/ofxgrip/ofx)
   Plugin     bounded registry, per-slot setHost/mainEntry trampolines, OFXGRIP_BUNDLE exports
   PluginSlot action dispatch with one catch boundary, host profile at load, lifecycle events
   Descriptor manifest → OFX descriptor, clips, parameters (registry handling rules applied)
   Effect     the author base: hooks, extensions, snapshot building, RenderContext
   OfxHostServices  HostServices over the suites: parameters, persistence, feedback, threads,
                    time, paths, log
   Actions · PropertySet · Suites · ResolveExtensions
 ──────────────────────────────── HostServices (abstract) ──────────────────────────────────────
 ofxgrip_core (host-neutral, include/ofxgrip/core; no OpenFX include)
   Types · Error · Value · Flags · Manifest (+ JSON) · Events (bus) · Extension (system)
   HostProfile + Quirks (registry table) · RenderSnapshot (+ codec) · Image (CPU, GpuImage)
 ────────────────────────────────────────────────────────────────────────────────────────────────
 Hosts: DaVinci Resolve (live), the mini host (CI), Nuke and Natron (profiles only so far)
```

`ofxgrip_core` is the GripTruck candidate: it is what AEGrip will share once the extraction in
the host libraries plan happens. `ofxgrip` is the OpenFX adapter and contains no effect logic.

## Lifecycle

| OpenFX action | What the adapter does | Events |
| --- | --- | --- |
| `setHost`, Load | Fetch and validate suites, build the `HostProfile`, log it, `EffectFactory::onLoad` | — |
| Describe | Validate the manifest, lower the effect-level properties | — |
| DescribeInContext | Lower clips and parameters for the context, append the hidden state parameter | — |
| CreateInstance | Create the `Effect`, bind parameters and clips, load persistence, load extensions, `onCreate`, flush | CreateInstance, FinishSetup, AddedToDocument, Flush |
| InstanceChanged | Map the name to an ID (unknown names ignored), `onInstanceChanged`, button clicks, flush | BeginInstanceChanged, InstanceChanged, ParameterClicked, Flush, EndInstanceChanged |
| SyncPrivateData | Commit the persistence blobs into the hidden state parameter | SyncPrivateData |
| RoD, RoI, IsIdentity, ClipPreferences | Build a snapshot at the action time, call the hook, answer or reply default | — |
| Begin/EndSequenceRender | Hooks | BeginSequenceRender, EndSequenceRender |
| Render | Build the snapshot (timed reads + `onBuildSnapshot` + `BuildSnapshot`), fetch output and inputs (RAII), validate depth, `render(RenderContext&)` | BuildSnapshot, Render |
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

## State tiers (D-002)

| Tier | Where |
| --- | --- |
| Host parameters | The host's parameter store, the source of truth |
| Persistent framework state | `PersistenceService` blobs, committed into the hidden `ofxgrip_state` string parameter on flush and `SyncPrivateData` |
| Shared runtime | Per-instance objects owned by the `Effect` (extensions, caches); none on render paths yet |
| Per render | The `RenderSnapshot` |

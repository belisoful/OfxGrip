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
- **A headless mini host.** Loads real bundles, runs the real action order, tiles, varies render scale and bit depth, renders concurrently, and imitates a named host's quirk profile. CI runs it with sanitizers and a coverage gate.
- **Probe and sample plug-ins.** `HostDump` records what a host really does; `Gain` shows an effect built on the framework.

## Status

Phase 0 (walking skeleton). See `Docs/Plan.md` for the phases and what each ports from FxGrip.

## Requirements

- CMake 3.24 or later
- A C++20 compiler: Xcode 15 or later on macOS, Clang 16 or GCC 12 on Linux, Visual Studio 2022 on Windows
- The OpenFX 1.5.1 headers are vendored under `external/openfx/`. No other SDK is required.

## Building

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

`cmake --list-presets` shows the sanitizer, coverage, x86_64, and release presets.

## Testing in DaVinci Resolve

```bash
scripts/install-bundle.sh build/debug/plugins/Gain/Gain.ofx.bundle
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

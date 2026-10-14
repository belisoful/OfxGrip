# AGENTS.md

## Project Overview

**OfxGrip** is a C++20 framework for OpenFX image-effect plug-ins. It is the OpenFX counterpart of
FxGrip (`../FxGrip`, the Objective-C FxPlug framework) and gives DaVinci Resolve, Fusion, Nuke, and
Natron plug-ins what FxGrip gives Final Cut Pro and Motion plug-ins: a declarative effect manifest, a
parameter model with tags and meta, an extension bus with lifecycle and API-traffic events, an
immutable render snapshot, presets, a host quirks registry, and a headless test host. It targets
macOS, Windows, and Linux. DaVinci Resolve 21.1.1 free is the live test host on the development Mac.

The plan is `Docs/Plan.md`. The design inputs are in `../Research/` (decisions, comparison, host
quirks registry). `Docs/Architecture.md` describes the layering.

## Build & Test Commands

OfxGrip builds with CMake 3.24 or later and a C++20 compiler (AppleClang 15+, Clang 16+, GCC 12+,
MSVC 19.36+). `CMakePresets.json` defines the configurations.

```bash
# Configure and build (macOS arm64, Debug)
cmake --preset debug
cmake --build --preset debug

# Run every test (unit, mini-host integration, artifact)
ctest --preset debug

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan

# ThreadSanitizer (the concurrent-render stress tests run here)
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

# x86_64 slice under Rosetta
cmake --preset x86_64 && cmake --build --preset x86_64 && ctest --preset x86_64

# Coverage (clang source-based); prints a report and fails under OFXGRIP_COVERAGE_MIN
cmake --preset coverage && cmake --build --preset coverage && ctest --preset coverage && scripts/coverage.sh build/coverage

# Validate every built bundle
python3 scripts/validate-bundle.py build/debug/plugins
```

### Full Check (required before commit)

Code is commit-ready only when every check below passes.

1. `debug` build and `ctest` on macOS arm64
2. `asan-ubsan` build and `ctest`
3. `tsan` build and `ctest`
4. `x86_64` build and `ctest` under Rosetta
5. `coverage` at or above `OFXGRIP_COVERAGE_MIN`
6. `scripts/validate-bundle.py` on the built bundles

Build and test are also run on Linux and Windows in CI once those platforms land (phase 4 in
`Docs/Plan.md`); until then the Linux and Windows jobs may fail without blocking.

### Live host

Resolve loads bundles from `/Library/OFX/Plugins`. `scripts/install-bundle.sh <bundle>` copies a built
bundle there (a copy, never a symlink: Q-RESOLVE-025). Resolve caches descriptors in
`OFXPluginCache.xml`; bump the plug-in version or delete the cache when a descriptor changes
(Q-RESOLVE-026). The `HostDump` probe writes its log to `~/Library/Logs/OfxGrip/`.

## Dependencies

- The OpenFX 1.5.1 C headers, vendored under `external/openfx/` (BSD-3-Clause).
- GoogleTest and nlohmann/json, fetched by CMake at configure time with pinned versions.
- No host SDK beyond the headers. No Objective-C. Metal is reached through metal-cpp (phase 1).

The reference repositories under `local/` are concept-only (D-001). Never copy, port, or closely
paraphrase their code. The official OpenFX headers are a separate case.

## Project Structure

- `include/ofxgrip/core/` — host-neutral headers, no OpenFX include: `Types`, `Error`, `Value`,
  `Flags`, `Manifest`, `ManifestJson`, `Events`, `Extension`, `HostServices`, `HostProfile`,
  `Quirks`, `RenderSnapshot`, `Image`. These are the GripTruck candidates.
- `include/ofxgrip/ofx/` — the OpenFX adapter: `Suites`, `PropertySet`, `Actions`, `Descriptor`,
  `Effect`, `Plugin`, `OfxHostServices`, `ResolveExtensions`.
- `src/core/`, `src/ofx/` — implementations, same split.
- `minihost/` — the headless OFX host (library and `ofxgrip-minihost` CLI).
- `plugins/` — `HostDump` (probe) and `Gain` (sample), each built into an `.ofx.bundle`.
- `tests/core/`, `tests/ofx/`, `tests/integration/` — GoogleTest. Test files mirror the header they
  cover: `ManifestTests.cpp` covers `Manifest.hpp`.
- `cmake/` — `OfxGripPlugin.cmake` (bundle layout, `Info.plist`), sanitizer and coverage helpers.
- `scripts/` — `install-bundle.sh`, `validate-bundle.py`, `coverage.sh`.
- `Docs/` — `Plan.md`, `Architecture.md`, `Testing.md`.
- `external/openfx/` — vendored headers and license.

Rules of the layering:

- `core/` never includes an OpenFX header.
- `ofx/` never contains effect logic.
- Effect code asks `HostProfile::has(Quirk)` for host behavior, never for a host name.
- Every parameter read and write goes through `ParameterService`, so the API-traffic events fire.
- Render reads only the `RenderSnapshot` and the images handed to it. It mutates no instance state.

## Code Conventions

- C++20. Headers are `.hpp`, sources `.cpp`. One class or one subject per header.
- Namespace `ofxgrip`, with the versioned inline namespace `v0` declared by `OFXGRIP_NAMESPACE_BEGIN`
  and `OFXGRIP_NAMESPACE_END` in `Types.hpp`. Hidden visibility and static linking: the adapter is
  linked into each plug-in binary.
- Types and functions are PascalCase and camelCase. Member data ends in an underscore. Constants
  start with `k`. Enums are `enum class`.
- `if` statements always use a block (`{}`), never a single-line body.
- Guard clauses over nested conditionals.
- No exceptions cross the C boundary. Every OFX entry point has exactly one catch boundary that
  maps to an `OfxStatus`. Unknown actions answer `kOfxStatReplyDefault`.
- Errors are `Error` values with a code, a user message, a developer message, and context tags.
  Cancellation is a separate outcome, never an error.
- No process-wide mutable globals on render paths. No environment variables at render time.
- Document the introducing version on new public types and functions.
- **Backward compatibility** — point releases stay backward compatible. Minor releases may break
  before 1.0, but minimize the breaks.

## Code Comments

The bar for a comment is high. Code explains itself through naming and structure; comments are for
what the code cannot express.

- Use `/*! ... */` blocks (Doxygen) on public types, functions, and members. Put multi-sentence
  rationale in a `@discussion`-style paragraph after `@brief`.
- Write an inline `//` comment only for something non-obvious: a subtle invariant, an external
  constraint or host quirk (cite the registry ID, for example `Q-RESOLVE-004`), or a deliberate
  omission a maintainer might otherwise "fix".
- Do not narrate what the code does, restate the name, or leave historical justifications.
- The same bar applies to tests: the test name describes intent.

## Documentation Style (enforced)

Documentation is written in direct technical statements. American English. Present tense. Clear,
thorough, easy to comprehend, brief, timeless, integrated.

Banned constructions:

- Antithesis ("not merely X, it Ys", "rather than X, it Ys"). State what it does, once.
- Em-dash dramatic asides. Use a period or a plain clause.
- Editorializing and filler.
- Rule-of-three rhetorical lists and build-up sentences. One fact per sentence.

Prefer subject–verb–object declaratives and bullet lists of `condition → result`.

## Adding New Source Files

1. Add the header under `include/ofxgrip/core/` or `include/ofxgrip/ofx/` and the source under the
   matching `src/` folder. Add both to the target's source list in `CMakeLists.txt`.
2. Add a test file under the matching `tests/` folder and to `tests/CMakeLists.txt`.
3. A `core/` header includes only standard headers and other `core/` headers.

## Git Workflow

Several agent sessions may work on the EveryGrip checkouts at once. Work in the OfxGrip checkout or
in a detached worktree, and commit directly on `main`. No feature branches, pull requests, or merges.

1. Run the Full Check.
2. Commit with a message that states the change. No Claude attribution lines.
3. `git push origin HEAD:main`. A rejected push → `git fetch && git rebase origin/main`, re-run the
   affected tests, push again.

Commits and pushes require developer approval.

## Safeguards (Anti-Patterns)

- **NEVER** run `git clone/mv/restore/rm/branch/commit/merge/rebase/reset/pull/push` without developer approval first.
- **NEVER** run `rm` on any path without developer approval first.
- **NEVER** erase or overwrite files for the task of unit testing.
- **NEVER** delete a file or folder until its associated task is completely finished.
- **NEVER** add Claude attribution (a `Co-Authored-By: Claude` trailer or a "Generated with Claude Code" line) to any commit, tag, or pull request.

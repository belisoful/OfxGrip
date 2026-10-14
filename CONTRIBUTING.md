# Contributing to OfxGrip

## Setup

1. Install CMake 3.24 or later and a C++20 compiler (Xcode 15+ on macOS).
2. `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`.
3. To test in DaVinci Resolve, run `scripts/install-bundle.sh build/debug/plugins/Gain/Gain.ofx.bundle` and restart Resolve.

## Changes

- Keep each commit to one change.
- Add or update tests under `tests/` alongside every source change. Test files mirror the header they cover.
- Run the Full Check in [AGENTS.md](AGENTS.md#full-check-required-before-commit) before committing. CI runs the same checks.

## Conventions

[AGENTS.md](AGENTS.md) is the project's coding standard: code conventions, documentation style, comment rules, and how to add source files.

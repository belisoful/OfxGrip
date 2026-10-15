# Slang C++ prelude

The four headers the C++ code `slangc -target cpp` emits include, copied from the Slang v2026.19
release (`slang-2026.19-macos-aarch64.zip`, `include/`). They are licensed under the Apache License
2.0 with the LLVM exception (`LICENSE`). OfxGrip's CPU kernel backend compiles an emitted kernel
against them; nothing else in OfxGrip includes them.

Files: `slang-cpp-prelude.h`, `slang-cpp-types.h`, `slang-cpp-types-core.h`,
`slang-cpp-scalar-intrinsics.h`.

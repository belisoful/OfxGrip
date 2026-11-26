# Slang preludes

The headers the code `slangc` emits includes, copied from the Slang v2026.19 release
(`slang-2026.19-macos-aarch64.zip`, `include/`). They are licensed under the Apache License 2.0
with the LLVM exception (`LICENSE`).

- The C++ prelude: `slang-cpp-prelude.h`, `slang-cpp-types.h`, `slang-cpp-types-core.h`,
  `slang-cpp-scalar-intrinsics.h`. OfxGrip's CPU kernel backend compiles an emitted kernel
  (`slangc -target cpp`) against them.
- The CUDA prelude: `slang-cuda-prelude.h`. `nvcc` compiles an emitted kernel
  (`slangc -target cuda`) against it to produce the PTX. `scripts/ofxgrip-slang.sh` passes this
  folder to `nvcc`, so a PTX build uses the prelude of the Slang release that emitted the source.
  No OfxGrip source includes it.

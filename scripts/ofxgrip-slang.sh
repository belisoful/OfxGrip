#!/bin/bash
#
# ofxgrip-slang.sh
#
# Compiles a Slang compute kernel written against the GripKernel module (kernels/GripKernel.slang)
# into the sources and reflection JSON OfxGrip's kernel engine loads: Metal source and JSON for the
# Metal backend, C++ source and JSON for the CPU backend, and, when asked, SPIR-V for the Vulkan
# backend and CUDA source for the CUDA backend. This is the command D-009 specifies, once per
# target:
#
#   slangc -I <module dir> <kernel>.slang -target <target> -entry <name> -stage compute
#          -line-directive-mode standard -reflection-json <kernel>.<target>.json -o <kernel>.<ext>
#
# Usage:
#   scripts/ofxgrip-slang.sh [--slangc <path>] [--module-dir <dir>] [--entry <name>]
#                            [--output-dir <dir>] [--targets metal,cpp,cuda,spirv]
#                            [--nvcc <path>] <kernel>.slang
#
#   --slangc       The slangc executable. Default: $OFXGRIP_SLANGC, then slangc on the PATH.
#   --module-dir   The directory holding GripKernel.slang. Default: $OFXGRIP_KERNEL_MODULE_DIR,
#                  then kernels/ beside this script.
#   --entry        The compute entry point. Default: fxGripKernel.
#   --output-dir   The directory that receives the outputs. Default: the kernel's own directory.
#   --targets      The targets to emit. Default: metal,cpp.
#   --nvcc         NVIDIA's nvcc. Default: $OFXGRIP_NVCC. With the cuda target, compiles the
#                  emitted <kernel>.cu to <kernel>.ptx, the module the CUDA backend loads.
#
# Outputs, for <kernel>.slang: <kernel>.metal + <kernel>.metal.json, <kernel>.cpp +
# <kernel>.cpp.json, <kernel>.cu + <kernel>.cuda.json (+ <kernel>.ptx with nvcc), <kernel>.spv +
# <kernel>.spirv.json. The emitted C++ and CUDA sources include their prelude by its bare name.
# The C++ compiles against the copy under external/slang-prelude/ on any machine; nvcc finds
# slang-cuda-prelude.h in the include folder of the slangc install. The outputs are checked in
# beside the kernel, so a build without slangc uses them unchanged.

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
slangc="${OFXGRIP_SLANGC:-}"
nvcc="${OFXGRIP_NVCC:-}"
module_dir="${OFXGRIP_KERNEL_MODULE_DIR:-}"
entry="fxGripKernel"
output_dir=""
targets="metal,cpp"
kernel=""

usage() {
	sed -n '/^# Usage:/,/^# Outputs,/p' "$0" | sed '$d; s/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--slangc) slangc="$2"; shift 2 ;;
		--module-dir) module_dir="$2"; shift 2 ;;
		--entry) entry="$2"; shift 2 ;;
		--output-dir) output_dir="$2"; shift 2 ;;
		--targets) targets="$2"; shift 2 ;;
		--nvcc) nvcc="$2"; shift 2 ;;
		-h|--help) usage; exit 0 ;;
		-*) echo "ofxgrip-slang.sh: unknown option $1" >&2; usage >&2; exit 1 ;;
		*)
			if [[ -n "$kernel" ]]; then
				echo "ofxgrip-slang.sh: one kernel per run" >&2; exit 1
			fi
			kernel="$1"; shift ;;
	esac
done

if [[ -z "$kernel" ]]; then
	usage >&2; exit 1
fi
if [[ ! -f "$kernel" ]]; then
	echo "ofxgrip-slang.sh: no kernel at $kernel" >&2; exit 1
fi
if [[ -z "$slangc" ]]; then
	slangc="$(command -v slangc || true)"
fi
if [[ -z "$slangc" || ! -x "$slangc" ]]; then
	echo "ofxgrip-slang.sh: slangc not found; set OFXGRIP_SLANGC or pass --slangc" >&2; exit 1
fi
if [[ -z "$module_dir" ]]; then
	module_dir="$script_dir/../kernels"
fi
if [[ ! -f "$module_dir/GripKernel.slang" ]]; then
	echo "ofxgrip-slang.sh: GripKernel.slang not found in $module_dir; set OFXGRIP_KERNEL_MODULE_DIR or pass --module-dir" >&2; exit 1
fi

kernel_dir="$(cd "$(dirname "$kernel")" && pwd)"
kernel_name="$(basename "$kernel")"
base="$(basename "$kernel_name" .slang)"
module_dir="$(cd "$module_dir" && pwd)"
if [[ -z "$output_dir" ]]; then
	output_dir="$kernel_dir"
fi
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"

# Compiling from the kernel's directory with a relative module directory keeps the #line
# directives to file names, so the emitted sources carry no build-machine paths.
cd "$kernel_dir"
module_rel="$(python3 -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$module_dir" "$kernel_dir")"
IFS=',' read -r -a target_list <<< "$targets"
for target in "${target_list[@]}"; do
	case "$target" in
		metal) ext="metal" ;;
		cpp) ext="cpp" ;;
		cuda) ext="cu" ;;
		spirv) ext="spv" ;;
		*) echo "ofxgrip-slang.sh: unknown target $target" >&2; exit 1 ;;
	esac
	"$slangc" -I "$module_rel" "$kernel_name" \
		-target "$target" -entry "$entry" -stage compute -line-directive-mode standard \
		-reflection-json "$output_dir/$base.$target.json" -o "$output_dir/$base.$ext"
	if [[ "$target" == "cpp" || "$target" == "cuda" ]]; then
		# The emitted source names its prelude by the absolute path of the slangc install. The C++
		# compiles against OfxGrip's vendored copy and the CUDA against nvcc's include path, so
		# both name the prelude bare.
		python3 - "$output_dir/$base.$ext" <<'PY'
import re, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as source:
    text = source.read()
text = re.sub(r'(?m)^#include ".*/(slang-(?:cpp|cuda)-prelude\.h)"', r'#include "\1"', text)
with open(path, "w", encoding="utf-8") as source:
    source.write(text)
PY
	fi
	if [[ "$target" == "cuda" && -n "$nvcc" ]]; then
		slang_include="$(cd "$(dirname "$slangc")/../include" && pwd)"
		"$nvcc" --ptx -std=c++17 -I "$slang_include" "$output_dir/$base.cu" -o "$output_dir/$base.ptx"
	fi
done

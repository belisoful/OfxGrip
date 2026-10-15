#!/bin/sh
# Merges the clang profiles a coverage build's test run produced and reports line coverage of the
# OfxGrip sources. Fails when the total is below OFXGRIP_COVERAGE_MIN (read from CMakeCache.txt,
# or passed as the second argument).
#
#   scripts/coverage.sh build/coverage [minimum-percent]
set -eu

build="${1:?usage: coverage.sh <build-dir> [minimum]}"
minimum="${2:-}"
if [ -z "$minimum" ]; then
	minimum="$(sed -n 's/^OFXGRIP_COVERAGE_MIN:STRING=//p' "$build/CMakeCache.txt" 2>/dev/null || true)"
fi
minimum="${minimum:-90}"

profdata="$build/coverage.profdata"
# Only profiles written after the last build count; older ones belong to earlier binaries.
newest="$build/tests/ofxgrip_core_tests"
profiles="$(find "$build" -name '*.profraw' -newer "$newest" | head -2000)"
if [ -z "$profiles" ]; then
	echo "no .profraw files under $build; run ctest in a coverage build first" >&2
	exit 1
fi
llvm_profdata="$(xcrun -f llvm-profdata 2>/dev/null || command -v llvm-profdata)"
llvm_cov="$(xcrun -f llvm-cov 2>/dev/null || command -v llvm-cov)"
# shellcheck disable=SC2086
"$llvm_profdata" merge -sparse $profiles -o "$profdata"

objects=""
for exe in "$build"/tests/ofxgrip_core_tests "$build"/tests/ofxgrip_ofx_tests "$build"/tests/ofxgrip_integration_tests "$build"/tests/ofxgrip_gpu_tests; do
	[ -x "$exe" ] && objects="$objects -object $exe"
done
for plugin in $(find "$build/plugins" -name '*.ofx' -type f 2>/dev/null); do
	objects="$objects -object $plugin"
done
first="$(echo "$objects" | awk '{print $2}')"
rest="$(echo "$objects" | cut -d' ' -f3-)"

root="$(cd "$(dirname "$0")/.." && pwd)"
# The gate covers the product code: the core, the adapter, and the plug-ins. The mini host is
# test infrastructure and is reported separately.
# shellcheck disable=SC2086
report="$("$llvm_cov" report "$first" $rest -instr-profile="$profdata" -ignore-filename-regex='(_deps|external|tests)' "$root/src" "$root/include" "$root/plugins")"
echo "== product code (gated)"
echo "$report"
echo "== mini host (informational)"
# shellcheck disable=SC2086
"$llvm_cov" report "$first" $rest -instr-profile="$profdata" -ignore-filename-regex='(_deps|external|tests|minihost/main\.cpp)' "$root/minihost" | tail -3
total="$(echo "$report" | awk '/^TOTAL/ {print $0}')"
lines="$(echo "$total" | awk '{for (i=1;i<=NF;i++) if ($i ~ /%$/) pct[++n]=$i} END {print pct[n-1]}' | tr -d '%')"
if [ -z "$lines" ]; then
	echo "could not read the line coverage from the report" >&2
	exit 1
fi
echo "line coverage: ${lines}% (minimum ${minimum}%)"
awk -v have="$lines" -v need="$minimum" 'BEGIN { exit (have + 0 >= need + 0) ? 0 : 1 }' || {
	echo "coverage is below the minimum" >&2
	exit 1
}

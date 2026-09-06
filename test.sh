#!/usr/bin/env bash
#
# test.sh -- determinism check + quick wall-time measurement for the C++
# optimizer. Meant to be the software half of the eventual "C++ vs FPGA"
# comparison: this produces the reference wall-time number, the cocotb bench
# produces the cycle count.
#
# It runs the optimizer several times at a HARDCODED seed and iteration count,
# asserts every run lands on the same final SSE (that is the determinism gate --
# a mismatch means the search path stopped being reproducible), and reports
# wall-time stats. Appends one row to bench/timing.csv.
#
# Iteration count is fixed at 8000: enough to be a stable timing sample, small
# enough that the matching cocotb run is not painfully slow.
#
# NOTE: EXPECTED_SSE is specific to this toolchain. The search path uses
# std::normal_distribution / std::uniform_real_distribution / double math, whose
# results are not portable across standard-library implementations. If you
# change compiler/libstdc++ or the search code, re-baseline it from a known-good
# run and commit the new value.

set -euo pipefail

# --- hardcoded parameters ------------------------------------------------------
TARGET="tests/mona_lisa_256.png"
SEED=1
ITERS=8000
RUNS="${1:-5}"                 # optional first arg overrides run count
EXPECTED_SSE=54798669          # seed 1, 8000 iters, default patience (libstdc++)
BUILD_DIR="build"
# ----------------------------------------------------------------------------

ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$ROOT"

RESULTS="$ROOT/bench/timing.csv"
TMPDIR_RUN="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_RUN"' EXIT
OUT="$TMPDIR_RUN/out.png"

echo "building ($BUILD_DIR, Release) ..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" >/dev/null
BIN="$BUILD_DIR/triopt"

echo "running $RUNS x : $BIN $TARGET <out> $ITERS $SEED"
echo

times=()
mismatch=0
for i in $(seq 1 "$RUNS"); do
    t0=$(date +%s%N)
    done_line="$("$BIN" "$TARGET" "$OUT" "$ITERS" "$SEED" 2>&1 | grep '^done\.')" \
        || { echo "FAIL: optimizer produced no 'done.' line"; exit 2; }
    t1=$(date +%s%N)
    ms=$(( (t1 - t0) / 1000000 ))
    times+=("$ms")

    got="$(printf '%s\n' "$done_line" | sed -E 's/^done\. sse [0-9]+ -> ([0-9]+).*/\1/')"
    if [ "$got" = "$EXPECTED_SSE" ]; then
        printf '  run %d: %5d ms   SSE %s   ok\n' "$i" "$ms" "$got"
    else
        printf '  run %d: %5d ms   SSE %s   != expected %s   <-- DETERMINISM FAIL\n' \
            "$i" "$ms" "$got" "$EXPECTED_SSE"
        mismatch=1
    fi
done

sorted="$(printf '%s\n' "${times[@]}" | sort -n)"
min="$(printf '%s\n' "$sorted" | head -1)"
max="$(printf '%s\n' "$sorted" | tail -1)"
median="$(printf '%s\n' "$sorted" | sed -n "$(( (RUNS + 1) / 2 ))p")"
sum=0; for m in "${times[@]}"; do sum=$(( sum + m )); done
mean=$(( sum / RUNS ))

if [ "$mismatch" -eq 0 ]; then verdict="ALL MATCH"; else verdict="MISMATCH"; fi

echo
echo "==================== results ===================="
printf ' target       : %s\n' "$TARGET"
printf ' seed / iters : %s / %s\n' "$SEED" "$ITERS"
printf ' runs         : %s\n' "$RUNS"
printf ' final SSE    : %s  (%s)\n' "$EXPECTED_SSE" "$verdict"
printf ' wall time    : min %d  median %d  mean %d  max %d   (ms)\n' \
    "$min" "$median" "$mean" "$max"
echo "================================================"

mkdir -p "$(dirname "$RESULTS")"
if [ ! -f "$RESULTS" ]; then
    echo "utc,host,commit,seed,iters,runs,expected_sse,match,min_ms,median_ms,mean_ms,max_ms" > "$RESULTS"
fi
printf '%s,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d,%d\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    "${HOSTNAME:-$(uname -n 2>/dev/null || echo unknown)}" \
    "$(git rev-parse --short HEAD)" \
    "$SEED" "$ITERS" "$RUNS" "$EXPECTED_SSE" "$verdict" \
    "$min" "$median" "$mean" "$max" >> "$RESULTS"
echo "appended to ${RESULTS#$ROOT/}"

exit "$mismatch"

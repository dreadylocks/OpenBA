#!/bin/bash
# Tests the hands-on pipeline WITHOUT a human and without any window or sound:
#   1. builds trace_harness.cpp against the shipping logger + pump on SDL's dummy video driver,
#   2. lets it perform the whole checklist with synthetic events and write a trace,
#   3. expects analyze.py to say PASS (exit 0) on it, FAIL (exit 1) on corrupted copies,
#   4. runs the analyzer unit tests (test_analyze.py).
# usage: tools/macos/handtest/run_pipeline_test.sh [build-dir]   (default build/mac-hand; needs
#        its libSDL3.a, i.e. any `cmake --build <dir> -t OpenBA` has run there)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE=$ROOT/tools/macos/handtest
B=${1:-$ROOT/build/mac-hand}
[[ "$B" = /* ]] || B=$ROOT/$B
SRC=$ROOT/src
OUT=$B/handtest_harness
mkdir -p "$B/tmp-handtest"
c++ -std=c++17 -arch arm64 -O0 -g -DBANISHED_TWEAK_OVERLAY=0 \
  -I$SRC -I$SRC/app -I$SRC/diag -I$SRC/platform/common -I$SRC/platform/sdl \
  -I$B/third_party/sdl3/include-revision -I$ROOT/third_party/SDL/include \
  $HERE/trace_harness.cpp $SRC/diag/darwin/input_trace_darwin.cpp \
  $SRC/platform/sdl/pump_sdl.cpp $SRC/platform/sdl/input_sdl.cpp $SRC/platform/darwin/input_darwin.cpp $SRC/platform/sdl/display_sdl.cpp \
  $SRC/platform/common/window_messages.cpp $SRC/platform/common/display_geometry.cpp \
  $SRC/app/options.cpp \
  $B/third_party/sdl3/libSDL3.a \
  -framework Metal -framework QuartzCore -framework CoreMedia -framework CoreVideo -framework Cocoa \
  -weak_framework UniformTypeIdentifiers -framework IOKit -framework Carbon -framework CoreAudio \
  -framework AudioToolbox -framework AVFoundation -framework Foundation -lm -lpthread -o "$OUT"
TRACE=$B/tmp-handtest/good_trace.log
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$OUT" "$TRACE"
echo "--- analyzer on the good trace (must exit 0)"
python3 "$HERE/analyze.py" "$TRACE" --no-manual
echo "--- analyzer on corrupted traces (each must exit 1)"
python3 "$HERE/corrupt_trace.py" "$TRACE" "$B/tmp-handtest"
rc=0
for f in "$B"/tmp-handtest/bad_*.log; do
  if python3 "$HERE/analyze.py" "$f" --no-manual > "$f.out"; then echo "UNEXPECTED PASS: $f"; rc=1; else
    echo "ok (FAIL as expected): $(basename "$f"): $(grep -m1 '^FAIL' "$f.out" | cut -c1-110)"; fi
done
echo "--- analyzer unit tests"
python3 "$HERE/test_analyze.py" || rc=1
exit $rc

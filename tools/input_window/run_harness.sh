#!/bin/sh
# Builds and runs pump_harness.cpp against the shipping pump/input/display code on SDL's
# dummy video driver (no window, no sound). Needs build/mac-input's libSDL3.a (build any
# target there first). Usage: tools/input_window/run_harness.sh [build-dir]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${1:-$ROOT/build/mac-input}
OUT=$B/input_harness
SRC=$ROOT/src
c++ -std=c++17 -arch arm64 -O0 -g -DBANISHED_TWEAK_OVERLAY=0 \
  -I$SRC -I$SRC/app -I$SRC/platform/common -I$SRC/platform/sdl \
  -I$B/third_party/sdl3/include-revision -I$ROOT/third_party/SDL/include \
  $ROOT/tools/input_window/pump_harness.cpp \
  $SRC/platform/sdl/pump_sdl.cpp $SRC/platform/sdl/input_sdl.cpp $SRC/platform/darwin/input_darwin.cpp $SRC/platform/sdl/display_sdl.cpp \
  $SRC/platform/common/window_messages.cpp $SRC/platform/common/display_geometry.cpp \
  $SRC/app/options.cpp \
  $B/third_party/sdl3/libSDL3.a \
  -framework Metal -framework QuartzCore -framework CoreMedia -framework CoreVideo -framework Cocoa \
  -weak_framework UniformTypeIdentifiers -framework IOKit -framework Carbon -framework CoreAudio \
  -framework AudioToolbox -framework AVFoundation -framework Foundation -lm -lpthread -o $OUT
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy $OUT

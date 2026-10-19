#!/usr/bin/env bash
# tools/build.sh -- the one build / deploy / run entry point over the CMake build (2026-10-11).
#
# usage: tools/build.sh <command> [flavour] [--jobs N] [--cross]
#   build   [flavour]   configure build/cmake-<flavour> if absent, build OpenBA
#   unit    [flavour]   build the unit tests and run them (exit = the suite's exit)
#   deploy  [flavour]   build + copy the exe to the game directory (BANISHED_DIR), then
#                       tools/ext_resources.sh --deploy (the extension .crs files, loose in WinData/Dialog/)
#   run     [flavour]   deploy + launch with BANISHED_AUTOSTART=1 and /profile:faithful (THE GATE's by-hand run)
#   run-menu [flavour]  deploy + launch, stops at the main menu
#   stress  [flavour]   build perf (default), deploy, tools/stress_run.sh --no-build $STRESS_ARGS
#   clean   [flavour]   remove build/cmake-<flavour> (all four when no flavour is given)
#   exe     [flavour]   print the exe path (for scripts)
#   flavours: debug (default) | og | perf | release   -- CMakeLists.txt's BANISHED_FLAVOUR
#   --cross             (macOS / Linux host) build the WINDOWS exe with mingw-w64 GCC through
#                       tools/toolchains/mingw-w64-x86_64.cmake, into build/cross-<flavour>.
#                       build / deploy / exe / clean only: the exe cannot be launched here, so
#                       deploy just copies it to BANISHED_DIR (e.g. the PC's game folder on a share).
#
# Jobs: --jobs N or BUILD_JOBS (default 8 since 2026-10-17, the user: no wide multi-agent builds any more; agents still use 2 -- memory
# feedback_build_parallelism_low_memory). Extra cmake args: CMAKE_EXTRA (e.g. "-DBANISHED_USE_CCACHE=OFF").
# Toolchain: cmake + ninja from pip on PATH, GCC from C:/msys64/ucrt64/bin (docs/reference/environment.md);
# on Linux the same script works with the POSIX targets (OpenBA, unit) -- no deploy / run there
# (tools/linux_gamedir.sh + tools/linux_run.sh do that).
set -u
CMD="${1:-build}"; shift || true
FLAV="debug"; [ "$CMD" = stress ] && FLAV=perf
FLAV_GIVEN=0
case "${1:-}" in debug|og|perf|release) FLAV="$1"; FLAV_GIVEN=1; shift;; esac
JOBS="${BUILD_JOBS:-8}"
CROSS=0
while [ $# -gt 0 ]; do
  case "$1" in
    --jobs) JOBS="$2"; shift 2;;
    --cross) CROSS=1; shift;;
    *) echo "unknown argument $1"; exit 2;;
  esac
done
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXT=.exe;; *) EXT="";; esac
# THE game-directory variable: env BANISHED_DIR (every script under tools/ reads this one name);
# the CMake cache var BANISHED_GAME_DIR is set from it at configure time.
BANISHED_DIR="${BANISHED_DIR:-C:/GOG Games/Banished}"
B="build/cmake-$FLAV"
CROSS_ARGS=""
if [ "$CROSS" = 1 ]; then
  case "$CMD" in build|deploy|exe|clean) ;; *) echo "build.sh $CMD: not with --cross (the exe only runs on Windows)"; exit 2;; esac
  EXT=.exe
  B="build/cross-$FLAV"
  CROSS_ARGS="-DCMAKE_TOOLCHAIN_FILE=tools/toolchains/mingw-w64-x86_64.cmake"
fi
EXE="$B/OpenBA$EXT"
# THE GATE RUNS FAITHFUL (docs/extensions/DESIGN.md §3 "Gates"): every fix and extension off for
# this one run, through the exe's own command line (retail's `/key:value` parser, System::Configuration),
# not settings.cfg and not an environment flag; src/ext/registry.h. run-menu (the by-hand look) does
# not pass it. MSYS2_ARG_CONV_EXCL keeps the shell from rewriting the leading `/` as a path.
FAITHFUL="/profile:faithful"

configure() {
  [ -f "$B/build.ninja" ] && return 0
  echo "== configure $B (BANISHED_FLAVOUR=$FLAV)"
  cmake -S . -B "$B" -G Ninja -DBANISHED_FLAVOUR="$FLAV" -DBANISHED_GAME_DIR="$BANISHED_DIR" $CROSS_ARGS ${CMAKE_EXTRA:-} || exit 1
}
build() { # targets...
  configure
  echo "== build $B: $* (-j$JOBS)"
  cmake --build "$B" -j "$JOBS" -t "$@" || exit 1
}
need_game_dir() {
  [ -d "$BANISHED_DIR" ] || { echo "build.sh $CMD: game directory not found: $BANISHED_DIR (set BANISHED_DIR to the Banished install)"; exit 1; }
}
deploy() {
  need_game_dir
  build OpenBA
  cp -f "$EXE" "$BANISHED_DIR/OpenBA.exe" || { echo "deploy: exe busy, is a game running?"; exit 1; }
  rm -f "$BANISHED_DIR/standalone_debug.log"
  echo "Deployed $EXE to $BANISHED_DIR/OpenBA.exe"
  # The extension resources (the Extensions dialog, the Enhanced tab, the sprites, the sharp_ui
  # fonts) are built inside the exe (docs/extensions/KITFREE.md); nothing loose is deployed.
  # tools/ext_resources.sh remains only as the cross-check oracle (the kit's compiled bytes).
}

case "$CMD" in
  exe)     echo "$EXE";;
  build)   build OpenBA;;
  unit)    build unit; "$B/unit$EXT";;
  deploy)  deploy;;
  run)     deploy; ( cd "$BANISHED_DIR" && BANISHED_AUTOSTART=1 MSYS2_ARG_CONV_EXCL="$FAITHFUL" start OpenBA.exe $FAITHFUL )
           echo "Launched (autostart, $FAITHFUL)";;
  run-menu) deploy; ( cd "$BANISHED_DIR" && start OpenBA.exe ); echo "Launched (menu only)";;
  stress)  deploy; tools/stress_run.sh --no-build ${STRESS_ARGS:-};;
  clean)   if [ "$FLAV_GIVEN" = 1 ]; then rm -rf "$B"; echo "removed $B"
           elif [ "$CROSS" = 1 ]; then rm -rf build/cross-debug build/cross-og build/cross-perf build/cross-release; echo "removed build/cross-{debug,og,perf,release}"
           else rm -rf build/cmake-debug build/cmake-og build/cmake-perf build/cmake-release; echo "removed build/cmake-{debug,og,perf,release}"; fi;;
  *) echo "unknown command $CMD"; sed -n 2,15p "$0"; exit 2;;
esac

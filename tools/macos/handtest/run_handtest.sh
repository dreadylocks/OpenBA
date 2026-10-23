#!/bin/bash
# Hands-on test of the Mac port's mouse / keyboard / window behaviour, with a machine-readable result.
#   tools/macos/handtest/run_handtest.sh                 the owner's test (visible window, ~3-5 minutes)
# Options:
#   --engine PATH       the OpenBA executable (default: build/mac-hand/OpenBA, then build/mac-pack/OpenBA,
#                       then dist/OpenBA.app/Contents/MacOS/OpenBA-engine; it must contain the input logger)
#   --game-dir DIR      the Banished folder or its WinData folder (default: whatever run/WinData points at)
#   --no-questions      do not ask the yes/no questions at the end
#   --max-seconds N     stop waiting for you after N seconds (default 900)
#   --background        developer self-check: small unobtrusive window (BANISHED_BACKGROUND=1), no checklist,
#                       stops by itself after --max-seconds (default 25 in this mode)
# The game runs in its own throwaway folder (run/handtest-<time>/): its saves, settings and logs go there
# (USERPROFILE is redirected), so your real game and settings are untouched. Result: handtest_result.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/tools/macos/handtest"
ENGINE=""; GAME_DIR=""; QUESTIONS=1; MAXS=""; BG=0
while [[ $# -gt 0 ]]; do case "$1" in
  --engine) ENGINE="$2"; shift 2;; --game-dir) GAME_DIR="$2"; shift 2;;
  --no-questions) QUESTIONS=0; shift;;
  --max-seconds) MAXS="$2"; shift 2;; --background) BG=1; QUESTIONS=0; shift;;
  -h|--help) sed -n '2,15p' "$0"; exit 0;;
  *) echo "unknown option $1"; exit 2;; esac; done
[[ -n "$MAXS" ]] || { [[ $BG == 1 ]] && MAXS=25 || MAXS=900; }

# --- find the engine and make sure it carries the logger
if [[ -z "$ENGINE" ]]; then
  for c in "$ROOT/build/mac-hand/OpenBA" "$ROOT/build/mac-pack/OpenBA" "$ROOT/dist/OpenBA.app/Contents/MacOS/OpenBA-engine"; do
    if [[ -x "$c" ]] && grep -aq BANISHED_INPUT_TRACE "$c"; then ENGINE="$c"; break; fi
  done
fi
if [[ -z "$ENGINE" || ! -x "$ENGINE" ]] || ! grep -aq BANISHED_INPUT_TRACE "$ENGINE"; then
  echo "No OpenBA build with the input logger found. Build one with:"
  echo "  cmake -S $ROOT -B $ROOT/build/mac-hand -G Ninja -DCMAKE_OSX_ARCHITECTURES=arm64 -DBANISHED_USE_CCACHE=OFF"
  echo "  cmake --build $ROOT/build/mac-hand -t OpenBA -j3"
  echo "or pass --engine PATH."; exit 2
fi

# --- find the game data
if [[ -z "$GAME_DIR" && -L "$ROOT/run/WinData" ]]; then GAME_DIR="$(readlink "$ROOT/run/WinData")"; fi
[[ "$(basename "$GAME_DIR")" == WinData ]] && GAME_DIR="$(dirname "$GAME_DIR")"
if [[ -z "$GAME_DIR" || ! -d "$GAME_DIR/WinData" ]]; then
  echo "Cannot find the Banished game folder. Pass --game-dir '<folder that contains WinData>'."; exit 2
fi

# --- the throwaway folder
RUN="$ROOT/run/handtest-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN/home/Documents/Banished" "$RUN/tmp"
# A fresh settings.cfg would start the game full screen (VideoFullscreen=true is its default): seed a
# windowed 1280x720 start so the test begins in a normal, resizable window.
cat > "$RUN/home/Documents/Banished/settings.cfg" <<EOS
VideoSettings=true
VideoFullscreen=false
VideoWidth=1280
VideoHeight=720
GameEdgeScroll=true
EOS
ln -s "$GAME_DIR/WinData" "$RUN/WinData"
# The engine finds WinData/ and writes UserData/ and its logs beside its own (symlinked) executable path,
# exactly as the .app launcher (packaging/macos/launcher.mm, the packaging PR) sets it up: so run it through a symlink inside the throwaway folder.
ln -s "$ENGINE" "$RUN/OpenBA"
AIRCONV=""
for c in "$(dirname "$ENGINE")/../Frameworks/libairconv.dylib" "$ROOT/third_party/airconv_build/build/libairconv.dylib"; do
  [[ -f "$c" ]] && { AIRCONV="$(cd "$(dirname "$c")" && pwd)/$(basename "$c")"; break; }; done

export BANISHED_INPUT_TRACE=1 BANISHED_METAL_STATS=1 USERPROFILE="$RUN/home" TMPDIR="$RUN/tmp"
[[ -n "$AIRCONV" ]] && export BANISHED_AIRCONV="$AIRCONV"
[[ $BG == 1 ]] && export BANISHED_BACKGROUND=1

if [[ $BG == 0 ]]; then
cat <<EOT

==============================  OpenBA hands-on test  ==============================
The game will open in a normal window. Do these in order (about 3-5 minutes). Do not
worry about doing them perfectly: the report tells us what it did and did not see.
Keep the game window in front the whole time. The game is in the folder
  $RUN

  0. Wait for the main menu, then start a new game (any map) and wait until you see the town.
  1. EDGES    Slowly push the mouse pointer against the LEFT, RIGHT, TOP and BOTTOM edge of the
              game window. The view should scroll each time.
  2. CLICK    Click one toolbar button at the bottom or top of the screen (anything, e.g. the
              build menu), then click it again to close it.
  3. ROTATE   Hold the MIDDLE mouse button and drag. If you have no middle button: hold the
              OPTION key and drag with the left button. (Do both if you can.)
  4. ZOOM     Scroll the wheel (or two fingers on the trackpad) up, then down.
  5. KEYS     Press, one at a time:  W  A  S  D  Q  E  F1  Space
  6. PAGE UP  Hold the RIGHT Option key and tap the Up arrow once.
  7. SCALE    Open Options, change "UI scale" (e.g. 100 -> 150 -> back), close Options.
  8. FULLSCREEN  Press Ctrl+Cmd+F (go full screen), wait 3 seconds, press Ctrl+Cmd+F again.
  9. RESIZE   Drag a corner of the window to make it bigger, then smaller.
 10. QUIT     Press Cmd+Q. (This ends the test; the analysis starts by itself.)

If something freezes or looks broken, just press Cmd+Q anyway, or wait: after $MAXS seconds
the test stops waiting for you. Starting the game now...
===================================================================================

EOT
fi

cd "$RUN" || exit 2
START=$(date +%s)
"$RUN/OpenBA" -nomods > "$RUN/launcher_out.txt" 2>&1 &
PID=$!
while kill -0 "$PID" 2>/dev/null; do
  sleep 1
  if (( $(date +%s) - START >= MAXS )); then
    echo "Stopped waiting after ${MAXS}s: closing the game (SIGTERM)."
    kill -TERM "$PID" 2>/dev/null; sleep 3; kill -0 "$PID" 2>/dev/null && kill -KILL "$PID" 2>/dev/null
    break
  fi
done
wait "$PID" 2>/dev/null; CODE=$?

META="$RUN/handtest_meta.txt"
{ echo "exit_code=$CODE"; echo "engine=$ENGINE"; echo "seconds=$(( $(date +%s) - START ))"
  echo "mac=$(sw_vers -productVersion 2>/dev/null) $(uname -m)"; } > "$META"

if [[ $QUESTIONS == 1 && -t 0 ]]; then
  echo; echo "Five quick questions (y = yes, n = no, s = did not try it):"
  ask() { local a; read -r -p "  $2 [y/n/s] " a; case "$a" in y|Y) a=yes;; n|N) a=no;; *) a=skip;; esac; echo "manual.$1=$a" >> "$META"; }
  ask ui_scale    "Did changing UI scale make the buttons and text bigger/smaller?"
  ask edge_scroll "Did pushing the pointer against an edge scroll the view?"
  ask toolbar     "Did the toolbar button you clicked react?"
  ask rotate_view "Did middle-drag / Option+drag rotate the camera?"
  ask zoom_view   "Did the scroll wheel zoom in and out?"
fi

echo; echo "================== RESULT =================="
python3 "$HERE/analyze.py" "$RUN/input_trace.log" --meta "$META" --frames "$RUN/standalone_metal_frames.csv" | tee "$RUN/handtest_result.txt"
AR=${PIPESTATUS[0]}
echo
echo "Game exit code: $CODE.  Everything is in: $RUN"
echo "  handtest_result.txt (this table), input_trace.log (every event), standalone_error.log (engine log)"
exit $AR

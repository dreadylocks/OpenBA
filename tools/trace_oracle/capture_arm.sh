#!/usr/bin/env bash
# Capture one in-game frame from OpenBA.exe under RenderDoc, for ANY arm.
#
#   capture_arm.sh <tag> [--frame N] [--expect-log REGEX] [--out DIR] -- ENV=V ...
#
# This is the generalisation of tools/dx11_state_diff/capture.sh, whose two arms
# are hard-wired to the VideoDX11 A/B seam flags. An arm is whatever environment
# the comparison needs, so the environment is a parameter here.
#
# WHY A SEPARATE SCRIPT AND NOT A PIXEL SHOT
# ------------------------------------------
# Wave 23 measured that seed + camera pin do NOT give a reproducible frame: five
# runs at identical seed, pin and command line split into two clusters ~98%
# apart, independently of the flag under test. What this captures instead is the
# COMMAND STREAM, which trace_oracle compares with set-based screens that need no
# frame determinism.
#
# --expect-log IS NOT OPTIONAL IN PRACTICE
# ----------------------------------------
# A run that silently did not arm your flag produces a perfectly good capture and
# a perfectly clean comparison. Pass a regex that only the armed arm prints (an
# attendance counter, a [port] line) and this script REJECTS the capture if the
# log does not contain it. Without it you are comparing an arm against itself and
# the tool has no way to tell you.
#
# Requires the game-directory lock:  tools/game_dir_lock.sh acquire <LANE> <secs>
# -- acquire it BARE, never piped (a pipeline reports the LAST command's status,
# so a timeout reads as a GRANT).
set -u

GAME="${BANISHED_DIR:-${BANISHED_GAME_DIR:-C:/GOG Games/Banished}}"
RDCMD="${RENDERDOCCMD:-C:/Program Files/RenderDoc/renderdoccmd.exe}"

TAG="${1:?usage: capture_arm.sh <tag> [--frame N] [--expect-log RE] [--out DIR] [--exe-args 'A B'] -- ENV=V ...}"
shift
FRAME=150
OUT="$GAME/rdoc_trace"
EXPECT=""
EXE_ARGS=""   # --exe-args '/profile:faithful': passed to OpenBA.exe (an arm that needs the exe's own switches)

while [ $# -gt 0 ]; do
  case "$1" in
    --frame)      FRAME="$2"; shift 2 ;;
    --expect-log) EXPECT="$2"; shift 2 ;;
    --out)        OUT="$2";   shift 2 ;;
    --exe-args)   EXE_ARGS="$2"; shift 2 ;;
    --)           shift; break ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
EXTRA_ENV=("$@")

[ -x "$RDCMD" ] || { echo "no renderdoccmd at $RDCMD" >&2; exit 2; }
[ -f "$GAME/OpenBA.exe" ] || { echo "no OpenBA.exe in $GAME" >&2; exit 2; }

mkdir -p "$OUT"
cd "$GAME" || exit 9

# THE GAME IS KILLED BY PID ONLY, NEVER BY IMAGE NAME (AO.md §7.6, 2026-10-16).
# The old reap was `wmic ... name='OpenBA.exe' delete`: it killed every game on
# the machine, including another lane's run. The pid is the one the game's own
# watchdog publishes in standalone_boot_state.txt (renderdoccmd returns at launch,
# so `$!` is not the game). The state file is deleted before the launch, so the pid
# read is always this run's.
game_pid() { sed -n 's/.*pid=\([0-9]*\).*/\1/p' "$GAME/standalone_boot_state.txt" 2>/dev/null | head -1; }
alive() {
  local p; p=$(game_pid)
  # Not published yet: still launching -- for at most 120 s (t = the wait loop's tick).
  [ -z "$p" ] && { [ "${t:-0}" -lt 60 ]; return; }
  tasklist //FI "PID eq $p" 2>/dev/null | grep -qi OpenBA.exe
}
reap() {
  local p; p=$(game_pid)
  [ -n "$p" ] && tasklist //FI "PID eq $p" 2>/dev/null | grep -qi OpenBA.exe &&
    taskkill //PID "$p" //F >/dev/null 2>&1
  true
}

# An already-running game is NOT ours to kill: it is an orphan or another lane's run
# (the caller holds the game-dir lock, so neither should exist). Name it and stop.
if tasklist //FI "IMAGENAME eq OpenBA.exe" 2>/dev/null | grep -qi OpenBA.exe; then
  echo "an OpenBA.exe is already running -- kill it by PID first (tools/game_dir_lock.sh orphans):" >&2
  tasklist //FI "IMAGENAME eq OpenBA.exe" 2>/dev/null | grep -i OpenBA.exe >&2
  exit 2
fi
rm -f "$GAME/standalone_boot_state.txt" "$GAME"/reimpl_capture*.rdc standalone_debug.log \
      standalone_error.log standalone_crash.log 2>/dev/null

# Record exactly what this arm was, beside the capture. A capture whose arm is
# not written down is not comparable later.
{
  echo "tag=$TAG"
  echo "frame=$FRAME"
  echo "expect=$EXPECT"
  echo "exe_args=$EXE_ARGS"
  echo "exe_md5=$(md5sum "$GAME/OpenBA.exe" 2>/dev/null | cut -d' ' -f1)"
  echo "when=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  for e in "${EXTRA_ENV[@]:-}"; do [ -n "$e" ] && echo "env=$e"; done
} > "$OUT/$TAG.arm.txt"

echo "=== capture '$TAG' frame=$FRAME env=${EXTRA_ENV[*]:-<none>} exe_args=${EXE_ARGS:-<none>} ==="

env "${EXTRA_ENV[@]:-BANISHED_TRACE_ORACLE=1}" \
    BANISHED_AUTOSTART=1 \
    BANISHED_CAPTURE_INGAME="$FRAME" \
    MSYS2_ARG_CONV_EXCL="/profile:" \
    "$RDCMD" capture --opt-ref-all-resources \
      -d "$GAME" -c "$GAME/reimpl_capture" \
      "$GAME/OpenBA.exe" $EXE_ARGS >/dev/null 2>&1 &

# WHICH .rdc IS THE IN-GAME ONE (AO.md §7.6). The menu loop triggers its own capture
# on its draw 10 whenever RenderDoc is attached (menudialogloopt_stubs.cpp), so the
# first file is ALWAYS the main menu; the old "newest file" pick took it. The in-game
# arm (gameupdateloop.cpp, BANISHED_CAPTURE_INGAME) is the one that logs
# "[RenderDoc] capture armed for 0 frames" to stdout (standalone_debug.log); its
# capture is the next "capture started" after that line. Its RANK among the run's
# captures (1-based) selects the file by mtime order, once that many captures ended.
ingame_rank() {
  awk '/\[RenderDoc\] capture started/ { n++ }
       /\[RenderDoc\] capture armed for 0 frames/ { print n + 1; exit }' \
      "$GAME/standalone_debug.log" 2>/dev/null
}
ended_count() { local n; n=$(grep -c '\[RenderDoc\] capture ended' "$GAME/standalone_debug.log" 2>/dev/null); echo "${n:-0}"; }

WAIT_ITERS="${CAPTURE_WAIT_ITERS:-450}"   # x2s -> 15 min default
rdc=""
for t in $(seq 1 "$WAIT_ITERS"); do
  sleep 2
  cand=""
  rank=$(ingame_rank)
  if [ -n "$rank" ] && [ "$(ended_count)" -ge "$rank" ]; then
    cand=$(ls -tr "$GAME"/reimpl_capture*.rdc 2>/dev/null | sed -n "${rank}p")
  fi
  if [ -n "$cand" ]; then
    s1=$(stat -c%s "$cand" 2>/dev/null || echo 0)
    sleep 3
    s2=$(stat -c%s "$cand" 2>/dev/null || echo 0)
    if [ "$s1" = "$s2" ] && [ "$s1" != "0" ]; then rdc="$cand"; break; fi
  fi
  alive || { echo "  process exited before a capture appeared" >&2; break; }
  [ $((t % 30)) = 0 ] && echo "  ...$((t * 2))s elapsed"
done

reap; sleep 1

if [ -z "$rdc" ]; then
  echo "FAILED: no capture produced for '$TAG'" >&2
  [ -s standalone_crash.log ] && { echo "--- crash log ---"; head -20 standalone_crash.log; }
  exit 1
fi

bad=0
if [ -s "$GAME/standalone_crash.log" ]; then
  echo "REJECTED: non-empty crash log" >&2; bad=1
fi
if [ -n "$EXPECT" ]; then
  if grep -qE "$EXPECT" standalone_debug.log standalone_error.log 2>/dev/null; then
    echo "  arm confirmed: log matches /$EXPECT/"
  else
    echo "REJECTED: log does NOT match /$EXPECT/ -- this arm did not take effect," >&2
    echo "          so comparing it would compare an arm against itself." >&2
    bad=1
  fi
else
  echo "  WARNING: no --expect-log given. NOTHING verified that this arm armed." >&2
fi

cp -f "$rdc" "$OUT/$TAG.rdc" || exit 1
cp -f standalone_debug.log "$OUT/$TAG.debug.log" 2>/dev/null
cp -f standalone_error.log "$OUT/$TAG.error.log" 2>/dev/null
rm -f "$GAME"/reimpl_capture*.rdc

echo "  -> $OUT/$TAG.rdc ($(( $(stat -c%s "$OUT/$TAG.rdc") / 1024 / 1024 )) MB)"
[ "$bad" = "1" ] && { echo "  ^ capture kept, but DO NOT COMPARE IT" >&2; exit 3; }
exit 0

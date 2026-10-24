#!/usr/bin/env bash
# tools/cell_dump/cell_dump.sh -- dump a save's map cells (the "red square" reports).
#
# Loads <save> unattended (BANISHED_AUTOLOAD) with BANISHED_CELL_DUMP set, so Map::Serialize's
# load arm writes cells_raw.txt (as saved, before the load fix-up pass) and cells_fixed.txt
# (after it) into <outdir>, then quits through the pause menu. BANISHED_CELL_TRACE is on too,
# so the run's footprintCount wraps and re-sorts land in <outdir>/standalone_error.log.
# Compare two dumps with tools/cell_dump/cell_diff.py. README.md alongside.
#
# usage: tools/cell_dump/cell_dump.sh <save-name | path/to/file.sav> <outdir> [seconds]
#   A path is copied into the save directory as CellDump_<basename>.sav for the run and
#   removed afterwards; a bare name loads <save dir>/<name>.sav as it is.
# The run is FAITHFUL (/profile:faithful: every fix and extension off) unless CELL_DUMP_PROFILE
# names another profile (CELL_DUMP_PROFILE=settings: the settings.cfg keys, fixes on by default).
set -u
G="${BANISHED_DIR:-C:/GOG Games/Banished}"
command -v cygpath >/dev/null 2>&1 && G="$(cygpath -u "$G")"
if [ -n "${BANISHED_SAVE_DIR:-}" ]; then S="$BANISHED_SAVE_DIR"
elif [ -n "${USERPROFILE:-}" ]; then S="$USERPROFILE/Documents/Banished/Save"
else S="$HOME/.local/share/Banished/Save"; fi
command -v cygpath >/dev/null 2>&1 && S="$(cygpath -u "$S")"
ARG="${1:?usage: cell_dump.sh <save-name | file.sav> <outdir> [seconds]}"
OUT="${2:?usage: cell_dump.sh <save-name | file.sav> <outdir> [seconds]}"
SECS="${3:-300}"
EXE="${CELL_DUMP_EXE:-build/cmake-debug/OpenBA.exe}"
mkdir -p "$OUT"
OUT_ABS="$(cd "$OUT" && pwd)"
OUT_WIN="$OUT_ABS"
command -v cygpath >/dev/null 2>&1 && OUT_WIN="$(cygpath -m "$OUT_ABS")"

TEMP_SAVE=""
if [ -f "$ARG" ]; then
  NAME="CellDump_$(basename "$ARG" .sav)"
  cp -f "$ARG" "$S/$NAME.sav" || { echo "[cell_dump] cannot copy $ARG into $S"; exit 2; }
  TEMP_SAVE="$S/$NAME.sav"
else
  NAME="$ARG"
  [ -f "$S/$NAME.sav" ] || { echo "[cell_dump] no such save: $S/$NAME.sav"; exit 2; }
fi

cp -f "$EXE" "$G/OpenBA.exe" || { echo "[cell_dump] exe busy, is a game running?"; exit 2; }
rm -f "$G/standalone_debug.log" "$G/standalone_error.log" "$G/standalone_crash.log" "$G/standalone_boot_state.txt"
rm -f "$OUT_ABS/cells_raw.txt" "$OUT_ABS/cells_fixed.txt"

cd "$G"
env BANISHED_AUTOLOAD="$NAME" BANISHED_CELL_DUMP="$OUT_WIN" BANISHED_CELL_TRACE=1 \
    BANISHED_PAUSEMENU="${CELL_DUMP_QUIT_FRAME:-300}" BANISHED_PAUSEMENU_QUIT=120 \
    MSYS2_ARG_CONV_EXCL="/profile:" ./OpenBA.exe "/profile:${CELL_DUMP_PROFILE:-faithful}" &
PID=$!
for i in $(seq 1 "$SECS"); do kill -0 $PID 2>/dev/null || break; sleep 1; done
if kill -0 $PID 2>/dev/null; then echo "[cell_dump] alive after ${SECS}s, killing"; kill -9 $PID; fi
wait $PID 2>/dev/null; RC=$?
cd - >/dev/null
[ -n "$TEMP_SAVE" ] && rm -f "$TEMP_SAVE"

echo "[cell_dump] exit status: $RC"
for f in standalone_error.log standalone_crash.log; do
  [ -f "$G/$f" ] && cp -f "$G/$f" "$OUT_ABS/$f"
done
grep -h "\[CellDump\]\|\[CellTrace\]\|\[TerrainResort\]" "$OUT_ABS/standalone_error.log" 2>/dev/null | head -40
[ -f "$OUT_ABS/cells_raw.txt" ] || { echo "[cell_dump] FAIL: no dump (the load never reached Map::Serialize)"; exit 1; }
echo "[cell_dump] done: $OUT_ABS/cells_raw.txt, $OUT_ABS/cells_fixed.txt"

#!/bin/bash
# Convenience launcher: builds the app if needed, then opens it.
#   tools/macos/run_openba.sh                 normal launch (folder picker on first run)
#   tools/macos/run_openba.sh --game-dir DIR  use this Banished (or WinData) folder
#   tools/macos/run_openba.sh --background    small unobtrusive window, no focus steal
# Extra arguments go to the engine. Logs: ~/Library/Application Support/OpenBA/Run/standalone_*.log
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/dist/OpenBA.app"
[[ -x "$APP/Contents/MacOS/OpenBA" ]] || "$ROOT/tools/macos/make_app.sh"
ARGS=(); for a in "$@"; do
  if [[ "$a" == --background ]]; then export BANISHED_BACKGROUND=1; else ARGS+=("$a"); fi
done
exec "$APP/Contents/MacOS/OpenBA" ${ARGS[@]+"${ARGS[@]}"}

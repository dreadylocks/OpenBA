#!/usr/bin/env bash
# tools/devloop.sh -- rebuild, relaunch, carry on where you were (docs/extensions/DEVLOOP.md).
#
# A developer loop, not a player feature. A build finishes -> build/devloop.stamp changes -> the
# running game is asked to autosave and quit (the BANISHED_DEVLOOP harness) -> the new build is
# swapped in -> the game is relaunched with BANISHED_AUTOLOAD=1 and the camera it left at.
# `watch` also follows a git branch, so one push restarts the game on every machine that watches it.
#
# usage: tools/devloop.sh <command> [--cfg FILE]
#   watch     long-running. Launches the game (or adopts a running one), then every POLL_SECONDS:
#             git fetch; when origin/$BRANCH differs from the last commit built, pull --ff-only,
#             build, stamp, restart. A stamp written by `build` from another terminal restarts too.
#   build     build this checkout and write the stamp (local edits; a running `watch` picks it up)
#   restart   one restart into the stamped build, without a watcher (no exit code: see below)
#   launch    launch the stamped build (nothing running)
#   stop      ask the running game to save and quit (no relaunch)
#   status    the stamp, the harness state file, the process
#   config    print the effective configuration
#
# Config: shell KEY=value lines, default ~/.config/openba/devloop.cfg (per machine, never in the
# tree; tools/devloop.cfg.example lists every key). --cfg FILE or DEVLOOP_CFG picks another.
#
# The contract with the in-game harness (src/diag/devloop_harness.cpp, BANISHED_DEVLOOP=<dir>):
#   we write   <dir>/request      one line "<commit> <subject>", atomically (request.tmp + mv)
#   game write <dir>/state        saving -> quitting (then exit 0); after a relaunch "running <commit>"
#              (<commit> = the exe's OPENBA_GIT_HASH, `git rev-parse --short=9`, "+" when dirty)
# The camera comes back by itself: the save stores it. An exe without the harness (no
# "BANISHED_DEVLOOP" string in it) is simply killed and relaunched (stage 0).
#
# Exit codes of the GAME, as the watcher reads them: 0 after our request = a clean restart;
# 3 = crash (standalone_crash.log), 4 = hang (standalone_hang.log), anything else, or any exit we
# did not ask for = reported loudly and NOT relaunched over. Only `watch` owns the game process and
# so knows its exit code; `restart` / `stop` from another shell only see that it went away.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) OS=win;;
  Darwin) OS=mac;;
  *) OS=other;;
esac

# ---------------------------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------------------------
say()  { printf '[devloop %s] %s\n' "$(date +%H:%M:%S)" "$*"; }
loud() { # a crash, a failed build: impossible to miss in a scrolling terminal
  printf '\n[devloop %s] ######## %s\n' "$(date +%H:%M:%S)" "$1"
  shift; local l; for l in "$@"; do printf '[devloop]   %s\n' "$l"; done; printf '\n'
}
die()  { say "error: $*" >&2; exit 1; }

notify() { # OS notification; never blocks, never fails the loop
  [ "${NOTIFY:-1}" = 1 ] || return 0
  local msg="$1"
  case "$OS" in
    mac) osascript -e "display notification \"${msg//\"/\'}\" with title \"OpenBA devloop\"" >/dev/null 2>&1 & ;;
    win) # NotifyIcon balloon: System.Windows.Forms, no module to install. Detached so it never stalls us.
         powershell.exe -NoProfile -WindowStyle Hidden -Command "
           Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing;
           \$n = New-Object System.Windows.Forms.NotifyIcon; \$n.Icon = [System.Drawing.SystemIcons]::Information;
           \$n.Visible = \$true; \$n.ShowBalloonTip(6000, 'OpenBA devloop', '${msg//\'/\'\'}', 'Info');
           Start-Sleep -Seconds 7; \$n.Dispose()" >/dev/null 2>&1 & ;;
  esac
  return 0
}

# ---------------------------------------------------------------------------------------------
# configuration
# ---------------------------------------------------------------------------------------------
CMD="${1:-}"; [ $# -gt 0 ] && shift
CFG="${DEVLOOP_CFG:-$HOME/.config/openba/devloop.cfg}"
while [ $# -gt 0 ]; do
  case "$1" in
    --cfg) CFG="$2"; shift 2;;
    *) die "unknown argument $1";;
  esac
done
# shellcheck disable=SC1090
[ -f "$CFG" ] && . "$CFG"

REMOTE="${REMOTE:-origin}"
BRANCH="${BRANCH:-claude/devloop}"
DEVLOOP_WORKTREE="${DEVLOOP_WORKTREE:-.}"      # "." or empty = build in this checkout
POLL_SECONDS="${POLL_SECONDS:-10}"
REQUEST_TIMEOUT="${REQUEST_TIMEOUT:-30}"      # request written -> state must say "saving"
SAVE_TIMEOUT="${SAVE_TIMEOUT:-60}"            # "saving" -> the process must have exited
BOOT_WAIT="${BOOT_WAIT:-300}"                 # relaunch -> state must say "running <commit>"
EXTRA_ARGS="${EXTRA_ARGS:-}"                  # passed to the exe; no /profile:faithful by default
EXTRA_ENV="${EXTRA_ENV:-}"                    # extra KEY=value words for the game's environment
NOTIFY="${NOTIFY:-1}"
BUILD_JOBS="${BUILD_JOBS:-8}"; export BUILD_JOBS

if [ "$OS" = win ]; then
  # Git Bash puts its own /mingw64/bin first, and the UCRT64 GCC then dies silently (exit 1, no
  # message) on every compile. The build wants MSYS2 UCRT64 first (docs/reference/environment.md).
  [ -d /c/msys64/ucrt64/bin ] && PATH="/c/msys64/ucrt64/bin:$PATH"
  FLAVOUR="${FLAVOUR:-debug}"
  GAME_DIR="${GAME_DIR:-${BANISHED_DIR:-C:/GOG Games/Banished}}"
  winpath() { cygpath -m "$1" 2>/dev/null || printf '%s' "$1"; }
  if [ -z "${SAVE_DIR:-}" ]; then
    docs="$(powershell.exe -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')" 2>/dev/null | tr -d '\r')"
    [ -n "$docs" ] || docs="$USERPROFILE/Documents"
    SAVE_DIR="$(cygpath -u "$docs" 2>/dev/null || printf '%s' "$docs")/Banished/Save"
  fi
  DEVLOOP_DIR="${DEVLOOP_DIR:-$HOME/.openba-devloop}"
  LOG_DIR="${LOG_DIR:-$GAME_DIR}"
else
  FLAVOUR="${FLAVOUR:-release}"               # tools/macos/make_app.sh's default
  GAME_DIR="${GAME_DIR:-${BANISHED_DIR:-}}"
  winpath() { printf '%s' "$1"; }
  SAVE_DIR="${SAVE_DIR:-$HOME/Documents/Banished/Save}"
  DEVLOOP_DIR="${DEVLOOP_DIR:-$HOME/Library/Application Support/OpenBA/DevLoop}"
  LOG_DIR="${LOG_DIR:-$HOME/Library/Application Support/OpenBA/Run}"
fi

if [ -z "$DEVLOOP_WORKTREE" ] || [ "$DEVLOOP_WORKTREE" = . ]; then WT="$ROOT"
else case "$DEVLOOP_WORKTREE" in /*|?:/*) WT="$DEVLOOP_WORKTREE";; *) WT="$ROOT/$DEVLOOP_WORKTREE";; esac
fi
STAMP="$WT/build/devloop.stamp"
DL_GAME="$(winpath "$DEVLOOP_DIR")"           # the absolute path the game is given
REQ="$DEVLOOP_DIR/request"; STATE="$DEVLOOP_DIR/state"; GAME_OUT="$DEVLOOP_DIR/game.out"
GOOD="$DEVLOOP_DIR/Auto.sav.good"
# the launcher execs the engine under the bundle's own name: .../dist/OpenBA.app/Contents/MacOS/OpenBA
MAC_PROC="dist/OpenBA.app/Contents/MacOS/OpenBA"
LIVE="$DEVLOOP_DIR/running.stamp"             # copy of the stamp the running game was launched from
AUTO="$SAVE_DIR/Auto.sav"

show_config() {
  cat <<EOF
config file       $CFG $( [ -f "$CFG" ] || echo "(absent: defaults)")
platform          $OS
checkout          $WT  (branch $BRANCH on $REMOTE)
flavour           $FLAVOUR
game dir          ${GAME_DIR:-<unset>}
save dir          $SAVE_DIR
devloop dir       $DEVLOOP_DIR  (game sees $DL_GAME)
logs              $LOG_DIR
stamp             $STAMP
timeouts          poll ${POLL_SECONDS}s, request ${REQUEST_TIMEOUT}s, save ${SAVE_TIMEOUT}s, boot ${BOOT_WAIT}s
extra args        ${EXTRA_ARGS:-<none>}
extra env         ${EXTRA_ENV:-<none>}
EOF
}

# ---------------------------------------------------------------------------------------------
# the stamp: commit, time, artifact -- written LAST and atomically, the one thing a watcher reads
# ---------------------------------------------------------------------------------------------
stamp_line() { [ -f "$1" ] && sed -n "${2}p" "$1" | tr -d '\r'; }
stamp_commit() { stamp_line "${1:-$STAMP}" 1; }
stamp_artifact() { stamp_line "${1:-$STAMP}" 3; }
commit_subject() { git -C "$WT" log -1 --format=%s "$1" 2>/dev/null; }
same_commit() { # compare on the first 9 hex digits, a trailing "+" (dirty) stripped
  local a="${1%+}" b="${2%+}"
  [ -n "$a" ] && [ -n "$b" ] && [ "${a:0:9}" = "${b:0:9}" ]
}
artifact_binary() { # the file to look into for the harness
  case "$OS" in mac) printf '%s/Contents/MacOS/OpenBA-engine' "$1";; *) printf '%s' "$1";; esac
}
has_harness() { grep -aqF BANISHED_DEVLOOP "$(artifact_binary "$1")" 2>/dev/null; }

do_build() { # -> 0 and a new stamp, or 1
  local commit dirty t0 t1 artifact
  commit="$(git -C "$WT" rev-parse --short=9 HEAD)" || return 1
  dirty=""; [ -n "$(git -C "$WT" status --porcelain --untracked-files=no)" ] && dirty="+"
  say "building $commit$dirty ($FLAVOUR) in $WT"
  t0=$(date +%s)
  if [ "$OS" = win ]; then
    ( cd "$WT" && tools/build.sh build "$FLAVOUR" ) || return 1
    artifact="$WT/$(cd "$WT" && tools/build.sh exe "$FLAVOUR")"
  elif [ "$OS" = mac ]; then
    # Never the .app that is running: build a staging bundle, swap whole bundles after the exit.
    artifact="$WT/dist/OpenBA.staging.app"
    ( cd "$WT" && FLAVOUR="$FLAVOUR" tools/macos/make_app.sh "$artifact" ) || return 1
  else
    die "devloop runs on Windows (MSYS2 / Git Bash) and macOS"
  fi
  t1=$(date +%s)
  mkdir -p "$WT/build"
  printf '%s\n%s\n%s\n' "$commit$dirty" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$artifact" > "$STAMP.tmp" &&
    mv -f "$STAMP.tmp" "$STAMP"
  say "built $commit$dirty in $((t1 - t0))s -> $artifact"
}

# ---------------------------------------------------------------------------------------------
# the game process
# ---------------------------------------------------------------------------------------------
GAME_PID=""        # set only when THIS shell launched it (then `wait` gives the exit code)
REQUESTED=0        # 1 while a restart / stop we asked for is in flight

win_game_pids() { tasklist //FI "IMAGENAME eq OpenBA.exe" //FO CSV //NH 2>/dev/null | tr -d '\r' |
                  sed -n 's/^"OpenBA.exe","\([0-9]*\)".*/\1/p'; }
game_running() {
  if [ -n "$GAME_PID" ]; then kill -0 "$GAME_PID" 2>/dev/null && return 0; fi
  case "$OS" in
    win) [ -n "$(win_game_pids)" ];;
    mac) pgrep -f "$MAC_PROC" >/dev/null 2>&1;;
    *) return 1;;
  esac
}
kill_game() {
  say "killing the game"
  case "$OS" in
    win) local p; for p in $(win_game_pids); do taskkill //F //PID "$p" >/dev/null 2>&1; done;;
    mac) pkill -9 -f "$MAC_PROC" 2>/dev/null;;
  esac
  [ -n "$GAME_PID" ] && kill -9 "$GAME_PID" 2>/dev/null
  local i; for i in 1 2 3 4 5 6 7 8 9 10; do game_running || return 0; sleep 1; done
  return 1
}

read_state() { [ -f "$STATE" ] && head -1 "$STATE" | tr -d '\r'; }

# Ask the game to save and quit; wait for it; kill as the fallback. -> 0 clean, 1 had to kill
request_quit() { # $1 = commit for the banner, "" for a plain stop
  local commit="$1" line st t
  mkdir -p "$DEVLOOP_DIR"
  [ -f "$AUTO" ] && cp -f "$AUTO" "$GOOD.tmp" && mv -f "$GOOD.tmp" "$GOOD"
  if [ -n "$commit" ]; then line="$commit $(commit_subject "${commit%+}")"
  else line="stop"; fi
  # the game shows it in an event-log row: printable ASCII, 120 characters
  line="$(printf '%s' "$line" | tr -cd ' -~' | cut -c1-120)"
  if [ -f "$LIVE" ] && ! has_harness "$(stamp_artifact "$LIVE")"; then
    say "the running exe has no BANISHED_DEVLOOP harness: stage 0, killing it"
    notify "Killing OpenBA to load the new build${commit:+ $commit}"
    kill_game; restore_good; return 1
  fi
  rm -f "$STATE"
  printf '%s\n' "$line" > "$REQ.tmp" && mv -f "$REQ.tmp" "$REQ"
  REQUESTED=1
  say "asked the game to save and quit (${line})"
  # phase 1: the harness acknowledges with "saving" (or is already past it)
  t=$(( $(date +%s) + REQUEST_TIMEOUT ))
  while game_running; do
    st="$(read_state)"
    case "$st" in saving*|quitting*) break;; esac
    if [ "$(date +%s)" -ge "$t" ]; then
      loud "the game did not answer the restart request in ${REQUEST_TIMEOUT}s" \
           "(in a menu or a modal, or an exe without the BANISHED_DEVLOOP harness)" \
           "killing it; the autosave is restored from the last good copy"
      notify "OpenBA did not answer: killing it to load the new build${commit:+ $commit}"
      rm -f "$REQ"; kill_game; restore_good; return 1
    fi
    sleep 1
  done
  # phase 2: the save finishes and the process exits
  t=$(( $(date +%s) + SAVE_TIMEOUT ))
  while game_running; do
    if [ "$(date +%s)" -ge "$t" ]; then
      loud "the game said '$(read_state)' but had not exited after ${SAVE_TIMEOUT}s" "killing it"
      notify "OpenBA did not finish saving: killing it"
      kill_game; restore_good; return 1
    fi
    sleep 1
  done
  rm -f "$REQ"
  say "the game saved and quit ($(read_state))"
  return 0
}

restore_good() {
  [ -f "$GOOD" ] || return 0
  cp -f "$GOOD" "$AUTO" && say "restored Auto.sav from $GOOD"
}

# Put the stamped artifact in place. The game must have exited.
swap_in() {
  local art; art="$(stamp_artifact)"
  [ -n "$art" ] && [ -e "$art" ] || { loud "no build to swap in (stamp: $STAMP)"; return 1; }
  case "$OS" in
    win)
      [ -d "$GAME_DIR" ] || { loud "game dir not found: $GAME_DIR (set GAME_DIR in $CFG)"; return 1; }
      mkdir -p "$DEVLOOP_DIR"
      [ -f "$GAME_DIR/OpenBA.exe" ] && cp -f "$GAME_DIR/OpenBA.exe" "$DEVLOOP_DIR/OpenBA.prev.exe" 2>/dev/null
      local i; for i in 1 2 3 4 5; do cp -f "$art" "$GAME_DIR/OpenBA.exe" 2>/dev/null && break; sleep 1; done
      cmp -s "$art" "$GAME_DIR/OpenBA.exe" || { loud "could not copy the exe into $GAME_DIR (busy, or no write access)"; return 1; }
      ;;
    mac)
      mkdir -p "$DEVLOOP_DIR"
      local app="$WT/dist/OpenBA.app" old="$WT/dist/OpenBA.old.app"
      if [ "$art" != "$app" ]; then
        rm -rf "$old"
        [ -d "$app" ] && mv "$app" "$old"
        mv "$art" "$app" || { [ -d "$old" ] && mv "$old" "$app"; loud "could not move $art into place"; return 1; }
        # the stamp now names the bundle in its final place
        printf '%s\n%s\n%s\n' "$(stamp_commit)" "$(stamp_line "$STAMP" 2)" "$app" > "$STAMP.tmp" && mv -f "$STAMP.tmp" "$STAMP"
      fi
      ;;
  esac
  cp -f "$STAMP" "$LIVE"
  say "swapped in $(stamp_commit)"
}

# Start the game in the background; GAME_PID is the exe itself (exec), so `wait` reads its exit code.
launch_game() { # $1 = 1 to autoload the autosave
  local autoload="$1"
  mkdir -p "$DEVLOOP_DIR"
  rm -f "$STATE" "$REQ"
  local -a envs=("BANISHED_DEVLOOP=$DL_GAME")
  [ "$autoload" = 1 ] && [ -f "$AUTO" ] && envs+=("BANISHED_AUTOLOAD=1")
  # The game's command line and environment are re-read from the cfg before every launch, so an
  # edit there applies at the next restart without restarting the watcher.
  if [ -f "$CFG" ]; then
    EXTRA_ENV=""
    EXTRA_ARGS=""
    eval "$(grep -E '^(EXTRA_ENV|EXTRA_ARGS)=' "$CFG")"
  fi
  # shellcheck disable=SC2206
  [ -n "$EXTRA_ENV" ] && envs+=($EXTRA_ENV)
  # shellcheck disable=SC2206
  local -a extra=($EXTRA_ARGS)
  say "launching $(stamp_commit "$LIVE") ${envs[*]:1}"
  case "$OS" in
    win)
      ( cd "$GAME_DIR" && exec env MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1 "${envs[@]}" ./OpenBA.exe ${extra[@]+"${extra[@]}"} >"$GAME_OUT" 2>&1 ) &
      ;;
    mac)
      [ -n "$GAME_DIR" ] || { loud "GAME_DIR is not set (the Banished / WinData folder; set it in $CFG)"; return 1; }
      ( exec env "${envs[@]}" "$WT/tools/macos/run_openba.sh" --game-dir "$GAME_DIR" ${extra[@]+"${extra[@]}"} >"$GAME_OUT" 2>&1 ) &
      ;;
  esac
  GAME_PID=$!
  REQUESTED=0
}

# After a launch: wait for the harness to say "running <commit>". -> 0 up, 1 died, 2 blocked / timed out
health_check() {
  local want st t bs rc
  want="$(stamp_commit "$LIVE")"
  local harness=1; has_harness "$(stamp_artifact "$LIVE")" || harness=0
  [ $harness = 1 ] || say "this exe has no BANISHED_DEVLOOP harness: health = the process (and boot_state on Windows)"
  t=$(( $(date +%s) + BOOT_WAIT ))
  local t_alive=$(( $(date +%s) + 20 ))
  while :; do
    st="$(read_state)"
    case "$st" in
      running*)
        if same_commit "${st#running }" "$want"; then
          say "UP: running ${st#running } (in game, save loaded)"
          [ -f "$AUTO" ] && cp -f "$AUTO" "$GOOD.tmp" && mv -f "$GOOD.tmp" "$GOOD"
          [ "$OS" = mac ] && rm -rf "$WT/dist/OpenBA.old.app"
          notify "Reloaded ${st#running }"
          return 0
        fi
        loud "SWAP FAILED: the game says '$st' but the build is $want"; return 2;;
    esac
    if ! game_running; then return 1; fi
    if [ "$OS" = win ]; then
      bs="$(BANISHED_DIR="$GAME_DIR" "$ROOT/tools/boot_state.sh" 2>/dev/null)"; rc=$?
      case $rc in
        2|5) loud "the relaunched game is stuck" "$bs"; return 2;;
        0) [ $harness = 0 ] && { say "UP (boot_state RUNNING; no harness, so 'save loaded' is not known)"; return 0; };;
      esac
    elif [ $harness = 0 ] && [ "$(date +%s)" -ge "$t_alive" ]; then
      say "UP (alive for 20s; no harness, so 'save loaded' is not known)"; return 0
    fi
    if [ "$(date +%s)" -ge "$t" ]; then
      loud "the relaunched game was not in game after ${BOOT_WAIT}s (state: '${st:-none}')"; return 2
    fi
    sleep 2
  done
}

report_exit() { # $1 = exit code (or "?")
  local code="$1" f=""
  case "$code" in
    3) f="$LOG_DIR/standalone_crash.log";;
    4) f="$LOG_DIR/standalone_hang.log";;
  esac
  local -a lines=("exit code $code, running $(stamp_commit "$LIVE" 2>/dev/null)" "not relaunching; the next build will launch it again")
  if [ -n "$f" ] && [ -f "$f" ]; then
    lines+=("first lines of $f:"); while IFS= read -r l; do lines+=("  $l"); done < <(head -25 "$f")
  else
    lines+=("tail of $LOG_DIR/standalone_error.log:")
    while IFS= read -r l; do lines+=("  $l"); done < <(tail -15 "$LOG_DIR/standalone_error.log" 2>/dev/null)
  fi
  case "$code" in
    3) loud "THE GAME CRASHED" "${lines[@]}"; notify "OpenBA crashed (exit 3): see the devloop terminal";;
    4) loud "THE GAME HUNG" "${lines[@]}"; notify "OpenBA hung (exit 4): see the devloop terminal";;
    0) loud "the game quit on its own (exit 0)" "${lines[@]}";;
    *) loud "THE GAME EXITED UNEXPECTEDLY" "${lines[@]}"; notify "OpenBA exited unexpectedly (exit $code)";;
  esac
}

# One restart into the stamped build. -> 0 up, 1 not
restart_into_stamp() {
  local commit; commit="$(stamp_commit)"
  [ -n "$commit" ] || { loud "no stamp yet: run tools/devloop.sh build"; return 1; }
  if game_running; then
    notify "New build $commit: saving and restarting"
    request_quit "$commit"
    if [ -n "$GAME_PID" ]; then wait "$GAME_PID" 2>/dev/null; GAME_PID=""; fi
  fi
  swap_in || return 1
  launch_game 1 || return 1
  health_check; local rc=$?
  if [ $rc = 1 ]; then
    local code="?"; if [ -n "$GAME_PID" ]; then wait "$GAME_PID"; code=$?; GAME_PID=""; fi
    report_exit "$code"; restore_good; return 1
  fi
  return 0
}

# ---------------------------------------------------------------------------------------------
# watch
# ---------------------------------------------------------------------------------------------
LOCK="$DEVLOOP_DIR/watch.pid"
take_lock() {
  mkdir -p "$DEVLOOP_DIR"
  if [ -f "$LOCK" ]; then
    local p; p="$(cat "$LOCK" 2>/dev/null)"
    if [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then die "a watcher is already running (pid $p)"; fi
  fi
  echo $$ > "$LOCK"
  trap 'rm -f "$LOCK"; say "watcher stopped (the game keeps running)"; exit 0' INT TERM
}

FAILED_COMMIT=""; WARNED=""
warn_once() { [ "$WARNED" = "$1" ] && return; WARNED="$1"; say "$2"; }

# Follow origin/$BRANCH. -> 0 when there is a new commit checked out to build
follow_branch() {
  git -C "$WT" fetch -q "$REMOTE" "$BRANCH" 2>/dev/null || { warn_once fetch "git fetch $REMOTE $BRANCH failed (offline?)"; return 1; }
  local want head built cur
  want="$(git -C "$WT" rev-parse "$REMOTE/$BRANCH" 2>/dev/null)" || return 1
  built="$(stamp_commit)"
  same_commit "$want" "$built" && return 1                 # already built (and stamped)
  [ "$FAILED_COMMIT" = "$want" ] && return 1               # its build failed; wait for the next push
  cur="$(git -C "$WT" rev-parse --abbrev-ref HEAD)"
  [ "$cur" = "$BRANCH" ] || { warn_once "branch:$cur" "the checkout is on $cur, not $BRANCH: not following"; return 1; }
  head="$(git -C "$WT" rev-parse HEAD)"
  if [ "$head" != "$want" ]; then
    if [ -n "$(git -C "$WT" status --porcelain --untracked-files=no)" ]; then
      warn_once "dirty:$want" "origin/$BRANCH moved to ${want:0:12}, but the checkout has local changes: not pulling"
      return 1
    fi
    if ! git -C "$WT" merge -q --ff-only "$REMOTE/$BRANCH" 2>/dev/null; then
      warn_once "diverged:$want" "origin/$BRANCH (${want:0:12}) does not fast-forward from HEAD: not pulling (rebase, then push)"
      return 1
    fi
    say "pulled ${want:0:12} $(commit_subject "$want")"
  fi
  # HEAD == origin/$BRANCH here (pulled, or the editing machine just pushed it): build it,
  # local uncommitted edits or not -- the stamp marks those +dirty.
  return 0
}

cmd_watch() {
  [ "$OS" = win ] || [ "$OS" = mac ] || die "devloop runs on Windows (MSYS2 / Git Bash) and macOS"
  take_lock
  SELF="$ROOT/tools/devloop.sh"
  SELF_SUM="$(cksum < "$SELF")"
  say "watching $REMOTE/$BRANCH every ${POLL_SECONDS}s; checkout $WT; game $GAME_DIR"
  say "devloop dir $DEVLOOP_DIR; Ctrl+C stops the watcher, not the game"
  # first build, when there is none
  if [ -z "$(stamp_commit)" ]; then do_build || die "the first build failed"; fi
  if game_running; then
    say "a game is already running: adopting it (its exit code will not be visible until the next restart)"
    [ -f "$LIVE" ] || cp -f "$STAMP" "$LIVE"
  else
    swap_in && launch_game 1 && { health_check; [ $? = 1 ] && { wait "$GAME_PID"; report_exit $?; GAME_PID=""; }; }
  fi
  local live_seen dead_reported=0
  while :; do
    # 1. did the game go away on its own?
    if [ -n "$GAME_PID" ] && ! kill -0 "$GAME_PID" 2>/dev/null; then
      wait "$GAME_PID"; local code=$?; GAME_PID=""
      [ "$REQUESTED" = 1 ] || report_exit "$code"
      dead_reported=1
    elif [ -z "$GAME_PID" ] && [ $dead_reported = 0 ] && ! game_running; then
      say "the game is not running (closed by hand?); the next build will launch it"; dead_reported=1
    fi
    # 2. a new commit on the branch -> build it
    if follow_branch; then
      local want; want="$(git -C "$WT" rev-parse --short=9 HEAD)"
      if ! do_build; then
        FAILED_COMMIT="$(git -C "$WT" rev-parse HEAD)"
        loud "BUILD FAILED at $want $(commit_subject "$want")" "the game keeps running the old build; waiting for the next push"
        notify "OpenBA build failed at $want"
      fi
    fi
    # 3. a stamp newer than the running build -> restart into it
    live_seen="$(stamp_commit "$LIVE")@$(stamp_line "$LIVE" 2)"
    if [ "$(stamp_commit)@$(stamp_line "$STAMP" 2)" != "$live_seen" ]; then
      restart_into_stamp && dead_reported=0
      cp -f "$STAMP" "$LIVE" 2>/dev/null    # tried once; a failed boot waits for the next build
    fi
    # 4. a pull changed this script: bash keeps running the old code, so start the new one.
    #    It adopts the running game (that game's exit code is then not visible).
    if [ "$(cksum < "$SELF")" != "$SELF_SUM" ]; then
      say "tools/devloop.sh changed: restarting the watcher on the new version"
      rm -f "$LOCK"
      exec bash "$SELF" watch --cfg "$CFG"
    fi
    sleep "$POLL_SECONDS"
  done
}

cmd_status() {
  echo "stamp:     $( [ -f "$STAMP" ] && tr '\n' ' ' < "$STAMP" || echo none)"
  echo "launched:  $( [ -f "$LIVE" ] && tr '\n' ' ' < "$LIVE" || echo none)"
  echo "state:     $(read_state || true)"
  echo "request:   $(cat "$REQ" 2>/dev/null || echo none)"
  echo "watcher:   $( [ -f "$LOCK" ] && kill -0 "$(cat "$LOCK")" 2>/dev/null && echo "pid $(cat "$LOCK")" || echo none)"
  if game_running; then echo "game:      running"; else echo "game:      not running"; fi
  [ "$OS" = win ] && echo "boot:      $(BANISHED_DIR="$GAME_DIR" "$ROOT/tools/boot_state.sh" 2>/dev/null | head -1)"
  return 0
}

case "$CMD" in
  watch)   cmd_watch;;
  build)   do_build || { loud "BUILD FAILED"; exit 1; };;
  restart) if [ -f "$LOCK" ] && kill -0 "$(cat "$LOCK")" 2>/dev/null; then
             die "a watcher is running (pid $(cat "$LOCK")): run 'tools/devloop.sh build' and it restarts the game"
           fi
           restart_into_stamp || exit 1
           disown -a 2>/dev/null; say "the game keeps running after this script exits";;
  launch)  game_running && die "the game is already running"
           swap_in && launch_game 1 && health_check; rc=$?; disown -a 2>/dev/null; exit $rc;;
  stop)    game_running || die "the game is not running"; request_quit "";;
  status)  cmd_status;;
  config)  show_config;;
  ""|-h|--help|help) sed -n '2,32p' "$0";;
  *) echo "unknown command $CMD"; sed -n '2,32p' "$0"; exit 2;;
esac

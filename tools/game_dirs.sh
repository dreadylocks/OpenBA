#!/usr/bin/env bash
# =============================================================================
# tools/game_dirs.sh -- a pool of TWO game directories, one lock and one user dir each
# =============================================================================
#
# WHY THIS EXISTS
#
# tools/game_dir_lock.sh serialises deploy+run over one game directory, so a second
# copy of the install (its own OpenBA.exe, logs and .lane_lock) lets two agents run
# at once. What a second copy does NOT separate is the game's USER DATA: settings.cfg,
# Save\ and the derived Cache\ live in <Documents>\Banished whatever directory the exe
# was launched from (FileSystem::GetSaveDirectory, IO::SettingsStore::DefaultPath).
# Two concurrent runs from two game dirs clobber one settings.cfg (the harness guard
# and the Settings dialog both write it), overwrite each other's Auto.sav, and fight
# over the cache's exclusive lock. BANISHED_USER_DIR (src/app/options.h, harness
# section) moves that whole folder: the functional dir gets a user dir of its own.
#
# THE TWO ROLES
#
#   perf        C:/GOG Games/Banished        user dir = the real <Documents>\Banished
#   functional  C:/GOG Games/Banished-B      user dir = <dir>/_userdata
#
# perf is THE game directory: the gate (tools/build.sh run), the resave gate and every
# perf A/B run there, against the user's real settings and saves. functional is for
# runs whose numbers nobody will quote: crash / clean-quit checks, menu routes, save
# fixtures, screenshots. Its user dir is created on first use from the real one
# (settings.cfg and the Save\ fixtures the resave gate needs: Gate.sav,
# GateRestrictedFoods.sav); the Cache is never copied, the exe rebuilds its own.
#
# THE PERF RULE. A perf A/B needs the machine quiet (feedback: "benchmark on a clean
# machine"): a functional run on B during a perf run on A is the coordinator's call,
# never an agent's own, and the perf report's "machine load" line says what else was
# running -- honestly, including a functional run the coordinator allowed.
#
# USAGE
#
#   eval "$(tools/game_dirs.sh env <lane> perf|functional [timeout-seconds])"
#       Acquires the matching directory's lock (game_dir_lock.sh acquire; default
#       timeout 900 s; every lock message goes to stderr) and prints the lines to eval:
#           export BANISHED_DIR=<game dir>
#           export BANISHED_USER_DIR=<user dir>     (functional)
#           unset  BANISHED_USER_DIR                (perf: the exe's retail arm)
#       Exit 1 without printing when the lock was not granted -- check the status, or
#       test that BANISHED_DIR is set after the eval. Never pipe this script's acquire.
#   tools/game_dirs.sh release <lane>      releases whichever directory the lane holds
#   tools/game_dirs.sh status              both directories: path, user dir, lock holder
#
# SCRIPTS: anything that touches the user data reads $BANISHED_USER_DIR (default
# ${USERPROFILE}/Documents/Banished), never a hardcoded Documents/Banished: see
# tools/resave_gate/resave_gate.sh, tools/stress_run.sh, tools/ach_reset.sh. The exe
# reads the same variable once at boot (options.cpp) -- nothing else does.
#
# CONFIGURATION (environment): BANISHED_DIR_PERF, BANISHED_DIR_FUNC (the two game
# dirs); BANISHED_USER_DIR_PERF (the real user dir, when Documents is relocated).
# =============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCKSH="$HERE/game_dir_lock.sh"

PERF_DIR="${BANISHED_DIR_PERF:-C:/GOG Games/Banished}"
FUNC_DIR="${BANISHED_DIR_FUNC:-C:/GOG Games/Banished-B}"
PERF_USER="${BANISHED_USER_DIR_PERF:-${USERPROFILE:-$HOME}/Documents/Banished}"
PERF_USER="${PERF_USER//\\//}"   # USERPROFILE comes with backslashes
FUNC_USER="$FUNC_DIR/_userdata"

# The Save\ fixtures the resave gate needs (tools/resave_gate/README.md).
FIXTURES="Gate.sav GateRestrictedFoods.sav"

die() { echo "[game_dirs] $*" >&2; exit 1; }

role_dir() {
    case "$1" in
        perf)       echo "$PERF_DIR" ;;
        functional) echo "$FUNC_DIR" ;;
        *)          die "unknown role '$1' (perf|functional)" ;;
    esac
}

# The functional user dir, created on first use from the real one. Settings and the
# gate fixtures only: never the Cache (derived data; the exe rebuilds it), never the
# user's own saves.
ensure_func_user() {
    [ -d "$FUNC_DIR" ] || die "functional game dir missing: $FUNC_DIR (copy the install there first)"
    if [ ! -d "$FUNC_USER" ]; then
        mkdir -p "$FUNC_USER/Save" || die "cannot create $FUNC_USER"
        if [ -f "$PERF_USER/settings.cfg" ]; then
            cp -f "$PERF_USER/settings.cfg" "$FUNC_USER/settings.cfg"
        else
            echo "[game_dirs] note: no $PERF_USER/settings.cfg to seed from; the exe starts with defaults" >&2
        fi
        for f in $FIXTURES; do
            if [ -f "$PERF_USER/Save/$f" ]; then
                cp -f "$PERF_USER/Save/$f" "$FUNC_USER/Save/$f"
            else
                echo "[game_dirs] note: fixture $PERF_USER/Save/$f absent; not seeded" >&2
            fi
        done
        echo "[game_dirs] created $FUNC_USER (settings.cfg + gate fixtures; no Cache)" >&2
    fi
}

cmd_env() {
    local lane="${1:?usage: game_dirs.sh env <lane> perf|functional [timeout]}"
    local role="${2:?usage: game_dirs.sh env <lane> perf|functional [timeout]}"
    local timeout="${3:-900}"
    local dir
    dir="$(role_dir "$role")" || exit 1
    [ -d "$dir" ] || die "game dir missing: $dir"
    [ "$role" = functional ] && ensure_func_user
    # The lock's own output (GRANT / waiting / DENIED) goes to stderr so the eval sees
    # only the export lines. A statement of its own: a compound command would hide
    # the refusal (game_dir_lock.sh's header, "DEFEATED BY A PIPE").
    if ! BANISHED_DIR="$dir" "$LOCKSH" acquire "$lane" "$timeout" 1>&2; then
        die "lock on $dir not granted for '$lane'"
    fi
    echo "export BANISHED_DIR='$dir'"
    if [ "$role" = functional ]; then
        echo "export BANISHED_USER_DIR='$FUNC_USER'"
    else
        echo "unset BANISHED_USER_DIR"
    fi
    echo "# game_dirs: $role dir for '$lane'; release with tools/game_dirs.sh release $lane"
}

lock_owner_of() {
    cat "$1/.lane_lock/owner" 2>/dev/null || true
}

cmd_release() {
    local lane="${1:?usage: game_dirs.sh release <lane>}"
    local dir released=0
    for dir in "$PERF_DIR" "$FUNC_DIR"; do
        if [ "$(lock_owner_of "$dir")" = "$lane" ]; then
            BANISHED_DIR="$dir" "$LOCKSH" release "$lane" 1>&2 || exit 1
            released=1
        fi
    done
    [ "$released" = 1 ] || die "'$lane' holds neither $PERF_DIR nor $FUNC_DIR"
}

cmd_status() {
    local role dir user owner
    for role in perf functional; do
        dir="$(role_dir "$role")"
        if [ "$role" = perf ]; then user="$PERF_USER"; else user="$FUNC_USER"; fi
        if [ ! -d "$dir" ]; then
            echo "$role: $dir (MISSING)"
            continue
        fi
        owner="$(lock_owner_of "$dir")"
        echo "$role: $dir  user=$user  lock=${owner:-free}"
    done
}

case "${1:-}" in
    env)     shift; cmd_env "$@" ;;
    release) shift; cmd_release "$@" ;;
    status)  shift; cmd_status "$@" ;;
    *)       sed -n '/^# USAGE/,/^# SCRIPTS/p' "$0" | sed '$d' >&2; exit 2 ;;
esac

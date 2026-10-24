#!/usr/bin/env bash
# tools/ach_reset.sh -- clear the persisted achievement awards.
#
# AchievementManager::SetAchievementMet writes Persistence key "Ach<N>" = true, and
# ReadAchievements loads them back at startup. AchievementManager::Reset does NOT
# clear them (it only rebuilds the instance array), so once an achievement has been
# awarded on this machine every later run inherits it -- and the manager SKIPS
# already-awarded achievements, which makes a re-test look like a pass.
# Run this before any achievement verification run, with the game NOT running.
#
# Phase D.4 SETTINGS E.4: the settings live in <Documents>\Banished\settings.cfg
# (one key=value per line), not in the registry. This deletes the ^Ach<N>= lines.
# The registry is never touched: when settings.cfg is absent the next boot
# imports the registry once, read-only, so boot the game once first.
if [ -n "${BANISHED_USER_DIR:-}" ]; then
    CFG="$BANISHED_USER_DIR/settings.cfg"   # the pool's user dir (tools/game_dirs.sh)
else
    DOCS=$(powershell.exe -NoProfile -Command "[Environment]::GetFolderPath('MyDocuments')" | tr -d '\r')
    if [ -z "$DOCS" ]; then
        echo "ach_reset: cannot resolve the Documents folder" >&2
        exit 1
    fi
    CFG="$(cygpath -u "$DOCS")/Banished/settings.cfg"
fi
if [ ! -f "$CFG" ]; then
    echo "ach_reset: $CFG absent (boot the game once: it imports the registry, then writes the file)" >&2
    exit 1
fi
n=$(grep -cE '^Ach[0-9]+=' "$CFG")
if [ "$n" -gt 0 ]; then
    sed -i -E '/^Ach[0-9]+=/d' "$CFG"
fi
echo "cleared $n awarded achievement(s) from $CFG"

#!/usr/bin/env bash
# OpenBA.sh -- start OpenBA on Linux from the directory this script is in.
#
# Put every file of the release package next to the game's WinData folder (or make WinData
# a symlink to the retail install's WinData, see README-linux.txt) and run ./OpenBA.sh.
# The renderer is the DirectX 11 code over dxvk-native (libdxvk_d3d11.so.0 / libdxvk_dxgi.so.0,
# beside the exe), which talks to the window through SDL3: DXVK_WSI_DRIVER tells it so.
# Logs (standalone_*.log) are written to the working directory; settings and saves go to
# ~/.local/share/Banished.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
export DXVK_WSI_DRIVER=SDL3
export LD_LIBRARY_PATH="$HERE${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/OpenBA" "$@"

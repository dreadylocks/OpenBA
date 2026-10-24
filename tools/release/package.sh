#!/usr/bin/env bash
# tools/release/package.sh -- stage and archive one platform's release package.
#
# usage: tools/release/package.sh windows|linux <version> [out-dir]
#   windows  build/cmake-release/OpenBA.exe + the FrancophilSans files + README / LICENCE texts
#            -> <out>/OpenBA-v<version>.zip            (the v1.0.0 layout: files at the zip root)
#   linux    build/cmake-release/OpenBA + libSDL3.so.0 + the two dxvk-native libraries + OpenBA.sh
#            + the same texts -> <out>/OpenBA-v<version>-linux-x64.tar.gz (one top-level folder)
# Both write <archive>.sha256 beside the archive. Run from the repo root, after
# `tools/build.sh build release` (and, on Linux, tools/fetch_dxvk_native.sh). The macOS package
# is tools/macos/make_app.sh + make_release_zip.sh.
#
# AGENTS.md section 1: no game content. The staging dir is checked for *.pkg / *.pkm / WinData
# before it is archived, as make_release_zip.sh does.
set -euo pipefail
PLAT="${1:?windows|linux}"; VER="${2:?version}"; OUT="${3:-dist}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
B="${BUILD_DIR:-build/cmake-release}"
FONTS="tools/release/fonts"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

texts() { # <stage dir>
  cp -f README.md LICENSE THIRD_PARTY_NOTICES.md "$1/"
  cp -f "$FONTS/FrancophilSans.ttf" "$FONTS/FrancophilSans-Bold.ttf" "$FONTS/FrancophilSans_README.txt" "$1/"
}
no_game_content() { # <stage dir>
  local bad
  bad="$(find -L "$1" \( -iname '*.pkg' -o -iname '*.pkm' -o -iname '*.crs' -o -iname 'WinData' \) -print)"
  [ -z "$bad" ] || { printf 'Forbidden package content:\n%s\n' "$bad" >&2; exit 1; }
}
checksum() { # <archive path>
  (cd "$(dirname "$1")" && sha256sum "$(basename "$1")" > "$(basename "$1").sha256" && cat "$(basename "$1").sha256")
}

case "$PLAT" in
  windows)
    EXE="$B/OpenBA.exe"
    [ -f "$EXE" ] || { echo "missing $EXE: run tools/build.sh build release"; exit 1; }
    STAGE="$TMP/stage"; mkdir -p "$STAGE"
    cp -f "$EXE" "$STAGE/OpenBA.exe"
    texts "$STAGE"
    no_game_content "$STAGE"
    ZIP="$OUT/OpenBA-v$VER.zip"
    rm -f "$ZIP"
    # python's zipfile: present on every build machine (CMake already needs it); MSYS2 has no zip.
    (cd "$STAGE" && python3 -m zipfile -c "$ZIP" ./*) || (cd "$STAGE" && python -m zipfile -c "$ZIP" ./*)
    python3 -m zipfile -l "$ZIP" 2>/dev/null || python -m zipfile -l "$ZIP"
    checksum "$ZIP";;
  linux)
    EXE="$B/OpenBA"
    [ -f "$EXE" ] || { echo "missing $EXE: run tools/build.sh build release"; exit 1; }
    DXVK="${DXVK_NATIVE_DIR:-third_party/dxvk-native/3.1.1/usr/lib}"
    SDLSO="$(find "$B" -name 'libSDL3.so.0' | head -1)"
    [ -n "$SDLSO" ] || { echo "no libSDL3.so.0 under $B"; exit 1; }
    NAME="OpenBA-v$VER-linux-x64"
    STAGE="$TMP/$NAME"; mkdir -p "$STAGE"
    cp -f "$EXE" "$STAGE/OpenBA"
    cp -fL "$SDLSO" "$STAGE/libSDL3.so.0"
    cp -fL "$DXVK/libdxvk_d3d11.so.0" "$DXVK/libdxvk_dxgi.so.0" "$STAGE/"
    cp -f tools/release/linux/OpenBA.sh "$STAGE/OpenBA.sh"
    cp -f tools/release/linux/README-linux.txt "$STAGE/README-linux.txt"
    chmod +x "$STAGE/OpenBA" "$STAGE/OpenBA.sh"
    texts "$STAGE"
    no_game_content "$STAGE"
    TAR="$OUT/$NAME.tar.gz"
    rm -f "$TAR"
    tar -C "$TMP" -czf "$TAR" "$NAME"
    tar -tzf "$TAR"
    checksum "$TAR";;
  *) echo "usage: $0 windows|linux <version> [out-dir]"; exit 2;;
esac

#!/bin/bash
# Build and package through tools/build.sh (the single engine build entry point).
# Usage: tools/macos/make_app.sh [OUT_APP]; FLAVOUR defaults to release.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="${1:-$ROOT/dist/OpenBA.app}"
[[ "$APP" = /* ]] || APP="$ROOT/$APP"
FLAVOUR="${FLAVOUR:-release}"
MINOS=14.0   # deployment target for engine, launcher, LLVM and converter (same constant in setup_deps.sh)
AIRCONV="$ROOT/third_party/airconv_build/build/libairconv.dylib"

echo "== Checking converter dependencies with setup_deps.sh (first run is slow)"
"$ROOT/tools/macos/setup_deps.sh" ${BUILD_JOBS:+--jobs "$BUILD_JOBS"}
[[ -f "$AIRCONV" ]] || { echo "missing $AIRCONV after setup_deps.sh"; exit 1; }
cd "$ROOT"
export CMAKE_EXTRA="${CMAKE_EXTRA:-} -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINOS -DBANISHED_RENDER_BACKEND=metal"
ENGINE="$(tools/build.sh exe "$FLAVOUR")"
BUILD="$(dirname "$ENGINE")"
# build.sh configures only absent build trees; reject an incompatible cached target.
if [[ -f "$BUILD/CMakeCache.txt" ]] && ! grep -q "^CMAKE_OSX_DEPLOYMENT_TARGET:[A-Z]*=$MINOS$" "$BUILD/CMakeCache.txt"; then
  echo "Deployment target mismatch in $BUILD; clean that flavour with tools/build.sh clean $FLAVOUR first."
  exit 1
fi
tools/build.sh build "$FLAVOUR"
file "$BUILD/OpenBA" | grep -q arm64 || { echo "$BUILD/OpenBA is not arm64"; exit 1; }

# Every Mach-O we ship must target exactly MINOS: refuse stale binaries built for another target.
for binary in "$BUILD/OpenBA" "$AIRCONV"; do
  minimum="$(xcrun vtool -show-build "$binary" | awk '/minos / {print $2; exit}')"
  [[ "$minimum" == "$MINOS" ]] || {
    echo "$binary records minos '$minimum', expected $MINOS; rebuild the engine with deployment target $MINOS or rerun tools/macos/setup_deps.sh for the converter" >&2
    exit 1
  }
done

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

# 1. engine + launcher
cp "$BUILD/OpenBA" "$APP/Contents/MacOS/OpenBA-engine"
xcrun clang++ -std=c++17 -fobjc-arc -arch arm64 -mmacosx-version-min=$MINOS -O1 \
  -framework Cocoa -o "$APP/Contents/MacOS/OpenBA" "$ROOT/packaging/macos/launcher.mm"

# 2. shader converter dylib; its only deps are system libs (otool -L below proves it)
cp "$AIRCONV" "$APP/Contents/Frameworks/libairconv.dylib"
chmod u+w "$APP/Contents/Frameworks/libairconv.dylib"
install_name_tool -id "@rpath/libairconv.dylib" "$APP/Contents/Frameworks/libairconv.dylib"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP/Contents/MacOS/OpenBA-engine" 2>/dev/null || true
if otool -L "$APP/Contents/MacOS/OpenBA-engine" "$APP/Contents/Frameworks/libairconv.dylib" \
     | grep -vE '^\S|/System/Library/|/usr/lib/|@rpath/libairconv' | grep -q .; then
  echo "unexpected non-system dependency:"; otool -L "$APP/Contents/MacOS/OpenBA-engine" "$APP/Contents/Frameworks/libairconv.dylib"; exit 1
fi

# 3. icon (source is only 48x48; Lanczos-upscaled with PIL when available, else sips)
ICON_TMP="$(mktemp -d)"
trap 'rm -rf "$ICON_TMP"' EXIT
ICONSET="$ICON_TMP/OpenBA.iconset"
mkdir -p "$ICONSET"
if python3 -c "import PIL" 2>/dev/null; then
  python3 - "$ROOT/images/ba_icon.png" "$ICONSET" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGBA")
for s in (16, 32, 64, 128, 256, 512, 1024):
    im.resize((s, s), Image.LANCZOS).save("%s/t%d.png" % (sys.argv[2], s))
PY
else
  for s in 16 32 64 128 256 512 1024; do sips -z $s $s "$ROOT/images/ba_icon.png" --out "$ICONSET/t$s.png" >/dev/null; done
fi
cp "$ICONSET/t16.png" "$ICONSET/icon_16x16.png";      cp "$ICONSET/t32.png" "$ICONSET/icon_16x16@2x.png"
cp "$ICONSET/t32.png" "$ICONSET/icon_32x32.png";      cp "$ICONSET/t64.png" "$ICONSET/icon_32x32@2x.png"
cp "$ICONSET/t128.png" "$ICONSET/icon_128x128.png";   cp "$ICONSET/t256.png" "$ICONSET/icon_128x128@2x.png"
cp "$ICONSET/t256.png" "$ICONSET/icon_256x256.png";   cp "$ICONSET/t512.png" "$ICONSET/icon_256x256@2x.png"
cp "$ICONSET/t512.png" "$ICONSET/icon_512x512.png";   cp "$ICONSET/t1024.png" "$ICONSET/icon_512x512@2x.png"
rm "$ICONSET"/t*.png
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/OpenBA.icns"

# 4. Info.plist
cp "$ROOT/packaging/macos/Info.plist" "$APP/Contents/Info.plist"

# 5. Ship full licence texts before signing. Missing licences are fatal.
LICENSES="$APP/Contents/Resources/licenses"
mkdir -p "$LICENSES"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$LICENSES/"
cp "$ROOT/LICENSE" "$LICENSES/OpenBA-LICENSE"
cp "$ROOT/third_party/airconv_build/COPYING.LIB" "$LICENSES/COPYING.LIB"
cp "$ROOT/third_party/wmadec/COPYING.LGPLv2.1" "$LICENSES/wmadec-COPYING.LGPLv2.1"
cp "$ROOT/third_party/llvm15_arm64/source/llvm/LICENSE.TXT" "$LICENSES/LLVM-LICENSE.TXT"
cp "$ROOT/third_party/SDL/LICENSE.txt" "$LICENSES/SDL-LICENSE.txt"
cp "$ROOT/third_party/freetype/LICENSE.TXT" "$LICENSES/FreeType-LICENSE.TXT"
cp "$ROOT/third_party/freetype/docs/FTL.TXT" "$LICENSES/FreeType-FTL.TXT"
cp "$ROOT/src/render/dx11/ao/assao/LICENSE" "$LICENSES/ASSAO-LICENSE"

# Ad-hoc signature, inside out.
codesign --force --sign - "$APP/Contents/Frameworks/libairconv.dylib"
codesign --force --sign - "$APP/Contents/MacOS/OpenBA-engine"
codesign --force --sign - "$APP/Contents/MacOS/OpenBA"
codesign --force --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
# The launcher is compiled here with -mmacosx-version-min=$MINOS; check what was actually recorded.
launcher_minos="$(xcrun vtool -show-build "$APP/Contents/MacOS/OpenBA" | awk '/minos / {print $2; exit}')"
[[ "$launcher_minos" == "$MINOS" ]] || {
  echo "launcher records minos '$launcher_minos', expected $MINOS" >&2
  exit 1
}

# 6. no game data may ever be inside the bundle
if find "$APP" \( -iname '*.pkg' -o -iname '*.pkm' -o -iname 'WinData' -o -iname '*.ttf' \) | grep -q .; then
  echo "game data found inside the bundle, refusing"; rm -rf "$APP"; exit 1
fi
echo "built $APP ($(du -sh "$APP" | cut -f1))"

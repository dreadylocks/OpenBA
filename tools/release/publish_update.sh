#!/usr/bin/env bash
# tools/release/publish_update.sh -- build, package, sign and publish a release players update to.
#
# usage: tools/release/publish_update.sh [--platforms LIST] [--out DIR] [--dry-run] [--feed URL]
#
# ext.auto_update (docs/fork/AUTO_UPDATE.md §10). Run on a clean checkout of a commit on origin/main whose
# CMakeLists.txt VERSION has a CHANGELOG.md section and no published release yet. For each platform in
# LIST it builds the release flavour with the update feed compiled in, packages it with the existing
# scripts, writes openba-update-<platform>.txt and signs it with ~/.config/openba/update_signing.key
# (tools/release/update_sign.cpp). Then it puts everything on the GitHub release v<VERSION> of
# dreadylocks/OpenBA: created as a DRAFT on this commit when it does not exist, its notes from
# CHANGELOG.md. Once the draft has a manifest for every required platform (windows-x64, macos-arm64) it
# is published, and from then on every player's game finds it at
# https://github.com/dreadylocks/OpenBA/releases/latest/download/openba-update-<platform>.txt.
#
#   --platforms  comma list of macos, windows, linux. Default: on a Mac "macos,windows" (Windows is
#                cross-built with mingw-w64, tools/build.sh --cross); on Windows "windows"; on Linux
#                "linux". Another machine can add its platform to the same draft later: it must be on
#                the same commit.
#   --out DIR    where the packages and manifests go (default dist/update-<VERSION>).
#   --dry-run    build, package, sign and verify, but touch nothing on GitHub.
#   --feed URL   the feed compiled into the builds (default the fork's latest/download/ folder). A test
#                feed (a local server) for a dry run; a published release must keep the default.
#
# Publishing is outward-facing: run it only when you mean to ship to players.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
REPO="dreadylocks/OpenBA"
REQUIRED="windows-x64 macos-arm64"
DEFAULT_FEED="https://github.com/$REPO/releases/latest/download/"
KEY="${OPENBA_UPDATE_KEY:-$HOME/.config/openba/update_signing.key}"
PLATFORMS=""; OUT=""; DRY=0; FEED="$DEFAULT_FEED"
while [ $# -gt 0 ]; do
  case "$1" in
    --platforms) PLATFORMS="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    --dry-run) DRY=1; shift;;
    --feed) FEED="$2"; shift 2;;
    -h|--help) sed -n '2,30p' "$0"; exit 0;;
    *) echo "unknown argument $1"; exit 2;;
  esac
done
case "$(uname -s)" in
  Darwin) HOST=macos; HOST_BUILD=build/mac-release;;
  MINGW*|MSYS*|CYGWIN*) HOST=windows; HOST_BUILD=build/cmake-release;;
  *) HOST=linux; HOST_BUILD=build/cmake-release;;
esac
if [ -z "$PLATFORMS" ]; then
  case "$HOST" in macos) PLATFORMS="macos,windows";; *) PLATFORMS="$HOST";; esac
fi
die() { echo "publish_update: $*" >&2; exit 1; }

VER="$(sed -nE 's/^project\([^)]*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
[ -n "$VER" ] || die "no project(... VERSION x.y.z) in CMakeLists.txt"
TAG="v$VER"
COMMIT="$(git rev-parse HEAD)"
SHORT="$(git rev-parse --short=12 HEAD)"
OUT="${OUT:-dist/update-$VER}"
[ -f "$KEY" ] || die "no signing key at $KEY (update_sign keygen makes one; docs/fork/AUTO_UPDATE.md §5)"
[ "$DRY" = 1 ] || [ "$FEED" = "$DEFAULT_FEED" ] || die "--feed is for dry runs: a published build must use $DEFAULT_FEED"

if [ "$DRY" = 0 ]; then
  [ -z "$(git status --porcelain --untracked-files=no)" ] || die "the tree has uncommitted changes"
  git fetch -q origin main
  git merge-base --is-ancestor HEAD origin/main || die "HEAD $SHORT is not on origin/main"
  python3 tools/release/changelog_section.py "$VER" > /dev/null || die "CHANGELOG.md has no v$VER section"
  if gh release view "$TAG" --repo "$REPO" --json isDraft -q .isDraft > /dev/null 2>&1; then
    [ "$(gh release view "$TAG" --repo "$REPO" --json isDraft -q .isDraft)" = "true" ] ||
      die "$TAG is already published: bump VERSION for a new release"
    TARGET="$(gh release view "$TAG" --repo "$REPO" --json targetCommitish -q .targetCommitish)"
    case "$TARGET" in "$COMMIT"|"$SHORT"*) ;; *) die "the draft $TAG is on $TARGET, this checkout is $COMMIT";; esac
  fi
fi

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
echo "== OpenBA $VER ($SHORT): $PLATFORMS -> $OUT (feed $FEED)"

# The build tree's feed: tools/build.sh configures only absent trees, so set the cache variable on an
# existing one too.
set_feed() { # <build dir> [cmake args...]
  local b="$1"; shift
  if [ -f "$b/CMakeCache.txt" ]; then cmake -S . -B "$b" -DOPENBA_UPDATE_FEED="$FEED" "$@" > /dev/null; fi
}

# The signing tool, from the host's own build tree.
set_feed "$HOST_BUILD"
if [ ! -f "$HOST_BUILD/CMakeCache.txt" ]; then
  if [ "$HOST" = macos ]; then
    CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DBANISHED_RENDER_BACKEND=metal" \
      tools/build.sh exe release > /dev/null
  else
    CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED" tools/build.sh exe release > /dev/null
  fi
fi
cmake --build "$HOST_BUILD" -t update_sign > /dev/null
SIGN="$HOST_BUILD/update_sign"
[ -x "$SIGN" ] || SIGN="$SIGN.exe"
PUB="$("$SIGN" public "$KEY")"
grep -q "$PUB" src/ext/auto_update/update_key.h || die "the key at $KEY is not the one src/ext/auto_update/update_key.h holds"

FILES=()
manifest() { # <platform> <package path>
  local plat="$1" pkg="$2" name
  name="$(basename "$pkg")"
  "$SIGN" manifest "$KEY" "$pkg" "$VER" "$SHORT" "$plat" \
    "https://github.com/$REPO/releases/download/$TAG/$name" "$OUT/openba-update-$plat.txt" > /dev/null
  "$SIGN" verify "$PUB" "$OUT/openba-update-$plat.txt" "$pkg"
  FILES+=("$pkg" "$pkg.sha256" "$OUT/openba-update-$plat.txt")
}

IFS=',' read -r -a WANT <<< "$PLATFORMS"
for p in "${WANT[@]}"; do
  case "$p" in
    macos)
      [ "$HOST" = macos ] || die "the macOS package is built on a Mac"
      echo "== macOS arm64"
      set_feed build/mac-release
      CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED" tools/macos/make_app.sh "$OUT/OpenBA.app"
      tools/macos/make_release_zip.sh "$OUT/OpenBA.app" "$OUT"
      mv -f "$OUT/OpenBA-mac-arm64.zip" "$OUT/OpenBA-v$VER-mac-arm64.zip"
      rm -f "$OUT/OpenBA-mac-arm64.zip.sha256"
      (cd "$OUT" && shasum -a 256 "OpenBA-v$VER-mac-arm64.zip" > "OpenBA-v$VER-mac-arm64.zip.sha256")
      rm -rf "$OUT/OpenBA.app"
      manifest macos-arm64 "$OUT/OpenBA-v$VER-mac-arm64.zip";;
    windows)
      echo "== Windows x64"
      if [ "$HOST" = windows ]; then
        set_feed build/cmake-release
        CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED" tools/build.sh build release
        tools/release/package.sh windows "$VER" "$OUT"
      else
        set_feed build/cross-release
        CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED" tools/build.sh build release --cross
        BUILD_DIR=build/cross-release tools/release/package.sh windows "$VER" "$OUT"
      fi
      manifest windows-x64 "$OUT/OpenBA-v$VER.zip";;
    linux)
      [ "$HOST" = linux ] || die "the Linux package is built on Linux"
      echo "== Linux x64"
      set_feed build/cmake-release
      CMAKE_EXTRA="-DOPENBA_UPDATE_FEED=$FEED" tools/build.sh build release
      tools/release/package.sh linux "$VER" "$OUT"
      manifest linux-x64 "$OUT/OpenBA-v$VER-linux-x64.tar.gz";;
    *) die "unknown platform $p (macos, windows, linux)";;
  esac
done

if [ "$DRY" = 1 ]; then
  echo "== dry run: nothing published. Files:"
  ls -l "$OUT"
  exit 0
fi

if ! gh release view "$TAG" --repo "$REPO" > /dev/null 2>&1; then
  echo "== creating the draft release $TAG on $SHORT"
  python3 tools/release/changelog_section.py "$VER" > "$OUT/notes.md"
  gh release create "$TAG" --repo "$REPO" --draft --target "$COMMIT" --title "OpenBA $TAG" --notes-file "$OUT/notes.md"
fi
echo "== uploading ${#FILES[@]} files to $TAG"
gh release upload "$TAG" --repo "$REPO" --clobber "${FILES[@]}"

ASSETS="$(gh release view "$TAG" --repo "$REPO" --json assets -q '.assets[].name')"
MISSING=""
for r in $REQUIRED; do
  grep -qx "openba-update-$r.txt" <<< "$ASSETS" || MISSING="$MISSING $r"
done
if [ -n "$MISSING" ]; then
  echo "== $TAG stays a draft: no manifest yet for$MISSING (run this script there, on $SHORT)"
  exit 0
fi
echo "== every required platform is on $TAG: publishing it"
gh release edit "$TAG" --repo "$REPO" --draft=false --latest
echo "== published: players get $VER at their next check"

# Changelog

One section per release, newest first. The section of the version being tagged is the GitHub
release's notes (`.github/workflows/release.yml` reads it with `tools/release/changelog_section.py`).
Headings are `## vX.Y.Z (YYYY-MM-DD)`; the version is the CMake `VERSION` in `CMakeLists.txt`.

## v1.0.1 (2026-10-23)

Second release of OpenBA, five days after v1.0.0: Linux and macOS packages for the first time,
mod loading an order of magnitude faster, the first contributed fixes and extensions, and the
release build itself now made by GitHub Actions.

**Platforms and packages**
- Linux x86-64 package (`OpenBA-v1.0.1-linux-x64.tar.gz`): the exe with SDL3 and dxvk-native 3.1.1
  beside it, a launcher script and a README. Unpack next to the game's `WinData` folder and run `OpenBA.sh`.
- macOS Apple-silicon package (`OpenBA-v1.0.1-mac-arm64.zip`): an app bundle with a Metal renderer and a
  DirectX-to-Metal shader converter, Retina-aware UI (LucaSammartino, PRs #5, #6, #7).
- Windows x64 zip unchanged in layout (`OpenBA-v1.0.1.zip`: `OpenBA.exe`, the FrancophilSans files, README, licences).
- Every package is built by the Release workflow from the tag; CI builds and runs the unit tests on
  Windows, Linux and macOS and the no-doors acceptance check on every pull request. `main` is PR-only.

**Loading and memory (issue #13, MegaMod)**
- Main menu with the MegaMod loaded: about 60 s down to 14 s; memory 13.5 GB down to 2.5 GB.
- Exact, faster package reading: bulk array reads and memcpy LZ decode, packed reads served from a
  read-only mapping of the package, a hash index beside the resource tree and the package directory,
  package remount and mod sort done once and reused.
- The loading screen shows during the boot's loader frames and names the mod being loaded
  (`ext.load_progress`); the loader loops return at once and draw while the loader works.
- New class A extensions, on by default unless noted: `ext.decode_ahead` (next compressed blocks decode
  on worker threads), `ext.async_gpu_create` (GPU buffers and textures created on worker threads),
  `ext.create_on_demand` (GPU objects created at first use, their bytes skipped at load),
  `ext.share_gpu_data` (equal immutable buffers and textures share one GPU object), `ext.compress_ao`
  (baked AO maps as BC4), `ext.package_cache` and `ext.stream_cache` (the merged package directory and
  the consumed part of each resource stream kept in an on-disk derived cache for the next start).
- An on-disk derived-data cache of load-time transforms of the user's own data (never shipped), with
  its size and a Clear cache button on the Enhanced tab.
- `fix.large_packages`: mod packages larger than 2 GiB load (64-bit offsets).
- Present runs outside the device lock, under a backend Present mutex.

**Fixes**
- `fix.terrain_resort`: the red squares. Flat ground no longer stays marked too steep after flattening,
  and a cleared tree, ore or goods pile leaves steep ground buildable. Reproduced from the saves Karl sent in;
  thanks to him for the report.
- Garbled screen after minimize / restore or a fullscreen toggle (kralyerg, PR #17).
- The Town Hall Inventory tab counted every building as a home (kralyerg, PR #15).
- `fix.rock_seat`: rocks and iron ore are drawn seated on the slope instead of floating (render only).
- Decals over a rebuilt terrain chunk are re-meshed with it (retail `RebuildDirtyChunks`).
- The resource-limit UI accepts any ledger index a row's config names, not only 0..10 (#10).

**Extensions**
- `ext.restricted_foods` (class C, kralyerg, PR #11): choose which foods citizens eat. The first class C
  extension; it brings the shared extension toolbar window, a save-state codec, and host records in the
  save tail so a B / C extension's window and state survive a reload.
- `ext.high_limits` (kralyerg, PR #9): limit boxes take up to 999,999,999.
- `ext.no_fog` (kralyerg, PR #14): remove the fog from the picture (off by default).
- `ext.better_camera`: zoom range 1..200 and a steady tilt (class B, off by default).
- `ext.draw_batch` and `ext.terrain_batch` (class A, on): merged decal, road, smoke and terrain draw runs,
  a CPU saver for weak CPUs; the render-pipe fence is released before Present.
- Exact performance work with no behaviour change: start-town scatter Perlin precomputed in parallel
  rows, sound-effect buffers decoded in parallel at load, an ambient-environment scan memo.

**Project**
- The extension standard (`docs/extensions/DESIGN.md` §3): ids, directories, `Ext::<Id>` namespaces,
  hosts, codec, seams, hand-ins and tests on one form; every extension moved onto it.
- Platform and renderer seams: `Platform::` hooks, `BANISHED_RENDER_BACKEND` with a `backend.cmake` per
  renderer, backend-neutral code in `src/render/common/`.
- The main menu's build line shows the git commit and build date beside the version (README "Versioning").
- `AGENTS.md` (the contributor rule sheet), `CONTRIBUTING.md`, `THIRD_PARTY_NOTICES.md`, `.clang-format`.
- The FrancophilSans typeface files are kept unchanged in `tools/release/fonts/` with their notice, so a
  release is built from the repository alone.
- `-ffp-contract=off` everywhere, for identical float results across compilers and ARM.

Thanks to kralyerg and LucaSammartino for their pull requests.

## v1.0.0 (2026-10-18)

First release of OpenBA, the work of eight months (February to October 2026). Matching functionality
to retail Banished 1.0.7 with more performant and extendable code: the main exe, the runtime and the
DX11 video module are all ported and contained in `src/`. Extensions add features such as ambient
occlusion, foliage shadowing and performance tweaks. Windows x64 only; the Linux build was in its
foundational stages.

# LARGEPKM: mod packages larger than 2 GiB (`fix.large_packages`, 2026-10-21)

> **Built: `fix.large_packages` (tier 1, default ON, off under `/profile:faithful`).** A mounted
> package larger than 2 GiB gets 64-bit entry offsets, unwrapped from its index and validated, and
> its packed reads seek 64-bit. Packages up to 2 GiB are never looked at and read byte for byte as
> retail reads them. GitHub fran-cap/OpenBA#13: the reporter's single-file MegaMod "All In One"
> build (`MegaMod.pkm`, 10,757,625,300 bytes) crashed retail and OpenBA identically; with the row
> on it boots, starts a new game and shows its content.

Retail entry: `docs/retail_bugs.md` §9. Inventory row F11. Branch `issue13/largepkm` from main
30d98f24c.

## 1. The format facts (verified on the file, header and index reads only)

| | MegaMod.pkm (AIO) | MegaMod1.pkm (7-part set, typical) |
|---|---|---|
| size | 10,757,625,300 | 499,603,608 |
| header word | 0x80000004 (compressed index, index at 4) | 0x9dc3d9fe (index at 499,374,590, after the data) |
| index blob | [4, 5,151,918) | [499,374,590, 499,603,608) |
| entries | 371,669 | 10,300 |
| data | contiguous in index order, 5,177,344 .. 10,751,678,584 (a 25 KB pad before it, a 5.9 MB tail after it) | contiguous from 4 |
| stored offsets | uint32 = true offset mod 2^32; in index order they decrease exactly twice (4 and 8 GiB) | no decrease |
| holes between entries | 0 | 0 |

Retail reads a packed entry at the int32 `baseOffset + cursor` (ReadFile 0x1800ace43) with
`SetFilePointer(h, abs, NULL, FILE_BEGIN)` (0x1800ace61): an entry in [2, 4) GiB seeks negative and
fails, one past 4 GiB reads at its wrapped offset. A sampled offline read (section 5) shows what that
means on this file: of 3,194 sampled entries, 543 fail the seek and only the 451 below 2 GiB read the
right bytes.

## 2. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `FileSystem::LoadPackages` (0x1800ab460), at the end | nothing | `_extPackagesMounted(*this)`: `Ext::LargePackages::OnMounted` builds the 64-bit table for every mounted package past 2 GiB |
| `FileSystem::ClosePackages` (0x1800abab0), at the end | nothing | `_extWidePackages = nullptr`, `_extWideCount = 0` |
| `FileSystem::ReadFile` packed arm (0x1800ace43..67) | `FileSeek32(pack, baseOffset + cursor)` | for an entry of a wide package: `Platform::FileSeek64(pack, offset64 + cursor)`; every other entry: retail's line |

Engine seams (DESIGN.md hook rule 5), all null by default: `IO::FileSystem::_extPackagesMounted`
(the hook), `_extWidePackages` / `_extWideCount` (the table the hook publishes). The faithful path
pays one null test per 32 KB packed read. `FileInfo` and `PackageFile` keep their pinned layouts:
the 64-bit offsets are a side table indexed by pack index and entry index, never a wider field. A
FileInfo whose low dword does not match the table (one opened before a remount, a different package
now at that index) takes retail's read.

`IO::FileSystem::PackedReadOffset(fi, &wide)` is the one place the absolute offset of a packed read
is computed (64-bit for a wide package, retail's int32 sum otherwise); the mapped-read work
(block readpath, issue #13) uses it.

Platform: `Platform::FileSeek64` / `FileSize64` in `platform_file.h`, twins in
`src/platform/win/file_win.cpp` (SetFilePointerEx / GetFileSizeEx),
`src/platform/posix/file_posix.cpp` and `src/platform/darwin/file_darwin.cpp` (lseek / fstat, off_t
asserted 64-bit). Only the Windows twin was built and run here.

## 3. The unwrap rule, and why it is safe

`src/ext/large_packages/large_packages_unwrap.{h,cpp}` (pure, unit-tested):

1. In index order, every decrease of the stored offset adds 2^32 (what this packer does).
2. The result is refused unless it describes a sane file: every entry inside the file after the
   4-byte header word; no entry over the index blob; every entry at or after the previous one's end
   (monotonic, no overlap); and at most 64 MiB of holes between consecutive entries (the index blob
   not counted). A decrease that is not a 4 GiB wrap (an index written out of order) either runs
   past the file end, overlaps, or shifts an entry gigabytes forward and fails the density check.
3. A refusal logs one line (`[LargePackages] <name> (<size> GiB): index does not unwrap (<why>,
   entry <n>), retail's 32-bit reads kept`) and the package keeps retail's reads.

The index extent comes from the package's own header word and blob length, read with the 64-bit
seek; nothing else is read at mount.

## 4. Fix off, the mod scan, the remounts, the save

- **Fix off**: no table, retail's reads, retail's crash (section 5). Unless the run is forced
  faithful (`/profile:faithful`: the hook stays null, nothing is logged), the hook still runs and
  writes one line per package past 2 GiB:
  `[LargePackages] <name> is <size> GiB: retail reads packages only up to 2 GiB and
  fix.large_packages is off, so its entries past 2 GiB read wrong bytes`.
  Ruling asked for, recommended YES: retail's crash two seconds later in
  `MaterialInstance::BuildParameters` names nothing about the cause; the line costs one
  `GetFileSizeEx` per package per mount and changes no behaviour. It is not written under the
  gate's faithful profile, so the gate logs are unchanged.
- **Mod scan** (`Modification::RefreshList`): reads each `.pkm`'s index into a stack PackageFile
  and closes it without reading an entry. Unaffected and untouched.
- **Mounts**: the boot's `.pkg` mount (`FileSystem::Startup`, inside `Engine::Startup`) runs before
  `Ext::InitFromSettings` installs the hook, and mounts no `.pkm`. Every mods mount
  (`Modification::SetupFileSystem` ACT 2: ClosePackages, LoadPackages) runs the hook; a remount
  rebuilds the table from the new pack order. The fix's bool is read at each mount, so a toggle
  applies at the next start (the row says so).
- **Save**: nothing serialized changes. The save's mod list carries the package path and the CRC
  from its header, both untouched; the fix has no pseudo-mod and no tail record (a fix never does).

## 5. Proof

All runs: perf build (fix commit + the TEMP blob-hash instrument, deployed md5
06385d225ad6d69c00201b514e8311e0, verified under the game-dir lock), per-entry profile from the
user's settings.cfg (extensions as set there; `fix.large_packages` absent = default ON) unless
stated, `BANISHED_AUTOSTART=1 BANISHED_SYNC_ENGINE_LOADER=0 BANISHED_DEFER_WINDOW_SHOW=0` (with the
synchronous loader the 80 s content load trips the 20 s unattended hang budget: run r1, exit 4, a
pre-existing property of the sync loader, not of this row). Machine load BUSY (other agents
compiling): timings are indicative only.

| Run | Set | Profile | Result |
|---|---|---|---|
| r2 | AIO `MegaMod.pkm` | per-entry, fix ON | menu, new game started at 91.8 s; `[LargePackages] MegaMod 10 (10.02 GiB, pack 4): 371669 entries read with 64-bit offsets (2 wraps, 0 hole bytes)` at both mounts; the Houses build menu shows 28 entries (vanilla has 3): screenshot `scratchpad/issue13/largepkm/r2_aio_on/s8.png` |
| r3 | AIO | `/profile:faithful` (fix off, seam null) | **exit 3 at 2.3 s**, AV READ of 0x2c in `Graphics::MaterialInstance::BuildParameters` +0x1b4 (from `MaterialInstance::Serialize` under `External::Load`): retail's crash; no `[LargePackages]` line |
| r4 | AIO | per-entry, `fix.large_packages=0` | exit 3 at 2.4 s, same AV; one line `[LargePackages] MegaMod 10 is 10.02 GiB: retail reads packages only up to 2 GiB and fix.large_packages is off, so its entries past 2 GiB read wrong bytes` |
| r5 | AIO | per-entry, fix ON | new game at 80.5 s; 376,651 External::Load calls (blob lines), 10 with no data |
| r6 | 7-part set | per-entry, fix ON | new game at 91.7 s; 376,650 calls, 10 with no data; no `[LargePackages]` line (every part is under 2 GiB) |
| r7 | vanilla (DebugMenu + betterFields mods) | per-entry | new game; 5,703 calls (the runner's 5,695 was `/profile:faithful`; the 8 extra are the extension resources the exe serves under the per-entry profile); no `[LargePackages]` line |

**Load counts, AIO vs 7-part.** 376,651 vs 376,650. 376,648 paths are loaded by both; the AIO has
`MegaModResources_resource.crs` (its single top-level list; the 7-part set has one per part, in both
runs) and the random menu scene / loading screen differ (`scene7` / `GameLoad5` vs `scene5` /
`GameLoad2`). The runner's 376,431 was counted at the main menu; these runs stop after the new game.

**Per-blob hash (decode's instrument: every value the BinaryReader hands the object, per load).**
Of the 376,648 common paths, **375,912 hash identically**: 57,048 come from the AIO below 2 GiB,
24,336 from 2-4 GiB (where retail's seek fails) and 289,396 from past 4 GiB (where retail reads
wrapped offsets), 5,132 from the base game and the other mods. The 736 that differ were each
checked on the raw package bytes (`rawdiff.py`): 735 have different bytes in the two builds of the
mod (the AIO and the 7-part set are separate builds: rebuilt sprite sheets, meshes, a few
templates) and 1 has several copies in the 7-part set and the 7-part mount picked another copy.
No path whose bytes are identical in both sets hashed differently.

**Sampled raw read (offline, `samplecmp.py`).** 3,194 AIO entries (3,000 random + 100 around each
wrap) read at the unwrapped offset vs the 7-part copy: 3,188 equal, 6 different content (same
rebuilt files as above). Retail's offset on the same sample: 543 negative seeks, and only the 451
entries below 2 GiB read the right bytes.

**Unit tests.** `tests/unit/ext_large_packages_test.cpp`, 13 cases: no wrap (3 GiB), one wrap, two
wraps in the MegaMod shape, an entry straddling 4 GiB, the index at the back, the index between
entries, and the refusals: past the file end, over the header word, over the index, out of order
(as a spurious wrap and as an overlap), a spurious wrap that only the density check catches, plus
the registry row. `tools/build.sh unit`: 1292 tests, 0 failures, 1 skipped (the kit cross-check).

**Faithful / vanilla unchanged.** Debug build of the fix without the instrument (branch head):
`tools/resave_gate/resave_gate.sh Gate` (mods parked, faithful profile) **PASS**, golden md5
971aaec2cf95f5f730bb1cd1a4fa8428 (main's), crash log 0. `tools/build.sh run` (faithful): new game
reached, RUNNING, crash log 0, no `[LargePackages]` line. Under the faithful profile the hook is
null, so no package is examined and ReadFile pays one null test; under the per-entry profile a
package up to 2 GiB is examined by one `GetFileSizeEx` per mount and read as retail (runs r6, r7).

## 6. Files

| File | Change |
|---|---|
| `src/engine/io/FileSystem.h` | the three seams, `ExtWidePackage`, `PackedReadOffset` |
| `src/engine/io/filesystem_openfile.cpp` | the seams' definitions, `PackedReadOffset`, the ReadFile packed arm's 64-bit branch |
| `src/engine/io/filesystem_packages.cpp` | the hook call at the end of LoadPackages, the clear in ClosePackages |
| `src/platform/common/platform_file.h`, `src/platform/{win,posix,darwin}/file_*.cpp` | `FileSeek64`, `FileSize64` |
| `src/ext/large_packages/` | `large_packages.{h,cpp}` (the hook), `large_packages_unwrap.{h,cpp}` (the rule) |
| `src/ext/registry.{h,cpp}`, `src/ext/registry_boot.cpp` | the row, `LargePackages::Sync()` |
| `tests/unit/ext_large_packages_test.cpp`, `tests/unit/sources.cmake`, `tests/unit/ext_registry_test.cpp` | tests |
| `docs/retail_bugs.md` §9, `docs/extensions/INVENTORY.md` F11, this file | docs |

Overlap: the ReadFile packed arm is shared with block readpath (mapped reads), which takes
`PackedReadOffset`; nothing else of this row touches another block's files.

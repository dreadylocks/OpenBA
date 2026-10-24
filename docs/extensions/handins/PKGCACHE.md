# PKGCACHE -- the package remounts and the mod list, exact (issue #13)

Block PKGCACHE, 2026-10-23, branch `issue13/pkgcache` (based on `issue13/lazy` 6892deb79). Two parts: a
class A row, `ext.package_cache`, and faithful perf cuts with no switch (SAVELOAD / PATHINDEX style).

## 1. What was slow

With MegaMod (7 parts, 401,167 package entries) a boot remounts the packages twice on the main thread
(`Modification::SetupFileSystem`: 0.42 s and 2.1 s) after `RefreshList` built the mod list (0.6-0.8 s).
Per remount: LoadPackages' third loop searches and inserts every package entry into
`FileSystem::_packfileIndex` (`_stricmp` descents); act 1 compiles every loaded resource's path
(`GetCompiledPath`) and inserts it into a case-sensitive AVL snapshot; act 3 compiles every path again
and searches the snapshot. RefreshList's sort swaps `Mod`s by three deep copies, each cloning the mod's
path set node by node (~57,000 paths per MegaMod part).

## 2. As built

**ext.package_cache** (class A, `src/ext/package_cache/`, Extensions row "Remembered mod file lists").
Two seams on `IO::FileSystem` (`_extIndexRestore`, `_extIndexBuilt`, null under the faithful profile, so
the cache is never opened there). The tree LoadPackages' third loop builds is a function of its inputs
alone: the packages in `_packfiles` order, each one's entry names in order and its `_overridable` byte.
Those bytes are the key (`KeyBytes`, XXH64 + size, producer `PKIX`, version 1, shape {packages,
entries}) in the derived-data cache (`src/ext/derived_cache/`). A miss runs the loop and stores the
tree (`Encode`: pre-order, per node the entry whose name buffer is its key, its value pair, balance,
height, children; 7.9 MB for MegaMod). A hit skips the loop: `Decode` allocates every node with the
loop's own allocator and constructor (`rtavl::FilenameNodeAlloc`, a copy of the entry's name, so the key
shares the entry's buffer exactly as the loop leaves it) and links the same shape, adding each node to
the PATHINDEX fold index as the loop's insert does. It validates every index and the strict in-order
`_stricmp` order and, on any failure, frees what it built and lets the loop run. Same tree: shape, key
buffers, values, balance, height; only node addresses differ.

**Faithful cuts** (no switch, every profile):
- `SetupFileSystem` act 1 / act 3 snapshot (`modification_setupfilesystem.cpp`, `SnapshotTable`): an
  open-addressing table on the compiled path's characters instead of the local signed-`strcmp` AVL tree.
  The tree is local, read only by act 3's `SnapshotFind` and freed in act 5; its comparator's equality is
  byte equality and an equal key keeps its first value, which is what the table answers. The debug exe
  still builds the tree and compares every lookup (`[Snapshot] MISMATCH`).
- Act 3's compiled paths (`CompiledPaths`): act 1's answers are kept and act 3 takes the one for the same
  resource when its path has the same characters. `GetCompiledPath` reads only the path and the data
  directory (set once at boot); act 3 only reads the answer. Debug exe: `[CompiledReuse] MISMATCH`.
- `ModSwap` (`modification_sort.cpp`): the two `Mod`s trade their bytes instead of three deep copies.
  Nothing points into a `Mod`; each slot ends holding the other's values (same strings, arrays, set
  shape); only allocation addresses differ. A self-swap stays a no-op, as retail's guarded assignments.

**Not done as briefed.** Memoizing `GetCompiledPath` globally (keyed on the path's characters, since
`ResourceEntry` is a fixed retail 0x18 layout with no room) was built, shadow-proven exact (376,183
entries, 262,144 hits checked, 0 mismatches) and measured: act 1 +170 ms (misses pay the store), act 3
-110 ms, about +90 MB. Neutral-to-worse, so it was removed; `CompiledPaths` replaces it inside the
remount. The mod entry set is not cached: RefreshList reads each `.pkm`'s own directory before any
mount, and after the `ModSwap` cut its remaining cost is `ReadPackfile` and the set inserts (~0.1 s).

## 3. Exactness evidence

- Unit (`tests/unit/ext_package_cache_test.cpp`; 1359 tests, 0 failures): the loop replicated
  (`BuildLikeLoadPackages`), encoded, decoded and compared node for node (shape, key buffer addresses,
  values, balance, height) plus every name looked up in both trees, over 40 random package sets with
  overrides and case variants and over the seven MegaMod parts (401,167 entries; skips without the
  install); the key bytes change with the override byte, the order and a case change; truncated, padded,
  out-of-range, reordered and childless payloads are refused with the tree left empty.
  `tests/unit/pkm_names.h` is the .pkm directory reader, shared with `path_index_test.cpp`.
- Shadow runs (TEMP, removed), perf exe, MegaMod boot + new game, warm cache:
  - package cache: remount 2 decoded beside the loop's tree, `SameTree` true, 376,953 / 376,953 nodes,
    407,003 lookups, **0 differ** (twice);
  - `ModSwap`: the boot's own swaps are all self-swaps, so every adjacent pair of the sorted mods was
    swapped both ways on copies (retail's three copies against the byte swap, 12 pairs, sets of
    10,300-87,445 paths) and compared member by member (strings by characters, sets by shape and keys,
    root parent null, arrays): **all same**;
  - snapshot table and compiled-path reuse with the debug checks forced on: 376,183 of 376,183 act-3
    answers reused, **0 MISMATCH** lines, fold index 8.4M answers 0 differ.
- Gate: see section 5.

## 4. Timings

Perf exe, MegaMod 7-part, warm derived cache (the PKIX entry present). **Machine load: NOT clean** (other
agents compiled during most runs: 0-5 compiler processes at each start).

| | before (lazy tip, 4 runs) | after (2 runs) | |
|---|---|---|---|
| RefreshList | 0.56-0.63 s | 0.48-0.51 s | -0.09 s |
| remount 1 | 0.39-0.48 s | 0.27 s | -0.15 s |
| remount 2 | 1.88-2.38 s | 1.23-1.30 s | -0.7 s |
| boot to menu | 17.6-22.8 s | 16.5-17.0 s | |
| working set in game | 2.06-2.10 GB | 2.03-2.07 GB | no change |

Remount 2 by act (LoadTime stamps, TEMP, removed), before -> after: snapshot (act 1) 640 -> 470 ms,
close (act 2) 300 -> 300 ms, LoadPackages 370-410 -> 240 ms (third loop 270 -> 120 ms: the decode), diff
(act 3) 570-590 -> 300 ms. Remount 1 has no resources loaded; it is the LoadPackages part alone.

## 5. Gate

`tools/build.sh unit`: 1359 tests, 0 failures (1 skipped, not this block's). `tools/build.sh run` (debug
exe, which builds retail's snapshot tree and runs `GetCompiledPath` behind every reuse; faithful
profile, mods parked): in game, crash log 0 bytes, 0 `MISMATCH` lines. `tools/resave_gate/resave_gate.sh
Gate`: **PASS** against the real golden (issue13-next's `golden/Gate.resaved.bin`, md5
971aaec2cf95f5f730bb1cd1a4fa8428, byte-identical to the file in this worktree), on bb4340aba and on the
merge f5210e06d.

## 6. Open

- Act 1 still compiles every path and calls `FindNext` / `GetPackfileId` per resource (~470 ms); act 2's
  `ClosePackages` frees the 376,953-node tree and every package (~300 ms).
- RefreshList: `ReadPackfile` of every .pkm directory and the per-mod set inserts.

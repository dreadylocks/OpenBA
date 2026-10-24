# DECODEAHEAD / MAPREAD hand-in (issue #13, 2026-10-22)

Branch `issue13/readpath`, based on `main` `30d98f24c`. Issue #13: a ~12 GB mod set (MegaMod, seven
`.pkm`) takes ~55 s to load at boot, all of it on the loader thread. Two changes to the read path:

| Commit | What |
|---|---|
| `io: packed reads served from a read-only mapping of the package (MAPREAD)` | exact faithful-code perf body, unconditional |
| `ext.decode_ahead: ...` | class A extension, default on, next start; needs MAPREAD's mapping |
| `docs: ...` | this hand-in, LIST.MD row, known_port_issues row 1331 |

`issue13/readpath-x-largepkm` (local) is this branch merged with `issue13/largepkm` (`fix.large_packages`),
with the one conflict resolved: the mapped arm reads at `PackedReadOffset`'s 64-bit offset for a wide
package, and `PackedMapSpan` / `PackedMapNoteRead` use the same rule. Runs r9-r11 below are from it.

## 1. What the time was

TEMP instrument (`[TMPRP]`, on `issue13/readpath-wip`, never merged), set7 boot, perf build:

| | calls / blocks | bytes | ms |
|---|---|---|---|
| packed ReadFile arm (SetFilePointer + ReadFile) | 1,400,000 reads (680 k <= 16 B, 278 k <= 256 B, 363 k 4-32 KB) | 6.32 GB | 8,976 |
| `Compression::Decompress` in `DecompressFromSource` | 700,000 compressed blocks | | 9,208 |
| streams opened | 350 k: 300 k one block, 20 k 2-3, 20 k 4-15, 10 k 16+ blocks | | |

58% of the blocks are block 2+ of a multi-block stream: those are the ones a look-ahead can take.

## 2. MAPREAD (exact)

`FileSystem::ReadFile`'s packed arm maps the file behind the package's handle once
(`Platform::FileMapOpen`, win: CreateFileMappingW + MapViewOfFile; posix + macOS: mmap; a 12 GB view
is fine in the 64-bit address space) and serves the read by memcpy (`Platform::FileMapRead`).
Exactness, all in the banner above ReadFile: ReadFile's bytes and count (short at end of file, 0 past
it); abs < -1 fails as SetFilePointer does; abs == -1 still passes and reads at the stale pointer (an
emulated pointer, updated by every mapped read); n < 0 and an unmapped package keep retail's handle
path. Lifetime: released in `PlatformCloseFile` before CloseHandle, so every ClosePackages (two
remounts per boot, a mod toggle) unmaps. The audio stream thread's packed reads go through the same
(locked) table; retail's shared OS file pointer was unlocked there.

Known divergence (row 1331): a disk error during a page fault is an in-page exception, where ReadFile
returned FALSE.

## 3. ext.decode_ahead (class A)

Seam `IO::CompressedStream::_extDecodeAhead` (null by default; installed once at boot, next start).
In `DecompressFromSource(b)` of a stream with >= 2 blocks over a packed FileStream: blocks b+1..b+8
whose bytes a ReadFile would return in full from the mapping (`FileSystem::PackedMapSpan`) are queued
for 1-3 workers (hardware threads - 2, the 4-core target keeps the loader and main threads their
cores); a block a worker finished is copied into the window and the source cursor / emulated pointer
are left where retail's SetCurrentPos + Read leave them. Block 0, a stored block, a block not queued, a
damaged block (decoded size != the header's) or a loose / unmapped / nested source take retail's
body. `Release` drops the stream's jobs (queued ones removed, a running one waited for), so no job
outlives its stream or the mapping. Nothing order-bound moves: every Read, Serialize and
External::Load runs where retail runs it; Decompress is pure. Job buffers are the ext's own (48 x 32
KB, malloc), never the game heap. OS read-ahead (PrefetchVirtualMemory / madvise) was measured in a
micro-bench and dropped: warm, prefetching 1 GB then reading took 1.83 s vs 1.55 s without; cold could
not be measured (§5).

## 4. Proof

| # | what | result |
|---|---|---|
| r1 | per-blob hash (decode's `blobhash.patch`), set7 boot, main vs this branch's code (+ the TEMP read instrument; ext on) | 376,431 vs 376,440 lines; the first 376,175 loads identical in ORDER and hash; the rest is the menu scene, chosen at random per boot (Scene9 vs Scene1, +9 Snow / Weather loads; decode's own main run drew Scene6). No other line differs |
| r2 | `tools/resave_gate/resave_gate.sh Gate`, debug, faithful, mods parked | PASS, golden md5 971aaec2cf95f5f730bb1cd1a4fa8428 (main's), resavedCompressedHash 4127020a2ae682eb, crash log 0 |
| r3 | the gate, per-entry profile, `ext.decode_ahead` = 1 vs 0 | identical: resavedCompressedHash 23c27092af5af219 both legs, 23,454,693 bytes |
| r4 | External::Load, new game (debug, per-entry, DebugMenu + betterFields) | created 5695, calls 5703, openFileFail 8: RULES' 5695 |
| r5 | External::Load, gate load route (debug, per-entry) | created 5694, calls 5702 |
| r6 | unit, `tools/build.sh unit` | 1285 tests, 0 failures, 1 skipped (ext_sheet_kit_crosscheck: no kit). New: `file_map_read_matches_seek_and_read`, `file_map_open_refuses_an_empty_file`, `ext_decode_ahead_*` (4) |
| r9 | AIO MegaMod.pkm (10.8 GB), merged branch, per-entry (fix + ext on) | boots, rc 0; loader 57.8 s cold, 41.4 s warm |
| r10 | AIO, merged branch, `/profile:faithful` | exit 3 at 2.4 s, AV READ 0x2c in `Graphics::MaterialInstance::BuildParameters` (from `MaterialInstance::Serialize`): retail's crash, unchanged |

## 5. Timing

Perf build, `BANISHED_SYNC_ENGINE_LOADER=0 BANISHED_DEFER_WINDOW_SHOW=0` (the watchdog kills a 50 s
parked main thread under AUTOQUIT), set7 MegaMod mounted, warm cache, loader.resources -> loader.load
of the boot's GlobalResources load. **Machine load: BUSY** (three other agents compiling and running):

| exe | runs (s) | mean |
|---|---|---|
| main (+ TEMP read instrument) | 62.0, 56.8, 50.5 | 56.4 |
| MAPREAD | 51.5, 52.4, 48.7 | 50.9 |
| MAPREAD + ext.decode_ahead | 42.6, 45.2, 41.6 | 43.1 |

The instrument is the steadier number for MAPREAD: the packed arm 9.0 s -> 1.6 s for the same 1.4 M
reads. With decode_ahead on, roughly half of the 700 k compressed blocks are decoded on workers (the
loader's own Decompress count drops to 300-400 k), and the workers also take the page faults of the
bytes they read. Vanilla boot (no mods), loader load: main 393 / 417 ms, this branch 350 / 329 ms.

Cold cache: no non-admin way to empty the standby list was found, so cold was not measured on
purpose. The one cold-ish data point is r9's first AIO boot (a file not read since the last
reboot-equivalent): 57.8 s vs 41.4 s warm, the packed arm 7.4 s, i.e. disk-bound.

## 6. Verdicts

- MAPREAD: KEEP (9.0 -> 1.6 s in the arm; 5.5 s mean on the whole load, BUSY).
- ext.decode_ahead: KEEP (7.8 s mean on top of MAPREAD, BUSY).
- OS read-ahead: DROPPED (no warm gain, cold unmeasurable).

## 7. Open

- Not built or run on Linux / macOS (`file_map_posix.cpp` is the twin for both; macOS has no darwin
  twin, mmap is POSIX). The Windows build, unit and gate only.
- No Extensions-dialog sprite `Entry_decode_ahead.png` (rock_seat / large_packages have none either).
- Re-time on a clean machine before merging (RULES).
- `RtIo::g_swBranch` in SetWindow is a non-atomic global tally; the workers never call SetWindow, so
  this extension adds no race there.

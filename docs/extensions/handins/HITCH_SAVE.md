# HITCH_SAVE hand-in (2026-10-18)

Branch `savehitch`, based on `hitch` `c049e5ad9`. The autosave hitch of the HITCH stutter hunt
(`HITCH.md`): run `20261018_100535_hitch_save_s50_f12000`, frame 6002, 145.9 ms over baseline, all in
PostSim. **Result: two faithful speed-ups (byte-identical saves) and one class A extension.** The
save frame drops from 146 ms over to 84 ms over (faithful) and 54 ms over (extension on). The rest is
retail's own work on the main thread. Not merged.

(2026-10-20: moved to `src/ext/async_save_compress/` (namespace `Ext::AsyncSaveCompress`: `Begin`,
`LastStats`), seam `IO::CompressedStream::_extAsyncCompress`, a null-by-default static
`bool (*)(CompressedStream&, int32_t point, int32_t index, uint8_t* dst)` with three points
(`kExtAsyncCompressWindow`, `kExtAsyncCopyQueued`, `kExtAsyncEnd`), set by `Begin` and cleared when
the save ends. The engine's old members `ExtBeginAsyncCompress` / `ExtAsyncCompressWindow` /
`ExtAsyncCopyQueued` / `ExtAsyncEnd` / `_extAsyncOwner` and `compressed_stream_async.cpp` are gone.
Re-proved with §3's method: per-entry profile, row off vs on, `resavedCompressedHash`
`d5fb0cce8335778a` on both legs, raw resave md5 `dbd72973...` on both, the on leg 724 jobs / 2
seek-backs served from the queue; faithful gate PASS `4127020a2ae682eb`.)

| Commit | What |
|---|---|
| `498c00344` perf | `ExternalFactory::GetReferenceId`: a per-save memo |
| `b846f7158` perf | `CompressedStream::Write`: SetWindow's fast path tested inline |
| `644020e00` ext | `ext.async_save_compress` (class A, default off): the windows compressed on a worker |
| `aeb1499b7` tools | resave gate `GATE_EXE` / `GATE_PROFILE`; `[Roundtrip]` prints the compressed resave's hash |
| (this commit) docs | this hand-in, the `perf/README.md` line |

## 1. What the 146 ms was

Retail's path, all on the main thread: `SaveLoad::UpdateSave` state 1 (retail 0x1800c2a80) calls
`GameUpdateLoop::SaveRestore` (0x1400959c0), which writes the whole game state through an
`IO::CompressedMemoryStream` with two 32 KB windows. Every eviction compresses a window inline
(`CompressWindow` 0x1800a9810). The persistence worker only computes the CRC and writes the file.
So the serialize and the compression are both retail's main-thread work. The probe below was
temporary and is not committed.

Probe leg `20261018_104136_savehitch_probe` (machine BUSY: other agents compiling):

| part | ms |
|---|---|
| `SaveMetaData` (thumbnail and header) | 0.55 |
| `SaveRestore` total | 144.1 |
| of it: the serialize, compression excluded | 102.5 |
| of it: `Compress`, 780 windows, 25.6 MB in, 10.6 MB out | 39.1 |
| of it: `Close` (Commit: the archive into the memory stream) | 2.3 |
| of it: `Store` (memcpy of 10.3 MB) | 0.3 |
| state 2: the frees, at frame 6061 | 0.35 |

The serialize by section, compression included: entities 97.5 (compression 21.3), world geometry
15.8 (10.9), map 16.1 (2.8), the rest about 10. The raw image is 24.4 MB.

The sampler, inside the save period:
- **The reference-id lookup.** About 50 of 171 samples sat in `GetReferenceId`'s path:
  `AdjustDelimiters` and `StriCmpAscii`. Every `ExternalPtr` in the save asks it, about 185,000 times
  for about 920 distinct Externals. Each ask is two Filename copies, four `AdjustDelimiters` passes
  and a case-insensitive descent of the resource tree. That is retail's algorithm (0x1800bf100).
- **The writes.** 8.7 million `CompressedStream::Write` calls, every one of them 8 bytes or fewer.
  Each went through `SetWindow` and a CRT `memcpy`.
- **Compression.** About a third of the samples.

**What is ours and what is retail's.** Every part is retail's algorithm. Retail could not be timed:
it has no unattended route to load the stress town and autosave. The two faithful changes remove
work that retail's own code does too. They do not repair a defect of the port.

## 2. The fixes

- **The memo (`498c00344`, faithful).**
  - The window opens in `SerializeAll` (save direction) after `SerializeReferenceTable` has numbered
    the tree, and closes where reference mode is switched off.
  - The first ask for an External runs the retail body, so its in-place path normalisation still
    happens. Later asks return the kept id.
  - `COMPPROF=4` re-runs the retail body on every hit. Leg `20261018_123426_savehitch_verify`:
    184,527 hits, 0 differ.
  - Effect: SaveRestore 144 to 92 ms (busy machine).
- **The write fast path (`b846f7158`, faithful).** `Write` tests SetWindow's own fast-path condition
  and calls it only when it can do something. Scalar sizes are copied with a constant-size memcpy.
  A/B on the deterministic gate route (Gate.sav resave), two runs each:

  | build | SaveRestore ms |
  |---|---|
  | without | 81.0 / 79.4 |
  | with | 68.9 / 70.3 |

- **`ext.async_save_compress` (`644020e00`, class A, default off).**
  - `CompressWindow` copies the window and queues it. One worker runs retail's `Compress` in
    submission order, with the stream's own Compression object, whose hash table carries over
    between calls. So every block is the one the synchronous body writes.
  - The main thread stores the results in submission order. `Commit` waits for the last jobs before
    `PreCommit`.
  - A seek back into a still-queued block (SetWindow's in-memory branch) copies the job's bytes,
    which are what decoding the stored block returns.
  - Faithful cost: one test of a static pointer at four sites.
  - The game side is one seam in the save arm of `SaveRestore`, beside the existing tail seam.
  - **No row icon.** The Extensions sprite sheet (256 x 256) is full: a 29th icon fails
    `ext_sheet_pack_fits_pads_and_records`. The dialog draws nothing for the missing sprite. Growing
    the sheet is a UI call for the coordinator.

## 3. Identity

The gate verdict compares the decompressed image. The compressed image is compared through the new
`resavedCompressedHash`, or the probe's identical hash before that existed.

| leg | gate verdict | compressed hash |
|---|---|---|
| pre-change exe (`hitch`, hash line only), faithful | PASS | `4127020a2ae682eb` |
| final exe, faithful | PASS | `4127020a2ae682eb` |
| final exe, per-entry profile, extension off | (see below) | `b9fe1e948b186c3c` |
| final exe, per-entry profile, extension on | (see below) | `b9fe1e948b186c3c`, raw image byte-identical to the off leg |

**The per-entry legs differ from the golden** at byte 5 (the resource table, +150 to +300 bytes)
with the extension off as well. That is the per-entry profile itself, not this work. Its size even
moved between two sessions of runs as settings.cfg changed. The off and on legs of each pair ran
back to back on the same settings.

Unit (`tools/build.sh unit perf`): 1167 tests, 0 failures, 3 skipped (the kit cross-checks).
Gate.sav is unchanged (`b197edf5...`). Stress50.sav is untouched. Auto.sav was restored to its
pre-session copy.

## 4. Before and after

Stress town, speed 50, autosave at frame 6000, 9000 frames. Machine load CLEAN on both final legs.

| run | SaveRestore ms | frame 6002 period ms | excess ms | PostSim ms |
|---|---|---|---|---|
| `20261018_100535_hitch_save_s50_f12000` (main, before) | (not measured) | 162.8 | 145.9 | 142.6 |
| `20261018_122809_savehitch_final_faithful` | 79.8 | 101.2 | 84.2 | 81.1 |
| `20261018_123103_savehitch_final_ext` (`STRESS_EXT=async_save_compress`) | 48.1 | 70.1 | 53.7 | 49.4 |

**Extension leg's worker.** 775 jobs, 39 ms of compression. The main thread spends 1.2 ms copying
and waits 2.7 ms at Commit. 8 seek-backs were served from the queue.

**What is left is retail's.**
- Faithful: about 33 ms of compression and about 45 ms of serialize.
- The serialize is 8.7 million field writes through `Serializer` / `BinaryWriter` virtuals, one
  `Serializer`, with its allocation, per component, and the `InternalPtr` / `ExternalPtr` bodies.
- The save must be a consistent snapshot, so it cannot run beside the sim or be spread over frames.
- Nothing faithful is left that is as large as the two fixes. The sampler showed no single hot spot
  in what remains.

**Frame 6061 (save complete) is the harness.** It shows 7.2 ms over on the extension leg. That is
`saveload_harness.cpp`'s `DumpSaveDir`, about 30 `fprintf` lines at about 250 us each (see
`HITCH_POSTDRAIN.md`). It is harness-only and in the stderr agent's area, so it was left as is.

## 5. The 117 ms "outside" stall at frame 7994

**Not reproduced and not attributable to the save.**
- **Timing.** It came 33 s of wall time after the save, not 2 s. The save finished at frame 6061,
  about 101 s; the stall was at 133.5 s.
- **What was running.** By then the persistence worker had finished and state 2 had freed the
  buffers. Nothing in the save path runs at that time.
- **Where the time went.** All of it was outside Update and Draw: Present, the message pump, or
  the main thread blocked.
- **No recurrence.** None of the five legs here that ran past frame 7994 with the same save
  (`104136`, `114418`, `114820`, `122809`, `123103`) has any row with more than 40 ms outside.
  Neither do the earlier f12000 HITCH runs without a save.
- **Machine load.** The 6-second machine-load samples show nothing at that time.

If it recurs, sample it (`BANISHED_SAMPLE=1000`). In this session the sampler at 4 kHz and 2 kHz hung
the boot twice: the main thread was stuck in ntdll at `engine.window` / `engine.videomodes`. The
same sampler at 1 kHz, and every unsampled run, booted. That looks like the sampler suspending the
main thread inside a loader lock. It is a separate issue, not looked into further.

## 6. Notes

- The resave gate deploys its own `EXE` (default the debug build). Copying a perf exe in before it,
  as the kit's command did, ran the debug build. Use `GATE_EXE=build/cmake-perf/OpenBA.exe`. A
  game-dir lock around a gate that deploys a different exe flags `CLOBBER`. That flag is the gate's
  own copy, not a foreign one.
- `[Stress::save]` lines appear only in a stress run: the memo counts and the save's SaveRestore
  time and async counters.

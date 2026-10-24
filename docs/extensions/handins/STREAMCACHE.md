# STREAMCACHE: the loader's resource streams from the derived cache (issue #13, 2026-10-23)

> Branch `issue13/streamcache`, based on `issue13/lazy` (`6892deb79`). Windows only (perf + debug builds,
> unit, the gate, the resave gate, the MegaMod runs below). Not built: Linux, macOS (the code is
> backend-neutral standard C++ over the engine's IO classes; the Metal backend's Locate goes through the
> same seam). Three parts, three commits; the user ruled the shape (item 1 as option b: a bundle of the
> SMALL resources only; geometry and images stay compressed and gain the head / tail records).

## 1. What was measured (branch `issue13/bootprof`, TEMP, never merged)

A MegaMod 7-part boot with ext.create_on_demand on, perf exe, 2000 Hz sampler, two runs: menu at 18.9 /
18.6 s. Load 1 (the root Resources list + 7 mod lists) is 10.0 s on the loader thread for 376,107
`External::Load`s: 482,948 block decodes, 2.9 GB compressed in, 4.7 GB out, with 0 static GPU payloads
read. Per resource TYPE (exclusive ms of the 10.0 s): GraphicsMesh 2,093, ImageBuffer 1,646, PickingMesh
1,442, EdgeMesh 1,285 (98k loads, 4.63 GB decoded: 2.3 blocks per object, the header block and the
block each skipped payload ends in), ~40 *Description / UIConfig / Toolbar types 2,962 (247k loads of
0.2..2 KB, ~12 us each, 50 MB decoded in all: pure per-stream overhead). Of the loader thread's samples:
the block decode 32-34% (two thirds of it the FIRST block of every stream, read by the BinaryReader's
constructor; the rest the tail blocks after skipped textures and buffers), `CompressedStream::Open`
15% (`SetWindowCount`: a 32 KB window allocated and zeroed per stream), the allocator 18%, path /
string work 16.5%, ext.decode_ahead's hook 12.4% inclusive of which 9.3% WAITING for its own block
(19% of the blocks served). The sample files and the per-phase tables are listed in the report to the
lead (scratchpad `bootprof/`).

## 2. Part A: ext.decode_ahead stays off while ext.create_on_demand is on (`1c3f6efc7`)

With the payloads skipped a stream is read in its header block and in the block each skipped payload
ends in; the kAhead blocks queued at the header are mostly never wanted, and the one that is wanted
waits in the queue behind them. `DecodeAhead::Sync` leaves the seam null when both rows are on and says
so in one `[DecodeAhead]` boot line. Exact: decode_ahead never changes a byte, only where a block is
decoded. 3x3 interleaved A/B of menu time (section 6).

## 3. Parts B + C: ext.stream_cache (class A, next start, default ON)

One mechanism for both: a RECORD of what a load consumed. `CompressedStream::_extReadTap` sees every
read of the retail compressed stream (Read, and ReadArray's batched copy as the m reads it stands for);
the extension notes, per load, the ranges `[pos, pos + n)` of the DECOMPRESSED stream in read order with
their bytes, adjacent reads merged. A load whose reads all succeeded, never went backwards and consumed
at most `kRecordMax` = 8 KB is stored as a record keyed by the entry's identity {offset in the package,
size, block count, Hash64 of the block directory}. A small description is recorded whole (one segment
covering the total: part C); a mesh or texture whose payloads create_on_demand skipped is recorded as
its header and the size fields between the payloads (part B); CPU geometry (picking / edge meshes,
30..50 KB fully read) and anything else large exceeds the limit and stays on the compressed path. The
limit is a constant with its rationale in `stream_record.h`, not a setting.

**Storage.** Records are grouped per package into CHUNKS (16-byte header + records, 8-byte aligned) and
put in the derived cache (`src/ext/derived_cache/`, content-addressed, class A, never opened under the
faithful profile) at each load bracket's end (`Engine::LoaderLoop::_extLoadDone`, after the
`loader.done` stamp) by the cache's writer thread; one chunk per bracket that recorded something, chunk
ordinals 0, 1, ... under the package's key. The key: producer `STRM`, the format version constant,
shape {package CRC, create_on_demand on/off (what a load consumes depends on it), 0, chunk ordinal},
sourceSize = the package's entry count, sourceHash = the package IDENTITY: Hash64 over {CRC, version,
user version, entry count}, the package name and its whole entry table (offset + size of every entry),
so a rebuilt package at the same path is another identity. The build is NOT in the key: the recorded
bytes are a function of the package alone; only the read pattern depends on the build, and a record
that is too narrow is widened (below). A package's chunks are read back at its first load of the run
(from the newest generation's first chunk, section 8, until the first miss; 86-189 ms for 150-300 MB of chunks, Find + parse, counted in the
load-end line as "read back in") into a `Bundle` indexed by entry offset, a later chunk's record for an
entry replacing an earlier one; identities survive the two remounts (the pack index -> PackageFile
pointer is re-derived when the FileSystem's table changes).

**Lifetime: the boot only.** The bundles are dropped after the boot's last bracket -- the one whose loop
is the `MenuDialogLoopT` (the seam passes the loop's type) -- and no later bracket is served or
recorded (`[StreamCache] boot over: N MB of records dropped`). The later brackets are small: a new game
loads ~220 resources (the boot's 376k), a save load or a device-restart reload the same few; reading
150 MB of chunks back for them would cost more than the decode it replaces, and recording them would
add a chunk per run. So the 150 MB (300 MB for the one boot that widens a generation, section 8) is
resident for the boot's ~16 s and not for the session.

**Fallback (a record too narrow).** A read outside the record's segments does not fail the load: the
`SegmentStream` asks its opener for retail's CompressedStream on the same entry (the load's own
CompressedFileStream, whose packed FileStream is open: the `CompressedStream::Open` External::Load
skipped), sets its `_position` to the current decompressed position (CompressedStream::Read starts
every read with SetWindow(_position), so the block is decoded when retail would decode it) and forwards
every call from then on (Read, SetCurrentPos, GetCurrentPos, GetSize, Skip). The bytes already served
are the ones that stream holds (the record is those bytes), so the load is the same load. The rest of
the load is recorded on top of the record (`Recorder::Seed`; overlapping ranges must agree byte for
byte or the record is dropped) and the widened record goes out in the bracket's chunk, replacing the
narrow one at the next start. Counted as "extended" in the load-end line; an opener that cannot open the
stream (never seen) marks the stream Failed and is counted as "fallback failed".

**Serving.** `Object::ExternalFactory::_extLoadBegin` runs in `External::Load` with the packed entry's
FileStream open and before `CompressedStream::Open`: for a packed entry on a mapped package it reads
the block directory from the mapping (`PackedMapSpan`), forms the identity and asks the bundle. A hit
returns a `SegmentStream` (an IO::Stream in the decompressed stream's coordinates, backed by the
record) that the Serializer and the object's Serialize read instead of the compressed stream: no
window, no directory read into the stream, no decode. A read outside the record fails (false, nothing
copied) and marks the stream; `_extLoadEnd` counts and reports it ("stale") -- it cannot happen for a
record made by the same build (the key), and the counter is the canary. A miss returns null: retail's
`CompressedStream::Open` and read follow unchanged, tapped. Loose files, nested packages, memory
streams: null, nothing recorded.

**create_on_demand over a record stream.** `CreateOnDemand::Locate` asks `g_altLocate` when the stream
is not a CompressedStream, `Skip` asks `g_altSkip` first: the extension answers with the same Locator
the compressed stream gives (the package mapping from `Maps().Acquire(handle)`, the entry offset / size
the stream carries, the decompressed position, the length), checked with `RangeInStream` against the
mapped entry's own block directory, and advances the record stream's position past the payload. The
coordinate system is the original decompressed stream's throughout.

**Threads.** A thread keeps a stack of the loads it is inside (loads nest through ExternalPtr); the tap
attributes a read to the top. One mutex over the package table, the bundles and the pending records.

**Exactness.** The record is the bytes the serializer itself consumed in order; the record stream
answers the same reads with the same bytes at the same positions. WHICH resources load and in what
order is the serializer's business and unchanged (the save's reference table cannot move). Proof:
section 5.

## 4. Files

`src/ext/stream_cache/{stream_record,segment_stream,stream_cache}.{h,cpp}`; seams in
`object/External.h` + `external_load.cpp`, `io/Stream.h` + `compressed_stream_read.cpp`,
`engine/UpdateLoop.h` + `loops.cpp`, `render/common/create_on_demand/stream_locate.{h,cpp}`; the
registry row (`registry.{h,cpp}`, `registry_boot.cpp`), `tests/unit/ext_stream_cache_test.cpp` (+
`sources.cmake`), DESIGN.md's Names table, LIST.MD.

## 5. Proof

- **Shadow run** (TEMP, not committed: on every hit the retail CompressedStream over the same entry was
  opened too and read in step -- every Read of the record stream compared byte for byte with retail's
  Read of the same count, Skip and SetCurrentPos mirrored, the end positions compared). A MegaMod boot
  to a new game, perf exe, warm records: **83,806,898 reads, 0 mismatches, 0 end-position differences**,
  0 stale records, crash log 0. 362,106 hits (288,722 whole-stream, 73,384 elided), 14,315 misses (all
  streams over the 8 KB limit).
- **Unit** (`tools/build.sh unit perf`): 1359 tests, 0 failures, 1 skipped (the kit cross-check); 4
  `ext_stream_cache_*`: a recording merges adjacent reads, keeps gaps as segments and answers exactly the
  recorded ranges (a read across a gap or past a segment fails); a backwards read, a failed read, an
  empty recording or more than kRecordMax bytes drop the record; a chunk round-trips through a Bundle,
  is found by its full identity only (offset AND size AND block count AND directory hash), a second
  chunk adds to it; a bad magic / count / truncation / segments out of order refuse the whole chunk.
- **Gate** (`tools/build.sh run`, debug exe, `/profile:faithful`, mods parked), on 7aa41b610: the
  world built (`[GUL::Load] EXIT`), 99 s in the town, crash log 0 bytes, the seams null (no
  `[StreamCache]` line). Not repeated on bcdaad91b (the game-dir lock was held for the 5-minute window;
  the maintainer runs it at the merge). **Resave gate** `Gate`: NEW-GOLDEN with md5
  `971aaec2cf95f5f730bb1cd1a4fa8428` -- the golden the brief names -- then **PASS** on 7aa41b610, and
  **PASS** again on bcdaad91b: the save and its reference table are byte-identical.
- **Fallback proof** (the follow-up, commit `bcdaad91b`; TEMP probe + shadow, not committed): every
  served record truncated by 16 bytes so that EVERY load read outside it and took the fallback, and
  retail's stream (a second CompressedFileStream over the same path) read in step and compared on
  every read, Skip and SetCurrentPos mirrored. MegaMod boot to a new game: **hits 362,106, extended
  362,106 (every one), 83,646,961 shadow reads, 0 mismatches, 0 end-position differences**, 0 fallback
  failures, 0 overlap mismatches, crash log 0; the widened records went out as 11 chunks (150.4 MB) and
  served the next start whole (extended 0). The unit test `ext_stream_cache_segment_stream_serves_then_
  falls_back` does the same on a synthetic three-block entry with the engine's CompressedMemoryStream
  as the fallback; `..._seeded_recording_widens_a_record` the widening.

## 6. Timings

Perf exe, MegaMod 7-part, create_on_demand on, the research branch's warm BC4 cache, `[LoadTime]`
clock (menu.opened). **Machine load: NOT clean** -- four other agents were building and running on this
machine; the compiler count at launch is given per run. Script: scratchpad `streamcache/run.sh`.

**Part A**, 3x3 interleaved, A = decode_ahead hook on (lazy HEAD), B = the hook null (this branch):

| pair | A menu.opened / load 1 | B menu.opened / load 1 | compilers at launch (A / B) |
|---|---|---|---|
| 1 | 17.93 / 9.15 | 17.37 / 8.91 | 5 / 3 |
| 2 | 17.22 / 8.99 | 17.08 / 8.68 | 0 / 0 |
| 3 | 17.57 / 9.14 | 17.02 / 8.72 | 0 / 0 |

B first in every pair: menu.opened mean 17.57 -> 17.16 s (-0.41), load 1 9.09 -> 8.77 s. (The bootprof
session's single A/B, 2000 Hz sampler on, saw 18.64-18.92 -> 18.29.)

**Parts B + C**, exe D (A + the stream cache), two cold / warm pairs; cold = no records (the recording
run), warm = the records that run wrote:

| run | menu.opened | load 1 | loads 2-4 | remount 2 | compilers |
|---|---|---|---|---|---|
| cold 1 | 21.83 | 12.76 | 0.84 / 0.80 / 0.78 | 1.94 | 6 |
| warm 1 | 16.38 | 7.78 | 0.87 / 0.79 / 0.78 | 1.90 | 4 |
| cold 2 | 21.68 | 12.87 | 0.86 / 0.80 / 0.81 | 1.99 | 8 |
| warm 2 | 17.19 | 8.28 | 0.89 / 0.82 / 0.80 | 1.92 | 6 |

Against B (same session, pairs 2-3 clean): load 1 8.7-8.9 -> 7.8-8.3 s, menu.opened 17.0-17.4 -> 16.4-17.2
s with compilers running in the warm runs. The cold run pays ~+2.9 s on load 1 for the recording (a
copy of every read, 132.7 MB in all) and the chunk writes (the cache's writer thread, after the bracket).
**Cache on disk:** 14 chunks, 150.6 MB for 362,316 records (derived.dat 927.1 -> 1077.6 MB); the same
150 MB stays in memory for the run (the bundles). Loads 2-4 and remount 2 are untouched (they are the
clean-generation visit pass and the package directory work; bootprof report items 3-4).

**After the follow-ups** (exe G = commits 1c3f6efc7 + 7aa41b610 + bcdaad91b), warm, 0 compilers at
launch: menu.opened 16.82 s, hits 362,106, read back 189 ms (two chunk generations resident, 300.8 MB,
the probe run's widened records beside the originals), dropped at the menu's bracket. Cache on disk for
a fresh MegaMod boot: 11 chunks, 150.4 MB (derived.dat +150 MB); a widened generation adds another
150 MB on disk (and the consolidated chunk a third, section 8) until the cap's compaction ages the
superseded chunks out; from the next start only the live generation is read back.

## 7. Open

- kRecordMax = 8 KB leaves 14.3k streams (picking / edge meshes, sounds, a few large descriptions) on the
  compressed path; the per-type table says they are the CPU geometry the game needs whole.
- Not built: Linux, macOS. The Metal backend's Locate reaches the same `g_altLocate` seam.
- The record stream takes BinaryReader's per-element array loop (ReadArrayFromWindows is exact-type on
  the compressed streams); a batched copy for it would be the same proof again.

## 8. Generations: the latest-generation marker

A widened record replaces the record it was seeded from (a later chunk's record wins in the `Bundle`),
so the generation that held the narrow ones becomes dead weight: still read back at every start, still
resident for the boot, and, because every `Find` puts its key on the keep list, never aged out by the
cap while the package is installed. The marker makes the dead chunks unreachable instead.

**Consolidation rule** (`ShouldConsolidate`, `stream_record.h`). At the boot's last bracket, before the
bundles are dropped, a package's bundle is rewritten when it spans at least two chunks and either the
superseded records take at least a quarter of its bytes (`Bytes() - (16 + LiveBytes())`), or it spans
more than `kConsolidateChunks` = 16 chunks. A boot whose chunks are all live (the usual 11 chunks for 9
packages: a package records in more than one bracket) is left alone, and one widened record among
thousands does not rewrite a package. `Bundle::Consolidate` writes ONE chunk of exactly the live
records, each copied byte for byte from its chunk (header, segment table, bytes, padding), in
entry-offset order; the chunk and record format is unchanged (kChunkVersion 2).

**The marker.** A derived-cache entry of its own per package: the stream cache's key with `shape[2]` =
1 (0 is a chunk) and `shape[3]` = the marker ordinal (0, 1, ...); payload 16 bytes { 'STRG', version 1,
start ordinal, ~start }. The consolidated chunk goes at the next free chunk ordinal and is queued
before its marker (the writer stores in queue order). `ReadBack` reads the markers from ordinal 0 until
a miss, takes the newest one whose start chunk is stored, and reads chunks from there to the first
missing ordinal; with no usable marker it reads from ordinal 0, as before. A missing or torn marker
therefore degrades to the old behaviour, and a marker whose chunk was never stored (the cap refused it,
or a crash or a full disk lost it) falls back to an older marker or to ordinal 0, with one log line;
the next consolidation lands at that same free ordinal. Chunks below the start are never asked for
again: they leave the keep list (`derived.used`) at once and leave derived.dat at the next compaction
past the cap. What a load reads from a record is untouched -- only which chunks are read back changes,
and the consolidated records are the same bytes (the unit test; boot 4 below served all 362,106 hits
with 0 extended and 0 fallback failures).

**Measured** (debug exe, game dir `Banished-B`, MegaMod 7-part mounted + data0 / data1 = 9 packages,
create_on_demand on, an empty derived cache at boot 1, each boot to the menu and Quit with
`BANISHED_AUTOQUIT=1`; from the `[StreamCache]` boot-end line):

| boot | hits | extended | records (in memory) | read back | chunks put | chunks read | superseded skipped | consolidated |
|---|---|---|---|---|---|---|---|---|
| 1 cold | 0 | 0 | 362,106 (150.4 MB) | 0 ms | 11 (150.4 MB) | 0 | 0 | 0 |
| 2 warm | 362,106 | 0 | 362,106 (150.4 MB) | 120 ms | 0 | 11 | 0 | 0 |
| 3 widening probe | 362,106 | 362,106 | 362,106 (300.8 MB) | 120 ms | 11 (150.4 MB) | 11 | 0 | 9 (150.4 MB) |
| 4 warm | 362,106 | 0 | 362,106 (150.4 MB) | 122 ms | 0 | 9 | 22 | 0 |

Boot 3 runs the section-5 fallback probe (TEMP, not committed: every served record cut by 16 bytes, so
every hit reads outside it and is widened): it writes a whole second generation, and at its end each of
the 9 packages is rewritten as one live chunk (`package df769ff1: 80535 live records of 2 chunks (63.8
MB) rewritten as chunk 2 (31.9 MB), marker 0`). Boot 4 reads one chunk per package, skips the 22
superseded ones, holds 150.4 MB instead of 300.8 MB, and reads back in the time of one generation (122
ms; boot 2: 120 ms; two generations: 189 ms in section 6). `derived.used` went from 952 to 760 bytes (the
22 dead chunk keys out, the 9 markers in). Crash log 0 bytes on every boot. Unit tests:
`ext_stream_cache_consolidate_keeps_exactly_the_live_records`, `..._marker_round_trip`,
`..._read_back_follows_the_marker`, `..._read_back_falls_back_when_the_start_is_missing`.

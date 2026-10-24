# DECODE -- exact bulk array reads and a wider LZ decode on the package read path (issue #13)

Block DECODE, 2026-10-22, one of four issue #13 agents (with loadscreen, readpath, largepkm).
Faithful perf work: two EXACT bodies in faithful code, no registry row, no settings key, no
`BANISHED_*` flag, no `Opt()` field, no switch. Branch `issue13/decode`.

**Short version.** On a 12 GB mod set the loader thread spent about a third of its time reading
byte arrays one byte per virtual call, and 17 % in a byte-at-a-time LZ decoder. Both now make the
same decisions as retail with wider copies. Every value the 376,440 External loads of a MegaMod
boot hand their objects hashes identical old vs new.

## 1. BinaryReader array slots -> CompressedStream::ReadArray

Retail's array body (0x1800a8410.., `serialize_family.cpp` shape 3) calls the scalar slot per
element; the scalar slot is a tail jump to `Stream::Read(sizeof(T), &arr[i])`, result ignored.

New (`serialize_family.cpp` `ReadArrayFromWindows`, `compressed_stream_read.cpp`
`CompressedStream::ReadArray`): when the stream is EXACTLY a `CompressedFileStream` or a
`CompressedMemoryStream` (typeid), the array goes through `ReadArray`, which walks the elements and

* copies in one memcpy the run of elements that lie wholly inside the active window
  (`_activeWindow` in range, `start >= 0`, `start <= pos`, `elem end <= min(end, start + 0x8000)`);
* sends every other element through the real `Read`, one element, as retail does.

Why the batch is exact: for such an element, retail's `Read` is SetWindow's fast path (stores the
unchanged `_position`, bumps the `g_swBranch.fastPath` tally, which nothing reads), a full chunk,
one memcpy and `_position += size` -- nothing else. Every boundary crossing, miss, eviction,
`DecompressFromSource`, failing SetWindow (block past the directory) and the zero-chunk spin past a
short last block happens inside a real `Read` call, at the same element, from the same state. After
a failing SetWindow the active window is free (start -1), so the batch condition is false and every
remaining element calls `Read` again, as retail's loop does (the same useCount ageing, the same
untouched destination).

Not observable from the slot: the scalar slots apply no transform (exact for all eleven element
types, not only bytes); `BinaryReader` is `final` and nothing re-stamps its vptr; neither stream
class overrides `Read`, and any other stream type (MemoryStream, FileStream, TextFileStream, the ext
text reader, a future subclass) takes retail's loop unchanged. The writer stays per element.
`String::Serialize` and `RtVideo_SerializeBlobArray` (ImageBuffer) reach the batch through the
array slots; nothing else on the load path loops scalar reads.

## 2. Compression::Decompress (0x1800c4660)

Same control-byte walk, same two bounds checks and `return 0` in the same order, same unsigned
do-while exit, same return. Literal runs (1..32 bytes, different buffers) copy in exact 8- / 4-byte
steps; matches at distance >= 8 copy 8 bytes a step (each step reads only bytes already final);
distance 1 is a memset; distance 2..7 keeps the byte loop. Nothing is written past `op + total`.
`mp < dst` is tested as `dist > op - dst` (the same answer, without forming the pointer). The dead
`g_branch` counters (null on every call, no setter, no reader) are removed. Signature, purity and
reentrancy are unchanged; the precondition (src and dst do not overlap) is retail's and holds for
both callers. Micro-bench (858 real 32 KB blocks, machine BUSY): retail 17.6-21.8 us/block, new
14.3-18.6 us/block, about 15-20 %: the decoder is bound by its per-op control flow.

## 3. Proof

| check | result |
|---|---|
| unit `decode_exact_*` (4 tests): retail's loops as oracles | encoder corpus 22,000 cases, overlap corpus (every distance 1..40 x length 3..264, mp < dst) 31,483, garbage + srcLen 0 20,256: 0 mismatches over return value AND the whole destination; ReadArray vs per-element Read through real CompressedMemoryStreams, 3,508 read steps (1,274 past the end, corrupt blocks, 1-3 windows, element sizes 1/2/3/4/8): 0 mismatches over dst, `_position`, `_activeWindow`, every window's start/end/useCount/buffer and the source stream's call log; the BinaryReader slots vs the scalar loop: 0 |
| effectiveness controls | a whole-match memcpy in Decompress: 7+ mismatches caught; ReadArray ignoring `w.end`: 21 caught |
| unit suite | 1284 tests, 0 failures, 1 skipped (the kit cross-check, no kit) |
| per-blob hash, MegaMod 7-part ON, menu scene pinned 0 (`BANISHED_MENU_SCENE=0`) | old `30d98f24c` and new `issue13/decode` + `scratchpad/issue13/shared/blobhash.patch`: 376,440 lines each, 11.47 GB of payload, files byte-identical (same order too). Unpinned runs differ only by the random menu scene and its scene-only resources (Scene6 / Scene7 / Scene1 + snow + weather) |
| External::Load counts, MegaMod ON | calls 376,431 (scene 6/7) / 376,440 (scene 1, scene 0), openFileFail 10, retNull 10, identical old vs new on the same scene (gdb on `RtObject::g_lc`) |
| External::Load counts, vanilla | menu route, scene 0 (2 mods: DebugMenu + betterFields): calls 3,438, openFileFail 8, retNull 8, old = new |
| resave gate | `tools/resave_gate/resave_gate.sh Gate` on the new perf exe, mods parked: PASS (resave == golden, 23,453,453 bytes, md5 971aaec2cf95f5f730bb1cd1a4fa8428, crash log 0) |

## 4. Timing (perf build, MegaMod 7-part ON, scene 0, to the menu)

Old = `30d98f24c` perf, new = `issue13/decode` perf, runs interleaved old/new, each under the
game-dir lock. GlobalResources = `[LoadTime] loader.resources -> loader.load` (first pair, loader
thread); menu = the poller's first `menu.update.first` (ms after process start). Machine load:
LIGHT (no compiler process at either check; other agents' game runs were serialized out by the lock).

| run | old GlobalResources | old menu | new GlobalResources | new menu |
|---|---|---|---|---|
| 1 | 44.08 s | 55.3 s | 24.88 s | 36.1 s |
| 2 | 48.67 s | 60.4 s | 25.09 s | 36.9 s |
| 3 | 43.67 s | 54.9 s | 24.40 s | 36.1 s |
| mean | **45.5 s** | **56.9 s** | **24.8 s** | **36.4 s** |

GlobalResources -45 %, menu -20.5 s. Read 6,362.9 MB in every run, peak private ~13.9 GB both.

## 5. Profile after (BANISHED_SAMPLE=2000, loader thread)

`scratchpad/issue13/decode/s_new2/` (new perf exe, MegaMod ON, scene 0, 2000 Hz; the first
2000 Hz attempt hung at boot in `engine.videomodes`, the known sampler / loader-lock hazard, and was
retried). Loader thread, 2.2-28.9 s, 36,641 samples. Self: ntdll 21.7 %, win32u 17.3 %, the LZ
decoder 27 % (Decompress 15.1 % plus its inlined copy helpers 12 %), ucrtbase (memcpy) 11.6 %,
nvwgf2umx 4.5 %, StriCmpAscii 4.0 %, allocator ~3 %, ReadArray 1.0 %. `CompressedStream::Read`
(13.9 % self before) and `SetWindow` (11.4 %) and `BinaryReader::SerializeChar` (1.8 %) are gone from
the list. Inclusive: DecompressFromSource 35 % (decode + its packed reads), GPU resource creation
(d3d11 24 %, VertexBufferData::Create 12.8 %), `FileSystem::ReadFile` 14.7 %
(`Platform::FileRead` 12.6 %), `CompressedStream::Open` 11.0 % (the per-External block directory).
What now dominates: the decoder's control flow (readpath's parallel decode still pays), the packed
read path (readpath's mmap), and D3D11 resource creation.

## 6. Notes for the other issue #13 rows

* `RtIo::g_swBranch` (SetWindow's tally) is a non-atomic global with no reader: a race if
  `readpath` drives SetWindow from several threads. Told to readpath.
* SetWindow's scan tests the ACTIVE window's start (retail quirk, compressed_stream_read.cpp §3.1),
  so with the active window loaded and another one free, any pos < 0x7fff matches the free window
  and the next Read memcpys a negative chunk. Only a `_position` store that bypasses SetWindow
  reaches it; the unit test found it with a raw store and no longer makes one. Faithful, not guarded.

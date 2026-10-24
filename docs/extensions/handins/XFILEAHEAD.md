# XFILEAHEAD hand-in: next-file prefetch for ext.decode_ahead (issue #13) -- DROPPED

Branch `issue13/xfile-ahead` (this document only), based on `issue13/integrate` `abd5beba8`. The
code is kept, unmerged, on `issue13/xfile-ahead-code` (`6f4769b22`, `8f5a0e42d`). Verdict: **DROP**,
no measurable gain on the MegaMod boot, with or without `ext.async_gpu_create`.

## 1. The question

`ext.decode_ahead` decodes blocks b+1..b+8 of the stream being read. Most of a MegaMod boot's 376 k
resources are one small block, so block 0 of each file still decoded on the loader thread
(`BinaryReader::BinaryReader -> CompressedStream::Read` 10.25 % of the loader in the integrate
profile). The task: open the loader's NEXT files and decode their first block on the workers.

## 2. What the loader reads next (TEMP trace, never merged)

A TEMP trace of every `FindOrLoad` / `External::Load` / dependency element on a MegaMod boot
(376,430 loads, 4.02 M FindOrLoad calls, 155,898 dependency elements):

* **The dependency array does not predict the next loads.** Its elements are the SOURCE files the
  compiler read (`...\CutterYard.fbx`, `...\CutterYardAO.png`, `Texture\AOTexture.rsc`), used to
  check a compiled file's freshness. The next loads come from the object's own `Serialize`
  (`ExternalPtrSerializeBody` -> `FindOrLoad`), which only the loader knows as it reads. Resolving
  each such path ahead of time would duplicate the path work (ExpandRelativePath + Find +
  BuildCompiledPath + the package BST walk, ~10 % of the loader) for no saving.
* **The package index does.** The boot loads nearly every entry of every package (pack 8: 82,582 of
  87,444 entries), and the .pkm index is sorted by path, so a model's meshes, materials and textures
  sit next to each other. Simulated on the trace: queue entries i +- 32 of the package when entry i is
  read, a 16 k-entry cache, a hit = queued at least 4 loads earlier: **96.7 %** of the loads
  (+-8 / 4 k entries: 92 %). No path, ExternalFactory, path stack or mod list is needed: the pack
  and entry index are the tag `OpenFile` writes into the FileInfo.

## 3. What was built (`issue13/xfile-ahead-code`)

Under the same row and seam (`ext.decode_ahead`, `IO::CompressedStream::_extDecodeAhead`): the same
workers, the same exactness argument, the same MAPREAD dependency, so a sibling row would only add a
second switch for one mechanism.

* `FileSystem::PackedEntrySpans(pack, first, n, ...)`: the mapped bytes of a run of entries, one
  lookup of the mapping (the wide-offset rule of `PackedMapSpan`). `FileSystem::_extPackedMapReleasing`:
  run by `PackedMapRelease` before a package view is unmapped.
* At `DecompressFromSource(0)` of a stream over a mapped package: the unseen entries i +- 1..32 are
  queued (nearest first, a per-package seen bitmap, reset when a view is released). A worker reads
  the entry's block count and block 0's header from the mapping (bounds-checked) and decodes block 0
  into an entry cache keyed by the entry's mapped start (cap 64 MB decoded, 8192 jobs, 512 queued;
  the oldest untaken block is evicted). The loader's block-0 decode takes it only when the job
  decoded exactly its block (same src pointer, csize, usize, result == usize); a queued job is
  handed back to the loader, a running one waited for. Release of a package view drains the jobs.
* `Take` of a stream block still queued hands it back to the caller instead of waiting (with block
  0 instant, the stream reached block 1 before the queue did).
* CompressedStream::Open's 6.9 %: 6.1 % is `SetWindowCount` (MemAlloc + memset of the 32 KB window,
  game heap, retail's zeroed bytes are observable past a short block). It cannot move to a worker.
  The header reads are already a memcpy from the mapping. Not attempted.
* Path handling (optional): `StriCmpAscii` 5.5 % (the 376 k-entry resource tree walk) and
  `AdjustDelimiters` 2.2 % (the Filename copy ctor in `Find`). Not attempted: §5 shows loader CPU
  is not what bounds the load on this machine.

## 4. Proof (on the code branch)

| check | result |
|---|---|
| per-blob hash (decode's `blobhash.patch` via `apply_blobhash.py`), MegaMod 7-part ON, `BANISHED_MENU_SCENE=0`, 3 boots | h1 / h2 / h3: 376,440 lines, 11,465,292,481 bytes, sorted dump IDENTICAL to `decode/i_on1` each time |
| prefetch effectiveness (TEMP stats) | 375 k block-0 decodes: 357,745 taken from the entry cache (95 %), 207 waited, 1,244 still queued, 0 rejected, 30,624 decoded and evicted untaken (8 %), 15,005 never queued |
| unit, `tools/build.sh unit` | 1311 tests, 0 failures, 1 skipped (kit cross-check). New: `ext_decode_ahead_take_hands_back_a_queued_block`, `_entry_block0_round_trip`, `_entry_other_block_or_bad_header_is_no_block`, `_entry_queue_cap_late_and_drop`, `_entry_byte_cap_evicts_the_oldest`, `_entry_real_decompress_round_trips` |
| resave gate, `tools/build.sh run` 60 s | not run: the code is dropped and is not on the hand-in branch |

## 5. Timing (perf build, MegaMod 7-part, scene 0, GlobalResources = loader.resources -> loader.load)

Interleaved under the game-dir lock, `compilers` = cc1plus / ninja processes at the end of the run.

| pair | A | B |
|---|---|---|
| integrate (`integ_devlock_perf.exe`) vs + prefetch | 20.36 / 19.71 / 19.32 s, mean **19.80** | 20.01 / 19.84 / 19.84 s, mean **19.89** |
| `issue13/gpu-create` tip vs + prefetch (merged locally) | 11.69 / 11.64 / 11.60 s, mean **11.64** | 11.77 / 11.91 / 11.92 s, mean **11.87** |

Machine load: CLEAN for all twelve runs (0 compilers); other agents' runs were serialised by the
lock. Menu times follow the same pattern (31.8 vs 31.4 s; 23.1 vs 23.4 s).

## 6. Loader profile after (2000 Hz, same window, base vs prefetch)

Samples on the loader thread, 21 s window, integrate exe vs the prefetch exe:

| | base | prefetch |
|---|---|---|
| `BinaryReader::BinaryReader` (block 0 on the loader) | 2,974 | 1,283 |
| `Compression::Decompress` (self) | 1,358 | 363 |
| `PackedMapRead` (page faults of the read) | 1,046 | 262 |
| `Hook` / `PrefetchNeighbours` / `SubmitEntry` | 1,253 / 0 / 0 | 2,165 / 409 / 262 |
| `d3d11.dll` inclusive | 10,403 | 11,445 |
| `ntdll` self / `win32u` self / `KERNELBASE` incl | 3,897 / 6,586 / 2,237 | 4,945 / 7,551 / 3,100 |
| total | 28,932 | 28,802 |

The block-0 work leaves the loader (about -1.7 k samples, ~0.85 s), and the same amount comes back
as D3D11 / kernel time on the same thread. Read: the loader is not bound by its own CPU here. The
likely mechanism is kernel contention: the workers now take the page faults of the mapped package
(the faults the loader took before), and page faults on a mapped file share the process's
address-space locks with the driver's allocation calls; with `ext.async_gpu_create` the workers
also compete with the GPU-creation threads. Not proven further: the gain is nil either way.

## 7. Verdict and notes

- **DROP** (rule: under ~1 s). Code on `issue13/xfile-ahead-code` for the record.
- Worth keeping in mind for any later loader-CPU work on this boot: removing loader CPU did not
  shorten the load on this 8-core / 16-thread box, with or without async GPU creation.
- The trace found that the dependency array lists source files, not load order (§2).
- A latent bug in the existing `BlockCache::Submit`, found while reading it: a block with
  `compressedSize > 0x8000` would overflow the worker's 32 KB + 64 scratch (retail would overflow
  its own `_alloca(0x8020)` there too, so such a block never occurs in a working file). The code
  branch guards it (`csize > kBlock` refused); the fix is one line if decode_ahead wants it.
- Raw data: `scratchpad/issue13/xfile-ahead/` (`runs/trace1/xfa_trace.txt`, `sim.py`, `rep_*.txt`,
  `runs/h1..h3`, `runs/p2_*`, `runs/gp_*`).

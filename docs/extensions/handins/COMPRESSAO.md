# COMPRESSAO: the baked AO maps as BC4, with a derived-data cache (issue #13, 2026-10-23)

> Branch `issue13/bc4`, based on `issue13/integrate` (`11dee1e9b`). Windows only (debug build, unit,
> the gate, the resave gate, the MegaMod runs below). Not built: Linux (dxvk-native: the same DX11
> code), macOS (Metal: a "not supported" body at the time; the Metal body followed on
> `issue13/metal`, section 5).

## 1. The cost

A MegaMod 7-part boot loads **7,505 R8_UNORM textures, 2.08 GB**: every one is a baked per-material
ambient-occlusion map (`WinData/Models/**/MaterialInstance/*AO_resource.crs`), mipmapped, 2D, seeded
from its resource file. BC4_UNORM holds the same single channel in half the bytes. Measured on branch
`issue13/bc4-ab` (TEMP instruments, not merged): -1.1 GB of process and GPU memory; top-level PSNR
p1 37.9 dB, p50 50.8 dB; the user saw no difference in a visual A/B and approved the extension.
Nothing reads these textures back (the `Lock` / `Read` / `Update` callers are generated BGRA images;
checked on bc4-ab). Encoding all of them costs ~36 s of CPU, ~5.4 s on the loader with 8 workers.

## 2. The change: `ext.compress_ao` (class A, default on, next start)

- **Encoder** `src/render/common/bc4/bc4.{h,cpp}`: pure C++, both palette modes, least-squares
  endpoint refit, a +-1 walk; ~57 MB/s per core in game. Built `-O2` in every flavour
  (`set_source_files_properties` in CMakeLists.txt; 7 MB/s at -O0).
- **Seam** `src/render/common/compress_ao/compress_ao_backend.h` (`CompressAoBackend::Request(on,
  cache)` / `Supported()`), called once at boot by `Ext::CompressAo::Boot` (registry_boot.cpp).
- **Encode + cache** `src/render/common/compress_ao/bc4_chain.{h,cpp}`: the levels' content key, a
  cache lookup, else bands of 16 block rows on up to 8 workers (half the hardware threads), then a
  `Put`. Bump `kEncoderVersion` when the encoder's output changes.
- **DX11** (`src/render/dx11/compress_ao/bc4_create.*`, EXT SEAM in `ImageBufferData::Create`): a 2D
  texture of engine format 4 (R8), usage 2 (seeded from a file), every level present
  (`levels->GetSize() == GetLevelCount()`), top level a multiple of 4 each way, is created as
  `DXGI_FORMAT_BC4_UNORM` with block-row pitches; the default SRV follows. `ImageBuffer::_format`
  stays 4. ext.async_gpu_create's deferred create copies the encoded levels. The loader bracket's
  close (`DeviceData::Slot4(false)`) prints `[CompressAo] textures=.. cache_hits=.. encoded=..`,
  asks the cache to persist its keep list and releases the cache's mapping.
- Off (the row, `/profile:faithful`): one relaxed load per texture create, retail's R8.

## 3. The derived-data cache (`src/ext/derived_cache/`, shared infrastructure)

Ruled allowed by the user (2026-10-22): it lives on the user's machine only, is built from the user's
own install and is never shipped (DESIGN.md section 3 rule 7). Later work (lazy GPU create, a mip
"Texture quality" setting) reuses the same store under its own producer FourCC.

| Aspect | Design |
|---|---|
| Location | `<Documents>\Banished\Cache\`, beside settings.cfg (`IO::SettingsStore::DefaultPath` over `Platform::DocumentsDir`: the platform twins decide where) |
| Key | producer (`AOB4`), encoder version, shape {width, height, levels, source format}, source byte count, Hash64 of the source levels (chained: each level seeded with the previous level's hash). No package path: a changed mod misses |
| Hash | XXH64, written in-tree from the published algorithm (`hash64.cpp`, -O2; vectors checked against xxhsum). ~10 GB/s vs ~1 GB/s for the tree's byte-wise FNV-1a: the warm boot hashes 2.08 GB in its 402 ms lookup total. 64-bit collision odds at 10^4 entries ~3e-12, and every key field is compared, so a collision would also need the same shape and size |
| Files | `derived.dat` (32-byte header + payloads, append-only), `derived.idx` (32-byte header + 80-byte records: key, offset, size, payload XXH64, record XXH64), `derived.used` (the last run's key ids), `derived.lock`, `derived.clear` |
| Crash | Open keeps the longest valid index prefix (magic, own hash, contiguous offset, inside the data file) and truncates both files to it; a payload that fails its hash on read is a miss and is stored again; the next Open keeps only a key's last record and rewrites the files without the earlier copy |
| Second instance | `derived.lock` opened exclusively (`Platform::FileOpenExclusive`: share mode 0 on Windows, `flock` on POSIX): the second instance logs one line and runs uncached |
| Full disk | a failed write truncates back, logs one line, stores nothing more that run; reads go on |
| Writes | one writer thread; `Put` queues (at most 256 MB, then waits); the loader never writes |
| Reads | `derived.dat` memory-mapped at Open; released at the end of the load that read it (the mapped pages counted +0.86 GB in the working set otherwise); later reads through the handle |
| Cap | `ext.derived_cache_mb` (256..65536, default 4096). Past it nothing is stored (one line). At the next Open an over-cap cache is rewritten keeping only what the previous run used, in order, up to the cap |
| Clear | Options dialog, Enhanced tab, "Data cache on disk: N MB" + **Clear cache**: the store stops at once, the files are deleted at the next start (they are held open) |
| Faithful | never opened: the row is off, and `OpenShared` refuses under the faithful profile |

Platform seam added: `Platform::FileOpenExclusive` / `FileTruncate` (`src/platform/win/file_rw_win.cpp`,
`src/platform/posix/file_rw_posix.cpp`; macOS uses the POSIX file).

## 4. Proof

Gate (`tools/build.sh run`, faithful, mods parked): RUNNING after autostart, crash log 0
(`[CompressAo] off`). Resave gate `Gate`: PASS (resave == golden, 23,453,453 bytes). Unit: 1325 tests,
0 failures (`bc4_*`, `ext_derived_cache_*`: round trip, torn tail, damaged payload, version / shape
/ size / producer in the key, cap refusal + keep-list compaction, second instance, clear, unknown
format; `ext_compress_ao_*`: the row, the cap key, a cached chain byte-identical to a fresh encode).

MegaMod 7-part, default profile, debug build, one run each (`scratchpad/bc4ext/run.sh`; process
sampled every 2 s, "in game" = the median of the last 30 s of in-game + 60 s):

| Run | Menu (s) | New game (s) | Encoded / hits | Private GB | Working set GB | GPU dedicated GB | Cache on disk |
|---|---|---|---|---|---|---|---|
| (a) cache empty | 59.9 | 73.3 | 4,508 / 2,994 (duplicates within the run), encode 4.78 s | 12.56 | 2.34 | 10.07 | 971.7 MB data + 0.36 MB index |
| (b) cache warm | 53.8 | 66.3 | 0 / 7,502, lookup 0.40 s | 12.49 | 2.22 | 10.06 | unchanged |
| (c) row off | 54.5 | 67.8 | - | 13.63 | 2.26 | 11.20 | - |

- **Memory: -1.14 GB private, -1.14 GB GPU dedicated** (b vs c); r8 2.084 GB -> bc4 1.042 GB.
- **Boot: the first start pays ~5 s** (a vs c); a warm start is within the noise of the row off
  (single runs, about +-1 s).
- (b) did zero encodes. The first warm run (before the mapping release) showed 3.20 GB working set;
  the table's (b) is the re-run with the release.
- 7,502 of 7,505 R8 textures qualify; `skipped=0` (no level of an unexpected size).

## 5. Not done / open

- Metal (branch `issue13/metal`, built by CI on macOS arm64, **never run on a Mac**):
  `src/render/metal/compress_ao/bc4_create.*` and the EXT SEAM in `ImageBufferData::Create`
  (`backend.mm`). The same candidates as DX11 (kind 2, format 4, 2D, the whole chain, top level a
  multiple of 4) are created as `MTLPixelFormatBC4_RUnorm` from `bc4_chain`'s output, each level
  uploaded with `replaceRegion` at the block-row pitch (the retail BC1 / BC3 path's arithmetic with
  8-byte blocks). `Supported()` needs a live device with `supportsBCTextureCompression` (Apple
  silicon on macOS 11+, every Intel Mac) and logs the reason for a "no" once. `Update` / `Read` refuse
  a BC4 texture. `Slot4(false)` prints the summary and releases the cache's mapping, as on DX11.
  There is no create worker on Metal, so the conversion runs on the loader.
- Full-disk behaviour is covered by code review only (no test fills a disk).
- Done on `issue13/dedupe` (SHAREGPUDATA.md section 4): inside the async loader bracket the
  conversion runs on the create worker; a cold start's big load went 16.8 -> 12.8 s, level with a
  warm one.
- The Enhanced tab row (our text + `SharedElements.rsc:buttonApply` with our caption) loads and its
  elements are found (`[DerivedCache] Enhanced tab: text=.. button=..`, `tabs=5 entries=5`, Options
  opened with `BANISHED_AUTOMENU_BUTTON=4`); its look and the click were not checked on screen.

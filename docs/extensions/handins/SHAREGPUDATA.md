# SHAREGPUDATA: equal immutable GPU data created once (issue #13, 2026-10-23)

> Branch `issue13/dedupe`, based on `issue13/bc4` (`6acdb7902`). Windows only (perf + debug builds,
> unit, the gate, the resave gate, the MegaMod runs below). Not built: Linux (dxvk-native: the same
> DX11 code), macOS (Metal: a "not supported" body at the time; the Metal body followed on
> `issue13/metal`, section 5).

## 1. The cost

A census on branch `issue13/research` (TEMP, XXH64 of every static create's payload and shape; perf
build, MegaMod 7-part): 9.82 GB of file-seeded GPU data, **2.54 GB of it byte-identical copies**
(2.40 GB with ext.compress_ao on): vertex buffers 1.47 GB (34.7k of 79.8k creates), index buffers
0.48 GB (59.6k of 79.8k), R8 AO maps 0.27 GB, BC1 0.20 GB, BC3 0.12 GB. Every duplicate group spans
different resource files: mesh variants, the `*_edge.crs` meshes (71 % of the duplicate VB bytes), AO
maps shared by building variants.

## 2. The change: `ext.share_gpu_data` (class A, default on, next start)

- **Key.** An IMMUTABLE, data-seeded vertex / index buffer, or a kind-2 texture (seeded from a
  resource file: shader resource only, no RTV / DSV) after ext.compress_ao's conversion, is keyed by
  the device, its whole descriptor (`D3D11_BUFFER_DESC` / `D3D11_TEXTURE2D_DESC` plus the SRV's
  format, dimension and mip range), the seed byte count and an XXH64 chained over every subresource
  with its pitch and size (`src/ext/derived_cache/hash64.*`). Equal key: the first object is handed
  out, AddRef'd, written into the same companion fields a create writes. Samplers stay per object.
- **Refused** (created as retail does): anything not IMMUTABLE (dynamic, default, staging, render
  targets, depth), a texture whose subresource count differs from what its descriptor makes D3D11
  read, or a level smaller than pitch x rows. Nothing can map or update an IMMUTABLE object, so no
  engine object can change what another draws: `Lock` / `SetData` / `Update` on a shared object fail
  exactly as they fail on retail's own immutable object, and `Read` / the save arm only copy from it.
  Nothing outside the backend reads the handles (checked: the AO pass and `Dx11ImageTexture` touch
  render targets only).
- **Table** (`src/render/common/share_gpu_data/share_table.{h,cpp}`, backend-neutral, unit-tested):
  holds its own reference on each entry's handles and counts the entry's users. Every user drops
  itself before its own Release (`~VertexBufferData`, `~IndexBufferData`,
  `ImageBufferData::ReleaseInterfaces`); the last one gets the handles back and releases the table's
  reference too. An entry therefore lives exactly as long as some user holds its object: the table
  never keeps a dead object alive and never hands out a freed one (a hit is counted under the table's
  mutex before its AddRef, so no other Drop can reach zero first). Two creates of an equal object
  racing on the two async workers: the loser's object goes and the winner's is handed out (nobody has
  seen the loser's yet: its user waits for the job).
- **Threads.** Hashing and the lookup run where the create runs: on ext.async_gpu_create's workers
  inside the loader bracket (the loader never hashes), else on the creating thread (the row off, a
  driver without concurrent creates, the main thread's creates). Drops come from any thread (render /
  main); one mutex, no D3D11 call under it; the fast path with nothing shared is one load.
- **Device lost / exit.** `DeviceData::Shutdown`, after the async drain, clears the table and releases
  its references; exit, both device-lost recoveries and a device rebuild all pass there. Users keep
  their objects; their later Drop finds nothing. The device pointer is part of the key as well, so an
  object of one device is never handed to another. Static resources are never recreated
  (`RecreateDynamic` touches generated / rendered images only).
- **Summary.** The loader bracket's close prints `[ShareGpuData] shared (run total): ...`.
- Wiring (DESIGN.md section 3): registry row `share_gpu_data` ("Shared graphics data"),
  `Ext::ShareGpuData::Boot` (registry_boot.cpp), seam `ShareGpuDataBackend::Request` / `Supported`
  (`src/render/common/share_gpu_data/share_gpu_data_backend.h`), DX11 body
  `src/render/dx11/share_gpu_data/share_create.*`, Metal body
  `src/render/metal/share_gpu_data/share_create.*` (section 5), `Entry_share_gpu_data.png`
  (`tools/ext_icons/sharegpudata.py`).

## 3. Proof

MegaMod 7-part, default profile, warm derived cache, `scratchpad/dedupe/run.sh` (process sampled every
2 s, "in game" = the median from in-game + 30 s). Interleaved on / off, perf build. Machine load:
**BUSY** (another agent's compiler ran through the series), so the boot times are indicative only.

| Run | Menu (s) | In game (s) | Private GB | Working set GB | GPU dedicated GB |
|---|---|---|---|---|---|
| on (3 runs) | 25.7 / 25.9 / 27.3 | 29.5 / 29.7 / 31.4 | 9.52 / 9.44 / 9.45 | 1.92 / 2.05 / 2.06 | 7.48 / 7.42 / 7.42 |
| off (2 runs) | 27.6 / 27.3 | 32.7 / 31.3 | 12.51 / 12.50 | 2.44 / 2.37 | 10.06 / 10.06 |

- **Memory: -3.06 GB private, -2.64 GB GPU dedicated** (with compress_ao on in both).
- Shared hits per boot: VB 34,679 (1,505 MB), IB 59,581 (489 MB), textures 6,022 (466 MB): 2.40 GB,
  the census figure. 77,935 distinct objects made; 0 textures refused; hashing 375 ms in total, on
  the workers.
- Debug build, cold cache: menu 54.3 s, in game 67.6 s, alive in town at +60 s, the same hit counts,
  crash log empty.
- **Rendering.** The showroom harness (`issue13/showroom` merged into a temporary copy of this
  branch; MegaMod, seed 12345, 48 buildings x 2 headings, debug build) with the row off vs on: 96
  pairs, no pair has more than 0.01 % of its pixels differing by more than 24 / 255 (the mean
  difference is at most 0.01; the rest is animated water and the "?" icons). Pairs looked at side by
  side: BoKaapRowhouse, NotreDameCathedral, Metalsmith, MDOldBailiffOffice, SBMediumHouse: the same
  meshes and textures.
- Unit: 1334 tests, 0 failures (`ext_share_gpu_data_*`: the row; an equal key shares and the last
  user frees; each key field (bytes, descriptor, kind, device, size) separates; a raced insert keeps
  or swaps; the view travels with its texture; clear leaves users their objects; four threads,
  8,000 create-or-share / release rounds with no object freed under a user and none leaked).
- Gate (`tools/build.sh run`, faithful, mods parked): in town at 60 s, crash log 0,
  `[ShareGpuData] off`. Resave gate `Gate`: PASS (resave == golden, 23,453,453 bytes).

## 4. The BC4 encode off the loader (ext.compress_ao, same branch, a separate commit)

Inside an armed async bracket `ImageBufferData::Create` now copies the R8 levels and the deferred job
converts them (`VideoDX11::CompressAo::ConvertForCreate`: the cache lookup, else the encode on
bc4_chain's pool and the Put) before its create. Outside the bracket the loader converts as before.
Perf build, MegaMod, quiet machine (no compiler at launch or in game), one run each; the big load is
`loader.resources` -> `loader.load`:

| Exe | Cache | Big load (s) | Menu (s) |
|---|---|---|---|
| before (`7ea0ee752`) | cold | 16.81 | 29.4 |
| after | cold | **12.80** | 25.9 |
| before | warm | 12.39 | 24.9 |
| after | warm | 12.12 | 24.8 |

A cold start now costs what a warm one does (-4.0 s); the warm start gains the lookup (~0.3 s). The
showroom with this commit and a cold cache against the row-off run: 96 pairs, at most 0.002 % of
pixels over 24 / 255.

## 5. Open

- Metal (branch `issue13/metal`, built by CI on macOS arm64, **never run on a Mac**): the same
  table through `src/render/metal/share_gpu_data/share_create.*`. Shared: a vertex / index buffer
  created not dynamic and seeded (what D3D11 makes IMMUTABLE), and a kind-2 texture (shader read
  only) with every mip of every face uploaded, after ext.compress_ao. Key: the device, length +
  storage mode, or pixel format / type / size / mips / array length / usage / storage mode, and the
  XXH64 of the uploaded bytes chained with each level's pitch and size. Metal itself could write
  these objects, so the backend refuses it as D3D11 does: `SetData` does nothing and answers true,
  `Lock` returns null, `Update` returns false. Lifetimes under ARC: the table's reference is a
  `CFBridgingRetain` released with `CFBridgingRelease`; each user drops before its strong member lets
  go (destructors, a re-Create, `Attach*`). `Shutdown` clears the table; `Slot4(false)` prints the
  summary.
- The textures' per-object samplers are not shared (D3D11 already returns one state object per
  descriptor).
- A device lost during the boot's load (retail's own window, deferred) could let the loader insert an
  entry for the old device after the clear; the device pointer in the key keeps it from being handed
  to the new device unless the new device lands at the same address.

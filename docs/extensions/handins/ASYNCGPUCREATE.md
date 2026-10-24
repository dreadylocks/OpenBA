# ASYNCGPUCREATE: GPU resource creation off the loader thread (issue #13, 2026-10-22)

> Branch `issue13/gpu-create`, based on `issue13/integrate` (`abd5beba8`). Windows only (perf + debug
> builds, unit, the runs below). Not built: Linux (dxvk-native), macOS (Metal: one no-op body added,
> not compiled here; see §6).

## 1. The cost

Loader-thread profile of the integrated build (2000 Hz, MegaMod 7-part boot, the 21 s GlobalResources
load): `d3d11.dll` 38 % inclusive, `nvwgf2umx.dll` 35 %, `win32u.dll` 25 % self (the kernel thunk of
one D3DKMT allocation per resource), `GDI32` 13 %. Most of those stacks were cut at the sampler's 20-frame
depth inside the driver, so the profile could not say which creates. A TEMP instrument (per-kind
`QueryPerformanceCounter` around every `Create*`, never merged) did:

| Kind (big load, `loader.resources` -> `loader.load`, 19.8 s) | Count | Bytes | Time | Each |
|---|---|---|---|---|
| immutable vertex buffer (`GpuBuffer_Serialize` -> `CreateBuffer`) | 73,485 | 4.35 GB | 2,831 ms | 38.5 us |
| immutable index buffer | 73,486 | 0.87 GB | 1,150 ms | 15.7 us |
| immutable texture (`ImageBufferData::Create`, usage 2, with levels) | 17,432 | 4.41 GB | 2,409 ms | 138 us |
| its SRV / sampler | 17,435 / 17,435 | | 62 / 155 ms | 3.6 / 8.9 us |
| constant buffers, dynamic buffers, render targets, shaders, states | < 100 | | ~ 3 ms | |

**6.6 s of the 19.8 s load (33 %) was driver create time.** Size histograms: buffers 512 B - 128 KB
(peak 4-16 KB), textures 1 KB - 1 MB (peak 64-256 KB). Every one is data-seeded and IMMUTABLE, made by
the loader thread inside `Engine::Loader::Run`. `D3D11_FEATURE_THREADING` on this NVIDIA driver:
`DriverConcurrentCreates=1 DriverCommandLists=1`.

## 2. The change: `ext.async_gpu_create` (class A, default on, next load)

Inside the loader bracket the engine already brackets every load with: `Engine::Loader::Run` calls
`DeviceData::Slot4(true)` before `LoadResources` and `Slot4(false)` after `Load` (retail's slot 4 is a
bare `ret`, 0x180001190; its only caller is that bracket). The DX11 backend uses it:

- `Slot4(true)` arms deferral **for the calling (loader) thread** when the row is requested and the
  driver reports concurrent creates. `Slot4(false)` waits for every job of the load and disarms.
- Inside it, `GpuBuffer_Serialize`'s load arm for a static buffer, and `ImageBufferData::Create` for a
  usage-2 texture with levels (no RTV / DSV, not dynamic), do everything retail does on the loader
  (owner stores, image registry, `ReleaseInterfaces`, descriptor, subresource pitches, the sampler
  descriptor from the same Configuration / Device reads), then hand the D3D11 calls to a worker. The
  buffer job owns retail's scratch block (the worker `MemFree`s it). The texture job owns a copy of
  the level bytes (the engine's `LevelArray` dies when Create returns).
- A worker runs the creates in retail's order and writes the same companion fields with what D3D11
  writes: buffer; texture, SRV only after a successful texture, sampler always. On failure the fields
  hold what the synchronous path leaves (null texture / buffer, a sampler).
- **Every use waits for its own job**: `AsyncCreate::Await(this)` at the top of every slot that reads a
  companion's handles or replaces them (Bind, Lock, SetData, Update, Read, Serialize, Create, Attach*,
  the destructors). Fast path: one atomic load of the pending count. Nothing outside the backend reads
  the handles (checked: no engine / game / ext code touches `VertexBufferData::buffer` or
  `ImageBufferData::texture` etc.; `ao_pass` reads only render targets, never deferred).
- `DeviceData::Shutdown` and both device-lost recoveries (`ContextData::Finish`, render_pipe's
  `FinishRecover`) drain before `ReleaseDynamic`. Each job holds a reference on the device it was made
  against, so a create queued just before a loss can never touch a freed device.
- Up to 2 workers (1 below 8 hardware threads); at most 256 MB of initial data in flight, then the
  loader waits.
- **Serial fallback**: `DriverConcurrentCreates = 0` (or no device yet) keeps retail's synchronous path.
  So does the row off, the faithful profile, and every thread outside the bracket (the main thread's
  creates, device-lost `RecreateDynamic`, in-game uploads).

**Classification: class A ext row, not an exact backend body.** The resources, contents, D3D11 calls and
failure state are retail's, and every one exists before the load completes; but WHEN a handle appears
moves (later than retail, before any use). The task's rule ("if the timing of resource availability
changes at all, prefer a class A row") decides it. One more difference: a deferred texture's
`ImageBufferData::Create` returns `true` before the create. The only caller that passes levels
(`RecreateFromFields`, from `ImageBuffer::PlatformSerialize`) discards the result, and the deferred
buffer path is reached only from `GpuBuffer_Serialize`, which discards it too.

Keep / drop: the gain is 8 s (rule: under ~1 s is dropped), so **keep**.

Wiring (DESIGN.md §3): registry row `async_gpu_create` (class A, default on, "Parallel graphics
loading"), `Ext::AsyncGpuCreate::Sync` at boot and on every toggle, the backend-neutral seam
`src/render/common/async_gpu_create/async_gpu_create_backend.h` (`AsyncGpuCreateBackend::Request` /
`Supported`), the DX11 body `src/render/dx11/async_gpu_create/`, the Metal body a "not supported" no-op
(`src/render/metal/integration.mm`). LIST.MD row added.

## 3. Proof

Timings, perf build, MegaMod 7-part ON, `BANISHED_MENU_SCENE=0`, interleaved (`time1.sh` with the
lock holder renamed). Machine load: no compiler running at the start or end of the series; agent
`xfile-ahead` was active in parallel, so **BUSY** (the coordinator re-times clean).

| Exe | big load (`loader.resources` -> `loader.load`) | menu.opened |
|---|---|---|
| base `integ_devlock_perf.exe` | 19,601 / 19,532 / 19,629 ms, mean **19.59 s** | 31.20 / 31.20 / 31.27 s, mean **31.22 s** |
| this branch | 11,597 / 11,538 / 11,540 ms, mean **11.56 s** | 22.80 / 22.64 / 22.70 s, mean **22.71 s** |
| gain | **-8.0 s (-41 %)** | **-8.5 s (-27 %)** |

Worker count (one boot each, same series conditions): 1 worker 12.14 s, 2 workers 11.75 s, 3 workers
12.08 s. With a TEMP probe on the queue: 0 ms back-pressure wait, 0 Await waits, 0 ms left to drain at
`Slot4(false)` in all four loads of the boot, with any of 1 / 2 / 3 workers: the loader never waits.

Loader profile after (2000 Hz, same window): `d3d11.dll` **0.3 %** inclusive (was 38 %), `nvwgf2umx`
0.1 % (was 35 %), `win32u` gone from the top. The handoff costs `CreateTextureDeferred` 3.4 % (the
level-byte copy, ~0.4 s) and `EnqueueBuffer` 1.0 %. What is left on the loader is decompression, the
byte-wise reads, path work (`StriCmpAscii` 10.8 %) and the allocator.

| Check | Result |
|---|---|
| `tools/build.sh unit` | 1305 tests, 0 failures, 1 skipped (the kit cross-check) |
| `tools/build.sh run`'s route (debug, `/profile:faithful`: the row off), 60 s | in town, still running at 60 s, crash log empty |
| debug, default profile (row on, `ext.render_pipe` on), new game | in town ~95 s, crash log empty |
| debug, row on, `ext.render_pipe=0`, new game | in town 60 s, crash log empty |
| `resave_gate.sh Gate`, mods parked (faithful) | **PASS**, golden md5 971aaec2cf95 |
| `BANISHED_FORCE_DEVICE_LOST=20000`, perf, row on, `ext.render_pipe` on | INJECT (render thread), `recreate OK`, `round trip COMPLETE`, alive 150 s |
| the same with `ext.render_pipe=0` (ContextData::Finish's path) | INJECT, `recreate OK`, `round trip COMPLETE`, alive 150 s |
| MegaMod 7-part, perf, row on, boot -> menu -> new game | menu 22.5 s, first game frame 28.3 s, alive 90 s, crash log empty; the frame-900 screenshot draws the town, trees, animals and terrain (all made by the workers) |

Not proved: the resave gate under the row on. Its route runs `/profile:faithful`; the per-entry
profile on this machine has class B rows on, whose pseudo-mods change the save by design. The
serializer itself is untouched: the deferred path only moves the D3D11 calls, never a Serialize.

## 4. Open

- The texture job copies the level bytes (3.4 % of the loader, ~0.4 s here). Taking ownership of the
  engine's level arrays instead would need a change to `Video::ImageBuffer::PlatformSerialize`'s
  contract; not done.
- The worker threads are not registered with the sampler (`DiagThreads`), so a profile does not show
  them.
- Metal is still "not supported". MTLDevice's resource creation is thread-safe, so a worker needs no
  device lock there; the work is the backend's own state (`stateMutex`'s buffer bookkeeping, the
  `ImageBufferData` / buffer members a create writes) and an `Await` before each object's first use,
  as on DX11.
- The sampler (1000 and 2000 Hz) hung the boot inside D3D11 device creation in 4 of 8 boots of this
  branch and 1 of 2 of the base exe: the sampler suspends the main thread inside the loader / function-table lock and then calls
  `RtlLookupFunctionEntry`. Diagnostics only; not touched.

## 5. Files

`src/render/dx11/async_gpu_create/async_create.{h,cpp}` (new), `src/render/common/async_gpu_create/
async_gpu_create_backend.h` (new), `src/ext/async_gpu_create/async_gpu_create.{h,cpp}` (new),
`src/render/dx11/buffers.cpp`, `imagebufferdata.cpp`, `devicedata_stubs.cpp`, `devicedata.cpp`,
`contextdata.cpp`, `render_pipe_finish.cpp`, `src/render/metal/integration.mm`, `src/ext/registry.{h,cpp}`,
`src/ext/registry_boot.cpp`, `docs/extensions/LIST.MD`, this file.

## 6. Platforms

Built and run: Windows (perf, debug, unit). Not built: Linux (the same dx11 code over dxvk-native;
nothing Windows-only added: `std::thread`, `std::mutex`, D3D11 calls), macOS (Metal: the two-function
no-op in `integration.mm`; Metal's CMake globs `src/render/metal/*.mm`, needs a Mac).

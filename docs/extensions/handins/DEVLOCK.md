# DEVLOCK: the loader-frame Present no longer blocks the loader (issue #13, 2026-10-22)

> Branch `issue13/devlock`, based on `issue13/integrate` (eb12931e9). Windows only (perf + debug
> builds, unit, runs below). Not built or run: Linux (dxvk-native), macOS (the Metal backend is not
> touched). Machine load during every timing below: CLEAN (no other agent running, no build).

## 1. The cost

With the loader loops drawing frames during a load (LOADPROGRESS.md §1), the MegaMod boot was
slower. The loader thread waited on `Video::Device`'s lock: retail's `ContextData::Finish`
(VideoDX11 0x1800017c0) presents and THEN drops the lock that `BeginCommands` (0x180001650) took,
and every `ImageBuffer` / `VertexBuffer` / `IndexBuffer` / `RenderBuffer` `PlatformConstruct` /
`PlatformDestruct` takes that lock (retail's inline Enter / Leave, `src/engine/video/buffers.cpp`)
to link the object into the engine's intrusive resource list. The D3D11 creates themselves
(`CreateBuffer` / `CreateTexture2D` with initial data) already run OUTSIDE the lock, in retail and
here: only the list link waited, and it waited out a whole Present.

## 2. What the lock protects

- The D3D11 immediate context. The device is created with flags 0 (no
  `D3D11_CREATE_DEVICE_SINGLETHREADED`), so `ID3D11Device` is free-threaded and the context is not.
  Every context user holds the device lock: the frame (Bind / Draw / Clear / Map / Unmap of the
  Context's dynamic buffers, recorded between BeginCommands and Finish on the presenting thread),
  and the off-frame uploads, whose callers take the lock: `SetData` (`GpuBuffer_SetData` locks
  itself), `CopyToTexture` -> `ImageBufferData::Update` (serialize_image_upload, minimap_upload,
  moddialog, gameupdateloop's image capture, the save-info thumbnails), `CopyFromTexture` ->
  `ImageBufferData::Read` (`DeviceData::SetImageBuffer`), and the buffers' Serialize save arm.
  The dynamic buffers' Map / Unmap are reached only from `ContextData` (and the Metal backend's twin).
- The engine's four resource lists (`_firstSurface`, both `_firstBuffer`s, `_firstRenderBuffer`),
  walked by `ReleaseDynamic` / `RecreateDynamic` on the device-lost path under the same lock.
- Nothing reads the owner / depth fields (+0x88 / +0x8C).

## 3. The change (dx11 backend only)

`ContextData::Finish` drops the device lock BEFORE the Present and presents under a backend
mutex, `Dx11PresentMutex()` (a `std::recursive_mutex`, `dx11_backend_access.h`). The four off-frame
context bodies take the same mutex inside the device lock their caller holds:
`GpuBuffer_SetData`, `GpuBuffer_Serialize`'s staging arm, `ImageBufferData::Update`, `::Read`.
So the Present still never overlaps another thread's use of the immediate context; it no longer
excludes a thread that holds the device lock only to link a list.

- **Lock order** is always device lock -> Present mutex; the Present holds the mutex alone and
  takes nothing inside it. Recursive, so a context use re-entered on the presenting thread cannot
  self-deadlock.
- **ext.render_pipe**'s `FinishPresent` (render thread) is unchanged: it still presents under the
  device lock. It runs only for `GameUpdateLoop` frames; every faithful frame drains it first.
- **Classification: a plain backend change, no ext row.** The engine (retail Runtime) is not
  touched; the renderer is our replacement for VideoDX11, not a byte port, and the span of its lock
  is not part of the engine's contract. Each thread makes the same D3D11 calls in the same order;
  the only new interleaving is a list link during a Present, and retail already runs the creates
  that precede it concurrently with Present. Same resources, same frames.
- **Device lost.** The recovery after a failed Present is unchanged (it ran after the unlock
  before as well). The device-lost / loader window (`reference_device_lost_loader_race`) is not
  widened: the loader's creates were already concurrent with the Present; only the list link moves,
  and it stays atomic under the device lock against `ReleaseDynamic`'s walk.

## 4. Proof

MegaMod 7-part ON, perf, default profile, `BANISHED_MENU_SCENE=0`, interleaved a / b / c, time to
`menu.opened` (`scratchpad/issue13/devlock/time1.sh`, the integ script with the holder renamed):

| Build | Run 1 | Run 2 | Run 3 | Mean | big load (`loader.resources` -> `loader.load`) mean |
|---|---|---|---|---|---|
| (a) base eb12931e9 | 36.19 s | 34.07 s | 34.05 s | **34.77 s** | 22.97 s |
| (b) TEMP: loader frames skip Present (upper bound) | 30.35 s | 30.43 s | 30.26 s | **30.35 s** | 18.97 s |
| (c) this change | 31.26 s | 31.31 s | 31.25 s | **31.27 s** | 19.63 s |

(c) wins 3.5 s of the 4.4 s that dropping the Present entirely could win (79 %). Sampler
(2000 Hz, one boot each, loader thread): `RtVideoDeviceLock` **10.5 %** of the loader's samples in
(a), **0.03 %** in (c). The rest of the gap to (b) is the Present's own GPU / driver time.

| Check | Result |
|---|---|
| `tools/build.sh unit` | 1305 tests, 0 failures, 1 skipped (the kit cross-check) |
| `tools/build.sh run` (debug, faithful) | in town, 72 s, no crash |
| default profile (`ext.render_pipe` on), debug, new game | in town, 54 s, no crash |
| `resave_gate.sh Gate`, mods parked | **PASS**, golden md5 971aaec2cf95 |
| MegaMod boot | menu reached in all 7 (c) boots, crash log empty |
| `BANISHED_FORCE_DEVICE_LOST=20000` (debug, faithful, in game) | INJECT, `recreate OK`, `round trip COMPLETE`, still running 30 s later |

## 5. Open

- A device loss injected at boot (`BANISHED_FORCE_DEVICE_LOST=10`, during the vanilla boot's loader
  frames) crashes in `MainLoop::OnVideoError` (mainloop_video.cpp:542, `Save::SaveLoad::_singleton`
  still null: READ of 0x58). The base exe crashes identically, so this change has nothing to do with it;
  it is the unguarded retail walk reached before SaveLoad exists. Not investigated further.
- Linux (dxvk-native) not built: the change is `std::recursive_mutex` + the same D3D11 calls.

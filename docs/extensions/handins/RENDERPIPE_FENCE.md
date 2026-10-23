# RENDERPIPE_FENCE -- `ext.render_pipe`: the fence released before Present

Addendum to RENDERPIPE1.md. Block RENDERPIPE_FENCE, 2026-10-20, from `gpuframe/ideas_cpu_submit.md` section 2.1
(GPUFRAME.md rank 6). Static gate only: the game was NOT run by this block (the coordinator owns the game dir).

## 0. Short version

- Before: THE FENCE (`kExtBeforeSteps`, threaded) waited for the whole emit just published, Present included,
  so every step frame ran serially: the end of the emit, the fence, the steps, the main half.
- Now: the render thread signals **released** right after the post-draw callbacks (`packet.h` `Emit`:
  `ops.Released(p)` between `PostDraw` and `EndCommands`). The fence waits on "released"; the emit's **tail**
  (EndCommands' MSAA resolve, `_frameUpdateTime`, `++Context::_frameIndex`, Present + Unlock, the forced-loss
  drive, the removed-reason test) runs **while the steps run**.
- The hand-off and every drain still wait for **idle**. A tail the fence left in flight is **settled** (wait
  idle, read the result, recover a lost device) at the next Frame tail: before the faithful tail of a drained
  frame, or, in a pipelined frame, after the main half and before the publish.
- Fix 2: `next.frameIndex` and `next.modelIndex` come from `_held` (+1) whenever a packet is pending **or** a
  fenced tail may be in flight, so the main thread never reads `Context::_frameIndex` while the render thread
  increments it.
- The command stream is unchanged (the same calls, the same order, the same thread); only the main
  thread's wait moved. Class A, no new row, no new key, no new flag, no new instrument, no faithful file touched.

## 1. What changed (all in `src/ext/render_pipe/` and its unit test)

| file | change |
|---|---|
| `handoff.h` | `_released` (atomic, true at rest) beside `_busy`. `Publish` clears it; `Release()` sets it (lock, release-store, notify); `Done()` stores `busy = false` **then** `released = true`; `WaitReleased()` = fast acquire load, else wait on the same condition variable as `WaitIdle`. |
| `packet.h` | `Emit` gains `ops.Released(p)` after `ops.PostDraw()`. Not a retail step: the boundary of what the sim may not overlap. |
| `pipe_driver.h` | `BeforeSteps` threaded -> `FenceReleased`: wait released, `Drained`, count the emit; if the render thread is already idle (tail done, or a failed BeginCommands that never released) it is the old full join (result, latency, recovery before the steps); else `_tail = true`. `SettleTail` (wait idle, `Collect`, `Recover`, false on lost / failed) runs at the top of every unpipelined `Tail` and after `MainHalf` in `Pipelined`; a false settle discards the packet just gathered, as the hand-off does. Fix 2 uses `inFlight = pending \|\| _tail`. `Host::Emit` takes `Handoff*` (non-null = threaded, the emit releases it) instead of `bool`. `Join` split into `WaitIdle` / `Collect` / `Recover` helpers, same order and counters. `TailInFlight()` for the test. |
| `render_pipe.cpp` | `EngineOps::Released(p)`: the RenderPipe shadow's post hash (COMPPROF=4 only), then `handoff->Release()`. `EngineOps` carries `Handoff*` instead of `bool threaded`. `EngineHost::Emit` takes the post hash from the release, not after Finish. |
| `tests/unit/ext_render_pipe_test.cpp` | `Released` in the recorded order; FakeHost models the tail (`tailing`: the frame index bump and the loss decision happen after the release) and checks the context index is never read and recovery / AfterJoin / FaithfulTail never run while anything is in flight; the scripted run checks the steps only overlap a tail (never an emit before release) and that every Frame tail ends with no tail in flight. New `ext_render_pipe_handoff_release_comes_before_idle`: WaitReleased returns while the job is still busy (the job waits for the "step"), and a job that never releases is seen idle at WaitReleased. |

## 2. The ordering argument

Release pair: everything the render thread did up to `PostDraw` (pass 2, SaveLoad::Draw and its capture flags,
SetCamera, the tool draw and the road tool's path, the UI draw, the post-draw callbacks) happens-before
`Handoff::Release` (lock + release-store), which the main thread's `WaitReleased` acquires before the steps.
So every write RENDERPIPE1 section 2 lists for the idle pair that the steps can read is still published to them,
except the tail's own writes, which no step reads:

| tail write / read | who else touches it while the steps run | verdict |
|---|---|---|
| `ContextData::EndCommands`: `dd->colorBuffer`, `swapChain->GetBuffer`, `ResolveSubresource` (immediate context) | device-owned; replaced only by `DeviceData_Resize` (BeginCommands, a drained faithful frame) and the recovery (main, at a settle). Step-side immediate-context users take `Video::Device::Lock`, which the render thread holds from BeginCommands to FinishPresent's Unlock, so they serialize behind Present (see risk 6). | safe |
| `MainLoop::_frameUpdateTime` (written) | read by the next emit's SetTime (render thread, after the next publish) and by `debugui.cpp:720` (already racy, RENDERPIPE1 section 9.5); the faithful `GameUpdateLoop::Draw` reads it only on a faithful frame, after the settle | as before |
| `++Context::_frameIndex` | main reads it only through `ContextFrameIndex`, now never with a tail in flight (fix 2); faithful `Renderer::Draw` (`wave27_lane_gfxbig.cpp:949`) runs only on faithful frames, after the settle. No step reads it (the `_frameIndex` the sim stamps is `ParticleInstance::_frameIndex`, a different static) | safe |
| `FinishPresent`: `Configuration::vsync`, the swap chain, `device->Unlock`, `GetDeviceRemovedReason` | a step applying the options writes `vsync` (an int read once per Present: a benign race, the value is 0 / 1); the swap chain is only resized at BeginCommands | safe |
| the packet `_held` | main reads it (fix 2) and rewrites it only after the settle | safe |
| `_result`, `presentUs` | read by main only after idle (`Collect`) | safe |
| the RenderPipe shadow's post hash (the Renderer and the sun) | moved before the release: after it the main half may run `SunLight::Set` (renderIdle is true after a fence) | safe |

Fixes 4 / 5 ("renderIdle" = nothing pending) now also hold with a tail in flight: `SunLight::Set` and
`UpdateCamera` write what pass 2 and the tool draw read, and both finished before the release.

`Done` stores `busy = false` before `released = true`, so a main thread that sees "released" through Done also
sees "idle": a failed BeginCommands (which returns before `PostDraw` and never releases) is always handled as a
full join at the fence, exactly as before (its DrawFailed / Context::Reset completed before the steps).

## 3. The risks and their handling

1. **Device lost during an overlapped Present.** The loss is reported in `_result` after the release, so the
   steps run before the recovery (retail recovers first). Handling: the recovery runs at the settle, on the main
   thread with the render thread idle (`Host::Recover`, unchanged), and the packet gathered after the steps is
   discarded (`PipeState::Discard`), exactly as at the hand-off. The minimap overlay: a step that uploads it
   (`gameupdateloop.cpp` step 16, `overlayDirty`) writes the OLD texture under the device lock and clears the
   flag; the recovery's `GameUpdateLoop::OnVideoError` releases both images and `Minimap::OnVideoRestart`
   (`minimap_upload.cpp:136`) **re-uploads both from their retained pixel arrays** (`terrainPixels`,
   `overlayPixels`), which the step updated first. So the overlay is current after the round trip and
   `Host::Recover` does **not** need to set `overlayDirty`; nothing was added. The terrain (`WorldGeometry::
   OnVideoRestart`) rebuilds from CPU data the same way. Under the synthetic `BANISHED_FORCE_DEVICE_LOST` the
   device is not really removed, so the step's uploads succeed. Under a real removal a step's upload talks to
   a removed device (`ImageBufferData::Update` does not check its `Map` result, retail's code): that is the
   same exposure retail has to a removal that happens between two Presents (removal is asynchronous; retail
   only learns of it at Present too), so it is not new in kind, only in window (the steps of one frame).
2. **`_frameUpdateTime`.** Written in the tail, after the release. Readers: the next emit's SetTime (same thread,
   ordered by the publish) and the debug UI (racy since RENDERPIPE1). Unchanged in value: it is still Present
   time minus the packet's own Frame start.
3. **A step that pumps a resize or a loop change while Present is in flight.** `SizeWindow` /
   `ApplyChangedSettings` only write `Device::backBufferWidth/Height` and `settingsChanged`; nothing in the tail
   reads them. The next Frame tail sees `settingsChanged` (or the loop change, or `_quit`) and runs unpipelined:
   `Tail` settles the tail (**waits idle**) before the faithful `MainLoop::Draw`, whose BeginCommands runs the
   resize. A loop change deletes the GameUpdateLoop inside the step: the tail reads none of its objects (the
   post hash moved before the release), and the Video objects it destroys are released with COM `Release`
   (thread-safe; the immediate context keeps its own references while bound). `MainLoop::_contextDirty = 1` is
   read by `ClearResourcesIfDirty`, which ran before the release.
4. **RenderDoc frame boundaries.** `RenderDocIntegration::OnFrame` stays at the packet's start and Present on the
   render thread: unchanged.
5. **`TweakOverlay::BeforePresent`** stays in `FinishPresent` on the render thread. The pump now runs (inside a
   step) while it draws; its event queue and action list are under `g_mu`, held only for the copy on either side,
   and `MainThreadTick` applies actions outside it with no device lock inside `g_mu`, so no lock-order cycle.
   `g_open` (acquire / release) publishes `InitContext` before the overlay draws.
6. **Step-side device users** (audit, `CopyToTexture` / `CopyFromTexture` / `Map` callers): the minimap overlay
   (step 16), `minimap_upload.cpp`, `serialize_image_upload.cpp`, `moddialog.cpp`, `saveload_saveinfo.cpp`, the
   buffer read-back in `buffers.cpp` all take `Video::Device::Lock` and so wait for the tail's Unlock (correct,
   and it costs the overlap only on those steps). `minimap_paint.cpp` and `w2_takescreenshot.cpp` run only on
   faithful frames (PlanTail), after the settle. `noiseui.cpp` (the NoiseUI terrain debug panel) uploads without
   the lock; it is a dev panel not reached in play. If it is ever opened with the thread on, its upload races
   the tail's Present on the immediate context: a finding to fix there (take the lock), not here.
7. **Exclusive fullscreen** (RENDERPIPE1 section 10.4): unchanged in kind; the main thread now pumps during more
   of the Present instead of parking in the fence wait, which can only help.

## 4. Counters (no new instrument)

`[Stress::pipe]` / `pipe_wait_us` keep their meaning: `waits` / `waitUs` now include the fence's wait for
"released" and the settle's wait for idle (`waitsFence` counts the fence and the drain settles, the pipelined
settle counts like the hand-off). `emits` counts the packet once, at the fence. `latency` (publish to Present
done) is counted at the settle, so `latencyN == emits` still holds.

## 5. How to A/B (for the coordinator)

- Legs: the base exe (main / `gpuframe`) vs this branch, everything on + render_pipe, heavy, speed 10, zoom 130
  (GPUFRAME's town), interleaved x2, machine CLEAN.
- Expect: `pipe_wait_us` down by about 0.84 ms x 60 / 265, **~190 us per draw frame**; the hitch CSV step class
  `med_outside_ms` down by **~0.8 ms**; `draw_emit_ms` **unchanged** (the emit does the same work; only the
  wait moved); `update_ms_mean` / sim per iteration unchanged. If `draw_emit_ms` grows, steps are contending for
  the device lock (risk 6) or the CPU.
- Proof the stream is identical: `tools/trace_oracle` S3 / S4 / S6 = 0 against the base exe.
- Shadows: the RENDERPIPE1 section 10.1 runs (`BANISHED_STRESS_COMPPROF=4 STRESS_EXT=render_pipe`, heavy /
  compact, speed 10 / 50): RenderPipe, RenderPipeFrame, RenderPipeIdem at 0; `discarded=0`, `recoveries=0`.
  RenderPipeFrame is the fix-2 check (the packet's index equals the context's at pass 2).
- Device lost: `BANISHED_FORCE_DEVICE_LOST=12000 STRESS_EXT=render_pipe`, COMPPROF=0, heavy speed 10: one
  `INJECTING ... render thread`, one `[RenderPipe] device lost in the render thread's Present`, one `round trip
  COMPLETE`, `recoveries=1`, `discarded` 0 or 1, the game keeps drawing with the minimap intact (look at it in
  a screenshot after the loss: risk 1), clean quit.
- By hand: a window resize and a return to the menu during play with the row on (risk 3).

## 6. Gates run here

- `tools/build.sh build perf --jobs 2`: exit 0, no warning in the touched TUs.
- `tools/build.sh unit perf --jobs 2`: **1192 tests, 0 failures, 1 skipped** (the new
  `ext_render_pipe_handoff_release_comes_before_idle` included).
- No faithful TU includes the changed headers (`packet.h`, `handoff.h`, `pipe_driver.h` are included only by
  `render_pipe.cpp` and the unit test), so no object proof was needed.
- clang-format is not installed on this machine: the touched lines were formatted by hand to `.clang-format`.
- Not run here (the coordinator's): the game, the stress A/B, trace_oracle, the COMPPROF=4 shadows, the
  forced device-lost run, the debug build.

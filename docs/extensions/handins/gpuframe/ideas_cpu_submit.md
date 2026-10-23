# Ideas: CPU submit path and the render thread (stress town, zoom 130, everything on)

Reviewer: ideas-cpu-submit, 2026-10-20. Read-only review. Nothing was built or run. No capture was opened.
Inputs: GPU_FINDINGS.md, plain_allon_zoom130_stress.csv, allon_zoom130.timing.{txt,json},
faithful_zoom130.timing.json, and the same run's `standalone_error.log` `[Stress::pipe]` lines and
`standalone_stress_hitch.csv` in `C:/GOG Games/Banished/`. The CSV there is byte-identical to the
scratchpad copy, so these come from the same run.

## 0. What the numbers already say

### 0.1 The frame is two populations: step frames and draw-only frames

The CSV's `frame` column counts sim steps, not draws. 600 steps per 10 s row is 60 steps/s.
`update_ms_mean` 1.48 is per step: ten iterations of 0.148 ms at speed 10. At 265 draws/s, only
**one draw frame in 4.4 holds a step**.

The `[Stress::pipe]` window lines (10 s each, last eight windows, all alike):

| per 10 s window | value |
|---|---|
| pipelined frames | ~2650 |
| blocking joins (`waits`) | ~1300 |
| of which at the fence (`fence`) | 600, one per step |
| so blocking hand-offs | ~700 |
| non-blocking hand-offs | ~1350 |
| `wait` | ~1.05 ms per draw frame = ~2.8 s of the 10 s |
| `latency` (publish to Present done) | 2.37 ms = emit 2.37 ms |

Latency equals the emit length within ~5 us, so the render thread starts the packet as soon as
it is published. **The wake cost of the hand-off is negligible**; a spin-then-park wait is not worth
building.

The hitch CSV prints the class medians for the frames that hold a step:

| step-frame period (median) | ms |
|---|---|
| period | **8.5** |
| update (GameUpdateLoop::Update) | 1.33 |
| draw (the main half of the frame before) | 1.86 |
| outside = everything else | **5.3** |

So 600 step periods take ~5.1 s of every 10 s. The other ~2050 draw-only periods share the
remaining ~4.9 s, about **2.4 ms each**. **23% of the frames take 51% of the wall time.** Only
1.33 ms of each step period is the sim.

The main thread's budget closes per second as 265 x 2.32 (main half) + 60 x 1.48 (steps) + 265 x
1.05 (waits) = 998 ms. Nothing else of size runs on the main thread.

### 0.2 What "packet", "emit" and "hand-off wait" are (src/ext/render_pipe/)

- **Main half** (`draw_main_ms` 2.32). This is `GameUpdateLoop::Draw` steps 1 to 6 plus pass 1:
  `Ext::RenderPipe::Pass1` in `passes.cpp`, with `AddScene`, `BeginDrawGroup`, the global
  `AddDrawItems`, `AddCulledDrawItems` (draw_gather plus its cull helpers) and three sorts per
  scene entry, over the main view, the sun shadow and the water reflection. It runs on the main thread.
- **Emit** (`draw_emit_ms` 2.37). This is `Ext::RenderPipe::Emit` (`packet.h:82`) on the render
  thread, from `FrameTailTime` and `BeginCommands` (which takes the device lock) through `Finish`
  (Present, then unlock).
- **Packet** (`draw_packet_ms` 1.53). This is the part of the emit between `PacketBegin` and
  `PacketEnd`: SetTime, the weather constants, **pass 2** (`Ext::RenderPipe::Pass2`, all 2135
  draws), SaveLoad, the camera, the tool and the UI. Emit minus packet is **~0.84 ms per frame**:
  BeginCommands, ClearResources, the post-draw callbacks, EndCommands (the MSAA resolve), the frame
  index and **Present**.
- **Hand-off wait** (`pipe_wait_us`). This sums every blocking join per draw frame. There are two
  kinds. The fence (`Driver::BeforeSteps` to `Join`) runs before every step and always waits for
  the whole emit just published. The hand-off (`Driver::Pipelined` to `Join`) runs after the main
  half of N+1 and waits for the rest of emit N.
- **Pipeline depth 1.** The fence drains it before every step. After the fence the main half of a
  step frame runs with the render thread idle. A step frame is therefore fully serial: the end of
  the previous emit, the fence wait, the step, then the main half.
- **It is not the frame limiter.** The cap is off. Present is inside the emit, so any Present
  blocking from DXGI queue back-pressure shows up as emit time and then as main-thread wait.

### 0.3 Who waits on whom

The main thread waits on the render thread, about 130 times per second: 60 fences and about 70
hand-offs. About 135 times per second the render thread finishes first and waits for the main half.
**Neither side is the bound on draw-only frames.** Main half and emit are both ~2.3 to 2.4 ms. The
stall is around the step.

That gives the leverage of a saving, as long as the GPU keeps up (see 0.5):

| a saving of 1 ms on | pays on about | wall saved |
|---|---|---|
| the render thread (emit) | 130 frames/s: every fence and every blocking hand-off | ~130 ms/s |
| the main thread (main half) | ~195 frames/s: 60 step frames with the render thread idle + 135 main-bound hand-offs | ~195 ms/s |

**The main half is the better lever, not the emit.** An emit-only cut stops paying once the emit
falls below the main half on draw-only frames.

The 5.3 ms of "outside" in a step period is the largest unexplained block in the frame. At most
~2.4 ms of it is one fence wait. The previous frame's hand-off should add only ~0.5 ms, since emit
2.37 minus main half 1.86 is 0.5. That leaves **~2.4 to 2.9 ms per step unexplained**. Three
hypotheses fit:

- **(H1) The step's own overhead outside the Update bracket.** That covers `Input::Device::Update`,
  the audio sweep, `Platform::Pump`, `MainLoop::Update` around the game loop's Update and the
  post-update callbacks (`mainloop_frame.cpp:371-440`).
- **(H2) Present blocking.** The DXGI queue fills during the fast draw-only frames, and the emit
  just before the fence carries the blocking.
- **(H3) Uneven emits.** The ~700 blocking hand-offs average ~1.9 ms each, which uniform emits of
  2.37 against main halves of 1.86 or more cannot produce. Some emits must run to about 4 ms. The
  likeliest are the packet after a step, where every pose changed and the per-frame memos start cold.

### 0.4 Per-draw CPU cost, from the capture's event ids

RenderDoc's event id counts every D3D11 call, so the gap between consecutive draws in
`allon_zoom130.timing.json` is the number of D3D calls per draw. There are 9158 calls in 2135
draws, 4.3 per draw:

| group (vs/ps in the capture) | draws | D3D calls per draw | calls |
|---|---|---|---|
| terrain (1476/1480, 1473/1490, 1485/1489) | 479 | 1.1 | ~530 |
| instanced trees, 52 per batch (6 pairs) | 488 | 2.3-2.5 | ~1170 |
| other instanced meshes | ~400 | 3.4-5.8 | ~1700 |
| **building ground decals (342/346)** | 307 | **7.1** | **2190** |
| **chimney smoke (575/579)** | 244 | **8.5** | **2070** |
| UI (288/302) | 30 | 8.5 | 255 |

The decal and smoke draws are 26% of the draws and **46% of the D3D calls**. Their path has these
causes:

- **Decals.** `WorldGeometry::Draw` (`geometry_draw.cpp:328`, modes 0x2000 and 0x4000) copies a
  **static CPU batch** into the dynamic buffers every frame. That is `BeginIndexedVertices`, two
  `Map(NO_OVERWRITE)`, two memcpys and `EndIndexedVertices`, giving two `Unmap`, an
  `IASetVertexBuffers` at the new ring offset, an `IASetIndexBuffer` and the draw, plus
  `SetTransform` / `SetColor` constant updates.
- **Smoke.** `ParticleInstance::Draw` (`wave27_lane_gfxpart.cpp:724`) does the same with
  per-frame vertices.
- **The index rebind is redundant.** `EndInstancedIndexedVertices` (`contextdata.cpp`) marks the
  index buffer dirty whenever the ring offset moves. `IndexBufferData::Bind` (`buffers.cpp:623`)
  then always binds offset 0, so the call repeats the same binding on every dynamic draw.
- **Constants.** Every constant buffer is DEFAULT usage, refreshed by a full `UpdateSubresource`
  in `ConstantBufferData::Bind` (`buffers.cpp:760`). That covers the instanced transforms (52 x 64
  bytes) and the bones (always 0xcc0 bytes when any bones are bound).
- **No redundant state elsewhere.** Setters filter before setting dirty bits
  (`Context::SetVertexBuffer`, `context_bind.cpp`). `BindResources` acts only on dirty bits, so
  terrain and trees are already near one call per draw.

On the "1.1 us per draw" figure: the divisor is wrong. About 0.84 ms of the emit is per frame:
Present, the resolve and BeginCommands. The packet is **0.72 us per draw**. That figure includes
our own pass-2 work: ~40k instance-transform lerps plus AsMatrix, the bone matrices, the particle
vertex fill and the decal memcpys. It also includes ~4.3 D3D calls.

Calls on the render thread are mostly enqueues. NVIDIA's threaded driver had its own worker thread
at 12% of process samples in IBS_20261012 §2.2. **Nothing in the tree says how the 0.72 us splits.**
For a D3D11 renderer with tree-like state, about 0.2 to 0.4 us of app-thread cost per draw is a
reasonable floor. The excess here sits in the dynamic draws, not in the instanced ones.

The **4.1 us per decal draw on the GPU** in GPU_FINDINGS is mostly the replay's per-event timing
floor, not GPU work. The faithful replay sums to 4.47 ms while that build ran at about 475 fps (2.1
ms per frame) under RenderDoc, so replay inflates by at least 2.1x, and most strongly on tiny draws.

### 0.5 The gate on everything below: is the GPU the bound at zoom 130?

If replay inflates the GPU time by the faithful arm's 2.1 to 2.8x, the all-on GPU frame is
**3.7 to 5.0 ms against a 3.73 ms frame**. A GPU-bound run looks exactly like section 0.1. The
three-frame DXGI queue (blt model, `devicedata.cpp:468`) fills during the fast draw-only frames.
Present then blocks inside the emit, the fence inherits the blocking, and the 5.3 ms "outside"
follows (H2). **If the GPU is the bound, no CPU-submit idea below raises fps on this box.** They
then matter only for the 4-core target and for closer zooms.

## 1. Settle these before building anything

**M0. GPU busy (no exe change, about 15 min).** Use either test:

- **Monitor the GPU** during the plain stress run, with `nvidia-smi dmon -s u -d 1` or PresentMon
  (GPUBusy and CPU-wait per frame). PresentMon needs admin rights for ETW, so the user runs it.
- **Run GPU-only A/B legs.** These change fill and nothing on the CPU: `ext.max_shadow_res` back
  to retail 2048, `ext.ambient_occlusion` off, or `ext.foliage_aa` off. Each is a settings.cfg
  key, one leg each, interleaved x2, machine CLEAN.
  - If fps rises clearly and `draw_emit_ms` falls with no change in `draw_main_ms`, the run was
    GPU-bound and Present was blocking.
  - If fps holds, the CPU is the bound and sections 2.1 to 2.6 apply.

**M1. One sampled run at this zoom.** Use `BANISHED_SAMPLE=2000`, not 4000, which can hang boot
(memory). Same stress, same settings.cfg, same camera, one run, read with
`tools/sample_report.py`. The render thread ('R') registers through `System::Thread`, so it is
sampled. It must settle these:

1. **Render thread, emit split by stack.**
   - Time inside `IDXGISwapChain::Present` (dxgi, d3d11, the driver and the ntdll waits under it).
     This decides H2 and idea 2.5.
   - Time in pass 2 under `ConstantBufferData::Bind` (UpdateSubresource), under `BufferData_Lock`
     / `Unlock` (Map / Unmap) and under `BindResources`.
   - Time in our pass-2 math: `GetInstanceTransform` / AsMatrix / Lerp, `GetBones`,
     `ParticleInstance::Draw`, the `WorldGeometry::Draw` memcpys and `UI::Draw`.
2. **Main thread, the waits split by caller.** Compare `Handoff::WaitIdle` under
   `Driver::BeforeSteps` (the fence) with `Driver::Pipelined` (the hand-off). Then compare step
   overhead outside `GameUpdateLoop::Update`: `Input::Device::Update`, `Platform::Pump` and the
   post-update callbacks. This decides H1. The pipe counters keep only one `waitUs` total, and the
   stacks split it without a new counter.
3. **Main thread, the main half split.** The question is which scene entry's gather and sort costs
   what: main view, sun shadow or reflection. Also compare `AddDrawItems` for the late pass
   (decals and particles) with the cull walk.
4. **H3, offline from the same file.** The sample lines carry a millisecond timestamp. Main-thread
   samples inside the step mark the step intervals. Classify render-thread samples by "first packet
   after a step" against "other". If the post-step emit is about 2x the others, H3 holds. Its fix
   is in the main half and the memos, not in the hand-off.

The sampler costs about 5% (perf/README). Read its shares, not its milliseconds, against the
baseline.

**M2. Free and offline: count calls by type in the existing capture.** This is a five-minute
RenderDoc Python script in the style of `tools/trace_oracle/gpu_timing.py`. It reads the
structured file only and needs no replay timing. It tabulates Map, Unmap, UpdateSubresource,
IASetVertexBuffers, IASetIndexBuffer, `*SetShaderResources` and `*SetConstantBuffers` per vs/ps
group, which turns section 0.4's per-draw gaps into exact per-type counts for ideas 2.2 and 2.3.
Run it when the machine is not benchmarking: it loads the 8192 shadow capture.

## 2. Ideas, ranked (CPU-bound case; the gains assume M0 says the GPU keeps up)

Each idea gives the change and where, the expected gain with how it was estimated, class and seam,
risks, effort, and how to A/B it. Every A/B is the stress town at zoom 130, user profile,
`ext.render_pipe` on, machine CLEAN, interleaved x2. Read `fps`, `frame_ms_mean`, `frame_ms_p95`,
`draw_main_ms`, `draw_packet_ms`, `draw_emit_ms`, `pipe_wait_us`, and the hitch CSV's
`med_outside_ms` for the step class.

### 2.1 Release the fence before Present: let the step overlap the emit's tail (rank 1)

**What changes.** Today the fence waits for the whole emit, including EndCommands, the frame-index
bump and Present. Nothing after `PostDraw` reads sim, scene or UI state. Only the device and the
Context are used, and steps reach the device only under `Video::Device::Lock`
(`gameupdateloop.cpp:1227` and the other users listed in RENDERPIPE1 §3). The render thread would
signal "released" after `ops.PostDraw()`.

- **Where.** `packet.h:Emit` gets one new op after `PostDraw`. `handoff.h` gets a second state
  (`_released` beside `_busy`, with the same release/acquire pair). `pipe_driver.h` waits on
  "released" at `BeforeSteps`, and still waits on "idle" at the hand-off and at every drain.
- **Fix 2 in `Driver::Pipelined`.** `next.frameIndex` and `modelIndex` must come from `_held`
  whenever an emit tail may be in flight. That means after a fence and not only when pending, so
  the main thread never reads `ctx->_frameIndex` while the render thread increments it.

**Gain.** The tail's per-frame emit is ~0.84 ms. Overlapped with each of 60 steps a second, that
saves about 50 ms/s: **frame mean -0.19 ms, about +5% fps, on the main thread's critical path.**
The step-period median should fall by up to ~0.8 ms. If M1 shows Present blocking (H2), the gain
is larger on this box, but then the GPU is the bound anyway.

**Class and seam.** This is inside `ext.render_pipe` (class A). It adds no registry row, no key and
no engine seam. The command stream is unchanged.

**Risks.**
- **Device lost during an overlapped Present.** The step runs before the recovery, and retail
  recovers first. A minimap upload in that step would go to the removed device. Check whether the
  recovery re-uploads the overlay; if not, mark `overlayDirty` in `Host::Recover`. Test with
  `BANISHED_FORCE_DEVICE_LOST=12000`.
- **`_frameUpdateTime`.** It is written after release. It is wall-clock presentation data, already
  read racily by `debugui.cpp:720`.
- **Stepping during Present.** A step that pumps a resize or loop change would run while Present
  is in flight. Those paths already drain at the next tail, but the drain must wait for idle before
  `BeginCommands`' resize path, and today it does.
- **Hooks around Present.** RenderDoc frame boundaries and `TweakOverlay::BeforePresent` stay on
  the render thread and are unchanged.

**Effort.** 6 to 10 h, plus the RENDERPIPE1 §10 shadows (COMPPROF=4) and the device-lost run.

**A/B.** `pipe_wait_us` should drop by ~0.84 x 60 / 265, about 190 us per frame. The hitch CSV
step class `med_outside_ms` should drop by ~0.8. `draw_emit_ms` should not move.

**Verify.** `tools/trace_oracle` S3, S4 and S6 should be 0 against the current exe. The stream is
identical by construction.

### 2.2 Static GPU buffers for the decal and road batches (rank 2)

**What changes.** In `WorldGeometry::Draw` (`geometry_draw.cpp:328`) the 0x2000 (`_decals`) and
0x4000 (`_roadBatches`) branches re-upload a CPU-resident `GeometryBatch` through the dynamic ring
on every draw of every frame: shadow, reflection and main. Upload each batch once into a
DEFAULT/IMMUTABLE VB/IB owned by the extension, or suballocate them in one big static pair. Then
draw it with `SetVertexBuffer` / `SetIndexBuffer` and `DrawIndexed`. That removes 2 Map, 2 Unmap,
the two memcpys and the ring rebind.

**Where.** The draw-side arm sits at the two `IndexedDrawFromBatch` calls. A rebuild hook is needed
where `_decals` and `_roadBatches` are rebuilt. That happens in steps (`WorldGeometry::Update` and
the decal / zone / terraform writers). A per-batch generation compare (pointer, size and a version
bumped at the rebuild site) is the cheapest correct key.

**Gain.** 307 decal draws (plus roads where visible) x 4 calls, plus up to ~0.5 KB of memcpy each.
At 0.3 to 0.6 us per Map or Unmap that is **0.35 to 0.75 ms of emit per frame**, estimated from
section 0.4's call counts. The per-call cost is unmeasured until M1. On the render thread it pays
about 130 frames/s, so roughly **+4 to +9% fps**. The GPU reads the same vertices, so no GPU change.

**Class and seam.** Class A, a new row such as `ext.static_world_batches` under `src/ext/<id>/`.
It needs one game-side seam (a null-by-default arm at the two call sites) plus the invalidation
seam. Retail streams these, so this is an extension, not a fix.

**Risks.**
- **A stale batch.** Missing a rebuild writer would draw a stale decal. That is a visible bug, so
  build the invalidation from a census of every writer of `_decals` and `_roadBatches`.
- **Device lost.** The static buffers are Externals: destroyed in `OnVideoError`, recreated on the
  next draw.
- **Memory.** The town's decal geometry is small, a few hundred KB.
- **Overlap.** If ideas-batching proposes merging the decals into a few draws, this idea is
  subsumed. Decide together.

**Effort.** 10 to 14 h, most of it the writer census.

**A/B.** `draw_packet_ms` and `draw_emit_ms` should fall, and `pipe_wait_us` with them. The
toggle is live (class A), so run leg pairs in one session.

**Verify.** The trace oracle should differ only in vb/ib identity on the 342/346 draws; S4 and S6
should be 0. Post-VS data for one decal draw (RenderDoc mesh export, by the lead) should be equal.

### 2.3 Dynamic draws without the per-draw ring rebind (rank 3)

**What changes.** These are three changes in the dx11 backend's dynamic path (`ContextData::
BeginVertices`, `BeginInstancedIndexedVertices`, `EndVertices`, `EndInstancedIndexedVertices` in
`contextdata.cpp`, and `BufferData_Lock` in `buffers.cpp`).

- **(a) Drop the redundant index rebind.** The dynamic index buffer is always bound at offset 0.
  Mark the index slot dirty only when the buffer changes, and pass the ring offset as
  StartIndexLocation, which is already done.
- **(b) Base-vertex addressing.** Round the vertex ring cursor up to a multiple of the stride, bind
  the dynamic VB once at offset 0, and pass the offset as BaseVertexLocation (StartVertexLocation
  for `Draw`). The stream is then rebound only when the stride changes. In the late pass, decals
  (0x14) and smoke (0x34) each form one sorted run.
- **(c) Optional.** One dynamic buffer created with VERTEX|INDEX bind flags, so the vertices and
  indices of one draw share a single Map/Unmap.

**Gain.** (a) and (b) save ~2 calls on each of ~575 dynamic draws, ~1150 calls. (c) saves 2 more.
That is about **0.1 to 0.3 ms of emit for (a)+(b), and 0.3 to 0.6 ms with (c)**. The smoke draws
gain the most, since they cannot use idea 2.2.

**Class and seam.** This is backend behaviour retail does not have, so it is a class A row (for
example `ext.dx11_ring_bind`) read by the backend through the engine-side null-pointer seam pattern
(fix 5, `registry_boot.cpp`). It must not be an `#if`. It lives in `src/render/dx11/`; other
backends answer "not supported".

**Risks.**
- **Faithful bugs.** The faithful path keeps retail's "index capacity tested against the vertex
  buffer" bug and the ignored PrimitiveType. Keep both on the ext path too.
- **Stride rounding.** It wastes less than one stride per allocation and changes when the ring
  wraps (DISCARD), which is harmless.
- **Combined buffer.** (c) changes resource creation, so it touches device-lost recreation.

**Effort.** (a)+(b) 4 to 6 h. (c) another 4 to 6 h.

**A/B.** `draw_packet_ms` should fall. M2's call counts give the expected delta to check against.

**Verify.** S3 should differ only in vb offsets and base-vertex fields, which are already excluded
(S6 ignores byte offsets). S4 and S6 should be 0.

### 2.4 Shrink the main half (rank 4 by evidence, rank 1 by leverage)

From section 0.3, a millisecond off pass 1 is worth about 1.5x a millisecond off the emit. No
concrete body can be proposed until M1.3 says which scene entry and which phase owns the 2.3 ms.
Candidates that were not already measured neutral:

- **The step-frame main half runs with the render thread idle,** 60 times a second. That thread is
  a fourth core on 8-core boxes. It could act as a third cull helper for the step frame only,
  through `ext.gather_threads`' existing helper protocol. It is not available on 4 cores, where it
  is already the render thread's core.
- **If M1 confirms H3** (a post-step emit about 2x the others), the cost is the first frame after
  a step re-deriving everything: sorts, side records, bones. Look at `Scene::Clean` / draw_gather's
  side-record refresh and the SORTMEMO hit rate on that frame, not at the hand-off.

Not re-proposed:

- XFORM / XFORM2: the per-item transform memo, measured neutral and removed.
- REFLCULL: the reflection cull, about 6 us, dropped.
- The async sort and the parallel walk: rejected by the user on 2026-10-14.
- The snapshot walk: rejected.

### 2.5 Flip-model Present (rank 5, only if M1.1 shows Present CPU of 0.2 ms or more)

**What changes.** In `devicedata.cpp` (the swap-chain desc, line ~468): `DXGI_SWAP_EFFECT_FLIP_DISCARD`,
three buffers, and `ALLOW_TEARING` when vsync is off. The MSAA colour buffer already resolves
into the back buffer (`EndCommands`), which flip-model requires.

**Gain.** It removes the blt-model DWM copy and part of Present's CPU cost: 0.1 to 0.3 ms of emit,
a guess until M1. It also lowers latency.

**Class.** Class A (presentation).

**Risks.**
- **Back-buffer reads after Present.** Flip-discard leaves the buffer undefined, which matters for
  the screenshot, SaveLoad thumbnail and minimap capture paths. Audit each.
- **Resize and device-lost.** `ResizeBuffers` needs the same flags.
- **Fullscreen and overlays.** Exclusive fullscreen behaviour and RTSS / RenderDoc hooks change.

**Effort.** 8 to 12 h.

### 2.6 Constant buffers through DYNAMIC + Map(DISCARD) (rank 6, only if M1.1 shows UpdateSubresource heavy)

**What changes.** In `ConstantBufferData::Bind` (`buffers.cpp:760`) and the companion create: use
DYNAMIC CBs and `Map(WRITE_DISCARD)` plus memcpy instead of DEFAULT plus full `UpdateSubresource`.

**Gain.** It depends on the driver. NVIDIA renames both paths, so expect 0 to 0.3 ms. Measure
before building.

**Class.** Class A backend row, same seam as idea 2.3.

**Risk.** Low. Verify with S3, since CB sizes are already non-verdict.

## 3. Considered and not proposed

- **Dropping the frame gathered just before a predicted step.** It saves one main half per step
  and loses one presented frame, a net ~0 frames per step cycle.
- **A spin-then-park hand-off.** The measured publish-to-start gap is about 5 us.
- **A deeper pipeline (depth 2).** The fence drains it every step, so it only adds latency.
- **D3D11 deferred contexts / parallel pass 2.** THREADRESEARCH §1.7 rated them very large and
  driver-dependent. There is also no spare core on the 4-core target.
- **Removing the fence by snapshotting pass 2's inputs into the packet.** That is the render-proxy
  decoupling the user rejected on 2026-10-14, because pass 2 calls into live Geometry objects that
  a step can free.
- **Moving the fence past the pump.** It saves only input, audio and pump time, tens of us, and
  the UI draw reads the pump's results.

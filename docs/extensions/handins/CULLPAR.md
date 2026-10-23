# CULLPAR -- `ext.gather_threads`, the parallel cull inside ext.draw_gather (hand-in)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/draw_gather/gather_threads.{h,cpp}` moved to `src/ext/gather_threads/` (namespace `Ext::GatherThreads`).)

Block 3 of `THREADPLAN.md` §2 (THREADRESEARCH rank 2, §2.3-§2.5, §5.2, §7.4). Branch
`worktree-agent-ae1186acfe4439411`, base main `04b4dff7c` (CPULIMIT merged), main merged again
at `449db6a8a` (RENDERPIPE-0). Static gate only: the game was not run. The coordinator's A/B and
shadow runs are the live proof (§9).

## 1. The row

`ext.gather_threads` -- Tier::Extension, Class::A, default OFF, formatVersion 0, display
"Parallel culling", description: splits the culling of Faster scene gathering over helper threads
on machines with more than 4 cores; on 4 cores or fewer no effect (one thread); needs Faster scene
gathering on; exactly the same picture. (The brief named the draw_gather row "Faster draw gather";
its display name is "Faster scene gathering", so the text uses that. "More than 4 cores" rather
than "6 or more": with `render_pipe` off the §7.4 rule gives 1 helper at 5 cores.)

It does something only when `ext.draw_gather` is on too (the walk it splits is draw_gather's). The
helper count is `Cpu::CullHelpers(Cpu::Get().physical, /*renderPipe=*/false)` -- 0 / 1 / 2 at
n <= 4 / 5 / >= 6 physical cores; this box (8 physical) gives 2. The call site carries
`// RENDERPIPE-1 passes Ext::Ext().render_pipe` (`draw_gather.cpp` `SyncThreads`).

## 2. The design as built

**Files.** `src/ext/draw_gather/gather_walk.h` (new): draw_gather.cpp's walk moved verbatim
(constants, `CullRecord`, `CullStack`, `CullStackInit`, `CullStackPushChildren`, the node boxes,
`ShadowCasterBox`, `Frame`, `Contains`, `BoxPasses`, `Walk`, `RetailTest`), so the unit test runs
the real code; the one edit is `Walk` = `CullStackInit` + `WalkStack` (the loop, :224), which
inlines back (§7). Then the parallel walk (:290-:545). `gather_threads.{h,cpp}` (new): the
helper pool. `draw_gather.cpp`: the branch, `ParallelGather`, the park seam's body, `SyncThreads`.

**d = 2** (`kTopDepth`, `gather_walk.h:296`). The frontier is depth 2: at most 16 subtree tasks
under 5 interior cells (the grid has 8 levels, so each task is a 6-level subtree). Chosen from
the numbers on hand, not measured in a run: heavy town 149,553 chain objects per frame over
18.07 passes (GATHERPF) = ~8,300 per pass; over 16 subtrees ~520 per task, above §2.5's minimum
chunk of ~200; d = 3 (64 tasks) gives ~130, below it. Empty frontier subtrees (`_cellCounts < 1`)
are dropped in the plan (the subtree walk's own first test), so a pass has <= 16 tasks. The stress
line's `tasks/pass` (§5) is the measurement; if the spot / cascade passes turn out to carry far
fewer objects than the main view, a per-pass cut-off (run small passes sequentially) is the
follow-up, not d = 3.

**The plan** (`ParPlan`, `gather_walk.h:423`). `Walk`'s own loop over the same `CullStack`
(`CullStackInit`, `CullStackPushChildren`), the same box tests in the same order, with one
difference: a popped record of depth `kTopDepth` (with a nonzero cell count) is not tested but
becomes a TASK (its record, with the masks it inherited, is the subtree's root). The plan is the
pre-order list of SEGMENTS: `{task >= 0}` or `{interior cell, its cullMask, shadowMask}`.
`CullStackPushChildren` pushes children 1..4 and pops 4 first, so the plan order is exactly the
sequential walk's order of cells.

**A task** (`ParRunTask`, `:398`): a private `CullStack` initialised as the root's (biases, scene)
with `rec[0]` = the task's root record, then `WalkStack` into the task slot's private vector with
`TaskSideTest` (draw_gather's `SideTest` with private counters, `:312`). The subtree walk is the
very loop the sequential walk runs on that subtree: the stack below a subtree is never touched
while it is walked, and `shadowMask` (the loop's carried local) is 0x3f in both when
`f.notShadow` and reassigned before every use otherwise.

**The pass** (`ParGather`, `:474`), main thread:
1. `side.Reserve(_grid._nodes.GetSize())` (§4);
2. the plan;
3. per helper a copy of the `Frame` (`HelperFrame`, 64-byte aligned, `boxes = 0`,
   `cullVerify = false`);
4. `Publish(tasks)` -- the helpers can start;
5. the interior cells' chains on the main thread (`TaskSideTest` into `interior`, each segment
   records its `[begin, end)`);
6. `Finish()`: the main thread claims and runs the tasks left, then waits for the ones helpers
   took (it never waits while a task is unclaimed);
7. the concatenation in plan order into `out` (= `g_lists[depth]`, `:533-:536`), the counters
   summed (`AddStats`), the helpers' box counts added to `f.boxes` (`:540`).

**The task index** (`HelperPool::Claim`, `gather_threads.cpp:96`): one `std::atomic<uint64_t>`
ticket = pass generation (bits 32+) | count (16..31) | next index (0..15), claimed by a CAS of the
whole word, so a claim can only take an index of the pass it loaded. `Publish` stores the ticket
with release after `fn` / `ctx` / `_done = 0`; a helper bumps `_done` (release) after each task;
`Finish` waits until `_done` == the tasks helpers took (acquire). Slots (`TaskSlot`: output vector
+ counters) and helper frames are `alignas(64)`: no false sharing on the per-object counter writes.

**Wake / spin / park.**

| point | where | what |
|---|---|---|
| create | `draw_gather.cpp` `SyncThreads` (from `DrawGather::Sync`: boot, `registry_boot.cpp:97`, and every class A toggle, `:116`) | `new HelperPool(n)`: n `Platform::ThreadCreateSuspended` threads (256 KB stack), `ThreadSetNormalPriority`, resumed, parked on a condition variable. Deleted (parked, quit, joined) when either row turns off. Never freed at exit (a static destructor would join during teardown; the pool is a leaked heap object like `g_addLog`) |
| wake | `draw_gather.cpp:215-218` (`ParallelGather`) | the first gather pass that finds the pool parked: `_awake = true`, bump the epoch under the mutex, `notify_all`. Once per frame |
| spin | `gather_threads.cpp:74-93` (`HelperLoop`) | while `_awake`: claim and run, else `Platform::CpuRelax()` (`pause`). No syscall inside the spin |
| park | `Scene::_extDrawPass1End` -> `draw_gather.cpp:240` (`ExtDrawPass1End` -> `Park`) | called once per `Renderer::Draw` right after pass 1 (`wave27_lane_gfxbig.cpp:1032`) and at the end of ext.render_pipe's copy `RenderPipe::Pass1` (`src/ext/render_pipe/passes.cpp`, after the merge of RENDERPIPE-0). The spin is bounded by pass 1 of the frame |

The seam is a null-by-default static on `Graphics::Scene` (DESIGN §3 rule 5; `Scene.h:317`,
`scene.cpp:1461`), set only while the pool exists: the faithful frame pays one null test per
`Renderer::Draw`. Every gather of a frame happens inside pass 1 (the faithful loop at
`wave27_lane_gfxbig.cpp:1001`, `passes.cpp:73`, and the nested ones reached from its dispatch:
`lights_vtable.cpp:439/446`, `wave28_lane_partsys.cpp:925`, `scene.cpp:1466`); a gather outside
any `Renderer::Draw` would only leave the helpers spinning until the next one's pass 1 ends. The
wake is lazy (the gather wakes a parked pool), so an early park only costs a second wake.

**The selection.** `ExtCulledGather` (`draw_gather.cpp:315`):
`if (__builtin_expect(g_parHelpers > 0, 0)) ParallelGather(...) else { SideTest; Walk }`.
`g_parHelpers` is written by `SyncThreads` only -- at boot and at every class A toggle, the only
points where either row or the count can change -- and read once per gather pass, never in the
walk. (The brief put the read beside `WalkBatch::BeginFrame`; that runs in `GameUpdateLoop::Update`
only, not for the menu / minimap draws, and RENDERPIPE-0 was rewriting that function. Latching at
Sync is strictly fewer reads.) `ParallelGather` is `noinline`, so the sequential arm keeps its code.

**0 helpers** = the existing sequential walk, no thread created, and once per run on stderr:
`[GatherThreads] 0 helpers on N physical cores: sequential walk`. With helpers:
`[GatherThreads] 2 helper thread(s) on 8 physical cores (NORMAL priority, parked between frames)`.
Thread names: the platform seam has no `SetThreadDescription`; skipped.

**Nesting.** A dispatch can re-enter the gather (`RenderTextures`); the nested pass reuses `g_par`
(plan, slots) -- safe, because the outer pass has joined and concatenated before its dispatch.

## 3. The §2.2 census, re-verified against the code

During a parallel pass the main thread and the helpers all run walk code (plan, interior chains,
tasks); the dispatch, `DrainLog`, `Clean` and every `g_counters` write happen before or after,
on the main thread only.

| access | what (file:line) | during the parallel walk |
|---|---|---|
| read | grid `_cellCounts`, `_cellHeads`, `_nodes[]._object/_next`, `_cellSize`, `_levelCount` (`gather_walk.h` WalkStack :224-:266, ParPlan, ParGather's interior loop) | written by `GridAdd` / `GridReindex` / `GridUnthread` (scene.cpp :1320 / :1349 / :1433) from `Scene::AddObject`, `RemoveObject`, `Clean`; `Clean` runs before `Renderer::Draw` (`gameupdateloop.cpp:1197`, `menudialogloopt.cpp:413`, `minimap_paint.cpp:210`). Not written by any walk. `AddObject` on the loader thread (scene_ext.h banner) races the sequential walk exactly as it races the parallel one: unchanged |
| read | the `Frame` (`const Frame&`): frusta, prepared tables, masks, detail, notShadow | built before the walk in `ExtCulledGather`; each helper reads its own copy (`HelperFrame`); the copies' `FrustumCullPlanes::src` point at the main Frame's frusta, read only by the reference body (kFastCull true, cullVerify false in the copies) |
| read | Geometry `_materialTypeFlags`, `_detailId`, `_worldBounds` (`TaskSideTest` :312) | writers are `Geometry::Clean` (in `Scene::Clean`, before Draw) and `MeshInstance::SetMesh` (sim / dispatch side, not in a walk) |
| write | `f.boxes` (`mutable`, `gather_walk.h:197`, `++f.boxes` :203) | main thread: the main Frame (plan, interior, the tasks the main thread runs); helper w: `frames[w].f.boxes`, its own; summed after the join (:540) |
| write | side records: `SideArray::Store` (`gather_side.cpp:12`) on an invalid record; `Valid` (:59) reads `_owner.size()` | per node id: a node is in exactly one cell's chain (`_cell`), so the plan's interior cells and the tasks' subtrees visit disjoint ids: disjoint element writes. Store can no longer grow: §4. Records of adjacent ids share a cache line across tasks (false sharing at subtree edges only, correctness unaffected) |
| write | counters `nodesTested / geoTouched / refreshes` (`g_counters`, draw_gather.cpp :164 in the sequential SideTest) | the parallel path writes none: `TaskStats` per task slot / for the interior, summed into `g_counters` after the join (`draw_gather.cpp:220-:227`) |
| write | the visible list | per task `TaskSlot::out`, the interior vector (main); `g_lists[depth]` is written only by the concatenation after the join |
| write | COMPPROF=4 reports (`StressHarness_Verify`, single-threaded) | not called inside the parallel walk: `TaskSideTest` records the DrawGatherSide checks (count, mismatches, first 4 ids), replayed on the main thread after the join (`draw_gather.cpp:229-:235`); helper frames have `cullVerify = false`, so `FrustumCull_ContainsVerified` (-> `g_frustumCullReport` -> Verify) only runs on the main thread |
| not touched by the walk | `Renderer` item lists, `AddDrawItem`, `RenderTextures`, any Geometry write | dispatch only, after the join (unchanged) |

## 4. The side-array pre-size

`SideArray::Reserve(n)` (`gather_side.cpp:30`, new): grows both vectors by Store's own rule
(geometric from 1024) until every id < n has a slot; new slots invalid. `ParGather` calls it with
`_grid._nodes.GetSize()` on the main thread before the plan (`gather_walk.h:479`). Every id the
walk reaches is a `_cellHeads` / `_next` value, i.e. an index into `_nodes` (< GetSize), so no
`Store` during the walk grows the arrays. Checked twice: `ParPassResult::sideGrew` (Size before vs
after the walk) feeds the stress counter `sideGrew` (must stay 0), and the unit test (§6).

## 5. Counters (stress `[Stress::gather]`, a second line)

`gather_threads helpers=H per frame: wakes passes tasks (T/pass) helperShare=% joinWait=us (us/pass)
| window: sideGrew=N`, printed while helpers exist or a parallel pass ran. All summed once per pass
from the pass result (`Counters::parPasses / parTasks / parHelper / parWaitTicks / parWakes /
parSideGrew`, gather_side.h); no per-object counter was added (the existing per-object
`nodesTested` etc. stay plain per-task fields, as `SideTest`'s were). The join wait is two QPC reads
per pass, only when the main thread actually waits. The first `[Stress::gather]` line's nodes /
geoTouched / refreshes still count the parallel passes (summed from the tasks).

## 6. Unit tests (`tests/unit/ext_gather_threads_test.cpp`, 4 tests)

- `ext_gather_threads_pool_clamps_and_runs_every_task_once`: `HelperPool(3)` = 2 helpers, `(-1)` = 0;
  for 0..3 helpers, 60 wake / park frames x 1-4 passes of 0-63 tasks: every task exactly once per
  pass, caller + helper runs = published, one wake per frame.
- `ext_gather_threads_parallel_walk_equals_sequential`: for 0, 1, 2, 3 (clamped to 2) helpers,
  250 random worlds each (1-6 levels, 0-499 objects in random cells with a bias to the top
  levels, free node slots, subtree counts with random zeros / stale ones, side arrays with valid /
  stale-hint / invalid records), random frusta around a random point (some all-pass), random pass
  mask / detail / shadow mode; the pool woken per "frame", parked between, every fifth case on a
  parked pool; plus 20 per count with no pool. `ParGather`'s list == `Walk` with the side test ==
  `Walk` with retail's reads, byte for byte; node / touched / refresh / side-check counts, the box
  test count and the side array afterwards equal the sequential walk's. **1,080 cases.** The
  helpers took 724-1,220 of ~1,800 tasks per count (printed; timing decides, the lists do not care).
- `ext_gather_threads_side_array_never_grows_during_a_parallel_walk`: 100 worlds, 2 helpers:
  reserved first, `Size()` before == after; from empty, the size after == `Reserve(node count)`'s.
- `ext_gather_threads_registry_row` (A, off, format 0; CullHelpers(4) = 0, (8) = 2).

The Scene and Geometries are raw storage with the walked fields set (the walk calls no method of
either). **Unit 1156 / 0 at the base -> 1160 / 0 at my tip -> 1168 / 0 after merging main**
(RENDERPIPE-0's tests).

## 7. Object proofs (perf; WALKGRID §3 method 1)

`tools/build.sh build perf --jobs 2` at `04b4dff7c` before any edit, objects kept
(`build/cullpar/base/`), rebuilt after; per function `objdump -d -r --no-show-raw-insn -C`,
function-relative targets, rip displacements and their comments stripped (`build/cullpar/objcmp.py`,
not committed).

| object | result |
|---|---|
| `ext/draw_gather/draw_gather.cpp` | **`Walk<SideTest>` and `Walk<RetailTest>` identical** (WalkStack inlines back); `CullStackPushChildren`, `DrainLog`, `ExtClean`, `StoreFull`, `Installed`, the vector helpers identical (16 of 21); `BoxPasses` identical but its trailing alignment nops; `ExtCulledGather` differs by the `g_parHelpers` load + predicted-not-taken branch + the out-of-line `ParallelGather` call (stack frame -16 bytes, so every slot offset shifts); `Sync` (now `SyncSeams` + `SyncThreads`), `TakeWindow` (bigger Counters) and the static init (`g_par`) changed on purpose |
| `ext/draw_gather/gather_side.cpp` | 7 / 7 identical, + `Reserve` |
| `engine/graphics/cull.cpp` (faithful) | 7 / 7 identical (not touched) |
| `engine/graphics/scene.cpp` | 29 / 29 identical (one new data symbol) |
| `engine/graphics/wave27_lane_gfxbig.cpp` | 23 / 24 identical; `Renderer::Draw` gains the seam's load / test / indirect call after pass 1, and re-reads `_sceneGroups._size` after it (the call may write memory) -- the allowed one null test per frame |
| `ext/render_pipe/passes.cpp` (vs main's object after the merge) | 8 / 9 identical; `Pass1` gains the same call (an ext TU) |
| `ext/registry.cpp` | the table grew one row (entry count, indices, the new accessor) |
| `diag/stress_harness.cpp` | `EmitRow` changed on purpose (the new line); `PfStats::Summarize` identical but TU-local call targets; `SmMemoReport` now out of line (was inlined into EmitRow, which grew) |

## 8. Gate (static)

`build debug`, `build perf`, `unit debug` at `--jobs 2`, after the merge of main: exit 0, **0 new
warnings** against the main checkout's `warn_baseline_{debug,perf}.txt` (gate.sh's shape, the
worktree prefix mapped to the main path); unit 1168 / 0; `castmetric.py src` 2 (the two `void*`
callback contexts carry `CAST-OK: user_data`); `abi_audit.py` OK; `acceptance.sh` 0 off target.

## 9. What the coordinator must measure

1. **Shadows at COMPPROF=4** with `draw_gather` + `gather_threads` on (heavy + compact): `DrawGather`
   and `DrawGatherSide` 0 mismatches, `sideGrew` 0. Note: with helpers on, the `FrustumCull`
   shadow's ext-site checks cover the main thread's box tests only (helper frames have
   `cullVerify = false`); the `DrawGather` shadow compares the concatenated list against retail's
   reads walked sequentially with the reference box test, which covers the helpers' answers.
2. **The A/B at the 8-CPU mask** (THREADPLAN §4, everything on, heavy + compact, speed 10 + 50):
   `gather_threads` off vs on with 2 helpers. 1 helper has no settings knob: `CullHelpers` gives 1
   at n = 5 physical, so the 1-helper leg is a mask of 5 physical cores (e.g. P5). Read the main
   thread's draw ms, fps, and the new line: tasks/pass (expect <= 16; if the average is far below,
   the spot / cascade passes are tiny -- see §2 d), helperShare, joinWait us/pass.
3. **P4 confirms 0**: the boot line `[GatherThreads] 0 helpers on 4 physical cores: sequential
   walk`, no second `[Stress::gather]` line, draw ms unchanged vs the row off.
4. **The spin's cost when the helpers idle**: a frame with few gather passes (the menu, a paused
   speed-10 frame) keeps 2 cores spinning for the whole of pass 1. Measure total CPU (process
   time / frame) row off vs on at speed 10 and in the menu; and whether the spinning helpers slow
   the main thread (shared core clocks / power budget) -- compare main-thread draw ms of the
   faithful-equivalent part (dispatch, sort) off vs on.
5. Exactness: resave golden `971aaec2cf95` unchanged with the row on (class A writes nothing).

## 10. Open items / findings

- **Finding (design, merged):** RENDERPIPE-0's pipelined frame runs its own copy of pass 1
  (`passes.cpp`), which never reaches the faithful `Renderer::Draw`'s seam; the park call is in
  both. RENDERPIPE-1 must keep `Pass1` on the main thread (it does by design) or move the call
  with it. RENDERPIPE-1 also passes `Ext::Ext().render_pipe` to `CullHelpers` (the comment marks
  the spot); `SyncThreads` already rebuilds the pool when the count changes, and `DrawGather::Sync`
  runs on every class A toggle, so a live `render_pipe` toggle re-sizes the pool.
- No per-pass cut-off: a pass with 1 task still publishes it (a helper may take it while the main
  thread does the interior). If the A/B shows small passes losing, gate `ParallelGather` on the
  plan's object count (a pure choice: the list is the same either way).
- Thread names: no `SetThreadDescription` seam in `src/platform`; a debugger shows the helpers
  unnamed.
- The merge commit `449db6a8a` carries git's default message (no attribution lines).
- Preemption risk (THREADRESEARCH §2.5): a helper preempted while holding a claimed task stalls
  that pass's join for up to a quantum; NORMAL priority as briefed. `joinWait us/pass` (max not
  recorded, sum only) is where it would show; a p95 / max column is a cheap follow-up if the sum
  looks spiky.

## Results (coordinator, 2026-10-14)

- **Gate CULLPAR at 0** (merge `429cc7e79`; GATE_TABLE row): routes exit 0, resave 5/5 == golden `971aaec2cf95`, unit 1168 / 0, 0 new warnings, castmetric 2, abi_audit OK, mods parked (faithful profile: rows OFF).
- **Shadows with the rows ON** (`BANISHED_STRESS_COMPPROF=4`, `STRESS_EXT=draw_gather,gather_threads`; `perf/results/20261013_23{1004,1128,1239}_cpsh_*`; this box: `[GatherThreads] 2 helper thread(s) on 8 physical cores`):

| run | DrawGather | DrawGatherSide | RenderPipe | `[Stress::gather] gather_threads` last window |
|---|---|---|---|---|
| heavy, speed 10, 3000 frames | 44508 / **0** | 359M / **0** | -- | wakes 1.00 / frame, passes 3.0, tasks 12.0 (4.0 / pass), helper share 70.3%, join wait 14.6 us / frame (4.9 / pass), sideGrew 0 |
| compact, speed 10, 3000 frames | 80325 / **0** | 411M / **0** | -- | passes 3.0, tasks 12.0, helper share 69.1%, join wait 3.1 us / frame, sideGrew 0 |
| heavy, speed 50, 1800 frames, + `render_pipe` (all three rows) | 5016 / **0** | 33.8M / **0** | 950 / **0** | helper share 69.0%, join wait 46.4 us / frame (15.5 / pass), sideGrew 0; pipe main share 78.7%, discarded 0 |

  The joined list equals retail's walk in every pass with two helpers claiming ~70% of the tasks; the side array never grew during a walk; with `render_pipe` on as well, the helpers park at the end of `RenderPipe::Pass1` (one wake per frame) and the RenderPipe shadow stays 0. **Note:** the stress town's camera pin gives 3 gather passes per frame here (THREADRESEARCH §2.1 counted 18 / 30 from GATHERPF's census of a different camera), so 12 tasks per frame at d = 2: the A/B must say what the helpers buy at this pass count.
- The A/B (helpers 0 vs 2 at the unrestricted and 8-CPU masks, the P4 boot line with 0 helpers, the idle spin cost) is owed on a CLEAN machine (THREADPLAN §4).
- **A/B at the unrestricted mask (2 helpers), COMPPROF=0, overlays stopped** (series ab4u, `perf/results/20261013_23*_ab4u_*`, 3000 frames at speed 10, interleaved x2; `STRESS_EXT=draw_gather` vs `draw_gather,gather_threads`; rep 1 of the heavy draw_gather leg ran BUSY, every other leg CLEAN):

| town | draw_gather alone: fps / draw ms | + gather_threads (2 helpers): fps / draw ms | draw | join wait / frame |
|---|---|---|---|---|
| heavy, speed 10 | 745 / 1.07 (BUSY), 790 / 1.03 | 954 / 0.86, 965 / 0.86 | **-17..-20%** | 42 us (14 / pass) |
| compact, speed 10 | 1295 / 0.68, 1334 / 0.66 | 1533 / 0.57, 1545 / 0.57 | **-14..-16%** | 15 us (5 / pass) |

  Far above THREADRESEARCH §2.5's ~0.10-0.14 ms estimate (0.17-0.21 ms saved per frame here): with 3 gather passes per frame the walk is a larger, better-chunked share than the 18-pass census assumed. The faithful control (`STRESS_EXT=none`) read 834 / 0.99 and 819 / 1.01 heavy, 1315 / 0.67 and 1300 / 0.68 compact: `draw_gather` alone is at or slightly below faithful in these towns, the helpers put it clearly above. Owed: the same pair at the 4-CPU mask (expect `0 helpers`, the sequential walk) and a 5-physical-core mask for the 1-helper point; the heavy render_pipe + helpers leg.

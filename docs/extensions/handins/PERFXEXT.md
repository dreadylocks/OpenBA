# PERFXEXT -- PerfX becomes extensions (`ext.render_culling` A, `ext.anim_batch` B)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/perfx/` (perfx.{h,cpp}, perfx_rules.h, perfx_table.h) moved to `src/ext/render_culling/` (render_culling.{h,cpp}, render_culling_rules.h, side_table.h; namespaces `PerfX` / `PerfXTbl` -> `Ext::RenderCulling` / `Ext::RenderCulling::SideTable`), ext.anim_batch's seam helpers to `src/ext/anim_batch/anim_batch.h`; tests `ext_perfx_test.cpp` + `perfx_table_test.cpp` -> `tests/unit/ext_render_culling_test.cpp` and `ext_anim_batch_test.cpp`.)

Block PERFXEXT, 2026-10-12. The user's decision (ELPERF2 close): PerfX -- a perf-build-only,
env-gated set of non-retail optimisations in `src/app/perf_experimental.cpp` -- becomes proper
extensions in `src/ext`, compiled into every flavour, default OFF; the `BANISHED_PERF_*` flags and
the `-DBANISHED_PERF_EXPERIMENTAL` master are retired. Static gate only: the game was NOT run by
this block (the coordinator runs the gate, the COMPPROF=4 shadows and the stress A/B).

## 1. Classification (written first)

Class A = no sim effect, no serialized state, live toggle. Class B = sim-divergent, format-neutral,
locked per game (DESIGN.md §2). "Faithful" = same results as retail's code by proof; not an
extension, unconditional code (as TREE_SNAPSHOT / WALKCACHE / BESTTARGET / PERFXTBL already are).

| knob | what it changes | can the sim (or the save) observe it? | verdict |
|---|---|---|---|
| `animCull` | last speed iteration: an off-screen model's `MeshInstance::Update` runs root-only (`_rootOnlyUpdates` = 1 for that one call), as retail's own intermediate iterations do | no. Root-only advances the clock and the root-motion offset exactly as the full update (it is retail's path for N-1 of N iterations). The sim's reads of a MeshInstance are `IsAnimating` (clock vs duration, layer 0; `mesh.cpp` 0x1800ff7d0), `RootAnimationOffset` (move tick) and `PlayAnimation`; the node pose is read only by presentation (`GetNodeToWorld`: particle attach `particlecomponent.cpp:216/719`, highlight, `tool_base_render.cpp`; the carried tool via `MeshInstance::Attach`, `action_base_complete.cpp:374`). The pose is not serialized. Pause flush re-poses with a zero-time update | **A** (`ext.render_culling`) |
| `geomXfLastOnly` | move tick: `Geometry::SetTransform` (render scene transform, bounds, children) skipped on intermediate iterations | no. `model->transform` (+0x148, the gameplay copy) is still written every tick. Game code never queries the graphics scene spatially (only Add/Remove/Clean, `grep scene->`); `meshInstance._transform` is read by the tutorial highlight and ext.eye_camera (presentation) only | **A** |
| `geomCull` | last iteration: the same push skipped for an off-screen citizen; pushed in the pause flush | no (as above) | **A** |
| `iconLastOnly` / `iconCull` | status icon: the positioning tail (`StatusIcon_SetIconTransform`: bounds / `GetTerrainPos` reads, `Geometry::SetTransform`) skipped on intermediate iterations / for an off-screen owner; replayed on pause | no. The scene add/remove and opacity part of the tick runs before the skip, unchanged; the tail writes only the icon geometry | **A** |
| `pickCull` | last iteration: `Picking::Scene::SetTransform` of an off-screen citizen deferred; replayed on pause | no. That scene (`GameUpdateLoop+0x70`) is queried only by the player tools (input); the map's own picking scene that the sim ray-casts for heights (`mapquery.cpp:133`, `Map+8`) is a different scene and is not touched | **A** |
| `particleCull` (as shipped in PerfX) | an off-screen ParticleComponent is not ticked at all (frozen) | **yes, as shipped.** The tick's fade integration is what calls `StopEmitter` (opacity reaching 0): that writes `emitters[i].sceneId = -1`, `active[i] = false` (serialized, `ParticleComponent::Persist` 0x140053270) and clears flag bit 2 (`RefreshEntityFlags`, update-tree membership). `TornadoComponent::Tick` reads `emitters[0].sceneId >= 0` to decide whether the tornado lives (`tornadocomponent.cpp:819`): a frozen off-screen funnel would keep moving and damaging | **A after a fix (§3.1)**: a component is frozen only while every live emitter has `fadeRate == 0` and `opacity > 0`. Then retail's tick provably changes only presentation state (node-follow transform, the ParticleInstance simulation); a fade starting while frozen unfreezes it on its next tick. Shadow `PerfXSim` (§5) |
| `animBatchAll` | speed N: the N-1 intermediate root-only updates of a model whose root offset "cannot differ" are skipped, the last iteration advances it by `(skipped+1) x tick` | **yes.** (1) The clock lags by up to N-1 ticks inside the frame and three sim reads compare it with the duration: `NaturalResourceComponent::Tick` (every iteration; on "not animating" it clears its bit and calls `RemoveResource`, `naturalresourcecomponent.cpp:53`), `AnimateNaturalResource::Execute` (a citizen action completes on `!IsAnimating`, `animatenaturalresource.cpp:81`) and the tornado's tree knock-down test (`tornadocomponent.cpp:375`). A falling tree batched at speed N finishes up to N-1 ticks late, so the felling citizen's next action starts late. (2) A `PlayAnimation` mid-frame switches the model to per-tick, but the last iteration still applies `(skipped+1) x tick`, the skipped ticks of the OLD animation, to the NEW one: its clock runs ahead, and for a non-instanced root-motion clip `RootAnimationOffset` is computed with that dt (the "exact walk speed" claim holds only for frames without a clip change). Animation events (`AnimAudioEvent`, the only `AnimationEvent` subclass) are audio only | **B** (`ext.anim_batch`, its own extension, formatVersion 1). Shadow `AnimBatchSim` (§5) counts the IsAnimating answers that differ |
| `animBatchCulled` | the same, only for off-screen models | yes (as above), and the divergence would depend on where the camera looks | **dropped** (no measurable gain on its own, perf/README iteration 12; `anim_batch` batches every eligible model) |
| `ambientThrottle` | `AmbientEnvironmentComponent::Update`: the biome histogram reused for 7 of 8 updates per emitter | **yes (the save).** The dominant biome selects `AmbientEmitterComponent` slot sounds (`SetSlot`), which `AmbientEmitterComponent::Persist` serializes (the sound ExternalPtr per slot); a map change shows up to 7 updates late. No gameplay read. Also: its cache (`unordered_map` keyed on `this`) is never pruned, so a new component at a freed address inherits a stale biome | **dropped** (retail's body restored). A class B lock for an ambient-audio crossfade is not worth a save entry; its gain was never measured alone (one of four in the 0.245 -> 0.231 step) |
| `treeSnapshot` | the update-tree walk from a flat in-order snapshot | no: the same visit order (WALKCACHE §3, PERFXTBL) | **faithful perf work**: unconditional (was default-on only in the perf build); a `constexpr` kill switch in `entitylist.cpp` |
| `terrainSidecar` | the move tick's terrain-type byte read from a 1-byte mirror | must be the same byte. It was NOT in every case: two live writers of `MapCell::terrainType` never told the mirror (`Cemetery_SpawnTombstone_6a90`, `citizen_lifeevent_handler.cpp:228`, and the pasture cell retype, `pasturecomponent_states.cpp:446`, both `= 2`), so after a burial or a pasture retype the move tick could read the cell's old type (the old verify run, 800x10 3000 frames, had neither). Fixed here (both now call the note). With the writer list complete it is the same byte | **faithful perf work**: unconditional; `BANISHED_PERF_TERRAIN_VERIFY` becomes the COMPPROF=4 shadow `TerrainSidecar` |
| `cullMargin` | the 6-unit frustum margin (16 for particles) | -- | `constexpr` (`PerfX::kCullMargin`) |
| `trace` | the 600-frame census on stderr | -- | **removed** (with `PerfX::Count` and its counters) |

**PERFXTBL / WALKCACHE when the extensions are off.** WALKCACHE and the snapshot are faithful and
stay on. PERFXTBL's side table serves only `anim_batch` (batch counts) and `render_culling`'s
particle verdicts: with both off, `PerfX::OnWalkSnapshot` builds nothing (one call per snapshot
rebuild that leaves the side unusable). `g_walkIndex` stays one unconditional store per entity in
the walk (a branch on the extension there would cost as much as the store). When either extension
turns on, `BeginFrame` marks the walk snapshot dirty so the side table is built with the next walk.

## 2. The extensions

| id | class | display name | the knobs it owns |
|---|---|---|---|
| `render_culling` | A (live) | "Skip unseen presentation work" | animCull, geomXfLastOnly, geomCull, iconLastOnly, iconCull, pickCull, particleCull (exact form) |
| `anim_batch` | B (locked per game, pseudo-mod `ext:anim_batch`, formatVersion 1) | "Batched animation at high speed" | animBatchAll |

One toggle each; the dialog has no sub-options (rows come from the registry table,
`extensionsdialog.cpp:189`), so the per-knob granularity is `PerfX::kRenderCulling`, a `constexpr`
struct the extension owns. No `.rsc` change: the dialog lists registry rows, so no kit run.

Both live in one directory, `src/ext/perfx/`, not one per id (DESIGN §3): they share the frame
state (`BeginFrame`, the iteration index) and PERFXTBL's side table, and splitting them would mean
a second module exporting PerfX's internals to the first.

## 3. What changed (commits, in order)

1. `docs:` this classification.
2. `move:` `src/app/perf_experimental.{h,cpp}` -> `src/ext/perfx/perfx.{h,cpp}`, `src/app/perfx_table.h`
   -> `src/ext/perfx/perfx_table.h`; includes and comment paths only.
3. `behaviour:` the terrain sidecar: `src/diag/perf_terrain.*` -> `src/game/Game/terrain_sidecar.*`
   (namespace `TerrainSidecar`), unconditional; `NoteWrite` added after the two unmirrored stores
   (`citizen_lifeevent_handler.cpp` `Cemetery_SpawnTombstone_6a90`, `pasturecomponent_states.cpp`);
   the verify mode is the COMPPROF=4 shadow; teardown calls `Report` / `Invalidate` from
   `GameUpdateLoop::Shutdown` (no longer through `PerfX::Reset`).
4. `behaviour:` the extensions: registry rows `render_culling` (A) / `anim_batch` (B, formatVersion
   1) and their `Ext::Extensions` fields; `perfx.{h,cpp}` rewritten around `g_flags` written by
   `BeginFrame` from `Ext::Ext()`; `perfx_rules.h` (the pure decisions); the particle exactness rule
   (§3.1) and the shadows (§5); `kTreeSnapshot` in `entitylist.cpp`; AMBIENT_THROTTLE removed
   (retail's body); the census (`PerfX::Count`, `kCnt*`) removed; `EntityList_MarkUpdateTreeDirty`
   declared in `EntityList.h`; the Options fields, their readers and `PerfBool` removed
   (`options.h` keeps a RETIRED note listing the flags); `-DBANISHED_PERF_EXPERIMENTAL` and its
   cache variable removed from `CMakeLists.txt`.
5. `test:` `tests/unit/ext_perfx_test.cpp`.
6. `docs:` perf/README.md's PerfX section, INVENTORY.md E2 / E3 / K8, STATUS.md.

### 3.1 The particle rule

`ParticleComponent::Tick` (0x140052b30) does, per emitter: node-follow `SetTransform` (presentation),
then for a live emitter the opacity branch -- `fadeRate != 0`: `op += dt * fadeRate`, StopEmitter
when the result is `<= 0` (clamped above the UV scale); `fadeRate == 0`: StopEmitter when
`op <= 0` -- and `ParticleInstance::Update` (presentation). `PC_UnstampOwnEmitters` before it is a
presentation frame tag. StopEmitter is the only sim / save write. `PC_Culled` now freezes a
component only when every live emitter (`instance && sceneId >= 0`) has `fadeRate == 0` and
`opacity > 0` (`PerfX::EmitterQuiet`); for such an emitter no dt makes the tick stop it
(`ext_perfx_particle_rule_matches_the_tick` checks it against a literal copy of the branch over a
grid of inputs). A fade starts only from outside the tick (`StartEmitterFade`, `FadeOutEmitter`,
`StopEmitterFade`: fire, disease), and the next `PC_Culled` sees it and lets the tick run.
Cost: a fading plume off-screen is simulated; most smoke is steady.

### 3.2 The seams (hook rules 4 and 5)

- `GameUpdateLoop::Update` start: `PerfX::BeginFrame` reads `Ext::Ext()` once into `g_flags`. A
  `render_culling` toggle off flushes every deferred pose / transform / icon (with the old flags)
  and forgets the frustum; on, it forgets the stale frustum. When the side table gains a user it
  marks the walk snapshot dirty so the next walk builds the table.
- Per model / citizen / icon / particle: one `g_flags` load and branch (as before, where it was
  the same load under the perf build's master). `AnimCullSkip` is an inline test in front of the
  out-of-line body; `ModelComponent::Tick` tests `g_flags.animBatch` before calling
  `AnimBatchDecide`, and passes `time->_ticks` otherwise.
- No `src/engine` file includes `src/ext` (the seams are all in `src/game`). The engine byte
  `MeshInstance::_rootOnlyUpdates` is the retail class static, as before.

## 4. The faithful profile

With both extensions off, every seam is a false branch and the code that runs is today's
faithful path, with two deliberate exceptions that make the faithful profile **run** code it did
not run before in the debug gate (it ran in every perf-build stress run since 2026-09-05):

1. the TREE_SNAPSHOT walk with WALKCACHE (was default-on only under the perf build's master) --
   same visit order, same ticks (shadow `TickList`: 155.5M checks, 0 mismatches, ELPERF2);
2. the terrain sidecar -- the same byte once the writer list is complete (shadow `TerrainSidecar`,
   to be run).

So the gate's first run after this block is the first debug gate of both. A resave DIFF or a
new-game census change there points at them, not at the extensions. The ambient throttle and the
census were never on in the debug gate, so their removal changes nothing there.

## 5. Shadows (`BANISHED_STRESS_COMPPROF=4`)

| name | where | what | expected |
|---|---|---|---|
| `PerfXVerdict` | `perfx.cpp` (PERFXTBL) | side table vs the old per-call hash; detail 1 batch count, 2 particle verdict (detail 3, the culled batch arm, is gone with it) | 0 |
| `PerfXSim` | `particlecomponent.cpp` `PC_Culled` | on every frozen particle tick, retail's StopEmitter decision for each live emitter with the tick's own dt and UV scale | 0 |
| `TerrainSidecar` | `terrain_sidecar.cpp` | every hot read: mirror vs the real `+0x29` byte (self-heals; `[TerrainSidecar] ...` totals at teardown) | 0 |
| `AnimBatchSim` | `perfx.cpp` `AnimBatchDecide` (anim_batch on) | detail 1: on an intermediate skip, `IsAnimating` at the batched clock vs the clock + pending ticks (layer 0); detail 2: on the last iteration, a model with skipped ticks that now needs per-tick root updates (a clip change) | **nonzero is the class B evidence**; 0 in a run would only say that run never felled a tree at speed > 1 |

## 6. Retired

`BANISHED_PERF_EXPERIMENTAL`, `_ANIM_CULL`, `_GEOMXF_LASTONLY`, `_ICON_LASTONLY`,
`_AMBIENT_THROTTLE`, `_TREE_SNAPSHOT`, `_PARTICLE_CULL`, `_PICK_CULL`, `_GEOM_CULL`, `_ICON_CULL`,
`_ANIM_BATCH_CULLED`, `_ANIM_BATCH_ALL`, `_TERRAIN_SIDECAR`, `_ANIM_CULL_MARGIN`, `_TRACE`,
`_TERRAIN_VERIFY`; the 17 `Options` fields `perfTerrainVerify` .. `perfBuildDefault` and their
readers; `PerfBool`; the CMake cache variable `BANISHED_PERF_EXPERIMENTAL` and the `-D` on the
perf / release flags (a stale `build/cmake-perf` cache keeps the unused variable harmlessly). No new
flag, no `getenv`, no new `Opt()` field.

## 7. For the coordinator

- Gate as usual (faithful). Then the A/B: `tools/build.sh stress` (faithful) vs
  `STRESS_EXT=render_culling,anim_batch tools/build.sh stress`; the second should land on today's
  perf-build numbers (compact 0.16 ms/iter) less whatever AMBIENT_THROTTLE was worth (never
  measured alone). The faithful number will be slower than today's perf-build faithful run,
  because that run had PerfX on (the `/profile:faithful` switch never covered PerfX).
- COMPPROF=4 with both on: the four shadows above.
- By eye (class A, live): toggle `render_culling` in the Extensions dialog in a running game at
  speed > 1 and pan; nothing should freeze or jump; toggling off must leave no frozen pose.

## 8. Doubts

- The two terrain writers were found by grep (`terrainType =` / `+0x29`). A write through another
  spelling (a whole-record copy into a live cell) would be caught only by the `TerrainSidecar`
  shadow.
- `render_culling` changes the frame-to-frame interpolation of render transforms
  (`geomXfLastOnly`: the render state's previous value is last frame's, not the previous
  iteration's), as PerfX did: a presentation difference, not a sim one.
- `anim_batch` is class B on reasoning plus a counting shadow; its divergence is small in size
  (up to N-1 ticks per finishing clip) but real. A future block could make it exact (never batch a
  non-looping layer-0 clip that ends within the frame; carry the skipped count across a clip change
  correctly) and move it to class A -- that needs every sim read of the clock (three found) to be
  rechecked.
- The flush on toggle-off and on pause dereferences models culled in the previous sim frame, as
  PerfX's pause flush always did; an entity drained in between would be a stale pointer (the
  existing exposure, not widened by the toggle, which happens between frames too).

## Coordinator's runs (2026-10-12 evening; idle machine, every report `machine load: CLEAN`)

Gate PERFXEXT 0 (the debug exe's first run of the snapshot walk, WALKCACHE and the sidecar; resave 3/3).
Shadows with both extensions on (`20261012_165244_perfxext_verify`, heavy town): PerfXVerdict 166.9M,
TerrainSidecar 79.2M, PerfXSim 30.5M, PathDistance 126k, TickList 156.5M, BestTarget 733k checks, 0
mismatches each; AnimBatchSim 109.5M checks, 61,913 mismatches = the class B evidence.

| town | run | sim ms/iter | Update ms/frame | EntityList |
|---|---|---|---|---|
| compact, TRUE faithful (`20261012_165xxx_perfxext_faithful_compact`) | every fix and extension off | 0.18 | 1.83 | 1.59 |
| compact, both on (`20261012_17xxxx_perfxext_on2_compact`) | fixes off | 0.17 (-6.7%) | 1.71 | 1.48 |
| heavy, TRUE faithful (`20261012_165926_perfxext_faithful_heavy`) | | 0.33 | 3.26 | 2.84 |
| heavy, both on, fixes off (`20261012_173646_perfxext_on3_heavy`) | | 0.30 (-8.8%) | 2.97 | 2.61 |

Speed 50, heavy, RTSS stopped (`20261012_170421_perfxext50_faithful` vs `20261012_17xxxx_perfxext50_on2`):
windows 1-2 Update 10.8 / 12.8 -> 9.5 / 11.7 ms, fps 172 / 116 -> 211 / 144; window 3 is not comparable
(the class B sim diverges: 1686 vs 1873 citizens by then).

**The old "faithful" number had PerfX on.** `perf/baselines/current.csv` (0.17, `2026-09-05_perfx.csv`) and
every perf-build run before this block ran PerfX unconditionally; the true faithful compact figure is 0.18
and the baseline is re-cut to it (`perf/baselines/2026-10-12_faithful_true.csv` = `current.csv`). The
extension-on compact figure (0.17) is above this morning's 0.16 because the ambient throttle is gone and
the two sidecar writers are fixed.

**A second confound, found here and fixed in `tools/stress_run.sh`:** STRESS_EXT runs left the five
fixes as settings.cfg had them (all ON; two had no line and defaulted ON) while `/profile:faithful`
forces them OFF. `fix.start_barn_height=1` alone removes 3,689 `MapComponent + ModelComponent` entities
(trees / rocks) from the large spread stress town (bisected with `STRESS_EXT=none STRESS_FIXES=<id>`;
`[Census]` lines at COMPPROF >= 1 are new). STRESS_EXT runs now write every `fix.<id>=0`. The earlier
extension A/Bs (FASTPATHS, PATHWORKERS, the pfh_* heavy runs) carried that bias; their pathfinding columns
stand, their sim deltas are slightly flattered. The mechanism (what consumes the model Z to remove
resources under the stress sets' buildings) is open: `docs/known_port_issues.md` row 1326.

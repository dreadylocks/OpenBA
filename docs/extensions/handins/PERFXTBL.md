# PERFXTBL -- PerfX's per-model / per-particle bookkeeping becomes one load

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/perfx/` (perfx.{h,cpp}, perfx_rules.h, perfx_table.h) moved to `src/ext/render_culling/` (render_culling.{h,cpp}, render_culling_rules.h, side_table.h; namespaces `PerfX` / `PerfXTbl` -> `Ext::RenderCulling` / `Ext::RenderCulling::SideTable`), ext.anim_batch's seam helpers to `src/ext/anim_batch/anim_batch.h`; tests `ext_perfx_test.cpp` + `perfx_table_test.cpp` -> `tests/unit/ext_render_culling_test.cpp` and `ext_anim_batch_test.cpp`.)

Block PERFXTBL, 2026-10-12. ELPROF section 6, item 2. PerfX is perf-build-only and non-retail;
its existing kill switches still work (`BANISHED_PERF_ANIM_BATCH_ALL` / `_CULLED`,
`BANISHED_PERF_PARTICLE_CULL`, `BANISHED_PERF_TREE_SNAPSHOT`: with the snapshot off the side table
is never built and every call takes the old hash path). No new flag, no new `Opt()` field, no
field added to `ComponentEntity` / `ModelComponent`.

## 0. Fix 1 first: the visibility cache was keyed on a stack address (`2cec1554e`)

`AnimCullSkip` and the `_ANIM_BATCH_CULLED` arm of `AnimBatchDecide` copied the model position
into a local `float pos[3]` and called `PointVisible(pos)`. `CachedVisible` keys its per-frame
cache on the pointer, so the key was the stack address of `pos`. The perf exe at `7f77a506a`
shows it: `AnimCullSkip` at 0x1401e6df0 does `lea 0x20(%rsp),%rcx` (0x1401e6e88) before
`CachedVisible`. Every model ticked from the walk reaches that line at the same `rsp` (one or two
addresses, one per walk call path), so within an Update frame every model reused the verdict of
the frame's first model, until some other key hashed into the same one of the 8192 slots and
evicted it.

**How many models it mis-decided (heavy town, estimate).** On the last iteration about 2,000
models go through `AnimCullSkip`. About 2,800 other keys (ModelCulled / PickCullSkip per citizen,
plus the particles) land in the 8192-slot table each frame. The shared entry survives a whole frame
with probability about e^(-2800/8192) ≈ 0.7. Most of the heavy town is off-screen, so the first
model is usually off-screen too. In roughly 70% of frames, then, every on-screen animated model
(the on-screen citizen count V, tens to a few hundred) was left root-only: drawn with a frozen pose
while it slid along. In the frames where the first model happened to be visible, the ~2,000 − V
off-screen models were fully posed and the cull saved nothing. Roughly V models were mis-decided
per frame, and the visible ones are exactly those whose pose shows.

The fix keys both calls on `&model->transform.translation.x`, the key `ModelCulled` and
`PickCullSkip` always used. That changes decisions: it corrects them. **The gate's newgame_perf
route cannot see pose decisions.** The fix is verified by the COMPPROF=4 shadow below (which
compares against the fixed hash path) and later by the user's eyes: on-screen citizens at speed > 1
in the perf build should animate, with no frozen limbs. A short note is in `perf/README.md`'s PerfX
section. Everything below preserves the FIXED semantics.

## 1. The old probes and their cost

ELPROF sampler, heavy town, EntityList tree phase: `CachedVisible` 3.9%, `BatchEntry` 3.5-4.1%,
`NeedsPerTickRootUpdate` 2.7%, `PlanesVisible` 1.2%, `AnimBatchDecide` 1.1%, `PC_Culled` +
`ParticleCullSkip` about 3%. Per iteration, in the default perf configuration (`ANIM_BATCH_ALL` on):

| call | per model / particle per iteration | what it did |
|---|---|---|
| `AnimBatchDecide` -> `BatchEntry(model)` | every model, every iteration (speed > 1) | multiply-hash into a 512 KB open-addressing table (32768 x 16 B), linear probe, claim-or-find the frame's skip count |
| `AnimBatchDecide` -> `NeedsPerTickRootUpdate` | every batched model, intermediate iterations | the animation-state pointer chase (+ `AnimationHasRootMotion`'s 4096-slot table for non-instanced layers) |
| `ParticleCullSkip` -> `CachedVisible` | every particle system, every iteration | `Cfg()` call (static-init guard), multiply-hash into the 8192-slot visibility table, 6-plane test on a miss |
| `AnimCullSkip` | every non-skipped model, every iteration | `Cfg()` call; the visibility test itself runs on the last iteration only |

The visibility verdict is fixed for an Update frame by design (the frustum does not change inside
one), so 9 of 10 particle probes at speed 10 re-derive an answer already known. The batch count is
per model per sim frame.

## 2. The new table and its key

`src/app/perfx_table.h` (header-only, keyed on `const void*`, unit-tested):

- `PerfXTbl::BatchHash<N>`: the old `BatchEntry` table, moved verbatim (same hash, 64 linear
  probes, claim rule), plus a find-only `Find` (stops at the first non-live slot, which is exact
  because `Entry` claims the first non-live slot of the chain).
- `PerfXTbl::Side`: one 40-byte `Slot` per entity of the TREE_SNAPSHOT walk array (`s_walkSnap`,
  sorted by pointer), holding `{entity, model, batchSerial, skipped, visKey, visSerial, vis}`.

**Key: the walk index.** `WalkUpdateTreeSnapshot` stores `PerfX::g_walkIndex = i` before ticking
entity `i` (one store per entity) and `-1` after the loop. A tick finds its slot with
`slots[g_walkIndex].entity == component->_entity` (one compare). Any other caller (wrong or `-1`
index) binary-searches the snapshot, and an entity not in the snapshot has no slot. The `modelId`
scene id was rejected: a model's id changes on `OnRemove` / `OnAdded` (herds, build state 8,
destroy, decal swaps) and the ids are spread across every scene object (trees, rocks), so the
array would be large and randomly accessed. The walk index is sequential, so the hot path is
prefetch-friendly.

The snapshot is rebuilt only at the start of an `EntityList::Update` (PreUpdate / DrainRemovals run
outside the walk). `RebuildWalkSnapshot` calls `PerfX::OnWalkSnapshot(data, n, sorted)`, and
`Side::Rebuild` merges the old slots into the new array by entity pointer. A slot whose entity is
gone hands its live count to the hash (`Spill`). An unsorted snapshot (`s_tickSorted` false) or a
failed rebuild (corrupt tree) spills everything and leaves the side unusable, so every call uses the
hash.

**The batch count's home** (one per model per sim frame): the slot of the model's entity when that
slot is live for `(model, frame)`; otherwise the hash entry; otherwise 0. Every transition preserves
it:

| event | what moves |
|---|---|
| first touch in a frame, slot reachable | the slot is claimed with `Find(model)`'s count (0 when absent): a count spilled earlier in the frame comes back |
| snapshot rebuilt, entity still there | the slot follows its entity to the new index |
| snapshot rebuilt, entity gone (left the update tree: e.g. bit 2 cleared by `ModelComponent::Tick`) | `Spill`: `Entry(model)->skipped = slot.skipped` |
| snapshot unsorted / walk falls back to the tree | all live slots spill; the side is unusable |
| a second model of the same entity claims the slot | the first model's count spills first |
| no slot (outside the snapshot / side unusable) | `Entry(model)` on the hash: the old code exactly |

**Visibility:** `Side::Visible(slot, p, frameSerial, compute)` returns the slot's verdict when it
holds key `p` for this Update frame. On a miss it calls `compute`, which is the old `CachedVisible`
(probe, plane test, store), and keeps the answer. Used by `ParticleCullSkip(pos, entity)` (new
overload, called from `PC_Culled` with `self->_entity.Get()`) and by `AnimBatchDecide`'s culled arm.
`AnimCullSkip`, `ModelCulled` and `PickCullSkip` run on the last iteration only (one call per model
per frame, so nothing to save) and keep calling `CachedVisible` directly.

Also removed from the per-iteration path: `Cfg()` in `AnimCullSkip` and in the particle test, now
`g_flags.animCull` / `g_flags.particleCull` and `s_modelMargin` / `s_particleMargin`. All four are
copied inside `Cfg()`'s initialiser, which `BeginFrame` runs before any tick.

## 3. The identity argument, decision by decision

- **Batch / no batch** (`AnimBatchDecide`'s `batch`): `g_flags.animBatchAll` unchanged. The culled
  arm's visibility is the first `CachedVisible` verdict of the frame for that key (below).
- **Count available** (`skipped == nullptr` -> no batch): old = `BatchEntry` saturated, which never
  happens at sane counts. New = the hash saturated on a spill or a no-slot call. The two tables'
  loads differ, so this is the one place the decisions could part. The shadow reports it.
- **Skip on an intermediate iteration**: `NeedsPerTickRootUpdate(model)` is unchanged and still
  evaluated on every intermediate iteration. It is NOT invariant over a frame: a blend ending, a
  `PlayAnimation` from another tick, or the `_instanced` byte being set by the next `Update` all
  change it between iterations. Caching it would change decisions, so it stays (2.7% not addressed).
- **The batched time** `tick * (skipped + 1)` on the last iteration: `skipped` is the count from
  its home (section 2). Each increment happens exactly where the old one did, on the same call. The
  home invariant makes the value read equal the old table's value for that model and frame. Unit
  tests: 40 random scripts plus 5 targeted ones, with the old per-call `BatchEntry` run beside the
  side table, give 0 mismatches. Mutation check (done, reverted): removing the rebuild spill fails 3
  tests; removing the same-entity spill fails 2.
- **Particle off-screen** (`ParticleCullSkip`): the old answer was `CachedVisible(p)`, which is the
  frame's first verdict for `p` unless its slot was evicted by a colliding key AND the recomputation
  gave a different answer. With the same planes (fixed for the Update frame) and a non-negative
  margin, it differs only when the emitter position moved between the two calls. Building emitters
  are static. A moving emitter would now keep its first verdict for the frame, which is PerfX's
  stated design ("the answer is cached per (object, frame)"). The shadow counts any such case.
- **Culled / deferred lists** (`s_culledModels`, `s_deferredPicks`, `s_deferredReplays`): written
  only by `AnimCullSkip`, `PickCullSkip` and `NoteDeferred` (status icon via `ModelCulled`). Their
  code is unchanged (all on the last iteration). They still ask `CachedVisible` directly. The old
  table now sees fewer particle probes, so its evictions differ, but since fix 1 every key is a real
  position address and an eviction only causes a recomputation with the same planes. Inside the
  last iteration a citizen's position is written once by its move tick, so the recomputation gives
  the same verdict.

## 4. Memory

40 B per snapshot entity (heavy town 2,630 entities: 105 KB), plus the same again for the merge
buffer (capacity kept, so no allocation after warm-up). One more 512 KB `BatchHash` in BSS for the
shadow, touched only at COMPPROF=4. Rebuild cost: one linear merge per snapshot rebuild, the same
order as the rebuild itself.

## 5. Verify hook (`BANISHED_STRESS_COMPPROF=4`)

`g_verifyOn` is read on each call (one predictable branch when off). When set:

- `AnimBatchDecide`: `s_batchShadow` (a separate `BatchHash`) runs the old `BatchEntry` on the same
  calls and is incremented on the same skips. Before this call's increment, its count must equal the
  slot's / hash's (`StressHarness_Verify("PerfXVerdict", ok, 1)` on a mismatch, detail 0 when ok).
- `SlotVisible` (particles and the culled arm): the old `CachedVisible(p, m)` is also called on
  every call, so the old table gets its full old access pattern. Detail 2 = particle verdict,
  3 = model verdict.

Expected at the coordinator's run: `PerfXVerdict` mismatches = 0 (the culled arm reports only with
`BANISHED_PERF_ANIM_BATCH_ALL=0`).

## 6. Doubts

- Not measured (rules: the coordinator runs the stress town). Expected: most of `BatchEntry` (one
  hash probe replaced by a sequential 40 B slot) and about 90% of the particle `CachedVisible`
  calls, around the 4-6% of the tree walk ELPROF attributed to them; `NeedsPerTickRootUpdate` and
  `AnimBatchDecide`'s self time stay.
- An entity with two ModelComponents (or two particle components) makes them share one slot: they
  flap between the slot and the hash. Correct (tested), just slower. None are known.
- `model->_entity` is assumed to be the entity that ticks it (WALKCACHE makes the same assumption).
  A violation only costs a binary search: the slot is keyed by entity.
- A freed model and a new one at the same address within one sim frame would share a count, as
  they did in the old table.
- Fix 1 changes on-screen pose decisions in the perf build. That is the intent, and neither the gate
  nor this block's tests can see it (section 0).

## Benchmark (coordinator, 2026-10-12; idle machine, `machine load: CLEAN`; BESTTARGET + PERFXTBL together, vs the ELPERF runs)

| town | sim ms/iter | tree walk ms/frame | buckets ms/frame | speed-50 windows 1-3 Update ms / fps |
|---|---|---|---|---|
| heavy, ELPERF (`20261012_144605`) | 0.29 | 1.955 | 0.531 | 9.1 / 11.4 / 13.5 -- 225 / 150 / 85 |
| heavy, ELPERF2 (`20261012_160153`, `20261012_16xxxx_elperf2_50_heavy`) | 0.29 | 1.918 | 0.551 | 9.2 / 11.4 / 13.3 -- 217 / 148 / 93 |
| compact, ELPERF (`20261012_144816`) | 0.16 | 1.094 | 0.285 | |
| compact, ELPERF2 (`20261012_160403`) | 0.16 | 1.074 | 0.285 | |

Within run noise on every column: the tree walk is -1.9% / -1.8% (PERFXTBL's share; with
`animBatchAll=1` the per-iteration probe was only `BatchEntry`, ~4% of the walk in the
sampler, so about half of it shows), the bucket phase is unchanged (BESTTARGET's estimate of
3-7% does not show; the shadow proves it identical, the A/B does not prove it faster). Both
stay merged on their identity proofs; neither moves the speed-50 fps. One run per cell.

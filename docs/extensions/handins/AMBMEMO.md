# AMBMEMO -- the ambient-environment biome scan, skipped while its cells are unchanged

Block AMBMEMO, 2026-10-19. Faithful perf work in the style of SKINMEMO / TerrainSidecar: an
always-on EXACT memo. No registry entry, no settings key, no `BANISHED_*` flag, no `Opt()` field,
no `getenv`, no door, no on/off switch. **The game was not run by this block** (build + unit only);
the coordinator runs the COMPPROF=4 shadow (`AmbientMemo`), the A/B and the resave gate.

Credit: the idea was re-prompted by the unbanished project's `docs/PERFORMANCE.md` (its ambience
reweigh fix). The old PerfX `ambientThrottle` (PERFXEXT.md row `ambientThrottle`) was the inexact
version of it and was dropped; this one is exact and pruned.

**Short version.** `AmbientEnvironmentComponent::Update` (retail 0x140043310) walks every cell of
its region each call to pick the dominant biome. The answer is a pure function of the cells' terrain
type and blocker/occupant entity description, the region rect and the (fixed) biome table. A write
clock over the cell array (`CellStamp`, one 64-bit stamp per 16x16 block) is bumped by every cell
writer; while no block under the region has a stamp newer than the clock recorded after the last
scan, the scan is skipped and the cached argmax used. The histogram's MemAlloc / MemFree, the
temperature crossfade and both `SetSlot` calls still run every tick, in the same order, with the
same arguments.

## 1. The proof

### 1.1 What the scan reads (`ambientenvironmentcomponent.cpp` `ScanRegion`, the retail loop unchanged)

| input | where | memo |
|---|---|---|
| `grid->size` (gridWidth) and `grid->cells` base | `TileGrid()` = `GameUpdateLoop::m_game` | `CellStamp` is keyed on (base, dim): a new array or dim rebuilds the table at a fresh clock -> miss |
| `regionX/Y/Width/Height` (+0x28..+0x34) | the component | stored in the memo, compared on every hit (InitRegion / Persist write them) |
| `_description` -> `AmbientEnvironmentDescription`, its `_entries` data/size | the component | desc pointer, table pointer and count stored and compared |
| each biome's `terrainMask`, `entityFilter`, `weight` | the description's table | immutable description data (loaded with the resource; nothing writes it in game) |
| per cell: `terrainType` (+0x29) | the cell | `CellStamp` (writers 1.2) |
| per cell: `blocker` (+0x00), else `occupant` (+0x08): null-ness and the pointer | the cell | `CellStamp` (writers 1.2) |
| per cell entity: `ComponentEntity::_description` (+0x28) | the entity | written only by the ComponentEntity ctor (`componententity.cpp:93`) and its Serialize (`:347`, load). A ctor'd entity reaches a cell only through a cell write; a load invalidates (1.3) |

The rest of Update (`saturate`, the weather temperature, `FindSiblingEmitter`, `SetSlot`) is
untouched and runs on hits and misses alike. `dominant` only selects `table[dominant].daySound /
nightSound`.

**Claim.** On a hit, `dominant` equals what the scan would compute now.
*Proof.* A hit needs: the rect inside the grid (`RectStamp` returns `kUncacheable` otherwise, and
then the scan always runs), the stored desc / table / count / rect equal to the current ones, and
`RectStamp(rect) <= m.clock`. `m.clock` was `CellStamp::Now()` right after the last scan, with no
cell write in between (single-threaded sim; the scan writes nothing). Every store to a cell's
+0x00 / +0x08 / +0x29 is followed by `NoteWrite`, which sets that block's stamp to `++g_clock` >
`m.clock`; a new array / dim / Invalidate rebuilds every block at `++g_clock` > `m.clock`. So no
cell input changed; the entity descriptions did not change (1.1); the table is immutable. The scan
is deterministic over those inputs, so its histogram and argmax are the cached ones. ∎

### 1.2 The cell writers (all hooked with `CellStamp::NoteWrite` right after the store)

Found by grep of every `blocker` / `occupant` / `terrainType` store, pointer alias
(`&cell->occupant`, `&grid[cell].terrainType`) and bulk copy in `src/game`, `src/ext`, `src/diag`:

| field | writer | file:line of the hook |
|---|---|---|
| +0x29 | `Map::SetTerrainType` (0x14009f2f0; reached by dc20, dee0, the start-town un-mark, the road / removal writers) | `Action/action_pf_roots_road.cpp:126` |
| +0x29, +0x08 | cemetery plot claim | `Component/cemeterycomponent.cpp:201`, `:219` |
| +0x29, +0x08 | `Cemetery_SpawnTombstone` | `Component/citizen_lifeevent_handler.cpp:233`, `:239` |
| +0x29, +0x08 | `PastureComponent::OnEvent` (pointer alias `*terrain = 2`) | `Component/pasturecomponent.cpp:346`, `:352` |
| +0x29, +0x08 | pasture state clear | `Component/pasturecomponent_states.cpp:450`, `:455` |
| +0x29, +0x08 | quarry dig | `Component/quarrycomponent_deform.cpp:330`, `:342` |
| +0x29 | removal-tool calc path | `Player/keystone_a_removalcalcpath.cpp:434` |
| +0x00 | `Map::StampFootprint` (0x14009d360; ZoneComponent Draw / OnRemove) | `Action/keystone_a_zone.cpp:182` |
| +0x08 | `MapComponent_overlay_dc20` (0x14009dc20; AddToMap), `CreateNaturalResource_impl_dee0` (0x14009dee0) | `Component/keystone_a_map.cpp:260`, `:334` |
| +0x08 | `StartTown_UnmarkResourceCellGrid` (the dee0 un-mark used by `MapComponent::RemoveFromMap`) | `Game/starttown.cpp:4455` |
| +0x00, +0x08 | the cell stream's type-check nulls (load or save) | `Game/gamegrid_serialize.cpp:173`, `:180` |
| bulk | stream load fixups (`Map::Serialize`, incl. the version<2 reclassify) | `Game/gamegrid_serialize.cpp:295` `CellStamp::Invalidate()` |
| bulk | worldgen classify `MapTileGrid_Build` | `Game/maptilegrid.cpp:82` `Invalidate()` |
| bulk | world teardown `GameUpdateLoop::Shutdown` | `Game/gameupdateloop.cpp:1880` `Invalidate()` |
| bulk | `CopyMapCell` / `ZeroMapCell` | run only inside the array's own resize, which changes the base or the count/dim -> rebuild |

Read-only (checked): the diag tool harness (`src/diag/tool_harness.cpp:1045` only reads through its
slot pointer), `TerraformCellTypeAt` (read), every other `.blocker` / `.occupant` use.

**Side finding (fixed here):** `PastureComponent::OnEvent` (`pasturecomponent.cpp`, the `*terrain = 2`
alias store) never called `TerrainSidecar::NoteWrite`, so the terrain sidecar's mirror went stale on a
pasture claim. The writer list in `terrain_sidecar.h` named only `pasturecomponent_states.cpp`. The
NoteWrite is added (`pasturecomponent.cpp:345`); the TerrainSidecar shadow should have flagged it on
any run that placed a pasture.

### 1.3 The death path and the description swap (the risk the block had to settle)

`InternalPtr` is a raw pointer (`object/Internal.h`: `CheckPtr` / `AssignId` are bare RETs, no
refcount, no weak-reference clearing). A dead entity does not turn a cell's `Get()` null: retail
relies on the owner clearing its cells **before** the object is freed. The paths:

- **MapComponent entities** (buildings, natural resources, piles): `ComponentEntity::MarkForRemoval`
  (0x1400c2ea0) broadcasts to the components -> `MapComponent::OnRemove` (slot 7, 0x1401242f0) ->
  `RemoveFromMap` -> the dee0 un-mark (`StartTown_UnmarkResourceCellGrid`), which nulls +0x08 on
  every footprint cell (hooked). The entity is freed later, when the removal list drains.
- **Zone entities**: `ZoneComponent::OnRemove` -> `StampFootprint(null)` nulls +0x00 (hooked).
- **dc20's stale-occupant eviction** (an entity whose components skip the broadcast,
  `g_dc20EvictNoTeardown`): dc20 overwrites that very cell's +0x08 with the new owner in the same
  loop (hooked).
- **Cemetery / tombstone / pasture / quarry**: the occupant store is explicit (hooked).

So every retail path that frees a cell-referenced entity writes the cell first, and the hook sees
it. A path that freed an entity while a cell still pointed at it would already be a retail
use-after-free in this very scan (it reads `ent->_description`); the memo would then keep the
pre-free answer where the faithful body reads freed memory. The COMPPROF=4 shadow below recomputes
the faithful scan on every hit and would show such a case as a mismatch (and use the faithful
answer). No extra stamp bump on the entity destroy path was needed.

**Description swap:** `ComponentEntity::_description` is written only by the ctor and by Serialize
(load). Buildings that complete, resources that grow, sites that become buildings: the blocker /
occupant ENTITY is replaced (a new entity, AddToMap -> dc20 / StampFootprint writes the cells), not
re-described in place. No hook was needed.

### 1.4 What else the hit path keeps

- The histogram's `MemAlloc(count*4, 4)` / `MemFree` pair still runs on every call. The engine
  allocator is a coalescing free-list allocator; an alloc + free relinks blocks at the head of
  their bins, so skipping the pair would change later allocation addresses relative to retail.
  Kept so the heap evolves exactly as before; it costs one lock pair per call. Dropping it is a
  possible follow-up if the coordinator rules heap layout outside the faithful contract.
- `f`, `FindSiblingEmitter`, both `SetSlot` calls: unchanged, every tick.
- The memo side table is an `unordered_map` keyed by the component, erased in
  `~AmbientEnvironmentComponent` (`ambientenvironmentcomponent.cpp:274`), never destroyed itself
  (a component torn down late still finds it). No field in the 0x48-byte object.

## 2. Where

| what | file |
|---|---|
| the write clock (table, `NoteWrite`, `RectStamp`, `Invalidate`) | `src/game/Game/cell_stamp.h`, `cell_stamp.cpp` |
| the memo, `ScanRegion` (the retail loop), the shadow, `ReportMemo`, the dtor prune | `src/game/Component/ambientenvironmentcomponent.cpp:200-290`, hit path `:317-370` |
| the dtor / `ReportMemo` declarations | `src/game/Component/AmbientEnvironmentComponent.h` |
| the hooks | section 1.2 |
| unit test | `tests/unit/cell_stamp_test.cpp` (listed in `tests/unit/sources.cmake`) |

## 3. Instruments (existing levels only)

- **COMPPROF=4** (`g_verifyOn`): on every memo hit the faithful scan runs on a scratch histogram;
  ok = same argmax AND same whole histogram (FNV hash of the counts). Reported as
  `StressHarness_Verify("AmbientMemo", ...)` -> the `[VERIFY] AmbientMemo checks=N mismatches=M`
  line of the verify summary; the first five mismatches print `detail` = `regionCell*2 + (argmax differs)`.
  A mismatch self-heals (the faithful answer drives that tick and the memo is corrected).
- **COMPPROF>=1 or 4**: one teardown line on stderr,
  `[AmbMemo] hits=.. misses=.. uncached=.. verified=.. mismatches=..` (`uncached` = a region not
  inside the grid, always scanned).

## 4. Verification (for the coordinator)

1. Shadow: a stress run at `BANISHED_STRESS_COMPPROF=4` long enough to fell trees, place and finish
   buildings, demolish, and burn (fire); expect `[VERIFY] AmbientMemo ... mismatches=0`,
   `[AmbMemo] ... mismatches=0` with `hits` >> `misses`, and `[VERIFY] TerrainSidecar ... mismatches=0`.
   A new-game run that places a pasture also exercises the TerrainSidecar fix.
2. A/B on a clean machine: `tools/build.sh stress` with main vs this branch, reading the
   `AmbientEnvironmentComponent` COMPPROF row (baseline 0.127 ms/frame, 80 calls/frame in
   `perf/results/20261018_172252_baseline_fed_clean/`) and `ph_entitylist_ms`.
3. `tools/resave_gate/resave_gate.sh Gate` (the slot sounds are serialized by
   `AmbientEmitterComponent::Persist`; expect PASS, golden unchanged).
4. `tools/build.sh run` (new game) for the boot / world invariants.

## 5. Risks

- A cell writer missed by the grep (a raw-offset store, a future writer). The shadow catches it as
  a mismatch; the fix is one `NoteWrite` at that store.
- A retail use-after-free on a cell entity (1.3) would read differently between the memo and the
  faithful body; none is known, and the shadow would show it.
- Write rate: every hooked store costs one division and one store. Trees growing / being felled
  write cells often in a big town; each write only re-scans the regions over that block (a 32x32
  region spans 4 blocks of 16x16).

## Measured (coordinator, 2026-10-19 night, perf exe, machine CLEAN, mods parked)

Shadow: `tools/build.sh stress` with `BANISHED_STRESS_COMPPROF=4` ->
`[AmbMemo] hits=476605 misses=2643 uncached=0 verified=476605 mismatches=0`, every `[VERIFY] AmbientMemo`
and `[VERIFY] TerrainSidecar` window mismatches=0 (`perf/results/20261019_225320_ambmemo_verify`).
A/B against `perf/baselines/current.csv` (`perf/results/20261019_230957_unbanished_ab`, merged branch
`perf-unbanished`): AmbientEnvironmentComponent 0.127 -> 0.010 ms/frame (80 calls/frame);
sim 0.16 -> 0.15 ms / iteration (-9.2%), update 1.65 -> 1.49 ms/frame (-9.3%), EntityList 1.40 -> 1.26
(-10.0%); hitch verdict PASS; PLAN 5% gate PASS. Resave gate PASS; unit 1182 / 0 / 0.

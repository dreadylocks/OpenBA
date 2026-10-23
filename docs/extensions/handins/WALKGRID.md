# WALKGRID -- `ext.walk_batch_grid`, the walk batch without Place (class B, default OFF)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: ext.walk_batch_grid's seam helpers are `src/ext/walk_batch_grid/walk_batch_grid.h` (Ext::WalkBatchGrid, over the body it still shares in `src/ext/walk_batch/`); its registry-row test is `tests/unit/ext_walk_batch_grid_test.cpp`; namespace `WalkBatch` -> `Ext::WalkBatch`.)

Block WALKGRID, 2026-10-13. Base `dffc78154` (main). WALKBATCH's measured class B variant (the batched
tick skips `CharacterGrid::Place`; WALKBATCH.md sections 3, 11, 13), removed as the constexpr switch
`kPlaceEveryTick` by CLEANUP row 5, is back as **its own extension row**, selectable by the player,
default OFF. It is not a switch in the code: the class A row `ext.walk_batch` keeps today's behaviour
exactly, and the class B row is a second, separately compiled walk that only runs when the player
turns it on.

**Short version.** New row `walk_batch_grid` ("Batched walking, faster"), `Tier::Extension`,
`Class::B`, default off, formatVersion 1: locked for the game, the pseudo-mod `ext:walk_batch_grid`
in the save, the block-1 manifest, the load notice -- all the registry's generic B machinery, no
per-row code. On, it batches the walk by itself (whether or not `ext.walk_batch` is on) and also
skips `Place` on the N-1 intermediate iterations of a frame. Selection: `WalkBatch::BeginFrame` reads
both rows once per frame; `GameUpdateLoop::Update` picks `EntityList::Update` / `UpdateWalkBatch` /
`UpdateWalkBatchGrid` once per iteration; the batched step is `Step<kPlace>`. **Object proof
(perf):** `EntityList::Update`, `EntityList::UpdateWalkBatch`, `WalkBatch::Tick`, `TickCharacter`
and `Step<true>` (the old `Step`) compile to the same instructions as main (section 3); the only
other change in either object is the COMDAT `Set<ComponentEntity*>::Clear` (teardown). Unit 1149 / 0
(+2). The motivation: `CharacterGrid::Place` is 6.5% of the speed-50 main thread with the class A
row (IBS_20261013_S50.md); the class B variant measured EntityList -3% / -4% against -1% / -2%
(WALKBATCH.md section 13). Not run in the game.

## 1. What the B row changes, and for whom

With `ext.walk_batch_grid` on and the game at speed N > 1, inside one frame:

- **The character grid lags.** A walker's batched tick does not call `Place`, so the grid holds its
  cell from its last real tick: the previous frame's last iteration, or its last declined tick in
  this frame. `CharacterGrid::QueryNearestCharacter` -- `MoveTo::Enter`'s follow flag for other
  walkers, the hunts, `FindBestTarget` -- can see a walker **up to N-1 ticks behind**. The last
  iteration of every frame is the faithful walk, which places every walker, so between frames the
  grid is retail's.
- **Bounded and deterministic.** The lag is at most N-1 ticks (the unit test measures exactly N-1 as
  the maximum at speeds 3, 5, 10, 20), and the lagging cell is always retail's own cell of that
  earlier tick (the test checks it tick by tick). Nothing depends on the camera or timing: two runs
  with one seed and the row on are the same game.
- **Everything else is the class A row's.** Position, yaw, speed `+0x38`, facing `+0x2C` / `+0x3C`,
  the path index, the arrival tick and the order of the instantaneous nodes are retail's at every
  tick; the drawn rotation lags inside the frame as with `ext.walk_batch` (WALKBATCH.md section 11).

**Who sees it:** the player, as a game that diverges from the original: a citizen that would have
noticed a walker one tick earlier notices it a few ticks later, so follow decisions and the nearest
target can differ, and the town drifts. The registry text says so plainly.

**What the two rows do together:**

| `ext.walk_batch` (A) | `ext.walk_batch_grid` (B, the game's locked value) | the intermediate iterations walk with |
|---|---|---|
| off | off | the faithful `EntityList::Update` |
| on | off | `UpdateWalkBatch` (Place every tick): exactly today's class A behaviour |
| off or on | on | `UpdateWalkBatchGrid` (no Place) |

The player needs one toggle for the faster walk. The A row stays live and its value is simply not
consulted while the game's B value is on.

**The menu** lists both rows: "Batched walking at high speed" (class A, "live") and "Batched walking,
faster" (class B; "locked for this game" while a game is running, the toggle then waits for the next
new game / load, with the Mods dialog's reload notice). **The save** records the B row only: the
pseudo-mod `ext:walk_batch_grid` in block 0's mod list (name = the display name, ModInfo `+0x10` = 0,
`+0x14` = `0x010007ff`, `+0x18` = 1) and the manifest record `walk_batch_grid` / formatVersion 1 at
block 1's tail; `ext.walk_batch` is class A and never appears. Retail loads such a save through its
modsMissing Yes/No modal; our loader re-locks the game with the B row on, and shows the load notice
when the player has the row off or the build lacks it.

## 2. The row text (`src/ext/registry.cpp`)

```
id             walk_batch_grid      (settings key ext.walk_batch_grid)
tier / class   Extension / B, default false, formatVersion 1
display name   Batched walking, faster
description    Above 1x speed, walking citizens take their steps inside one frame with a short
               calculation, as with Batched walking at high speed, and the spot the game uses to
               find who is near whom is moved only once per frame. Every walker follows the same
               path at the same speed, but other citizens can see a walker's position up to a frame
               late, so a game played with it diverges from the original. Locked for the game.
```

`Ext::Extensions::walk_batch_grid` is a new field after `walk_batch` (no existing field moves). The
`ext.walk_batch` row and its text are unchanged.

## 3. The selection, and the proof the faithful and class A paths are untouched

**Selection** (DESIGN.md hook rules 3 and 4; no toggle in any loop, no function pointer, no virtual):

- `WalkBatch::BeginFrame()` (once per frame, beside `PerfX::BeginFrame`): `g_frameWalk` =
  `kWalkFaithful` while the `BANISHED_MOVETICK_TRACE` diagnostic is on, else `kWalkNoPlace` when
  `Ext::Ext().walk_batch_grid` (the per-game locked value), else `kWalkPlace` when
  `Ext::Ext().walk_batch`, else `kWalkFaithful`. The only reader of either row.
- `WalkBatch::Select(rootOnly)` = `rootOnly ? g_frameWalk : kWalkFaithful` (two loads, no call).
  `GameUpdateLoop::Update` calls `UpdateWalkBatchGrid` / `UpdateWalkBatch` / `Update` from it, once
  per sim iteration.
- `EntityList::UpdateImpl<int kWalk>` (was `<bool kWalkBatch>`), `WalkUpdateTreeSnapshot<kWalk>`,
  `MakeWalkTick<kWalk>`: three instantiations. The batched lambda sends a `CharacterComponent` to
  `WalkBatch::TickCharacter` (`kWalkPlace`) or `WalkBatch::TickCharacterGrid` (`kWalkNoPlace`), both
  out of line in `walk_batch.cpp`. `RebuildWalkSnapshot<kWalk == kWalkFaithful>` keeps the faithful
  capacity hint exactly as before.
- `walk_batch.cpp`: `Step<bool kPlace>` (the `Place` call under `if constexpr (kPlace)`),
  `TickShadow<kPlace>`, and the always-inline bodies `TickImpl<kPlace>` / `TickCharacterImpl<kPlace>`
  behind the public `Tick` / `TickCharacter` (A) and `TickGrid` / `TickCharacterGrid` (B).
  `TickCharacter` still calls the public `Tick`, as it always did.

**Object proof.** `tools/build.sh build perf --jobs 2` was run in this worktree at `dffc78154`
before any edit, and `entitylist.cpp.obj`, `walk_batch.cpp.obj` and `gameupdateloop.cpp.obj` were
kept; then the same command after the change (same paths, so the same `__FILE__` strings). Two
comparisons:

1. Per function, `objdump -d -r --no-show-raw-insn -C`, with function-relative offsets and every
   operand kept, and only absolute display addresses replaced by their symbol and relocated
   operands by `<reloc>`.
2. The raw instruction bytes (objdump's byte column) of the functions that matter.

| object | function | result |
|---|---|---|
| `entitylist.cpp` | `EntityList::Update` (faithful, 1893 insns) | **identical instructions**. Raw bytes: the same except the rel32 of the two `call TreeMinimum [clone .part.0]` (a TU-local function that moved from `.text+0x1c0` to `+0x5e0`) |
| `entitylist.cpp` | `EntityList::UpdateWalkBatch` (class A, 1896 insns) | **identical instructions**. Raw bytes: the same two rel32 only |
| `entitylist.cpp` | 32 other functions (PreUpdate, PostUpdate, the Tree helpers, `TickComponents`, ...) | identical |
| `entitylist.cpp` | `EntityList::UpdateWalkBatchGrid` | new, 1896 insns (the class A walk with the other callee) |
| `entitylist.cpp` | `Collection::Set<ComponentEntity*>::Clear()` | **differs**: same 166 instructions, different register allocation / block order. It is a COMDAT template (`~EntityList` teardown only); about 75 other objects define the same COMDAT, and the linker keeps one of them. This is the same thing CLEANUP section 4 saw from a one-capture change: this function moves whenever the TU changes. |
| `walk_batch.cpp` | `Step<true>` vs the old `Step` | **identical raw bytes** (768) |
| `walk_batch.cpp` | `TickCharacter`, `Eligible [clone .constprop.0]`, `MemoFor` | **identical raw bytes** |
| `walk_batch.cpp` | `Tick` (class A, 298 insns) | **identical instructions**. Raw bytes: the same except the rel32 of its direct calls to `Step`, `MemoFor` and `Eligible`, which moved inside the object |
| `walk_batch.cpp` | `BeginFrame` | changed on purpose (40 -> 45 insns: both rows) |
| `walk_batch.cpp` | `TickGrid`, `TickCharacterGrid`, `Step<false>` | new (279 / 98 / 178 insns) |
| `gameupdateloop.cpp` | `GameUpdateLoop::Update` | changed on purpose (1573 -> 1601 insns): the three-way pick. 22 other functions identical |

Verdict: **the faithful `EntityList::Update` and the class A walk (`UpdateWalkBatch`, `Tick`,
`TickCharacter`, `Step`) are the same code as main**. The faithful iteration now pays one more
compare in `GameUpdateLoop::Update` (`Select` returns an int that is compared with two constants,
where it used to be one bool test), once per sim iteration. The scripts are under `build/wg/`
(`cmpobj.py`, `rawbytes.py`) in this worktree; they are not committed.

## 4. Shadows (`BANISHED_STRESS_COMPPROF=4`)

`TickShadow<kPlace>` is the WALKBATCH shadow with the legacy reference-cell handling restored for
`kPlace = false`. Every tick is the real tick (the sim is the faithful sim under COMPPROF=4); a tick
the batch would take is first computed by the batched step on the live object, read back and undone.

| verify name | `ext.walk_batch` only (A) | `ext.walk_batch_grid` on (B) |
|---|---|---|
| `WalkBatchSim` | **0** (the step is retail's arithmetic) | **0** |
| `WalkBatchSpan` | **0** (the decline rule) | **0** |
| `WalkBatchGrid` | **0**: the batch's own `Place` is the reference, and it matches the real tick's | **nonzero**: the real tick's `Place` (retail's) against the cell of the walker's last real `Place`, which is where the batch leaves it -- the class B evidence, as `AnimBatchSim` |

The `[COMPPROF] /char walkbatch` row (COMPPROF=2) counts the batched ticks of whichever walk ran.

## 5. Unit tests (`tests/unit/ext_walk_batch_test.cpp`)

- **`ext_walk_batch_grid_registry_row`** (new): class B, locked class, default off,
  formatVersion 1, valid id, the description says "diverges from the original" and "a frame late",
  key `ext.walk_batch_grid`. Installed on with the A row off: `Ext().walk_batch_grid` true and
  `walk_batch` false (the B row alone). Under `LockForGame`: locked (the A row not), a toggle only
  changes the request. `PseudoModsForSave` writes exactly one `ext:walk_batch_grid` (formatVersion 1,
  `+0x14` = `kPseudoModField14`, `+0x10` = 0, the display name) and no `ext:walk_batch`.
  `PseudoModLoadStatus`: 1 (modsMissing) while the player has it off, 0 once requested, 1 for a
  newer format. `LockForGameWithSet({"walk_batch_grid"})` is accepted and turns it on. The faithful
  profile writes no pseudo-mod.
- **`ext_walk_batch_two_worlds_bit_identical`** (restored, now the B row): the two scripted worlds at
  speeds 1 / 3 / 5 / 10 / 20, the batched world taking `Step<false>`. Position / path / speed /
  facing / stack bit-identical after every iteration, rotation and grid at every frame end; grid lag
  nonzero; the lag in ticks at most N-1 (measured max 2 / 4 / 9 / 19 = N-1); every lagging cell
  equal to the reference's cell of that many ticks earlier (`lagWrongCell` 0).
- **`ext_walk_batch_two_worlds_place_every_tick`** (the A row): the same with Place every tick; grid
  lag exactly 0 on every tick, max lag 0.
- `ext_walk_batch_registry_row` (the A row) is unchanged.

```
walk_batch_grid (no Place)
speed  3: 144000 ticks, 57776 batched (1522 at a waypoint), grid lag 2411 (max 2 ticks), rotation lag 23059
speed  5: 144000 ticks, 70927 batched (1856 at a waypoint), grid lag 4943 (max 4 ticks), rotation lag 28663
speed 10: 144000 ticks, 80190 batched (2143 at a waypoint), grid lag 10951 (max 9 ticks), rotation lag 37744
speed 20: 144000 ticks, 80621 batched (2135 at a waypoint), grid lag 20716 (max 19 ticks), rotation lag 44380
```

## 6. Build results (static gate)

| check | result |
|---|---|
| `tools/build.sh build perf --jobs 2` | exit 0; no new warnings in the touched files (the four `entitylist.cpp:253` / `:273` unused-parameter warnings are at base) |
| `tools/build.sh build debug --jobs 2` | exit 0; no new warnings in the touched files |
| `tools/build.sh unit perf --jobs 2` | exit 0, **1149 tests, 0 failures** (1147 + 2) |
| `python tools/abi_audit.py` | OK |
| `tools/acceptance/acceptance.sh` | 0 lines off target |
| `python tools/castmetric.py src` | total 2 |

The worktree needed `git submodule update --init` (third_party/SDL) before the first configure.
The game, the stress town and the resave gate were not run, per the brief.

## 7. How to measure (coordinator)

Heavy town = `--map large --citizens 1600 --sets 3 --spread --speed 10` (as WALKBATCH / STILLPATH).
`STRESS_EXT` sets `ext.<id>=1` in settings.cfg; the stress town is a new game, so the B row's
lock takes the requested value. Clean machine, RTSS / Afterburner stopped for the fps legs.

1. **Shadows** (COMPPROF=4, heavy):
   - `BANISHED_STRESS_COMPPROF=4 STRESS_EXT=walk_batch_grid tools/build.sh stress`: `[VERIFY]
     WalkBatchSim` 0, `WalkBatchSpan` 0, **`WalkBatchGrid` nonzero**; the other shadows
     (`StillFastPath`, `TickList`, `TerrainSidecar`, ...) 0.
   - `BANISHED_STRESS_COMPPROF=4 STRESS_EXT=walk_batch tools/build.sh stress`: all three 0
     (WALKBATCH section 13's 41M / 0 again).
2. **The A/B** (COMPPROF=0, interleaved pairs), legs **faithful** (`STRESS_EXT=none`) / **A on**
   (`STRESS_EXT=walk_batch`) / **B on** (`STRESS_EXT=walk_batch_grid`):
   - heavy town, speed 10, **window 1** only (before the B leg's population can diverge):
     EntityList ms/frame and sim ms/iteration. Expected from WALKBATCH section 13: A -1%, B -3%.
   - the same three legs with `render_culling,anim_batch` added to each (A -2%, B -4% expected).
   - **speed 50, everything on** (today's everything-on list; for the B leg add `walk_batch_grid`,
     which overrides `walk_batch`): fps, where `CharacterGrid::Place` was 6.5% of the main thread
     (IBS_20261013_S50.md). Speed-50 rows 1-3 only (the stress town starves past ~110k iterations).
3. **The faithful profile and the resave golden.** The gate as usual (`/profile:faithful` forces
   the row off): the resave golden `971aaec2cf95` must not move and `calls` must stay 5694.
4. **The save with the row on.** A new game with `ext.walk_batch_grid=1` in settings.cfg (or
   `STRESS_EXT=walk_batch_grid`), then one save:
   - `standalone_error.log`: one `[ExtSave] wrote the B / C manifest: 47 bytes at block-1 offset
     ...` line per save (magic, version, count: 12 bytes; the one record: 4 + 15 + 4 + 4 = 27;
     the trailer: 8; savetail.h's wire format).
   - the mod list: `grep -a -o 'ext:[a-z0-9_]*' "<the .sav>" | sort | uniq -c` must print exactly
     `1 ext:walk_batch_grid` (and no `ext:walk_batch` alone), provided block 0's mod list is stored
     uncompressed (see Doubts); otherwise load the save from the dialog with the row off: the load
     notice must name exactly one missing extension, "Batched walking, faster".
   - with the row off, the same route writes no `[ExtSave]` line and the save carries no `ext:`
     string.
   - the resave gate under the faithful profile reads Gate.sav, which has no manifest: unaffected.

## 8. Doubts

- **`Set<ComponentEntity*>::Clear` in entitylist.cpp's object moved** (section 3). It is a COMDAT
  template used only by `~EntityList`, and the linker may keep any of ~75 copies, so it is very likely
  not even the copy that runs; I did not chase it. The two walk functions that matter are identical.
- **The faithful iteration's extra compare** in `GameUpdateLoop::Update` (one int compare against two
  constants instead of one bool test, per sim iteration). It is outside every loop and should be
  unmeasurable; the faithful A/B leg in section 7 is where it would show.
- **The menu** lists two walk rows. The B row's description names the A row ("as with Batched
  walking at high speed") so the player sees they are the same walk, the B one faster. A player with
  both on gets the B walk; the A toggle is ignored for that game. If the user prefers the B row to
  require the A row, it is one line in `BeginFrame`.
- **The display name** "Batched walking, faster" is my choice; the dialog width was not checked in
  the game (kMaxRows is 64, the row count is fine).
- **Whether block 0's mod list is greppable in the raw .sav** was not checked here (section 7 item 4
  gives the load-notice fallback).
- Not run in the game by this block.

## 9. Files

- `src/ext/registry.h` / `registry.cpp`: the field and the row.
- `src/ext/walk_batch/walk_batch.h` / `walk_batch.cpp`: `enum Walk`, `g_frameWalk`, `Select`,
  `TickGrid`, `TickCharacterGrid`, `Step<kPlace>`, `TickShadow<kPlace>`.
- `src/game/Entity/EntityList.h` / `entitylist.cpp`: `UpdateWalkBatchGrid`, `UpdateImpl<int kWalk>`.
- `src/game/Game/gameupdateloop.cpp`: the three-way pick.
- `tests/unit/ext_walk_batch_test.cpp`: the row test, the restored two-worlds run, the lag bound.
- `docs/extensions/INVENTORY.md`: row E5 (and E4's seam list).

## Results (coordinator, 2026-10-13 night) -- MERGED as the opt-in class B row `walk_batch_grid` (default OFF)

Shadows (COMPPROF=4, heavy, mods parked): with the B row on `WalkBatchSim` 41,982,581 / 0, `WalkBatchSpan` 0,
**`WalkBatchGrid` 240,900 / 41,982,581 (0.57%: the class B evidence)**; with only the A row on all three 0
(41,442,745 checks); every other shadow 0 in both runs.

A/B series ab3 (CLEAN, overlays stopped, interleaved x2, COMPPROF=1, the exe deployed by hand under the lock;
`perf/results/*_ab3_*_wg*`):

| leg | heavy speed 10: EntityList ms/frame (steady) | window-1 upd/f | sim ms/iter |
|---|---|---|---|
| faithful (`STRESS_EXT=none`) | 2.28 / 2.28 | 2.28 / 2.21 | 0.26 / 0.26 |
| A on (`walk_batch`) | 2.23 / 2.28 | 2.27 / 2.31 | 0.26 / 0.27 |
| B on (`walk_batch_grid`) | 2.18 / 2.26 | 2.26 / 2.29 | 0.25 / 0.26 |

At speed 10 the B row reads -1..-4% EntityList against faithful, inside the pair spread (WALKBATCH section 13's
-3% stands as the better measurement). The speed-50 everything-on leg is where the row was aimed:

| leg (speed 50, the eight extensions + five fixes) | fps w1 / w2 / w3 | EntityList ms/frame | ms/iter w1 / w3 |
|---|---|---|---|
| everything on | 370 / - / 257, 372 / - / 236 | 10.5 / 10.9 | 0.19 / 0.23 |
| everything on + `walk_batch_grid` | **398 / - / 286, 404 / - / 285** | **10.0 / 9.96** | **0.17 / 0.21-0.22** |

**fps +8% in window 1, +12..+20% in window 3; sim ms per iteration -9%.** Draw unchanged (1.16-1.25). The user's
bar for the row (2026-10-13): a delay that is not visible or is within human reaction time is acceptable; the grid lag is
sub-frame in wall time and nothing drawn differs, so the row ships as the opt-in class B extension it is.

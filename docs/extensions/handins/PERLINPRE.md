# PERLINPRE -- the start town's scatter Perlin precomputed in parallel rows (hand-in)

Loading-speed follow-up to WGPAR. Base main `01fbb55ca`. Static gate only (build + unit): the game was
NOT run; the coordinator's runs below are the proof in the game. Always on, exact (same bits out), no
flag, no toggle.

## 1. What

`StartTown_SpawnResources` (`src/game/Game/starttown.cpp`, retail FUN_1400a12b0) scatters each resource
pool over every map cell. Its serial loop called `RtUtility_PerlinNoise_Generate(&perlin, (float)x *
scale, (float)y * scale)` for every one of the dim x dim cells, once per pool, before the cell's
`RandFloat` draw. The pre-WGPAR census (`LOADPLAN.md` §2.1) put `gul.starttown` at 459 ms, Perlin 59%.

Now, after the scatter Perlin is constructed (its two `RandTol` draws and the ctor unchanged, in retail
order) and before the serial loop, `WgCells_PerlinTable` fills a dim x dim float table with
`System::ParallelFor` over rows (grain 8, the WGPAR pool), and the loop reads `p = noise[dim * y + x]`
in place of the call. Nothing else in the loop moved: the threshold compare, the `RandFloat` draw, the
occupant / zone / town-exclusion / terrain tests, placement, the per-spawn variant noise (still called
in place, it is drawn only for spawned cells), `RegisterEntity`, `SeedInitialSpawn` and the `capped` exit.

| where | what |
|---|---|
| `src/game/Game/worldgen_cells.h:28`, `worldgen_cells.cpp:222` | `WgCells_PerlinTable(out, dim, pn, scale)` (in the WGPAR TU so the unit target links the real body) |
| `src/game/Game/starttown.cpp:4657` | one `std::vector<float>` of dim*dim for all pools (dim is fixed for the call) |
| `src/game/Game/starttown.cpp:4733-4743` | the table fill before the loop; the loop reads it |
| `tests/unit/wg_parallel_test.cpp:393` | `wg_parallel_scatter_perlin_table_matches_per_cell_call` |

## 2. Why it is bit-exact

* `RtUtility_PerlinNoise_Generate` (`src/engine/utility/perlin.cpp:202`) reads only its `const
  PerlinNoise*` (persistence, octaves, the two lattice offsets) and its two float arguments; it keeps no
  state, has no statics and draws no RNG (`RtLibm::Pow` / `Cos`, `SmoothNoise` are pure). WGPAR already
  relies on this for `wg.base` and the blend.
* The two seeds of the scatter Perlin are `RandTol` draws made BEFORE the ctor; the table is filled after
  the ctor and before the first `RandFloat` of the loop. Removing the call from the loop therefore removes
  no RNG draw and reorders none: the loop's `RandFloat` / `Rand` sequence is the old one, draw for draw.
* Each table slot is computed by the identical expression with identical types: `(float)x * scale`,
  `(float)y * scale` (int -> float convert, one float multiply, `scale` the same `const float`), then the
  call. x86-64 SSE floats have no excess precision and the build has no FMA contraction, so the value is
  the same in any TU and on any thread; the pool copies the caller's MXCSR into every job (WGPAR §1.1).
* No slot depends on another; nothing accumulates across cells.
* The loop visits cells in the same (y, x) order and consumes each value exactly where the call was. The
  `capped` exit could only leave values unread (`RES_POOL_CAP` is `0x7fffffff`, so it never fires today).
* There is no early path that skipped the call before: retail evaluates the noise for every cell and only
  then compares threshold and density, so the table is built whenever the loop runs (no zero-density
  skip exists to keep).
* The scratch is a `std::vector`, not `MemAlloc`: it is not a retail allocation, so the engine heap the
  scatter's entities come from sees the same allocation sequence as before.

## 3. Proof

* **Unit** (debug, `tools/build.sh unit --jobs 2`): `1175 tests, 0 failures, 1 skipped` (the skip is
  `ext_sheet_kit_crosscheck`, no kit output). The new test builds a Perlin from fixed seeds, fills the
  table and `memcmp`s it against a serial loop of the old call, dims 16 / 97 / 130 / 256, scale =
  1.0 / div for div 1 / 8 / 16.5 / 40 / 0.75, two seeds, 0 / 1 / 2 / 4 / 7 workers.
* **Build**: debug build clean, no warning in the touched files.
* **Coordinator, in the game** (not run here):
  1. `tools/build.sh run` -- a new game boots (`tools/boot_state.sh --wait 300`).
  2. World identity: `docs/extensions/handins/loadkit/hash.sh build/cmake-perf/OpenBA.exe <seed> <size>`
     for the seeds 12345 / 777 / 951625142 x small / medium / large (and the debug exe), compared with
     `hash_base_4bdffa55a.txt` -- in particular the `tiles= ... occupied=` columns, which cover the scatter.
     If main has moved the tiles hash since `4bdffa55a` for other reasons, compare against main
     `01fbb55ca` run the same way instead.
  3. `[LoadTime] gul.starttown` stamp (stderr) before / after on the perf exe, same seed and size
     (`loadkit/census.sh new <outdir>`), machine load CLEAN.
  4. `tools/resave_gate/resave_gate.sh Gate` (mods parked).

## 4. Expected saving and risks

* Perlin was ~59% of 459 ms (~270 ms) before WGPAR folded the diag prints; split over the pool it should
  fall to ~70-90 ms on 4 physical cores and ~40-50 ms on 8, plus one fork-join (~10-50 us) per pool.
  Estimate only; the `[LoadTime]` census is the measurement.
* Memory: dim*dim floats once per call (large map 1024^2 = 4 MB), freed when the function returns.
* Risk: low. The only ways it could differ are a non-pure `Generate` (it is not) or a compiler emitting
  FMA / x87 for one of the two copies of `(float)x * scale` (the build uses neither; the unit test pins
  it on this toolchain).
* One core (0 workers): `ParallelFor` runs inline on the caller, same values, same speed as before plus
  one 4 MB fill.

## 5. Credit

The idea (precompute the scatter's surface noise in parallel rows) was prompted by the unbanished
project's `docs/PERFORMANCE.md` (parallel surface-map rows). The implementation here is our own, on the
WGPAR `System::ParallelFor` pool.

## Measured (coordinator, 2026-10-19 night, perf exe, machine CLEAN, mods parked)

New game, default autostart map, `[LoadTime] gul.starttown -> gul.starttown.done`, two runs each:
main `01fbb55ca` 89.2 / 93.7 ms, branch 37.8 / 35.2 ms (-55 ms, -60%). All nine `[WorldHash]` rows
of `loadkit/hash_base_4bdffa55a.txt` reproduced bit for bit (debug exe); resave gate PASS; unit 1182 / 0 / 0
on the merged branch `perf-unbanished`. In the faithful profile the saving hides under the loading
dialog's ~1,010 ms minimum hold on a small map; with the user's settings (ext.quick_screens on) the
dialog-start to first-draw time fell 1,022 -> 930 ms for the three merged blocks together.

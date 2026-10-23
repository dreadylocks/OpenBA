# REFLCULL: the reflection pass skips terrain chunks wholly below the water cut

> **NOT MERGED (2026-10-19 night).** Built, proven exact, measured by REFLDOC (section at the end): 0 of 256 chunks
> qualify on the small test maps, 3 of 1024 on the large one, ~0.006 ms of GPU per frame in the best view, inside
> run-to-run noise. Dropped under the no-neutral-perf-bodies rule; this hand-in is the record. The code is on the
> agent branch `worktree-agent-a561f2113d6e6cae2` (348203d07) until that branch is deleted.


Faithful perf work, NOT RETAIL, always on, no flag. Title: `perf: reflection pass skips terrain
chunks wholly below the water plane (REFLCULL)`. Base `01fbb55ca`.

Credit: the idea comes from the unbanished project's `docs/PERFORMANCE.md`, "Drawing less". They skip
reflection tiles that lie wholly below the water, because the reflection shader discards everything
below the plane. Before they culled, terrain in the reflection was their largest GPU cost (8.5 of
22.7 ms).

## Part 1: does retail's terrain reflect program clip below the water? Yes, and here is how

**Which program.** `WorldGeometry::AddDrawItems` (exe 0x1400ae490) queues every visible chunk with
`_terrainMaterial`. That material is `Terrain\Terrain.rsc` (Valleys / Mountains
`_terrainMaterial`), whose Material is `Material\Terrain\TerrainMaterial.rsc`. Its `_passes` are
`normal`, `depth` and `reflect`, in that order. The installed `TerrainMaterial_resource.crs` lists
them in that order too. `Renderer::DrawItemList` binds `material->_passes[shaderIndex]`
(`src/engine/graphics/batchc_gfx_leaves.cpp:734`). The reflection group is shader 2
(`ReflectionImageBuffer::Render`, `src/engine/graphics/imagebuffer_derived.cpp:653`), so the chunks
draw with `reflect`. The installed `TerrainMaterial_reflect.crs` names `Normal_vs` +
`Normal_refl`, `TerrainLayout`, and `OpaqueMaterial.rsc:rsRef`.

**Evidence: the installed DXBC.** I read `data0.pkg` (the patch pkgs do not override these) with
`tools/audio_tail/pkg_sounds.py`'s reader and disassembled it with `D3DDisassemble`. Nothing was
written to the repo. All four detail variants of `TerrainMaterial_Normal_refl.crs` (ps_4_0) start
with the same two instructions:

    lt r0.x, -v4.y, l(0.000000)   ; then discard_nz r0.x

The input is declared `dcl_input_ps linear v4.xy`: perspective-correct, not centroid. A fragment is
discarded iff `v4.y > 0`. All four variants of `TerrainMaterial_Normal_vs.crs` (vs_4_0) compute the
attribute the same way: `o4.y = (itof(v0.z) * 0.00390625 + 0.75) * -0.666667`. Here `v0.z` is the
vertex's raw int16 height (`TerrainVertex::position[2]`, `itof` of an integer input). This matches
the kit's `GetUnderWaterAmount(z)` = `(z + 0.75) / -1.5`, with z = height/256.

(a) **The clip exists and lives in LOCAL height units.** It is a hard-coded `z/256 < -0.75`
on the vertex's raw integer height, before any transform. It is not computed from the reflection
plane.

(b) **No displacement.** The vertex position is `(x, y, z/256)`, multiplied by `transforms[0]` and
then by `worldToProjection`. No texture fetch, wind or other term moves a vertex. The value the clip
reads is the uploaded int16.

(c) **What the chunk draw covers.** `WorldGeometry::Draw` (0x1400aea10) draws a chunk as
`DrawIndexed(baseVertex = chunk * 1089, startIndex 0, indexCount = 32*32*6)` from the shared index
buffer (`geometry_draw.cpp`, default branch). `BuildTerrainMesh` (0x1400ac1e0) writes that buffer
with ids `row*33 + col`, row and col in 0..32, so the maximum id is 1088. A chunk draw therefore
reads exactly its own 33x33 block and nothing else: no skirts, no neighbour's vertices. The water
contours (+0x1c0 / +0x1d0), decals and roads are separate draw items with their own materials. They
are untouched.

(d) **The plane.** The installed `Terrain\RiverReflect2Texture` resource (the
`ReflectionImageBuffer` that both `River.rsc` and `Lake.rsc` bind as "reflection") serialises
`_reflectionPlane = (0, 0, 1, -0.75)`, a global level. The four floats sit at offset 100 of the
installed resource. The terrain's own cut sits at the same level. The skip, however, tests the
shader's cut, not the plane. A mod that moves the plane changes nothing here.

**The exactness argument.** Let a chunk's uploaded heights all satisfy `z <= -193`, which is the
code's `maxZ < kClipZ`, with `kClipZ = -192`.

1. Per vertex, `z * 2^-8` is exact and `+ 0.75` is exact (both are multiples of 2^-8 below 2^8).
   The sum is at most -1/256. Multiplying by -0.666667 gives `o4.y >= 0.0026`, and at most about 86
   (the int16 floor). The sign holds under any reassociation the driver may do (`z*(-1/384) - 0.5`
   gives the same margin), because the exact boundary is z = -192, which is never skipped.
2. The reflection target is single-sampled. `ImageBufferData::Create` uses `SampleDesc.Count = 1`
   for every texture (`src/render/dx11/imagebufferdata.cpp:523`), and the reflection's colour and
   depth both come from `ImageBuffer::Create` (`imagebuffer_derived.cpp:479`). So attributes are
   evaluated only at pixel centres inside the triangle: there is no MSAA centre extrapolation. Near-
   or guard-band clipping makes new vertices whose attributes are convex combinations of the old
   ones. The interpolated `v4.y` is therefore a convex combination of values >= 0.0026. Float32
   interpolation error is about 1e-6 relative on values <= 86, which is far below that margin. So
   `v4.y > 0` holds, and every fragment is discarded. Helper-pixel lanes never write.
3. A discarded fragment writes neither colour nor depth. The draw has no other output (no UAV, no
   query, no stream-out), so drawing the chunk leaves the reflection target unchanged.
4. Skipping the draw item leaves every other draw unchanged:
   - **State.** Every `WorldGeometry::Draw` sets its own transform, colour, vertex buffer, index
     buffer and slot 6. Slot 6 (`e_transition`) is read only by the terrain program (the kit
     declares it nowhere else).
   - **Order.** The item sort (`DrawItemLess`) is total over every field `Draw` consumes, apart
     from items identical in all of them. Removing items can reorder only identical draws.
   - **Instancing.** An instanced run needs equal instance, arg5, userData and arg10. Any item that
     sorts between two such neighbours shares the same prefix, so it is another chunk of this
     geometry, and chunk items carry `d = 1` and never batch.

**Why the skip is tied to retail's program.** The argument holds only for retail's program.
`ReflCull::IsRetailReflectPass` checks the material's pass 2 on every reflection pass. All 4 VS and
all 4 PS `_byteCode` blobs must carry retail's DXBC container checksums. If a mod replaces
`TerrainMaterial` / `Normal_refl` / `Normal_vs`, the skip turns itself off. The 32 checksum words
identify the user's data and are not data, as with the FAA checksum keys ruled to keep. ext.foliage_aa's
rewriter does not match this program: there is no `sample`/`add` before the `lt`, so its grammar
leaves it byte-identical.

## Part 2: what was built

- `src/game/Game/WorldGeometry.h:196-233`: the `ReflCull` namespace (banner, `kClipZ`, `kUnknown`,
  the side-table API, `g_report`). The class is `MemAlloc(0x230)` and statically asserted full, so
  the per-chunk max lives in a side table keyed by the owner (the game's and the menu's
  WorldGeometry). It is used on the main thread only: the geometry Update and pass 1.
- `src/game/Game/geometry_draw.cpp:77-170`: the table, the checksum test and `Drop` in
  `~WorldGeometry` (`:207`).
- `src/game/Game/geometry_draw.cpp:331-379`: in `AddDrawItems`, when `GetShader() == 2` and the pass
  is retail's, a chunk that passed the frustum test is not queued when `stored < kClipZ`. The
  frustum test, the CULLROUTE count and every other pass are unchanged.
- `src/game/Game/serialize_heightmap_mesh.cpp:263-327, 471`: `BuildTerrainMesh` drops the table,
  then resets it to all `kUnknown` after `Create`, and stores each chunk's maximum after its `Set`.
  The value is `kUnknown` when the Create or the Set failed. `RebuildDirtyChunks` does the same per
  dirty chunk (retail ignores that `Set` result; we only read it). These are the only writers of
  `_vertexBuffer`. The device-lost path (`OnVideoRestart`) goes through `BuildTerrainMesh`. With
  ext.render_pipe on, the emit is joined before every fixed step, and `GameUpdateLoop::Update` (the
  rebuild) runs only after that join, so the table and the GPU buffer stay in step for every packet.
- COMPPROF=4 only (`src/diag/stress_harness.cpp:186`, verify slot `ReflCull`): in a reflection
  pass with no chunk dirty, each visible chunk's stored max is re-derived from the heightmap with
  `FillTerrainVertices`' clamped indexing and compared. This renders nothing differently. Nothing is
  counted per frame outside that level.

Unit: `1174 tests, 0 failures, 1 skipped` (the skip is the usual `ext_sheet_kit_crosscheck`). The
debug build shows 0 warnings. The perf build result is in the final report.

## Verification (coordinator)

1. `tools/build.sh run` on a map with a lake or a wide river in view, with Reflections on in the
   Options (no reflection pass otherwise). Then `tools/boot_state.sh --wait 300`. This checks that
   it boots and plays.
2. **RenderDoc, before (main) and after (this branch), same seed and pin, water in view.** The
   reflection pass is the draws into the half-viewport RGBA8 target with the `Normal_refl` pixel
   shader (the terrain draws there: VB stride 0x10, indexCount 6144).
   - Compare the number of those terrain draws. After should be fewer by the number of skipped
     chunks; everything else in that pass should be the same set.
   - The real exactness check is inside the BEFORE capture alone. For each terrain reflect draw
     whose `baseVertex / 1089` chunk is missing AFTER, `get_post_vs_data` must show TEXCOORD3.y
     (`o4.y`) > 0 at every vertex. Then the PS discards all of its fragments: `pixel_history` on
     any pixel it covers shows it discarded or failing.
   - Per CLAUDE.md, **a pixel diff of the two frames is not the check**, because frames are not
     reproducible. Use `tools/trace_oracle/` (dump_state.py on both captures, then
     `trace_oracle.py --test`) for the rest of the frame: S3 (floored), S4 and S6 must stay 0, and
     the only S1 / S2 difference expected is the missing terrain reflect draws.
3. Optional: `BANISHED_STRESS_COMPPROF=4 tools/build.sh stress`. There must be no
   `[VERIFY] MISMATCH ReflCull`. The slot only checks when a reflection pass draws terrain with
   reflections on.

## Expected win, risks

- **The win is small and map-dependent.** A chunk is 32x32 cells, and it is skipped only when all
  1089 of its vertices are below -0.75. Narrow rivers almost never empty a whole chunk. Lake beds
  and wide river bends do. The unbanished 8.5 ms figure came from their whole terrain-in-reflection
  cost, and most of that is not this case. The draw-count delta in step 2 tells the size of the win.
  Expect a few draws per frame at most on river maps, and more on lake-heavy maps.
- **Mods.** A mod that replaces the terrain reflect program turns the skip off (the checksum test).
  A mod that replaces only `TerrainLayout` (the position format) and keeps the shaders is not
  detected. That is implausible, and the terrain would not render.
- **Heightmap writes outside the dirty set** cannot break the skip, because the table follows the
  uploaded buffer and not the heightmap. They would show up only as COMPPROF=4 `ReflCull`
  mismatches, which would be a finding about the dirty marking.
- **Precision.** The argument relies on D3D11 float32 interpolation staying far inside the
  >= 0.0026 margin, and on a single-sampled reflection target. If the reflection target ever became
  multisampled (for example a future extension), centre extrapolation could break the argument, and
  the skip would need to be revisited.

## Measured (REFLDOC)

Coordinator block REFLDOC, 2026-10-19, on `perf-unbanished` 5bc928d9e with this branch merged.
Perf flavour exes, mods parked, the user's settings unchanged (`VideoReflectionDetail=2`, window
2560x1377, reflection target 1280x688). Every capture is a separate run of `BANISHED_AUTOSTART=1
BANISHED_SEED=12345 BANISHED_MAP_SIZE=large BANISHED_CAMERA_PIN=0,-112,90`, in-game frame 150,
under RenderDoc (`--opt-ref-all-resources`), API D3D11. The captures are in
`C:/GOG Games/Banished/rdoc_ab/refldoc_*.rdc`.

**How many chunks it can ever skip.** I read the whole terrain vertex buffer from a capture and
took each chunk's max int16 height (the same value the side table holds):

| map | chunks | chunks with max < -192 |
|---|---|---|
| seed 12345 small | 256 | 0 (the lowest chunk max is -8) |
| seed 777 small | 256 | 0 (the lowest chunk max is 2) |
| seed 12345 large | 1024 | 3 (chunks 399, 400, 618; the lowest max is -683) |

On the two small maps the change never removes a draw, from any view. The view used below is
the best case on the large map: the pin sits between chunks 399 and 400, so both are in the
reflection frustum.

**The view moves between exes, so the control matters.** Pin and seed do not fix the camera at
frame 150. The scene pass worldToProjection (VS cb0) is identical in before1/2/3 (the main tree's
perf exe, md5 1416ab72…), but the after exes land elsewhere, and a BEFORE exe built from this
same tree with the four REFLCULL files reverted (`beforeL`, md5 45d14abc…) lands where after2
lands. So the camera depends on the binary and the run, not on the change. The cleanest test
is therefore `beforeL1`/`beforeL2` against `after2`, which share an identical camera.

| capture | exe | refl draws | refl terrain draws | refl terrain ms | refl pass ms | frame ms | scene draws / terrain | shadow draws / terrain |
|---|---|---|---|---|---|---|---|---|
| before1 | main BEFORE | 70 | 38 | 0.091 | 0.348 | 1.504 | 84 / 36 | 108 / 73 |
| before2 | main BEFORE | 70 | 38 | 0.086 | 0.340 | 1.461 | 83 / 36 | 108 / 73 |
| before3 | main BEFORE | 70 | 38 | 0.084 | 0.342 | 1.508 | 84 / 36 | 108 / 73 |
| beforeL1 | local BEFORE | 71 | 38 | 0.086 | 0.344 | 1.520 | 86 / 38 | 108 / 73 |
| beforeL2 | local BEFORE | 71 | 38 | 0.087 | 0.350 | 1.531 | 86 / 38 | 108 / 73 |
| after1 | REFLCULL | 69 | 36 | 0.081 | 0.344 | 1.498 | 84 / 36 | 108 / 73 |
| after2 | REFLCULL | 69 | 36 | 0.080 | 0.358 | 1.504 | 86 / 38 | 108 / 73 |

GPU ms are RenderDoc `EventGPUDuration` sums on replay: 11 replays each, the first dropped as
warm-up, and the mean of the other 10. The draws were classified by output target (the 1280x688
RGBA8 target = reflection, depth only = shadow, 2560x1377 = scene). A terrain draw is VB stride
0x10 with indexCount 6144, and its chunk is baseVertex / 1089. "frame ms" is the sum over all
events. The machine was clean: no other build or run.

- **Draws removed:** 2 of 38 reflection terrain draws (5.3%), chunks 399 and 400, in both after
  captures. They are the only two BEFORE draws that the after captures lack. Chunk 618 is not in
  this view.
- **GPU time:** the reflection terrain draws cost 0.084–0.091 ms in the five BEFORE captures and
  0.080–0.081 ms in the two after captures, a saving of about 0.006 ms (≈ 3 µs per chunk). The
  reflection pass (0.340–0.358 ms) and the frame (1.46–1.53 ms) vary run to run by 5–10 times
  that, so neither moves measurably: the saving is about 0.4% of the GPU frame, inside the noise.
- **Rest of the frame:** `compare_state.py` of beforeL1 vs beforeL2 (control) gives identical
  pipeline-configuration and signature sets, with every configuration count equal. beforeL1 vs
  after2 and beforeL2 vs after2 (test) give the same identical sets, and the one configuration
  whose count differs is `5301f7427c55` (Normal_vs / Normal_refl into the 1280x688 target):
  38 vs 36. The total instance count per (VS, PS, indexCount) group is equal across beforeL1,
  beforeL2 and after2 in every pass. Scene and shadow terrain chunk sets are unchanged. In the
  before1–3 vs after1/2 comparisons, other instance counts also differ, in the scene and shadow
  passes too, which this change does not touch. They follow the camera difference above.
- **Exactness (BEFORE captures, post-VS `TEXCOORD3.y` = the reflect PS's clip input):** for
  chunks 399 and 400, all 1089 VS-out vertices give min 0.815 / 0.898 and max 2.138 / 2.141.
  All are > 0, so every fragment is discarded. I checked them in before1, before2 and before3
  (6 chunk-draws, 6534 vertices). As a negative control, neighbours 398 and 401, which are not
  skipped, reach -0.971 and -0.500.

**Verdict (REFLDOC):** the skip is exact and changes nothing else in the frame, but it is not
worth keeping. Two small maps give 0 skippable chunks. On a large map 3 of 1024 chunks
qualify, and a view built around them removes 2 draws for about 6 µs of GPU per frame. That is
below the run-to-run noise of the reflection pass and the frame. Against that, it carries a side
table, an upload hook in two places and 32 DXBC checksum words. **Recommendation: drop.** The
unbanished 8.5 ms figure was their whole terrain-in-reflection cost. Here all 38 reflection
terrain draws together cost 0.09 ms at 1280x688, so even a perfect terrain cull in this pass
could save at most about 0.09 ms in this view.

Helper scripts (not in the repo): scratchpad `REFLDOC/` (`cap.sh` is the perf-exe capture wrapper,
because the perf build strips the `[RenderDoc]` printf lines that `capture.sh` ranks captures by,
so `capture.sh` never finds the in-game file; also `refl_analyze.py`, `chunk_maxz.py`, `extra.py`,
`instsum.py`, `camcb.py`, `state_cmp.py`).

# TERRAINBATCH: ext.terrain_batch, terrain chunk rows as one draw + the shadow chunk cull (2026-10-20)

> Built static only (perf exe + unit). **Not run**: the A/B, the trace oracle and the draw-count
> capture below are for the coordinator. Branch `ext-terrain-batch`, based on `gpuframe` 208c2a787.

## 1. What it is

One class A row, `ext.terrain_batch` ("Batched terrain drawing"), default ON like the other class A
rows, live toggle. It carries two ideas from `gpuframe/ideas_passes.md`:

- **Rank 2, the row merge.** `WorldGeometry::AddDrawItems` gave every visible terrain chunk its own
  draw item (134 main + 134 reflection + 211 shadow = 479 a frame at zoom 130 in the stress town).
  With the row on, consecutive visible chunks of one grid row become ONE item, and
  `WorldGeometry::Draw` draws it with ONE `DrawIndexed` over a run index buffer. Expected: about
  40-60 terrain draws a frame (a run is capped at 16 chunks on retail's maps, section 2).
- **Rank 4, the shadow chunk cull.** In the shadow pass (renderer shader 1) retail tests chunks
  against the sun box only, so every chunk in the box is drawn (211). With the row on, a chunk is
  also dropped when the box its shadow can reach misses the VIEW frustum, the rule retail already
  gives objects in `Scene::AddCulledDrawItems`, made height-aware (section 4). Expected ~134.

**Why an extension row and not unconditional faithful perf work.** `ideas_passes.md` suggested the
merge could be unconditional because the picture is identical. It is identical, but the draw stream
is not: the item list, the item keys and the D3D call sequence differ from retail's, and AGENTS.md §2
puts anything retail does not do in an `ext.*` row. FRUSTUMCULL / SORTMEMO were exact at the level
of their outputs (the same booleans, the same sorted array); this one is not, so it is a row and the
faithful profile keeps retail's stream byte for byte. Rank 4 changes which shadow draws happen, so it
had to be a row anyway; both share the row because both live in the same loop.

## 2. Code

| File | What |
|---|---|
| `src/ext/terrain_batch/terrain_batch_rules.h` | pure rules: the run key, the run split, the run index list, the caster sweep box, the margin (unit-tested) |
| `src/ext/terrain_batch/terrain_batch.{h,cpp}` | `AddChunkItems` (the batched chunk loop), `DrawRun`, the per-geometry run index buffers (`OnTerrainIndices` / `OnVideoError` / `Forget`, `Ready`) |
| `src/game/Game/WorldGeometry.h` | the seam `static const bool* _extTerrainBatch` (a static: the 0x230 layout is unchanged) |
| `src/game/Game/geometry_draw.cpp` | four `EXT SEAM`s: `~WorldGeometry` (Forget), `OnVideoError`, AddDrawItems step 4 (`if (ready) AddChunkItems else retail's loop`), Draw (the run arm, before the chunk branch) |
| `src/game/Game/serialize_heightmap_mesh.cpp` | one `EXT SEAM` in `BuildTerrainMesh`: retail's just-uploaded chunk index list goes to `OnTerrainIndices` |
| `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | the row; the seam set once at boot, null under the faithful profile (the `max_shadow_res` pattern) |
| `src/ext/ui/...`, `tools/ext_icons/terrainbatch.py`, `ext_sprites_data.h` | the dialog row's icon (a chunk grid with one solid row), baked |
| `tests/unit/ext_terrain_batch_test.cpp` | six cases, section 6 |

**Faithful profile:** the pointer is null. No run buffer is built, AddDrawItems takes retail's loop
(one null test), Draw's run arm is a key test that is never true (no retail key has both top bits set,
proved in the unit test), the dtor / device-loss seams are one null test each.

**The run key.** A run's key2 is `0xC000 | row << (c + l) | col << l | (len - 1)`, where `c` is the
coordinate width for the grid and `l = 14 - 2c`. Retail's chunk keys are `(row << 6) | col` and its
other modes `0x2000..0xA000`; `0xC000` / `0xE000` are free. On retail's maps (up to 32 chunks a
side) `c = 5`, `l = 4`: runs of up to 16 chunks. A grid of 64 a side gets runs of 4; more than 64
does not fit retail's own key either, and then the row stays per chunk. The run length also obeys the
16-bit index limit (`len x 1089 <= 65536`, 60). The key is self-contained: Draw needs no per-frame
state, which matters for ext.render_pipe (the render thread draws frame N while the main thread
gathers N+1).

**The run index buffer.** Built in `BuildTerrainMesh`'s seam from retail's own index bytes (the list
it just uploaded), copied `maxRun` times with `+m x vertsPerChunk` (196 KB on retail's grid),
immutable. One per WorldGeometry (menu and town), a fresh device half each build (dx11's `Create`
on a live companion overwrites its D3D buffer without releasing it). `OnVideoError` releases the
device half; the restart's `BuildTerrainMesh` builds it again. Until it is ready, AddDrawItems takes
retail's loop; a run item drawn when it is not (a device loss between gather and draw) falls back to
one retail chunk draw per chunk, so a run item always draws correctly.

## 3. Why the row merge is pixel-identical

1. **Same vertices, same order.** A run of `len` chunks from chunk (row, col) is
   `DrawIndexed(base = start, first = 0, count = len x 6144)` over the run buffer, where
   `start = (chunksPerSide x row + col) x 1089`, retail's own start. Index slot `m x 6144 + i` holds
   `pattern[i] + m x 1089`, so it fetches vertex `start + m x 1089 + pattern[i]`: exactly what retail's
   draw of chunk `col + m` fetches at its slot `i` (unit test
   `ext_terrain_batch_run_draw_fetches_the_chunk_draws_vertices`). The pattern is retail's bytes, not
   a re-derivation.
2. **Same chunk order.** The draw-item sort orders terrain items (same flags, sort key, material,
   instance) by key2. Retail's keys sort row-major; run keys sort row-major by first chunk (unit test).
   So the GPU receives the triangles in retail's order, and D3D11 keeps primitive order within a draw.
3. **Same state.** Every chunk item of retail runs the same per-item work with the same values: the
   material `Setup`, `SetTransform(0, lerped transform)`, `SetColor(1,1,1, opacity)`, the height image
   in slot 6, the vertex buffer. The run item runs it once with the same values and binds the run
   buffer where retail binds its own; `DrawIndexed` forces triangle lists either way.
4. **What could break it**, for the capture check: a terrain shader that reads `SV_PrimitiveID` (it
   restarts at 0 per draw) or `SV_VertexID`. Neither is expected for a heightfield; the capture's
   terrain VS / PS input signatures confirm it (section 5).

The dx11 counters `trianglesSubmitted` / `verticesSubmitted` are unchanged; `drawsSubmitted` drops.

## 4. The shadow chunk cull: why it never drops a chunk that can shadow the view

In the shadow pass the draw group's camera is the view (draw group 0's camera, as for objects) and
its cull camera the sun box. For each chunk retail keeps (it touches the sun box), the row also
builds, in the geometry's space:

- `light` = the sun box's plane-0 normal, normalised: the sun camera's forward, the direction light
  travels (the same vector `Scene::AddCulledDrawItems` extends casters along; the unit test checks
  `RtVolume_Frustum_MulTransform3` carries plane 0 to the camera's forward).
- `margin` = 8 texels of a 512 map over the sun box width (`-(w2 + w3)` of its side planes, which a
  rigid transform keeps; unit-tested), at least 2 units. 512 is the smallest map the game builds, so
  this is 8+ texels of any real map: more than the shadow filter's reach around a receiver's texel.
- the chunk box, retail's (`-16..16` in z, `exe 0x14017228c` / `0x1401721d0`), grown by `margin`,
  and its copy moved `T = max(11, grown height / -light.z)` along `light`; `reach` = the bounding box
  of both.

The chunk is dropped only when `reach` is entirely outside one plane of the view frustum. The
argument: a point P of the chunk shadows only points `P + t x light`, `t >= 0`, and only those at or
above the lowest terrain, z >= -16. For those `t <= (P.z + 16) / -light.z <= T`, so the segment lies
in the convex hull of the box and its moved copy, inside `reach`. A receiver whose shadow lookup (its
own texel plus the filter's neighbours) can see P's depth lies within `margin` of such a segment,
so within the grown hull, so inside `reach`. If `reach` misses the view, no visible receiver can be
darkened by the chunk, and the chunk's absence from the map changes no visible pixel. `T >= 11`
keeps at least retail's object extension. A light at or above the horizon (`light.z >= -0.001`)
gives no finite sweep and the chunk is kept. The view test is the same conservative
`FrustumCull_Test` (outside only when the whole box is behind one plane).

Same scope as retail's object rule: receivers are what the main pass draws. The water reflection is
rendered with `_shaderDetail 0` (no shadow pass of its own); if its terrain program samples the main
sun map, a chunk outside the main view could in principle shade a reflected point, exactly as an
off-screen object retail already drops could. The trace / crop check at the shoreline covers it.

## 5. How to A/B (coordinator)

The exe is built (`tools/build.sh build perf --jobs 2`); nothing was run here.

1. **Stress legs**, the coordinator's STRESS_EXT pair at `BANISHED_CAMERA_PIN=town,130`: everything
   on vs everything on with `ext.terrain_batch=0`. Read `draw_main`, `draw_emit_ms` (render_pipe) and
   fps. Expected from `ideas_passes.md`: ~430 fewer draws x ~1.8 us = 0.6-0.8 ms CPU over both threads
   (main gather + render emit), plus ~0.12 ms from the shadow cull; GPU ~0.15-0.2 ms live, the less
   certain half.
2. **Faithful gate**: `tools/build.sh run` forces the faithful profile, so the seam is null and the
   stream is retail's; `tools/resave_gate/resave_gate.sh Gate` is not affected (no sim, no save).
3. **trace_oracle S3 / S4 / S6 = 0** with the row on vs off at the same pin (the terrain chunks are the
   items that change; any nonzero is a finding).
4. **Draw count from a capture** (`capture_arm.sh`, `gpu_timing.py`): terrain draws per pass for the
   terrain shader pairs (vs1473 / vs1476 / vs1485 in GPUFRAME's capture): main and reflection about one
   per run (up to two per screen row of chunks), shadow pass ~134 chunks' worth of runs. While the
   capture is open: the terrain VS / PS input signatures carry no `SV_VertexID` / `SV_PrimitiveID`
   (section 3.4).
5. **Shadow crops** at the screen edges at low sun (the `ideas_passes.md` rank 4 check): terrain
   shadows reaching in from off-screen hills must still be there.

## 6. Tests

`tests/unit/ext_terrain_batch_test.cpp`:
- `ext_terrain_batch_layout_fits_the_grids`: the key layout and run cap per grid size, 16-bit cap.
- `ext_terrain_batch_run_key_round_trips_and_sorts_row_major`: every (row, col, len) round-trips,
  run keys sort row-major, no retail key is a run key.
- `ext_terrain_batch_rows_split_into_runs`: fixed rows and 2000 random rows: the runs cover exactly the
  visible chunks, left to right, none longer than the cap.
- `ext_terrain_batch_run_draw_fetches_the_chunk_draws_vertices`: with retail's index pattern, a run
  draw fetches the same vertex at every index slot as the per-chunk draws; largest index fits 16 bits.
- `ext_terrain_batch_caster_box_holds_every_shadow_ray`: 3000 random suns and chunks: every shadow
  ray point down to z = -16, jittered by the margin, is inside the box; retail's 11 units too; a
  non-downward light keeps the chunk.
- `ext_terrain_batch_sun_box_width_survives_the_transform`: the width and plane-0 direction read
  through the engine's `RtVolume_Frustum_MulTransform3`.

No game content: the run buffer is built at run time from the index list the game itself computes;
the icon is our own programmer art.

## 7. Not done / open

- Not run: no gate, no stress, no capture (the game dir is the coordinator's).
- The chunk z range is retail's fixed -16..16 (a real per-chunk range would need the dirty-chunk
  rebuild to update it; `ideas_passes.md` §3 rates the gain unmeasurable).
- Linux / macOS not built.

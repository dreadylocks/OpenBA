# Ideas: shadow map, reflection and main opaque passes (zoom 130, stress town, all-on)

Read-only review, 2026-10-20. Inputs: GPU_FINDINGS.md, allon_zoom130 / faithful_zoom130 timing
txt + json (re-analysed per shader pair, script `ana_passes.py` beside this file), plain_allon_55s.png,
and the code cited per section. Nothing was built or run.

## 0. How the numbers below are estimated

- **Replay ms are serialised and ~2.8x the live frame.** Live GPU estimate = replay ms x 0.36
  (3.7 / 10.4). That is crude: it assumes the live GPU is busy all frame.
- **The live frame is close to balanced.** Main thread update 1.48 + draw 2.32 ms, render thread
  emit 2.38 ms, GPU finishing inside 3.7 ms. So a GPU-only saving shows as fps only when the GPU is
  the limiter (higher resolution, weaker GPU). A **draw-count** saving cuts CPU on both threads and
  is the most likely to move fps on this box.
- **CPU per draw.** Packet 1.53 + emit 2.38 ms over ~2135 draws is ~1.8 us of CPU per draw item,
  before the gather walk. Used below as "draws x 1.8 us".
- **Fill vs vertex split.** The same tree shaders run in the reflection (1280x688, a quarter of the
  pixels) and the main pass (2560x1377). Solving cost = V + F x pixel_share per triangle:

| pair | reflection ns/tri | main ns/tri | V (vertex) | F (fill at full res) |
|---|---|---|---|---|
| pine | 0.44 | 0.92 | ~0.28 | ~0.64 |
| birch | 0.57 | 1.67 | ~0.20 | ~1.47 |
| terrain | 1.54 | 2.58 | per-draw dominated (3.2 / 5.3 us per draw) | |

  The reflection shaders are different programs, so this is a direction, not a measurement. It says
  main-pass trees are mostly pixel cost, and terrain is mostly per-draw cost.

## 1. What retail does today (answers to the brief's questions)

**Shadow map.** `Graphics::SunLight::GetSunCamera` (src/engine/graphics/sun_camera.cpp) builds ONE
orthographic box: the four near corners of the view frustum, each extruded 1000 units and cut at the
z = -2 ground plane, give 8 points; their bounding sphere's radius x sqrt(|view forward z|) is the box
half-size; the eye is pulled 128 units back along the sun; depth 1..256; the box is snapped to the
texel grid using `Width()`. No cascades, no distance limit, so the map covers the whole visible ground
including the fogged horizon in this oblique view.

Casters are chosen in `Scene::AddCulledDrawItems` (src/engine/graphics/cull.cpp:411). The shadow
group is begun with draw camera = view, cull camera = sun box
(src/engine/graphics/wave28_lane_partsys.cpp:889). Objects must touch the sun box, and their box,
grown by its own radius and 11 units along the sun box's plane 0, must touch the VIEW frustum. So
off-screen casters are already trimmed: 82 birch / 82 pine shadow draws vs 80 / 81 in the main pass.
Terrain is different: `WorldGeometry::AddDrawItems` (src/game/Game/geometry_draw.cpp) tests chunks
against the cull camera only, so the shadow pass draws every chunk in the sun box: 211 chunks vs 134
on screen.

**ext.max_shadow_res today.** A data-pointer seam in SunLight (`_extMaxShadowRes`,
`_extMaxShadowSize`, sunlight.cpp:264-370). At retail's "High" it raises the raw map from 2048 to
4096 or 8192; Medium / Low keep 1024 / 512. A change rewrites the descriptors and raises
`Device::settingsChanged` on the main thread, which rebuilds the map: a hitch, so it cannot follow
the camera frame by frame as it stands. `src/ext/max_shadow_res/` holds only the Enhanced-tab combo.

**Is the 8192 shadow pass fill or vertex bound?** Both, and it differs per tree.

| pair | faithful 2048 ns/tri | all-on 8192 ns/tri | reading |
|---|---|---|---|
| birch shadow vs964/ps968 | 0.52 | 1.15 (10 us/draw) | ~half fill: sun-facing alpha-tested leaf cards cover many texels |
| pine shadow vs926/ps930 | 0.33 | 0.48 | mostly vertex |
| buildings / terrain | 0.45-0.64 | 0.49-0.66 | vertex / per draw |

The faithful view is closer and steeper (42 terrain chunks vs 134), so the per-tree texel count rises
~5x between the two arms, not 16x. The birch rise is the fill share.

**Texel vs pixel at zoom 130 (estimate).** ~134 chunks of 32x32 units are on screen, roughly 450 x
300 units of ground. The sun box side is then ~450-500 units: 0.06 units per texel at 8192, 0.12 at
4096, 0.24 at 2048. A screen pixel near the bottom edge covers ~0.18 units. So at max zoom-out 8192
gives ~3 texels per pixel, 4096 ~1.5, 2048 ~0.75. 8192 is oversampling here; it earns its keep only
zoomed in. Confirm with the `[ShadowRes]` stderr line plus one camera dump before relying on it.

**Reflection.** `ReflectionImageBuffer::Render` (src/engine/graphics/imagebuffer_derived.cpp:609)
queues a whole scene group with the mirrored camera as both draw and cull camera, shader 2,
`_shaderDetail` 0 (so no shadow pass inside it), detail id = the Options "Reflection Quality"
(`Configuration::reflectionDetail`). Every Geometry defaults to detail 2, the terrain is 1, sky and
sun 0 (scene.cpp:529, gameupdateloop_load.cpp:661-970). So retail's Medium is already
"terrain + sky only" and Low is sky only. The user is on High.

- **Rendered when no water is visible: yes, always.** The group is queued from
  `Renderer::AddDrawItem` when a material with a rendered texture is added (wave9_lane_gfx.cpp:404).
  `WorldGeometry::AddDrawItems` adds all water contour ranges (4 + 3 in this map) with **no frustum
  test**. Both captures show the 7 water draws, including the faithful one with a different view.
- **Resolution follows the window:** half the back buffer (`ImageBuffer::Width/Height`, flag bit 9,
  src/engine/video/buffers.cpp:252/267). 1280x688 here.
- **It re-culls the whole mirrored view.** Nothing limits it to objects whose reflection can land on
  water. In this frame water is the lake plus the river, maybe 12-15% of the screen, yet 417 draws
  and 2.4M triangles go into the reflection.

**Main pass trees.** No LOD in retail: the tree meshes vary by growth stage (122-240 triangles per
instance) and the bare variant in the atlas is the season / dead look, not a distance LOD. The split
above says main-pass tree cost is mostly pixel shading, so geometry LOD buys little there.
`ext.better_trees` rewrites the cutout pixel programs (alpha to coverage with MSAA, else the discard
stays plus the mode-2 alpha fill) and the billboard colour vertex shader (shadow lookup at the card
centre). Per triangle the all-on birch / pine main pairs are not dearer than faithful (1.67 vs 1.86,
0.92 vs 1.09 ns/tri), but the views differ, so the row's own cost is not yet measured.

**Terrain.** 134 chunk draws of 2048 triangles, one `DrawIndexed` each, in each of three passes
(134 + 134 + 211 = 479 draws a frame). Chunk vertices are laid out row-major, chunk after chunk
(`start = (chunksPerSide x row + col) x scale` in `WorldGeometry::Draw`), every chunk uses the same
local index list with a base vertex. The chunk box's z range is a fixed -16..16.

## 2. Ranked ideas

### Rank 1 — ext.reflection_cull: draw into the reflection only what can land on water

**What changes.**
1. Load time: build a water occupancy grid from the WorldGeometry (cells of e.g. 8 units where the
   water contour mesh or height < water level), plus a summed-area table, plus an AABB per water
   contour range. Immutable until the terrain changes (rebuild in `RebuildDirtyChunks`'s wake).
2. `WorldGeometry::AddDrawItems`: test each water range AABB against the local frustum before its
   `AddDrawItem`. No visible water range means no water item, so `RenderTextures` never queues the
   reflection group. Frames with no water on screen lose the whole pass.
3. One seam in `Graphics::Renderer::AddDrawItem` (wave9_lane_gfx.cpp), used only while the current
   draw group is the reflection (shader 2): reject an item whose reflection cannot cross water. A
   point P at height h above the water plane d reflects where the eye-to-mirror ray crosses the
   plane, displaced from P toward the eye by (h / (eyeZ - d + h)) of the horizontal eye distance.
   So: sweep the object's xy box toward the eye by that amount for its top, grow by a margin for the
   water shader's normal-map offset, and ask the summed-area table for any water cell. Terrain chunk
   items (same Geometry, chunk in key2) use the chunk box.
   Filtering at `AddDrawItem` covers the faithful gather, ext.draw_gather and ext.gather_threads in
   one place; the table is read-only, so helper threads are safe. A later step can move the test into
   the walk to save the virtual `AddDrawItems` calls too.

**Visual effect.** None, if conservative: an object whose mirrored image cannot overlap a water pixel
never contributes to a sampled reflection texel. The one tuning knob is the distortion margin; too
small would clip reflections at the shore edge. The plane comes from the image's own `_planeD`.

**Expected gain.** This frame, by eye from the screenshot: the forest on the left, the town interior
and the fogged top third have no water near them. Estimate 60-80% of reflection draws and chunks
dropped: ~1.0-1.3 ms replay, ~0.35-0.45 ms live GPU, and ~250-330 draws x 1.8 us = ~0.45-0.6 ms
CPU across the two threads. A view with no water: the whole pass, 1.70 ms replay and 417 draws.
Zoomed in on a town, most views have no water at all.

**Class and seam.** Class A (no sim, no save). Data-pointer seams per DESIGN.md hook rule 5: one in
`Renderer::AddDrawItem`, one in `WorldGeometry::AddDrawItems`; the grid lives in `src/ext/reflection_cull/`.

**Risks.** The water plane is assumed flat at one level, which is what the single reflection plane
already assumes. A terraform or new dock changes the grid, so rebuild it with the terrain's dirty
chunks. render_pipe: the decisions are all in pass 1 (main thread or the gather helpers), nothing on
the render thread. Must not remove water items from the main pass when any part is visible.

**Effort.** 16-24 h (grid + SAT 4, range cull 3, seam + sweep test 6, margin tuning and A/B 6).

**A/B.** capture_arm.sh both arms at the same pin and zoom, with `--expect-log` on a one-line
`[ReflCull]` count. gpu_timing.py: pass 0 draw count and ms. For "no visual change", compare the
water draws' sampled region with trace_oracle and look at shoreline crops by eye; a pixel diff
between runs measures nothing. Stress run with the cap off for the fps / CPU columns.

### Rank 2 — terrain chunk runs: one draw per run of visible chunks in a row

**What changes.** `WorldGeometry::AddDrawItems` emits one draw item per run of consecutive visible
chunks in a grid row instead of one per chunk; `WorldGeometry::Draw` draws k chunks with one
`DrawIndexed(k x primCount, base vertex = run start)`. The index buffer needs the local pattern
repeated with `+ m x scale` offsets for m = 0..k-1: one buffer holding the 64-chunk pattern serves
every run length (take the first k x primCount indices). With ~1089 vertices per chunk a 16-bit
index still reaches ~60 chunks. The run length rides in an AddDrawItem argument the per-item Draw
already receives (key2 has no room: 12 bits are row / col, the top 3 are the mode).

**Visual effect.** None: the same triangles, same material, same depth; chunks do not overlap.

**Expected gain.** 479 terrain draws become ~40 (about a dozen runs per pass). ~430 draws x 1.8 us =
~0.6-0.8 ms CPU over both threads, the largest CPU item here. GPU: terrain is 1.37 ms replay across
the three passes and reads as per-draw cost, so maybe 0.4-0.6 ms replay saved, ~0.15-0.2 ms live.
Replay inflates per-event cost, so the GPU part is the less certain half.

**Class and seam.** Pixel-identical by construction; by the FRUSTUMCULL / SORTMEMO precedent this
could be faithful perf work, unconditional. If the user prefers a toggle, a class A row.

**Risks.** The draw-item sort orders terrain items among themselves; a merge must happen after the
sort or within one material key. Device-lost rebuild (`OnVideoRestart`) must recreate the extra
index buffer. Shadow pass terrain is in the same loop and benefits too.

**Effort.** 6-10 h.

**A/B.** gpu_timing.py draw counts per pass for vs1473 / vs1476 / vs1485; the stress run's draw and
emit columns; resave gate not affected.

### Rank 3 — zoom-adaptive sun map size (extend ext.max_shadow_res)

**First, zero code:** the user can set the combo to 4096 today and compare at zoom 130. If it reads
the same, the remaining question is only about keeping 8192 for close-ups.

**What changes.** Create the sun depth map with 2 or 3 mips (8192, 4096, 2048) once. Each frame,
pick the level whose texel stays at or under ~1.5 per screen pixel (from the sun box side, which
`GetSunCamera` already computes, and the view's pixel footprint), capped at the user's choice, with
hysteresis. The dx11 backend renders through a DSV of that mip and samples through an SRV limited to
it (`MostDetailedMip`), so shader UVs are unchanged; `Width()` reports the level's size so the texel
snap and the PCF texel size follow. No rebuild, no hitch. A backend that cannot do it answers "not
supported" and keeps the full size.

**Visual effect.** Stated plainly: when zoomed out, shadow edges get about 1-2 screen pixels softer
(the PCF footprint doubles in world units), and the edge snap grid coarsens. Zoomed in, nothing
changes. The user should rule by eye.

**Expected gain.** At 4096, birch shadow fill drops ~4x: ~0.4 ms; pine ~0.1; buildings and terrain
~0.1. About 0.5-0.7 ms of the 2.08 ms replay, ~0.2 ms live GPU at max zoom. No CPU change.

**Class and seam.** Class A, the existing SunLight seam plus a backend hook for the mip views.

**Risks.** The level switch moves the snap grid, which can shimmer once at the switch zoom, hence
hysteresis. The map is +33% memory (8192 D24S8 is 256 MB; with mips ~340 MB). render_pipe: the
pick happens in `AddDrawItems` (main thread); the render thread only reads it.

**Effort.** 10-16 h.

**A/B.** Capture both arms at zoom 130 and at a close zoom; gpu_timing.py pass 1 ms; shadow edges
judged by the user on crops.

### Rank 4 — terrain shadow casters: the same view test the objects get

**What changes.** In `WorldGeometry::AddDrawItems`, while the renderer's shader is 1, keep a chunk
only if its box, extended along the sun direction down to the lowest terrain (z = -16), touches the
view frustum: the rule retail applies to objects with a fixed 11 units, made height-aware.

**Visual effect.** None, if the extension reaches the lowest terrain. Low sun means long extensions,
which the rule follows.

**Expected gain.** 211 shadow chunks to ~140: ~0.08 ms replay, ~70 draws x 1.8 us = ~0.12 ms CPU.
After Rank 2 the CPU part mostly disappears and the gain is the triangles only.

**Class.** Class A. **Effort.** 3-5 h. **A/B.** Pass 1 vs1485 draw count; shadow crops at the screen
edges at low sun.

### Rank 5 — zoom-adaptive reflection detail (retail's own Medium when far)

**What changes.** A class A row that lowers the reflection group's detail id from 2 to 1 above a
camera distance, so trees and buildings leave the reflection and terrain and sky stay. Seam at the
`reflectionDetail` read in `ReflectionImageBuffer::Render`.

**Visual effect.** Visible: tree and building reflections vanish from lakes and rivers at zoom-out.
At zoom 130 they are blurred and small, but the lake edge in this frame does show them. The user
rules. Largely superseded by Rank 1, which keeps the reflections and drops only invisible work.

**Expected gain.** Trees and buildings are ~1.25 of the reflection's 1.70 ms replay and ~280 draws.

**Effort.** 2-3 h.

### Rank 6 — measure foliage overdraw before any tree pixel work

The fill split says main-pass birch spends ~1.0 ms replay on pixels and pine ~0.6. Whether that is
overdraw (canopies over canopies, alpha-tested, so late depth writes) or shader cost decides the
fix. The lead should run RenderDoc's overdraw analysis on the existing capture for pass 2, and one
capture with ext.better_trees off at the same pin to price the row itself. Only if overdraw is above
~1.5 on canopy pixels is a depth-only pre-pass for trees worth trying: it runs the alpha test twice,
so it pays only with real overdraw. No proposal until measured.

## 3. Considered and rejected

- **Distance or fog cull of shadow casters or far trees.** Retail's fog stops at `fogMax` 0.95, so a
  fogged tree still shows at 5%; culling it changes pixels. The caster cull is already tight.
- **Tree LOD or impostors.** Retail has none; main-pass tree cost is mostly pixels; generating
  impostors at load is allowed by the content rule but costs 30+ h and changes the look.
- **Cheaper tree shadow geometry (opaque cards, no alpha test).** Turns dappled canopy shadows into
  blobs; clearly visible.
- **Cascaded or tighter shadow fit.** Would change every shadow and the retail fit (including its
  preserved clamp bug); a different feature, not a perf row.
- **Smaller reflection target.** It is already half the window; a quarter would blur the reflections
  visibly and saves less than Rank 1.
- **Tighter terrain chunk z range.** The fixed -16..16 box rarely changes a frustum verdict; gain too
  small to measure.

## 4. Summary table

| rank | idea | visual | gain (estimate) | class | effort |
|---|---|---|---|---|---|
| 1 | reflection cull to water | none | 0.35-0.45 ms GPU + 0.45-0.6 ms CPU this view; whole pass when no water | A | 16-24 h |
| 2 | terrain chunk runs | none | 0.6-0.8 ms CPU + ~0.15-0.2 ms GPU | faithful or A | 6-10 h |
| 3 | zoom-adaptive sun map mip | 1-2 px softer edges at zoom-out | ~0.2 ms GPU at max zoom | A | 10-16 h |
| 4 | terrain shadow caster view test | none | ~0.03 ms GPU + ~0.12 ms CPU | A | 3-5 h |
| 5 | reflection detail by zoom | reflections lose trees | ~0.45 ms GPU + ~0.5 ms CPU | A | 2-3 h |
| 6 | foliage overdraw measurement | n/a | decides any tree pixel work | n/a | 1 h |

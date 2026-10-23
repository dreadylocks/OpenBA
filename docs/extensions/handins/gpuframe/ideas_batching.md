# Ideas: batching the decal, smoke and instanced-mesh draws (read-only review, 2026-10-20)

Inputs: GPU_FINDINGS.md, allon_zoom130 / faithful_zoom130 timing .txt + .json, and the code paths
listed per idea. Nothing was built, run or captured.

## 0. Three corrections to the brief, they change the estimates

1. **The ~4 us per tiny draw is mostly the replay counter's floor, not the decals' GPU work.**
   In the same capture the UI draws (vs288/ps302, 2-15 triangles each) cost 3.86 us each (faithful
   3.95 us). That is the same as the 40-triangle decals (4.0 us) and the 46-triangle smoke (3.9 us).
   EventGPUDuration replays each event serialised, with its own timestamps, so every draw pays a
   ~4 us floor. The replay's 1.23 + 0.95 ms for decals + smoke is therefore NOT the live GPU saving.
   Live GPU cost of 551 tiny draws on this GPU is likely 0.05-0.3 ms; it must be measured live
   (section 5). **The certain gain is CPU: draw submission on the render thread.**
2. **Decals are indexed triangle LISTS, not strips.** `WorldGeometry::Draw` ends with
   `EndIndexedVertices(0)`, and `ApplyPrimitiveType(0)` is TRIANGLELIST
   (src/render/dx11/contextdata.cpp). The capture agrees: 120 indices = 40 triangles,
   `DrawIndexed`. Smoke is the same (6 indices per quad, list). Lists concatenate with no restart
   or degenerate triangles.
3. **The render thread is on the critical path at zoom 130.** The main thread waits 1.07 ms per frame
   for the hand-off (render_pipe). Update 1.48 + main draw ~1.25 own work + 1.07 wait = 3.8 ms, about
   the frame time. So render-thread emit time saved turns into frame time almost 1:1, up to ~1 ms.
   The 1 ms ceiling is where the main thread's own work becomes the limit.

## 1. How retail submits these draws (the code)

**Decals.** `DecalComponent::OnAdded` (src/game/Component/decalcomponent.cpp) calls `WorldGeometry::AddDecal`.
That call tessellates one footprint into a `GeometryBatch` slot of `WorldGeometry::_decals` (+0x1F0).
The slot holds CPU arrays: 0x14-byte `GeometryBatchVertex`, uint16 indices, with the alpha
(decalScale * 255) baked into the vertices. Each frame `WorldGeometry::AddDrawItems`
(src/game/Game/geometry_draw.cpp) frustum-tests every slot and adds one draw item per visible decal:
geometry = the WorldGeometry, instance = the slot's material, key1 = 1 (3 if `alternateKey`),
key2 = `idx | 0x2000`, arg8 = 1. Roads are the same with key2 `idx | 0x4000` and key1 = 2. Only
`shader != 1` passes get them, so the shadow pass does not.
`Renderer::DrawItemList` (src/engine/graphics/batchc_gfx_leaves.cpp) then does the following per decal:
- `MaterialInstance::Setup(context, shader)`.
- `WorldGeometry::Draw`, which runs `SetTransform(0, lerp(_transform))` and `SetColor(1,1,1, lerp(opacity))`.
  Both values are the same for every decal, so after the first decal the compare in `SetTransform` finds no change.
- `BeginIndexedVertices(n, 0x14, m)`, which does a `Map` NO_OVERWRITE on the 8 MB dynamic VB and on the 4 MB dynamic IB.
- A memcpy of the slot's vertices and indices.
- `EndIndexedVertices(0)`, which does 2 `Unmap`, `IASetVertexBuffers` at a new byte offset, an index-buffer
  rebind at a new offset, `BindResources` and `DrawIndexed(m, start, 0)`.

So each decal costs 4 map/unmap calls, 2 IA binds, a resource flush and a draw. None of that state
varies between consecutive decals of one material.

**Smoke.** `ParticleInstance::AddDrawItems` (src/engine/graphics/wave27_lane_gfxpart.cpp) adds one item
per emitter with live particles: key1 0x8000, sort key = squared camera distance (back to front),
userData = the emitter's pool, arg8 = 0x34 (52) when the Particle is `_active`.
DrawItemList already instances consecutive items that share the same pool (instance, arg5, userData
and arg10 all equal). That is the inst 2-5 draws: chimneys of one building type sharing one
simulation. Every other emitter is one draw:
- `ParticleInstance::Draw` part A: `SetTransformScale(0, M_emitter, (1,1,1,opacity))`. The matrix differs
  per emitter, so the transform constants go dirty.
- `BindTransforms` re-uploads `_transformCount * 64` bytes with `UpdateSubresource`. `_transformCount`
  is a high-water mark that only `Context::Reset` clears, so after the tree draws it is 52 and every
  single-emitter smoke draw uploads 3.3 KB.
- Part B/C: `BeginInstancedIndexedVertices(4n, 0x34, 6n, inst)`, then 4 vertices + 6 indices per live
  particle, walked BACKWARD with a uint16 base.
- `EndInstancedIndexedVertices`.

Particle positions are emitter-local offsets. The VS places them with transforms[SV_InstanceID],
which is slot 0 for a single emitter.

**Where 52 comes from (idea C).** Five places, all agreeing:
- `Video::Context::_transforms[52]` at +0x10d8. That is 0xd00 bytes, the CPU staging that `Reset` memsets.
  It is pinned by the Context layout's static_asserts (src/engine/video/Context.h, `kTransformSlots`).
- `ContextData::GetMaxDynamicInstances` returns the literal 0x34 (backend slot 13, contextdata.cpp).
  Weather particles use it.
- `GraphicsMesh::DetailLevel::_maxInstances` (+0xa8) is serialized from the mesh resource (graphicsmesh_vtable.cpp).
  `MeshInstance::AddDrawItems` passes it as arg8, so the 52 is baked into retail content.
  `ParticleInstance` passes the 0x34 immediate.
- The transform constant buffer is BANKED (`ConstantBufferData::Bind`, src/render/dx11/buffers.cpp).
  Its rung is `sizeBytes / bankStride - 1`, so buffers exist for 1..52 matrices.
- The instanced VS presumably declares a float4x4[52] array (208 float4). That still has to be read
  from the DXBC of vs952/913/964/926.

D3D11 allows 4096 float4 per constant buffer, which is 1024 float4x4. The hardware limit is
therefore far above 52. The limit is the shader declaration plus the fixed Context staging.

## 2. The seam (shared by ideas A and B, and usable by C)

Following draw_gather's shape (src/engine/graphics/Scene.h `_extCulledGather`, hook rules 2, 4 and 5 in
docs/extensions/DESIGN.md §3):
- Add one null-by-default static `Graphics::Renderer::_extDrawItemList` of type
  `void (*)(const Renderer&, Video::Context&, int group, int list, int shader)`.
- `Renderer::DrawItemList` gets one test at its top:
  `if (_extDrawItemList) { _extDrawItemList(*this, ctx, g, l, s); return; }`.
  That is one null test per pass list, never per item. The faithful body is unchanged.
- `src/ext/draw_batch/` holds the replacement body. It is the faithful loop, copied, plus run
  detection: the toggle decision is hoisted to the seam, so there is no toggle in the inner loop.
- `Sync()` sets or clears the pointer at boot and on the Extensions-dialog toggle, as `Ext::DrawGather::Sync` does.

With render_pipe on, DrawItemList runs inside the emit on the render thread. The replacement does
too, and reads exactly what the faithful Draw callbacks read: `_decals`, the particle pools.
Lights' Draw arms tail into DrawItemList, so shadow and reflection lists go through the same seam.

---

## Idea A (rank 1): merge consecutive decals (and roads) into one dynamic draw

**What changes.** A new `ext.draw_batch` row, class A, settings key `ext.draw_batch`, default ON
like the other class A rows. Its DrawItemList replacement looks for runs of consecutive items with
all of these properties:
- `_geometry` is the WorldGeometry. Snapshot `GameUpdateLoop::_singleton->m_worldGeometry` once per call.
- `(arg5 & 0xE000)` is 0x2000 (decals) or 0x4000 (roads), the same value for the whole run.
- `_arg8 <= 1`.
- `_instance` is the same, and `_instance->_hasRenderTexture == 0`. The latter keeps GenerateTextures out of the question.
- The item passes the faithful draw test, the material pass program check.

For a run it does this:
1. `Setup(context, shader)` once.
2. The WorldGeometry::Draw prologue once: `SetTransform(0, ...)` and `SetColor(...)`.
3. Sum the vertices V and indices I.
4. Split the run where V would pass 65535, so uint16 indices hold. Also split where it would fail
   retail's capacity tests, including the sic index-vs-vertex-capacity test.
5. `BeginIndexedVertices(V, 0x14, I)`, then memcpy each slot's vertices in item order.
6. Write each slot's indices plus the running vertex base.
7. `EndIndexedVertices(0)`.

A small helper in src/ext/draw_batch/ does the fill: `GeometryBatch` lists in, VB and IB bytes out.
It is pure, so unit-testable. Files touched:
- src/engine/graphics/Camera.h or Renderer header: the static.
- batchc_gfx_leaves.cpp: the one test.
- src/ext/draw_batch/*, src/ext/registry.{h,cpp}.
- tests/unit/ext_draw_batch_test.cpp.
- docs/extensions/handins/DRAWBATCH.md.

**Why it is visually identical (bit-exact GPU input).**
- The vertex bytes are the slots' own, in the same order, with the same alpha baked in.
- The transform, color, material textures, the shadow map SRV, the programs and the render state
  are the same for every decal in a run. They are set from the same values the faithful code
  re-sets with no change.
- Indices are offset by the base. The vertex each index names is the same one.
- D3D11 rasterizes and blends the primitives of one draw in submission order. The blend over
  overlapping translucent footprints (alpha 0.5 planned foundations) is therefore the same sequence of operations.
- Depth state is the same, so depth results are too.

What differs is non-visual: dynamic-buffer byte offsets (trace_oracle already calls them non-verdict),
the Context's `_drawsSubmitted` / `_objectsSubmitted` statistics, and the RenderDoc draw count.

**Per-decal parameters that could block a merge.** None observed.
- Material: the capture shows exactly two instances, 270 + 37 draws (faithful 176 + 31), each one
  contiguous because the sort groups them. Result: 2 draws.
- `alternateKey` decals sort under key1 3, a separate run anyway.
- Alpha is in the vertices. Transform and color belong to the one WorldGeometry.

**Expected gain.**
- Draws: 307 to 2 (faithful 207 to 2). Roads add their run (17 draws of vs814 in the blended pass, if those are the roads).
- CPU, render thread: per decal we drop 4 map/unmap calls, 2 IA binds, one BindResources, one
  DrawIndexed and the Setup/prologue. Each costs about 1-2 us of D3D11 runtime plus driver time.
  The memcpy stays the same.
- Estimate: 305 x 1.0-2.0 us = **0.3-0.6 ms of the 2.38 ms emit**. Through the hand-off that is
  about the same off the 3.73 ms frame, so 8-15%.
- In the faithful profile there is no render thread, so it would come straight off the main thread's draw.
  The faithful profile turns extensions off by definition; the number only matters to show the structure.
- GPU live: about 0.05-0.15 ms (front-end per draw). The replay will show ~1.2 ms drop, mostly the counter floor.
- How the estimate was made: emit time / draw count (2.38 ms / 2135 = 1.1 us average, with tree draws
  carrying their per-instance work). Small dynamic draws carry more map/unmap work than average.
  Measure it, do not quote it.

**Risks.**
- The 16-bit index limit is handled by splitting.
- The faithful path pays one null test per DrawItemList.
- The replacement body must stay a faithful copy for every item it does not merge. Diff it against
  the faithful loop in review, and unit-test the run detection.
- The shadow-map sampling is unaffected: same SRV, same sampler, same per-pixel math.
- Sort order: only consecutive items merge, so nothing is reordered.

**Effort.** 5-8 h, including the unit test and the hand-in.

---

## Idea B (rank 2): merge smoke emitters into a few draws

Consecutive items share the smoke material: runs of 150, 65 and 26, split only by single
other-particle draws (SRVs 1835/1837) that the distance sort puts between them. Merging can only
join consecutive items, which keeps the back-to-front order. The obstacle is that each emitter
needs its own matrix and opacity, and the VS picks the matrix with SV_InstanceID.

**B1, bit-exact (preferred): emitter index per vertex, matrices in slots 0..51.**
- At load, rewrite the smoke VS, using the FAA rewriter `src/render/common/better_trees/dxbc.{h,cpp}`
  (it already parses, re-emits and re-checksums DXBC).
- The rewrite: wherever the shader indexes the transform array with SV_InstanceID, use
  `SV_InstanceID + k`. Here k is the vertex's dword at +0x0C, which every particle vertex writes as 0 today.
- The replacement DrawItemList then fills up to 52 emitter slots per draw:
  - `SetTransformScale(k, M_k, (1,1,1,opacity_k))` with exactly the faithful part-A math.
    A shared-pool run of n instances takes n consecutive slots, with exactly GetInstanceTransform's math.
  - It writes every emitter's quads exactly as part C does, in item order, with k at +0x0C.
    A shared-pool run is written n times, instance by instance, because a D3D instanced draw
    rasterizes all of instance 0 before instance 1.
  - It issues one `DrawIndexed`, so SV_InstanceID is 0 and the index is k.
- The GPU then multiplies the same matrix bytes by the same vertex bytes in the same op order,
  in the same primitive order. The output is bit-identical.
- Draws: 244 to about 9 (150 to 3, 65 to 2, 26 to 1, plus the 3 singles). Faithful 162 to about 6.
- **Step 0, a gate. Do it before any code**, when the RenderDoc session is free:
  - Disassemble vs575 and read its input layout (`get_vertex_inputs` / `disassemble_shader` on any smoke draw).
  - If +0x0C is not a VS input (for example POSITION is float3 and nothing reads +0x0C), the rewrite
    must also add an input element. That is a new layout plus an ISGN rewrite, which is harder: add about 4 h.
  - If the VS reads `transforms[iid]` in more than one place, rewrite each one.
- Faithful particles with other materials keep the retail VS. Only an item whose bound VS is the
  rewritten copy is merged, the FAA pattern of a copy kept beside the retail program.
- Effort: 10-16 h, of which 4-6 h for the DXBC rewrite and its unit test.

**B2, fallback without a shader change: pre-transform on the CPU.**
- Apply M_k to each particle centre on the CPU and fold opacity_k into the vertex alpha.
- Bind slot 0 = identity with w column (1,1,1,1). Use one draw per run, split at 65535 vertices.
- This is visually identical, but NOT bit-exact. The CPU fp32 mul/add order differs from the GPU's
  mad, and alpha*opacity is folded before the shader does its own product. Expect sub-ULP differences
  in clip-space position and 1-ULP differences in alpha. Invisible, but not provable by the gates.
- It also requires knowing the VS's exact use of the matrix, for example whether rotation applies to
  the offsets and how the w column is consumed. So step 0 is needed anyway.
- Rank B2 below B1 and use it only if B1's input-layout case turns out hard.

**Expected gain (B1).**
- CPU, render thread: per emitter we drop 4 map/unmap calls, 2 IA binds, BindResources, the draw,
  and the 3.3 KB transform `UpdateSubresource` (the `_transformCount` high-water effect above).
  We add one 64-byte `SetTransformScale` per emitter, which we already pay.
- Estimate: 235 x 1.2-2.5 us = **0.3-0.6 ms render thread**.
- GPU live: small, 0.05-0.15 ms. The replay's ~0.9 ms drop is mostly the counter floor.

**Risks.**
- Blending order is kept only if the fill walks items in list order and particles backward, as part C does.
- The uint16 base must restart at the run's vertex offset. Retail's 16-bit wrap never triggers at these counts.
  Split at 65536 vertices so the merged base never wraps where faithful would not.
- k must stay below 52 (the slot count and the banked cbuffer rungs).
- Other particle systems are untouched (they keep the retail VS).
- Device-lost recreation must rebuild the rewritten VS, the FAA stage's existing pattern.

---

## Idea C (rank 3): instance batches larger than 52 (`ext.wide_instancing`, class A)

**What changes.**
1. At load, rewrite the instanced mesh VS programs, at least the tree main, shadow and reflection
   variants (vs952/913/946/910/964/926 here). Raise their transform cbuffer declaration from 208
   float4 to 4N (`dcl_constantbuffer` operand plus the RDEF cbuffer and variable sizes). Copies sit
   beside retail, as in FAA.
2. `Context::_transforms[52]` cannot grow, because it is ABI-pinned. The extension therefore keeps its
   own N-matrix staging array and its own constant buffer(s), bound at the same VS slot for wide
   batches only.
   That needs a capability in src/render/common/render_backend.h. Backends without it answer
   "not supported" and stay at 52 (AGENTS.md §4).
3. The DrawItemList replacement (the section 2 seam) uses N instead of `item._arg8` for items whose
   VS has a wide copy. It does so only when `SetSharedInstanceData` returns slot 0: static meshes,
   so trees qualify and skinned meshes do not.

N = 256 is a reasonable start, 16 KB per upload. 1024 is the D3D11 maximum.

**Why it is identical.** Instances keep their order, matrices and vertex data, and each is drawn with
the same per-instance math. Only the index range per draw grows. This holds for opaque and alpha-tested
trees with depth, for the shadow depth, and for foliage_aa's coverage. Instance order inside a draw is
the same as the item order across draws.

**Expected gain.**
- Draws: about 490 tree draws (main 161, shadow 164, reflection 163) become about 100 at N=256.
- CPU: about 390 x 1.0-1.5 us = 0.4-0.6 ms render thread.
  The per-instance work stays: GetInstanceTransform + SetTransform, about 12.5k per frame across the three
  passes. It is probably the larger share of tree emit.
- GPU: tree draws are real work (10-15 us each in replay, 9-12k triangles), so only the per-draw
  front end is saved, about 0.1 ms live.
- Payoff is the least certain of the three.

**Risks.**
- A shader rewrite across several VS variants.
- A new backend capability and a second constant-buffer path.
- Interaction with skin_memo / anim_batch, which touch instancing. Restricting to static meshes avoids most of it.

**Effort.** 20-30 h.

**Cheaper neighbour, out of scope here.** A memo of the static trees' per-instance matrices would
attack the larger per-instance cost without any shader work. That is worth its own idea.

---

## 3. Ranking

| rank | idea | draws (all-on) | CPU render thread | GPU live | exact? | effort |
|---|---|---|---|---|---|---|
| 1 | A decals (+ roads) | 307 to 2 | 0.3-0.6 ms | ~0.1 ms | bit-exact | 5-8 h |
| 2 | B1 smoke, emitter index | 244 to ~9 | 0.3-0.6 ms | ~0.1 ms | bit-exact, after the step-0 VS check | 10-16 h |
| 3 | C wide instancing | ~490 to ~100 | 0.4-0.6 ms | ~0.1 ms | bit-exact | 20-30 h |

A + B1 together: about 540 draws out of 2135 and **~0.6-1.1 ms of render-thread emit**. Against the
1.07 ms hand-off wait, that is most of the render thread's lead over the main thread, so frame
3.73 ms to roughly 2.9-3.2 ms.

## 4. Class and seam summary

- `ext.draw_batch`, class A: no sim effect and no saved state, so it can toggle live. The settings key is `ext.draw_batch`.
  - One engine seam, `Renderer::_extDrawItemList` (null default, one test per list).
  - The B1 VS copy lives in a backend stage under src/render/dx11/ and transforms the user's installed
    DXBC at load (rule 7). Nothing is shipped.
  - Its Names-table entry goes in DESIGN.md §3.
- `ext.wide_instancing`, class A: the same seam, plus a render_backend.h capability.

## 5. How to A/B

**CPU (the verdict).**
- Run `tools/build.sh stress` (perf exe) on a CLEAN machine, everything-on, at the zoom-130 camera
  the lead used for plain_allon_zoom130_stress.csv.
- Arm X is the user's settings.cfg. Arm Y is the same with `ext.draw_batch=0`. Run each arm 3 times.
- Compare fps, frame_ms mean / p95, and the render_pipe columns: emit ms, hand-off wait.
- Expect emit down 0.3-0.6 ms per idea, and the hand-off wait down by about the same.
- A second pair with the faithful profile + only `ext.draw_batch=1` versus faithful shows the
  main-thread form, with no render thread.

**GPU, replay, descriptive only.**
- `tools/trace_oracle/capture_arm.sh <tag> --expect-log '<the hand-in's attendance line>' -- ...` for both arms.
- Then run `gpu_timing.py` on each capture.
- Expected: the vs342/ps346 group goes from 307 draws to 2 and the vs575/ps579 group from 244 to about 9.
- Report the replay ms but do not quote it as a live saving (correction 1).
- For live GPU, use PresentMon's GPU-busy column if it is installed. Otherwise use a GPU-bound setting
  (higher resolution, shadow 8192) and frame time. Never add an in-exe timer.

**Correctness.**
- Run `trace_oracle.py --test X.jsonl Y.jsonl --control X.jsonl X2.jsonl`.
- S4 and S6 must be 0. S6 checks the strides, which stay 0x14 and 0x34.
- S2 must still show the decal and smoke signatures, with fewer draws.
- Decals are static between runs of the same town, which allows a stronger check. Add a small script
  next to trace_oracle, a tool rather than an in-exe instrument. It exports the IA vertex + index data
  of every vs342 draw in both captures (RenderDoc `get_buffer_data`) and compares the ORDERED triangle lists.
  For a given material run they must be identical byte for byte.
- Smoke positions move every frame, so for B check structure: per-run triangle count, k per vertex in
  0..51, and the matrix slots. Back that with the unit test.
- Unit tests (`tools/build.sh unit`):
  - The fill helper's output equals the concatenation, with indices rebased.
  - It splits at 65535 vertices.
  - Shared-pool expansion happens in instance order.
  - The DXBC rewrite gets a test on a self-built SM4 blob, as ext_better_trees_test does.
- Finish with `tools/build.sh run` and the resave gate, which should be untouched since nothing serializes.

# GPUFRAME: where the zoomed-out render frame goes, and the ranked ideas (2026-10-20)

> **IDEAS PHASE, nothing built.** Measurements and the three reviewers' proposals are in
> `gpuframe/` beside this file. Execution is the user's pick from the ranking below.

## The question
"Is there a RenderDoc for pipeline cost, and where does the stress town's frame go at max zoom-out?"

## The tools (all exist now)
- `tools/trace_oracle/capture_arm.sh <tag> --frame N [--exe-args '/profile:faithful'] -- ENV=V` captures one
  in-game frame under RenderDoc. On the perf exe the `[RenderDoc]` printf lines are stripped, so the
  script cannot find the in-game capture's rank: take `reimpl_capture_capture_2.rdc` by hand (the first
  file is always the menu) and kill the game by PID.
- `tools/trace_oracle/gpu_timing.py <cap.rdc> [out.json]`: RenderDoc's EventGPUDuration counter (min of
  three replays) per pass and per shader pair, plus the top draws. Run it with the renderdoc_mcp venv
  python and `RENDERDOC_MODULE_PATH=tools/renderdoc_mcp/runtime`. Replay times are RELATIVE (the replay
  is serialised, ~2.8x the live frame here) and every draw pays a ~4 us counter floor, so tiny draws
  read as GPU cost they do not have live. Rank with it, do not quote it as ms.
- The live truth is the stress CSV (`frame_ms`, `draw_ms`, the render_pipe columns) plus
  `nvidia-smi dmon -s u` during the run for GPU utilisation, and `STRESS_EXT_VALUES=k=v` legs.

## The measurements (stress town, zoom 130, 2560x1377, the user's all-on settings)
Replay share: main opaque 37% (trees 2.1 ms of 3.8, terrain 0.7), late pass 24% (building ground
decals 307 draws + chimney smoke 244 draws, mostly the counter floor), 8192 shadow map 20%, water
reflection 16% (the whole scene again at half res, every frame, no water-visibility test), AO 3%, UI 1%
with a panel open. Faithful profile: 4.5 ms replay, 1086 events.

Live: 268-273 fps, frame 3.7 ms, update 1.5, draw CPU 3.8 (render_pipe main 2.3 / emit 2.4 / hand-off
wait 1.0), GPU SM utilisation 97%. The GPU is the bound at this zoom. Settings legs: shadow 8192 -> 4096
= -0.52 ms (+16% fps); AO q3 off = -0.30 ms (+11%); foliage mode 2 -> 1 = 0; 4096 -> 2048 = -0.03;
the three tree extensions off = -0.08. Details and tables: `gpuframe/GPU_FINDINGS.md`.

## Ranked ideas (merged from the three reviews)
GPU first, because it is the bound today; the CPU items become the bound right after and follow.

1. **Shadow map size by zoom** (`gpuframe/ideas_passes.md` rank 3): one 8192 map, the sun camera
   renders into a mip / sub-rect sized to the zoom so nothing is rebuilt (no hitch); 8192 is three
   texels per pixel at zoom 130 and earns its keep only zoomed in. Measured upper bound 0.5 ms at
   zoom-out. Visual: shadow edges 1-2 px softer at max zoom-out, THE USER RULES by eye. Class A, 10-16 h.
2. **ext.reflection_cull** (`ideas_passes.md` rank 1): skip the reflection pass when no water range
   is in the frustum (retail renders it every frame on any map with water) and drop objects whose
   mirror image cannot reach a water cell (occupancy grid + summed-area table, mirror-ray sweep).
   No visual change if conservative. Est. 0.35-0.45 ms GPU + 0.45-0.6 ms CPU here; the whole pass
   (417 draws) in views without water. Class A, 16-24 h. Different from REFLCULL (below-water terrain
   chunks, dropped 2026-10-19 at 6 us).
3. **ext.draw_batch, decals and roads** (`ideas_batching.md` idea A): merge consecutive decal items of
   one material into one dynamic draw (307 -> 2); bit-exact GPU input, same order. CPU render thread
   0.3-0.6 ms. 5-8 h. Pays once the GPU levers land, and on 4-core targets.
4. **Terrain chunk rows as one draw** (`ideas_passes.md` rank 2): 479 chunk draws per frame
   (134 main + 134 reflection + 211 shadow) -> ~40; vertices are contiguous row-major with one index
   pattern. Pixel-identical. 0.6-0.8 ms CPU. 6-10 h.
5. **Smoke emitters merged** (`ideas_batching.md` idea B): up to 52 emitters per draw by rewriting the
   smoke VS at load (emitter index per vertex). Bit-exact if the VS reads vertex dword +0x0C; gate:
   disassemble vs575 first. 244 -> ~9 draws, 0.3-0.6 ms CPU. 10-16 h.
6. **Release the render-pipe fence before Present** (`ideas_cpu_submit.md` 2.1): the step overlaps the
   emit tail; stream unchanged. ~+5% fps on the main thread's critical path. Inside ext.render_pipe. 6-10 h.
7. **Terrain shadow chunks get the object view test** (`ideas_passes.md` rank 4): 211 -> ~134 shadow
   chunk draws, invisible, ~0.12 ms CPU. 3-5 h.
8. **Instance batches above 52** (`ideas_batching.md` idea C): rewrite the instanced tree shaders and
   the Context staging; 490 tree draws -> ~100. 20-30 h, least certain.

Not proposed: fog cull (fogged objects still show at 5%), tree LOD / impostors (tree cost is pixels),
opaque shadow cards, cascades, smaller reflection target, AO changes (the user's look).

## Before building
- `BANISHED_SAMPLE=2000` run at zoom 130 to split the render thread between Present, buffer maps and
  constant updates (`ideas_cpu_submit.md` section 1).
- RenderDoc overdraw on the main pass, and one capture with better_trees off at the same pin.

## Rulings (the user, 2026-10-20)
- 1 shadow by zoom: CLOSED, keep 8192. SHADOWZOOM.md shows 8192 is ~2x oversampled at every zoom, not
  only zoomed out, so a zoom-adaptive map never drops below 8192 under the "keep 8192 sharpness" rule.
  4096 everywhere would be a sharpness change at all zooms; not taken.
- 2 reflection cull: DROPPED (water is almost always on screen).
- 3 decal batching, 4 terrain row merge, 5 smoke batching, 6 fence before Present, 7 terrain shadow
  chunk cull: ACCEPTED, built in worktrees from `gpuframe`.
- 8 instance batches above 52: NOT BUILT until there is proof that more batching moves perf.

## Results and the merge (2026-10-20 night)
Built in worktrees from `gpuframe`, integrated on `gpuframe-int` (sprite atlas re-baked: the text merge
kept both new sprites but one count). Unit 1207 / 0 / 1 skipped.

A/B, stress town, zoom 130, the user's settings with the 4096 map (so CPU savings can show), three
interleaved rounds; one leg dropped (another session switched the shared checkout and compiled during it):

| arm | fps | frame ms | p95 ms | render-thread emit ms |
|---|---|---|---|---|
| base | 324 | 3.09 | 7.13 | 1.84 |
| fence before Present (RENDERPIPE_FENCE.md) | 331 | 3.02 | 6.13 | 2.10 |
| draw_batch off / on (DRAWBATCH.md) | 331 / 333 | 3.02 / 3.00 | 6.81 / 6.99 | 1.78 / 1.70 |
| terrain_batch off / on (TERRAINBATCH.md) | ~330 / 333 | ~3.0 / 3.00 | 6.9 / 7.0 | 1.78 / 1.78 |

Captures of the integrated exe (8192 map): 2156 draws with both batching rows off, 1145 on (reflection
416 -> 297, shadow 591 -> 389, main 512 -> 386, late pass 620 -> 56); the live frame does not move: the
GPU is the bound on this box (RTX 5080, SM 97%). trace_oracle: S6 0, S4 identical on both sides
(pre-existing exceptions), S3 suppressed (no control pair). Device-lost forced on the render thread's
Present with the fence: one round trip, clean quit.

**RULED (the user, 2026-10-20):** the fence ships; ext.draw_batch and ext.terrain_batch ship default ON
despite the neutral fps on this box, for weaker CPUs (an explicit exception to "no off-switched perf
bodies"); idea 8 (>52 instances) stays unbuilt — the 2156 -> 1145 capture is the proof that more batching
does not move this frame. The rock / iron shadow gap the user saw is a separate investigation (SHADOWGAP).

# GPU frame cost, stress town, zoom 130 (retail max), 2560x1377, 2026-10-20

Capture: C:/GOG Games/Banished/rdoc_gpu/gpu_allon_zoom130.rdc (perf exe 59e7d263, main c4fddf41f,
user's settings.cfg = every extension ON: AO q3, max_shadow 8192, foliage_aa 2, better_trees,
render_pipe, render_culling, draw_gather ...). Raining. 2135 draws, 3 clears.
Timing = RenderDoc replay EventGPUDuration, min of 3 runs, per-event sum = 10.40 ms.
RELATIVE numbers (replay is serialised), ranking is reliable, absolute ms is not.

| pass | ms | % | draws | tris | what |
|---|---|---|---|---|---|
| 0 reflection 1280x688 | 1.70 | 16 | 417 | 2.4M | the whole scene again at half res for the water |
| 1 shadow map 8192x8192 depth | 2.08 | 20 | 585 | 3.0M | trees 1.3 (birch 0.82 + pine 0.47), rest buildings |
| 2 main opaque 2560x1377 | 3.84 | 37 | 506 | 2.8M | birch 1.17 (80 draws x52 inst) + pine 0.90 (81 x52) + terrain 0.71 (134 draws, 2048 tris) + ... |
| 3-16 AO (ASSAO half res) | 0.31 | 3 | 18 | - | cheap |
| 17 blended/late 2560x1377 | 2.52 | 24 | 610 | 121k | building ground decals 1.23 (307 draws, 40-tri strips, one 2048 atlas) + chimney smoke 0.95 (244 draws, 44-46 tri strips, 128 blob+normal) + UI 0.1 |

Per-draw cost in pass 17: 4.1 us mean for ~40 triangles = pure draw overhead, not fill.
Shader pairs (RenderDoc ids in this capture): birch main vs952/ps960, pine main vs913/ps921,
birch shadow vs964/ps968, pine shadow vs926/ps930, terrain vs1476/ps1480, decals vs342/ps346,
smoke vs575/ps579 (SV_InstanceID input but inst 0-5), UI vs288/ps302.
Instance batch size is 52 everywhere (retail cbuffer limit).

Not measured yet: CPU/GPU split at this zoom (frame_ms vs update+draw CPU), the faithful arm,
the panel-open arm.

## Arms B and C, and the CPU side (2026-10-20 21:05-21:20)

Panel-open arm (gpu_allon_zoom130_panel.rdc, Wood Cutter panel held): total 10.31 ms replay,
UI group vs288/ps302 = 28 draws 0.108 ms. The whole UI incl. the panel is ~1% of the frame.

Faithful arm (/profile:faithful, every ext and fix OFF; gpu_faithful_zoom130.rdc): 1086 events,
4.47 ms replay total. Shadow map is 2048 (0.59 ms), reflection 0.66, main 3.22 of which
building decals vs342/ps346 207 draws 0.86 ms (19%), chimney smoke vs575/ps579 162 draws 0.65 (14%).
NOTE the faithful view differs (ext.better_camera / eye_camera off change pitch and visible area:
19 tree draws vs 80), so faithful vs all-on is not a like-for-like diff; the per-draw overhead of the
decal + smoke groups is the common top item in BOTH (33% faithful, 21% all-on).

Plain run, no RenderDoc, everything on, zoom 130, stress town, fps cap off (plain_allon_zoom130_stress.csv):
  268 fps, frame 3.73 ms (p95 9.09), update 1.48 ms, draw 3.85 ms CPU
  render_pipe: draw main 2.32 ms, packet 1.53, emit 2.38 on the render thread, hand-off wait 1.07 ms/frame
  RenderDoc live overlay on the same arm read 4 ms / 201 fps.
So at this zoom the frame is ~3.7 ms with the GPU finishing inside it; the replay's 10.4 ms is ~2.8x
inflated (serialised replay). CPU submission (2.3 ms main + 2.4 ms emit) is of the same order as the
GPU: both sides matter, neither dominates. The faithful live run under RenderDoc ran at ~475 fps.

The "wonky terrain" the user saw during arm A (stepped terrain shading, black triangle spikes)
appeared only between the menu capture and the in-game capture under RenderDoc; the plain run's
screenshot at 55 s (plain_allon_55s.png) and the capture frame itself are clean. Not reproduced
without RenderDoc.

## Live levers at zoom 130 (stress legs, no RenderDoc, fps cap off, machine clean, 2026-10-20 21:37-21:55)

GPU utilisation during the all-on plain run: nvidia-smi SM% p50 97, p90 98 (RTX 5080). THE GPU IS THE
BOUND at this zoom with the user's settings.

Legs with the user's values (fixes on, every ext on), `STRESS_EXT_VALUES` overrides:

| leg | frame ms | fps | hand-off wait us | delta |
|---|---|---|---|---|
| control: shadow 8192, foliage mode 2, AO q3 | 3.66 | 273 | 1033 | - |
| shadow 4096 | 3.14 | 318 | 533 | -0.52 ms, +16% |
| foliage_aa mode 1 | 3.67 | 272 | 914 | 0 |

Earlier legs (runner bug: values zeroed, so shadow 4096 / mode 0 / fixes off; fixed in stress_run.sh):

| leg | frame ms | fps | delta |
|---|---|---|---|
| all (4096, mode 0) | 2.97 | 336 | - |
| shadow 2048 | 2.94 | 340 | -0.03 |
| better_trees + foliage_aa + billboard_shadow off | 2.89 | 346 | -0.08 |
| ambient_occlusion off | 2.67 | 374 | -0.30 ms, +11% |

Reading: 8192 -> 4096 is worth 0.5 ms at zoom-out (3 shadow texels per screen pixel at 8192, see
ideas_passes.md), AO q3 0.3 ms, everything else under 0.1. Once the GPU levers land, the CPU side
(draw main 2.3 + emit 1.9 ms) is the bound again at ~3 ms, so the CPU items follow, not replace.

# Historical macOS measurements

Recorded on 2026-10-19. These notes describe earlier builds, not current build or test instructions. Local evidence and harnesses are not reproducible from this checkout. Later sections may supersede earlier findings.

# Frame hitches and low 1% fps (2026-10-19)

> Not shipped: the local tooling and result files cited here (local artifact “soak.py,” local artifact “*,” docs_metal/codex2/*, perf/results/*) are measurement artefacts from the author's machine and are not part of this repository; the numbers below are the recorded results of those runs.

Scenario: local artifact “soak.py” (local, not shipped), 2000-citizen stress town, 640x360 background window, speed 10, silent,
statistics after 120 s. The other processes' games were running on the same Mac during every run, so
absolute numbers are noisy; the before/after pair ran back to back.

## What was measured (OPENBA_METAL_STATS=1 now also writes stall columns)
standalone_metal_frames.csv gained: sem_ms (in-flight semaphore wait), drawable_ms (nextDrawable),
pipeline_ms / new_pipelines (synchronous newRenderPipelineState), newbuf_ms (upload page / rename buffer).

| Hypothesis | Finding |
|---|---|
| (a) lazy pipeline compile | 37 pipelines in the whole run, all in the first ~14 s (21 in one 91 ms frame, 13 in one 184 ms frame, ~3 ms each). 0 new pipelines after warm-up. Not a steady-state cause. |
| (b) airconv shader conversion | Happens at program creation (load) and is cached in TMPDIR/OpenBA-metal-v1-airconv26. Not in the frame loop. |
| (c) buffers / drawable / semaphore | semaphore wait: max 1.1 ms. Buffer allocation: 1.4 s total, max 5 ms after warm-up (656 ms once at frame 3, load). **nextDrawable: every slow frame after warm-up (15-21 ms) is a 13-20 ms nextDrawable wait**: stress mode runs uncapped (displaySyncEnabled=false) so the compositor, not the game, paces the 1% low. |
| (d) allocator / world update | No frame outside the load phase correlates with Update spikes in the hitch CSV. |
| (e) page faults | See the 8.6 s hitch below. |

## The 8.6 s hitch (10 min soak, 20261019)
Not rendering and not the stress harness: standalone_stress_hitch.csv shows period 8605 ms of which
update 47 ms, draw 18 ms, **outside 8541 ms**, harness_ms 0.15. samples.csv shows RSS falling to 7.7 MB at
0 % CPU at the same moment (and to 43 MB / 2 % at 80 s, matching the 1.4 s hitch at 79.6 s, and again at
the 1.5 s hitch at 111 s). The whole process was frozen and its memory compressed/paged out by macOS
(App Nap on an occluded small window plus memory pressure from other work). In the 5 minute reruns
with nothing else squeezing RAM the worst frame after warm-up was 26 ms. It could not be reproduced on demand.

## Changes (src/render/metal/backend.mm only)
1. App Nap / timer coalescing disabled for the process (NSProcessInfo beginActivity,
   UserInitiatedAllowingIdleSystemSleep | LatencyCritical). Targets the freeze above. NOT verified by
   measurement (could not provoke the freeze); it is the standard fix for this signature.
2. nextDrawable is acquired in Present() instead of BeginCommands(): CPU encoding of the frame
   overlaps the compositor wait instead of queueing behind it. The command buffer is created at begin.
3. layer.drawableSize is only assigned when the window size changed.
4. Stall columns in the frame CSV (cheap, only with OPENBA_METAL_STATS).

## Before / after (5 min soak, after 120 s warm-up)
| | frames | avg fps | 1% low | 0.1% low | worst frame |
|---|---|---|---|---|---|
| before (instrumented, perf/results/perf2_base5 (local, not shipped)) | 52009 | 290.2 | 79.8 | 53.2 | 26.1 ms |
| after (perf/results/perf2_fix1 (local, not shipped)) | 61282 | 342.1 | 83.5 | 60.1 | 21.5 ms |

The old 10 min soak (perf/results/soak_10min_20261019 (local, not shipped)) read 205.6 avg / 28.3 1% low / 8.6 s worst; that
run contained the OS freezes described above. The remaining tail (13-20 ms frames) is the compositor
releasing drawables at refresh rate under uncapped presentation; with displaySync on (normal play)
frames are paced by vsync and this wait is the pacing itself.

## Not done
No Retina native run was repeated. Pipelines are still created lazily (load-time hitches of 90-180 ms
in the first 14 s); prewarming would need the list of state combinations. The App Nap change is unproven.

## Pipeline prewarm and readback robustness (pipeline investigation, 2026-10-19)

This supersedes the lazy-pipeline limitation above for combinations learned by a
previous run. `src/render/metal/pipeline_cache.h` records shader SHA-256 identities
and complete main draw-pipeline descriptors (vertex attributes/layouts, blending,
coverage, attachment formats and sample count) in a property list named
`~/Library/Caches/OpenBA/pipelines.txt`. When USERPROFILE is supplied, that profile
is the cache root. No shader, texture or other game bytes are stored there.
Shader creation registers functions and replays available combinations through
Metal's asynchronous pipeline API. Completion handlers own their entries and
synchronize publication; an unavailable draw pipeline still has a synchronous
fallback. First runs and unseen settings can therefore still compile on demand.
The private ASSAO fullscreen-pipeline cache is not included in this mechanism or
the existing pipeline_ms counters.

Build: `build/mac-codex2`, arm64, perf, ccache off; existing airconv/LLVM reused.
First 30 seconds after first presentation, 640x360, 2,003-citizen stress scenario:

| Run | Worst frame ms | Sync pipeline creates | Total sync pipeline ms | Worst frame's pipeline work ms (maximum across frames) |
|---|---:|---:|---:|---:|
| Original baseline, validation off | 2223.08 | 34 | 14.38 | 6.39 |
| Modified, descriptor cache cold, validation on | 1209.98 | 16 | 219.88 | 111.25 |
| Modified, descriptor replay warm, validation on | 807.60 | 0 | 0 | 0 |

Raw frame CSVs: `docs_metal/codex2/{baseline,cold,warm}/` (local, not shipped); derived values:
`docs_metal/codex2/measurements.json` (local, not shipped) and `report.py`. Cold/warm used both
MTL_DEBUG_LAYER=1 and METAL_DEVICE_WRAPPER_TYPE=1. Other processes built code on
this Mac during these runs; validation differed from baseline, Apple's driver
cache was uncontrolled, and the worlds/loading frames are not deterministic.
These figures establish elimination of synchronous main-pipeline work in the
warm sample, **not** elimination of all loading/first-frame hitches or a clean
whole-frame speedup. No steady-state throughput claim is made.

Readback now ends encoding, blits through an aligned shared buffer, commits and
waits, then resumes the current frame without consuming a second in-flight slot.
Multisample sources resolve first. BGRA8 is already the engine/DX11 packed pixel
format; bytes are preserved, including encoded sRGB values. Compressed/depth
readback remains unsupported. The engine screenshot request hook produced
`cold/engine-screenshot.bmp` and `validation-a/engine-screenshot.bmp`; their PNG
conversions were viewed. This tests the TakeScreenshot path, not physical keyboard
input. Autosave completed via BANISHED_SAVE_TEST=300, and its thumbnail was viewed
in the Load dialog (`load-thumbnail/window-15.png`). Saves stayed in /tmp profiles.

Runtime rebuilding previously updated only the default render buffer, leaving
FrameCopyImageBuffer aliases pointing at old targets: water could show black or
stale menu pixels. The backend now tracks every live main color/depth alias,
updates all on sample changes, and unregisters destroyed aliases. The scripted
configuration sequence off -> 4x -> 2x -> off completed within a 38-second launch
(`msaa-fixed/`, LLDB transcripts and screenshots); viewed 4x/final-off images show
correct water. This changes the real Configuration field through LLDB, not the
Options Apply UI. No runtime feature flag was added. It does not establish a
long-duration leak test or UI Apply behavior.

Validation uses two separate 35-second stress launches (`validation-a`,
`validation-b`) with API validation, actual 8x targets, AO and foliage mode 1.
This obeys the per-launch 40-second cap; it is **not** a continuous 60-second soak.
No Metal API/pipeline/command warnings or errors were observed. Ordinary game
placement warnings are unrelated to Metal. Main menu/new-game, runtime 0/2x/4x,
and fixed 8x are covered; runtime 8x switching and device-loss recovery remain
untested. No allocation/leak profiler or DX11 parity comparison was run.

Reproduction scripts are isolated under `local artifact “codex2” :: ` (local, not shipped): `run.sh` enforces
background mode, dummy audio, throwaway HOME/USERPROFILE, an owned-PID EXIT trap,
a hard SIGKILL timeout, and pgrep after cleanup; `msaa.py` drives the configuration
sequence and `capture.py` captures owned background windows. The initial baseline
used SIGTERM before cleanup was strengthened to hard SIGKILL. Window captures
reported layer=-1 and appFrontmost=false. No owned game process remains.

The final AO/foliage/8x replay sweep was not fully prewarmed: 5 synchronous main
pipeline fallbacks / 97.13 ms total (maximum pipeline work in one frame 67.29 ms),
versus 33 / 306.00 ms cold. Its worst first-30-second frame was 1275.41 ms,
versus 994.51 ms cold. Late-created transformed shaders can arrive immediately
before their first draw, leaving too little lead time for async compilation.
This remains unfinished prewarming coverage; the zero-fallback result above
applies to the faithful-profile run only. Both final sweeps remained validation
clean; their whole-frame timings do not establish a performance improvement.

Final OpenBA/unit build passed; unit exit 0, **1,193 passed / 0 failed /
5 expected skips (1,198 total)** (`docs_metal/codex2/unit-final.log` (local, not shipped)). Executable
copies used for experiments were removed after recording SHA-256 values in
`docs_metal/codex2/executable-hashes.json` (local, not shipped); logs/CSVs/captures remain. Final pgrep
found no owned OpenBA-codex2 process. No getenv readers were added.


## Metal buffer footprint fix (memory investigation, 2026-10-19)

`src/render/metal/backend.mm` now records actual vertex/index draw use with
per-command-buffer atomic completion tickets, weak buffer identities and
`addCompletedHandler`. SetData and ring rewind write in place unless encoded or
in-flight draws still read that allocation. Old copies are owned only by Metal's
normal command-buffer resource retention. Readback/resume gets a new ticket.
Expanded-input cache entries invalidate on source update/recreate/destruction.

The dominant old retention was an autorelease lifetime on the loading path:
terrain's hundreds of chunk SetData calls cloned the whole buffer outside the
rendering autorelease pools. An unconditional-rename control WITH a local pool
uses only 700 MB footprint / 167 MB graphics at 60 s, demonstrating why ARC
release alone had not freed the old terrain allocations. There is no unbounded
renamed-buffer list. No private driver retain-chain class was identified.

| 60 s sample | Footprint before → after | Graphics before → after | Avg fps before → after | 1% low before → after |
|---|---|---|---|---|
| New game | 7285 → 668 MB | 6779 → 167 MB | 105.5 → 120.0 | 45.4 → 111.6 |
| Stress town | ~12.0 GiB → 595 MB | ~11.5 GiB → 190 MB | 333.2 → 361.3 | 74.9 → 75.0 |

FPS uses seconds 30–55 and reciprocal mean of slowest ceil(N/100) intervals.
Baseline runs overlapped initial compilation and one another; the normal after
run overlapped the unit-test tail. Other process/desktop activity was uncontrolled.
This rules out an obvious regression, not a clean causal speedup claim.
After-run vmmap graphics swapped bytes are zero; IOKit resident is 64 KiB and
non-graphics IOAccelerator resident is 592 KiB in both modes.

Stats behind existing OPENBA_METAL_STATS report live buffers by resource kind,
backend texture counts/bytes, cumulative rename traffic and in-place writes.
Normal/stress both retain only two renamed buffers (12 MiB), three constant
upload pages (24 MiB), and five cached expanded inputs (31,376 bytes). The reload
run has 405 backend textures / 116,641,920 bytes and 557 MB total footprint;
texture Update does not allocate a replacement each frame. ASSAO's private
cache targets are excluded from the backend texture count.

Unit: **1193 passed / 0 failed / 5 expected skips (1198 total)**. Two separate
60 s Metal-validation runs: default plus AO/Better Trees enabled, 4x/2x/off
MSAA, autosave and screenshot readback; no Metal API validation errors.
Autosave reloaded successfully with the final build. Settings apply executed the
backbuffer-rebuild path, but background policy kept the physical target at
640x360. Before/after and shadow/AO reference captures were visually inspected;
weather/simulation differ, so no pixel-equivalence claim. All owned games stopped.

Full raw footprint/vmmap output, frame CSVs, hashes, screenshots, resource-kind
table, process-hygiene helper and limits: historical memory evidence (not a shipped reproduction procedure).
Remaining: long soak, clean uncontended A/B, native Retina/arbitrary resize matrix,
exhaustive feature-quality equivalence and private-driver retain-chain inspection.

# Historical macOS measurements

Recorded on 2026-10-19. These notes describe earlier builds, not current build or test instructions. Local evidence and harnesses are not reproducible from this checkout. Later sections may supersede earlier findings.

# Soak / regression harness (local artifact “soak.py,” not shipped)

> Not shipped: the local tooling and result files cited here (local artifact “soak.py,” local artifact “window_id,” perf/results/*) are measurement artefacts from the author's machine and are not part of this repository; the numbers below are the recorded results of those runs.

Runs the stress town for N minutes in a small background window, silent, with HOME/USERPROFILE
redirected to `/tmp/openba_soak_<stamp>/home` (the real ~/Documents is never touched). The exe is
copied to a staging dir beside a WinData symlink to the read-only Steam install.

    python3 local artifact “soak.py” (local, not shipped) [--minutes 5] [--citizens 2000] [--exe build/mac-hyg/OpenBA]
                               [--warmup 120] [--out perf/results/<dir> (local, not shipped)]

It sets OPENBA_BACKGROUND=1, SDL_AUDIODRIVER=dummy, OPENBA_METAL_STATS=1 (writes
standalone_metal_frames.csv), BANISHED_STRESS=1 with an effectively endless frame count,
BANISHED_AUTOSTART=1, BANISHED_CAMERA_PIN=town,90. Every 5 s it samples RSS and CPU with `ps`;
every minute (and once at the end) it screenshots the window (`screencapture -l`, id from
local artifact “window_id,” (local, not shipped) no focus steal). Output dir: report.md, samples.csv, shot_NNmin.png and the
copied standalone_* logs. Report fields: max/avg RSS (also max after warmup), leak slope
(least squares MB/min after warmup), fps min/avg/1% low (after warmup), crash yes/no, hang logs.
The load phase peaks at 2.5-3.7 GB RSS, hence the warmup exclusion.

## Environment flags are all Options fields
`grep -rn getenv src` finds only src/app/options.cpp (the one reader). Nothing in src/app, src/diag,
window_sdl.cpp, process_sdl.cpp, src/render/metal calls getenv (the other hits are comments).
Existing fields (src/app/options.h, read with Opt(); OPENBA_* names are historical, now BANISHED_*): OPENBA_BACKGROUND -> background,
OPENBA_NATIVE_RETINA -> nativeRetina, OPENBA_WINDOW_WIDTH/HEIGHT -> windowWidth/windowHeight,
OPENBA_METAL_STATS -> metalStats, BANISHED_AIRCONV -> airconv. No new fields were needed.

## Result: 10 minute soak, 2026-10-19 (perf/results/soak_10min_20261019/ (local, not shipped))
Build build/mac-hyg (perf flavour), 2000 citizens, speed 10, ran 601 s, no crash, no hang log,
crash log empty.
- RSS: load peak 2704 MB (first 60 s), settles to ~104 MB at 115 s, then grows to ~163 MB by 515 s
  (about +11 MB/min while the town grows to 33k sim ticks, 2650 entities), then drops to 134 MB.
  Max after warmup 1407 MB is the tail of the load phase, not steady state.
  Recomputed from samples.csv after 120 s: slope +9.7 MB/min, max 164 MB. Modest, non-monotonic
  (falls at the end), so not proven a leak, but the town was still growing; a longer run would tell.
  (The report.md of that run used a 60 s warmup and printed -14 MB/min, skewed by the load tail;
  the default warmup is now 120 s.)
- CPU avg 98 %, max 223 %.
- fps (after 60 s): avg 205.6, 1% low 28.3; the frame CSV's slowest frame was 8.6 s (a hitch, likely
  the load/hand-off), so "min 0.1 fps" in report.md is one such frame.
- 9 screenshots taken.

## Leak hunt, 2026-10-19 (build/mac-soak, perf flavour; data in perf/results/soak25/ (local, not shipped))
local artifact “soak.py” (local, not shipped) was extended: `--mode stress|normal`, `--caffeinate`, `--leaks-at`, `--sample-at`,
`--debuggable`; every 30 s it records `footprint` and `vmmap -summary` (both with -forkCorpse, so the game is
paused for ~1 s) into footprint.csv / vmmap.csv, and the report fits a line (MB/min, R^2, 95% CI).
Frames > 500 ms that line up with leaks/sample/vmmap runs are tool pauses, not game hitches.

**Verdict: leak: NO (no growth over 25 min in either mode). Separate finding: a large, constant GPU-memory block.**

| run | RSS after warm-up | phys_footprint slope (after 5 min) | `leaks` (5/15/23 min) |
|---|---|---|---|
| stress town, 25 min | 200-230 MB, slope -1.2 MB/min (R^2 0.42), -0.3 after 10 min | +0.63 MB/min (R^2 0.62), 12261 -> 12277 MB | 552 leaks / 22752 bytes every time (no growth) |
| normal new game x10, 25 min | settles ~135-160 MB after load tail, -2.6 MB/min after 10 min | -2.6 MB/min (R^2 0.57), 7108 -> 7069 MB | 552 leaks / 22752 bytes every time |

Which region grows (stress, MB/min): MALLOC_SMALL +0.25, untagged VM_ALLOCATE +0.19, IOAccelerator (graphics) +0.23
(CI +/-0.13): all tiny, consistent with the town still growing. Normal game: nothing above +0.1. The earlier
"+10 MB/min" (build/mac-hyg) is not reproduced; it was RSS being re-paged after the load peak.
`sample <pid> 5` at 20 min (stress25/sample_20min.txt) was taken; growth did not continue, so not analysed further.

### The 12 GB / 6.8 GB block (real problem, not a leak by slope)
phys_footprint is 12.3 GB (stress) / 7.1 GB (normal) on a 24 GB Mac, almost all "IOAccelerator (graphics)"
(Metal buffers), mostly swapped/compressed, flat for the whole run. With MallocStackLogging=1 + `vmmap -stacks`
(perf/results/soak25/diagnosis/ioaccel_by_alloc_site.txt (local, not shipped)): 6609 MB of 6795 MB came from 832 live MTLBuffers of
~9.6 MB each, allocated in `VideoMetal::RenameBuffer` (backend.mm:73) <- `VertexBufferData::SetData`
<- `WorldGeometry::BuildTerrainMesh()` (serialize_heightmap_mesh.cpp:321) during loading. Each SetData
clones the whole terrain vertex buffer (preserve=true) and the old copies are never released. Who keeps them
alive (app cache vs driver deferred-free list) is NOT proven: `leaks --traceTree` only showed AGX's own resource
list. Suggested fix (src/ untouched, by brief): in SetData write in place unless the buffer is in flight, and
check the old buffers are released. This 7-12 GB of swapped GPU memory is the best candidate for the old 8.6 s paging hitch.

### App Nap / caffeinate (partial)
10-minute stress runs with no sampling tools: default build, 1 frame > 500 ms (830 ms at t=0.9 s, load);
`caffeinate -i` wrapped, 1 frame > 500 ms (659 ms at t=0.8 s, load). No hitch after load in either, so no
evidence of App Nap hitches with the opt-out present; caffeinate makes no difference. The control (old exe
build/mac-hyg without the opt-out) was NOT run to completion (stopped by the owner). The window was an ordinary
background window, not verified as fully occluded. (In the caffeinate run the RSS columns tracked the wrong PID; frame CSV is valid.)

### Not done
Old-exe App Nap control; occluded-window test; identifying the exact retainer of the terrain buffers; fix.

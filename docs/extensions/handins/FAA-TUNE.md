# FAA-TUNE -- ext.foliage_aa: the floor, the ramp, k, MSAA off, the seasons (hand-in, agent part, 2026-10-16)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

Block FAA-TUNE of `docs/extensions/FOLIAGE_AA.md` §4. Branch `worktree-agent-a6bf7b9c15b33a401`, from main
`20486dadd`, main merged (`faa72f48d`, docs only). Not merged to main. **The user's look ruling is owed**
(section 6 is the material for it).

**Recommendations, one number each:**

| Row | Recommendation | The number |
|---|---|---|
| 1 floor | `kDistanceFillFloor = 0.15`, gating the fill **and the coverage** | a fill-only floor at 0.1 removed only 13% of the covered samples under 0.1 (309,702 -> 269,158); the gate removes all of them by construction, for 0.37% of the canopy's covered samples at the overview |
| 2 ramp | **no clamp**: the ramp is unchanged | edge width 1.19 px at the overview, 1.03 px at street level (aliased would be < 1, soft > 2) |
| 3 k | `kDistanceFillK = 0.15` (was 0.25) | at the overview the canopy is +6.3% over the mip-0 reference at k 0.15, +8.0% at 0.25, +9.6% at 0.40 |
| 4 MSAA off | discard kept, the fill is the thicker cutoff | `MSAA off -> discard kept` in the stage log; canopy samples +4.9% (k 0.15) over retail |

## 0. Method: one capture, many arms

Every row's numbers come from a **deterministic replay**: one RenderDoc capture per pin (seed 12345, summer by
FAA-SPIKE S1's season route, mode 2 on, every other ext row off, the user's video settings, 4x MSAA), and in
the replay every transformed cutout copy is **replaced** (`ReplaceResource`) by a variant made offline from the
retail blob the capture holds. Every arm then sees the same geometry, camera and depth, so a 0.2% difference is
real and not run-to-run noise (FAA-SPIKE measured the noise floor between runs at several percent).

- The variants come from a scratch host tool (`build/faatune/`, git-ignored, not committed): the tree's
  `dxbc.cpp` + `cutout_rewrite.cpp` for the production arms, and a scratch copy of the rewriter with debug
  options (raw alpha to RGB, mip-0 sampling, a coverage select by raw-alpha band, a fwidth scale / clamp). A
  production arm and the matching debug arm gave identical counts (27,872,463 samples both).
- **Covered samples** = the `SamplesPassed` counter per draw (the occlusion count after the A2C mask), summed
  over the main-pass canopy draws (programs G1-G4 of FAA-SPIKE S2; G1 deciduous, G2 conifer carry 99.99%).
- **Speckle count** = covered samples whose *raw* sampled alpha (after the season term) is under a threshold:
  an arm that keeps only those samples' coverage, read through the same counter.
- **Edge width** = per pixel coverage levels 0..4 from the 4 sample planes before / after each G1 / G2 draw;
  width = partial pixels / total variation of the coverage, calibrated on synthetic edges (a 4x MSAA geometric
  edge reads 0.95 px, an A2C ramp of w px reads w).
- Captures (game dir, never in the tree): `rdoc_foliage/faatune_summer_street_m2.rdc` (`town,25`),
  `faatune_summer_over_m2.rdc` (`town,120`), `faatune_summer_over_m2_msaa0.rdc`. Arm results:
  `faa_tune/arms_*.json`. A `town,200` capture (`faatune_summer_far_m2.rdc`) drew no canopy (656 draws, almost
  all shadow): 120 stays the overview, as in the spike.

## 1. Row 1 -- the fill floor

**What the speckles are** (MSAA on, overview, k 0.25, 30 main-pass canopy draws; colour = the real shaded
sample, raw alpha from the debug replay of the same draws):

| raw alpha | covered samples | mean saturation | blue >= green |
|---|---|---|---|
| [0.00, 0.05) | 15,479 | 0.102 | **83%** |
| [0.05, 0.10) | 14,093 | 0.100 | 58% |
| [0.10, 0.15) | 21,237 | 0.112 | 41% |
| [0.15, 0.20) | 25,459 | 0.122 | 28% |
| [0.20, 0.30) | 100,618 | 0.140 | 16% |
| [0.30, 0.50) | 362,794 | 0.17 | 3-7% |
| [0.50, 1.00] (leaf) | 4,323,598 | 0.212 | 3.4% |

The blue-grey (low saturation, blue over green) samples are the low-alpha ones, grading smoothly up to ~0.2.

**Why a fill-only floor does not remove them.** Under A2C the coverage is
`saturate((a_f - 0.5) / fwidth(a_f) + 0.5)`. With the fill, `a_f = a (1 + L k)`, the effective cutoff moves to
`0.5 / (1 + L k)` and the ramp keeps its width in `a`; where fwidth is wide (the distance) the ramp alone
covers an *unfilled* texel under the floor. Measured at the overview (covered samples with raw a < 0.1):

| arm | < 0.05 | < 0.10 | < 0.20 | canopy total |
|---|---|---|---|---|
| mode 1 (no fill) | 5,964 | 12,744 | 51,305 | 26,123,865 |
| k 0.25, no floor | 163,584 | 309,702 | 804,558 | 27,983,042 |
| k 0.25, **fill-only** floor 0.1 (the brief's first form, commit `07b455bba`) | 156,225 | 269,158 | 784,383 | 27,975,135 |
| k 0.25, floor 0.1 gating the coverage | 0 | 0 | 563,957 | 27,916,997 |
| k 0.25, floor 0.15 gating the coverage | 0 | 0 | 356,753 | 27,872,463 |
| k 0.25, floor 0.20 gating the coverage | 0 | 0 | 0 | 27,807,876 |

So the floor now gates the result too (`03fde4b4b`): after the sharpen `and P.x, P.x, M.x` (M.x is the fill's
`rA.c >= floor` mask, all ones / zero), and with MSAA off `discard_z M.x` after the retail discard. The fill
itself still skips under-floor texels (`ge` + `movc`, `07b455bba`). Mode 1 has no floor (no fill: 12,744
under-0.1 samples, 0.05%). Cost: floor 0.15 -0.37% of the canopy's covered samples at the overview, -0.04% at
street level (the masked samples are partly re-covered by the canopy behind: depth is no longer written there).

**Value: 0.15.** It removes the bands that are 41-83% blue-over-green; 0.20 would also remove the 28% band for
-0.60%. The user's look decides between them (`kDistanceFillFloor`, one constant).

**MSAA off:** the floor never acts (k 0.25: 0 samples under 0.1 pass, 218 under 0.2 of 7.0 M). The hard
cutoff needs `a (1 + L k) >= 0.5`, i.e. a >= 0.2 at the deepest lod; the MSAA-off speckles FAA-SPIKE saw
(with the isotropic lod, +0.75 mip high) were in [0.2, 0.3): 14,038 samples, 27% blue-over-green, 0.4% of the
canopy at k 0.25. A smaller k shrinks that band (row 3).

**Unit tests** (`tests/unit/ext_foliage_aa_test.cpp`, synthetic blobs only): `ext_faa_emit_fill_floor`
(both rT shapes x MSAA on / off: the `ge` + `movc` after the k mad, reading rA.c, the floor immediate, no new
temp, the `and` right after the sharpen's `add_sat`, the `discard_z M.x` right after the retail
`discard_nz`), and `OptionsFor` with / without a floor. `tools/build.sh unit`: **1226 / 0**.

## 2. Row 2 -- the ramp width

| pin | arm | partial pixels (1 / 2 / 3 of 4) | edge width |
|---|---|---|---|
| overview | retail-equivalent hard cut | 179,703 / 60,774 / 46,860 | 0.50 px (geometry edges only) |
| overview | mode 1 | 299,598 / 174,377 / 190,463 | 1.20 px |
| overview | mode 2 (k 0.25 floor 0.1) | 296,669 / 148,664 / 152,470 | 1.19 px |
| overview | mode 2 (k 0.25 floor 0.1 gated) | 274,342 / 148,279 / 146,953 | 1.12 px |
| overview | mip-0 reference | 301,825 / 150,987 / 215,700 | 0.99 px |
| street | retail-equivalent hard cut | 19,286 / 23,427 / 16,196 | 0.21 px |
| street | mode 1 | 94,446 / 94,705 / 95,832 | 1.06 px |
| street | mode 2 (k 0.25 floor 0.15 gated) | 86,743 / 92,868 / 89,579 | 1.03 px |

The edge is about one pixel at both zooms: not aliased at the overview, not soft at street level. **No fwidth
clamp was added**; the ramp is FAA-CORE's as shipped.

## 3. Row 3 -- k

Covered samples of the canopy (main pass, G1-G4), one capture per pin, every arm on the same replay. Floor 0.15
gated in every fill arm. The **mip-0 reference** samples mip 0 (`sample_l` lod 0) under the same ramp: the true
leaf silhouette with no mip averaging.

| arm | overview | vs mode 1 | vs mip-0 | street | vs mode 1 | vs mip-0 |
|---|---|---|---|---|---|---|
| mip-0 reference | 25,806,044 | -1.2% | 0 | 23,995,142 | -0.2% | 0 |
| retail-equivalent hard cut | 26,226,278 | +0.4% | +1.6% | 24,076,015 | +0.2% | +0.3% |
| mode 1 (k 0) | 26,123,865 | 0 | +1.2% | 24,031,981 | 0 | +0.2% |
| **k 0.15** | 27,422,523 | +5.0% | **+6.3%** | 24,161,481 | +0.5% | +0.7% |
| k 0.25 | 27,872,463 | +6.7% | +8.0% | 24,228,362 | +0.8% | +1.0% |
| k 0.40 | 28,281,882 | +8.3% | +9.6% | 24,308,099 | +1.1% | +1.3% |

(G1 deciduous alone at the overview: mip-0 9.92 M, mode 1 10.09 M, k 0.15 10.85 M, k 0.25 11.12 M, k 0.40
11.35 M.) **At this zoom no arm loses canopy area against mip 0**: the A2C ramp turns the averaged alpha of the
small mips into partial coverage instead of dropping it, so mode 1 is already at +1.2%. The fill is therefore a
*look* choice (a denser, less dithered canopy), and every k overshoots the true silhouette. **Recommended: 0.15**,
the closest to the reference and half the under-0.1 speckle count of 0.25 (139,446 against 269,158 with the
fill-only floor). Committed as `kDistanceFillK = 0.15` (`4edc93b98`); the k 0.25 / 0.40 screenshots are in the
look set for the user's pick. A k change is one constant (the 24 shaders are re-created).

## 4. Row 4 -- MSAA off (`VideoMultisample=0`)

- The stage logs `mode 2 (smooth edges + distance fill), MSAA off -> discard kept, fill k ... floor ...`; mode 1
  logs `MSAA off -> discard kept, fill k 0.000` and emits nothing (Transform returns false: retail).
- Part 2 is the thicker cutoff (overview replay of `faatune_summer_over_m2_msaa0.rdc`): retail-equivalent
  6,560,121 samples; k 0.15 6,882,298 (**+4.9%**), k 0.25 7,001,126 (+6.7%), k 0.40 7,117,271 (+8.5%); the
  mip-0 reference 6,497,903.
- Screenshots `summer_{street,over}_m{0,1,2}_msaa0` (m1 == m0 by design).

## 5. Row 5 -- the look set (screenshot index)

`C:/GOG Games/Banished/faa_tune/README.md` lists every file: 4 seasons (spring 450 s, summer 1200 s, autumn
2310 s, winter 3150 s of yearTime) x street (`town,25`) / overview (`town,120`) x Off / mode 1 / mode 2, AO off;
summer with AO on at Medium / 20 (Off and mode 2, both pins: the 1-pixel AO edge of §6.4); summer MSAA off x 3
modes x 2 pins; summer mode 2 at k 0.25 and 0.40 x 2 pins. `.bmp` + `.png`, and `cmp/` holds 1:1 crops side
by side. Five runs first came up without the camera pin (no `[CameraPin]` line; the autostart camera roamed);
they were re-shot, and the hold script now retries any run without that line. Each panel is its own run, so
framings differ slightly; compare by eye.

## 6. For the user's ruling -- RULED 2026-10-16 (live, through the debug tweak overlay)

**k = 0.15 and floor = 0.15 stay** ("look fine"); **mode 2 (smooth edges + distance fill) is the look** (stays the mode key's default; the row itself ships OFF like every extension). Nothing else to move.
The user also asked for the shadow entry to offer 4096 AND 8192 -> block FAA-SHADOW2.

The material that was offered:

1. k: 0.15 (committed) / 0.25 / 0.40 -- `cmp/summer_*_m2_k015_k025_k040.png`.
2. The floor: 0.15 (committed) or 0.20 (removes the last bluish band, -0.6% canopy).
3. The four seasons x two zooms: `cmp/<season>_<pin>_off_m1_m2.png`.
4. MSAA off and AO on: `cmp/summer_<pin>_msaa0_off_m1_m2.png`, `cmp/summer_<pin>_ao_off_m2.png`.

## 7. Runs, hygiene, not done

- Every run held `game_dir_lock.sh` as `faatune` (lane devlostmem waited on, the lock released between
  holds), `assert-held` with the exe inside `run_arm.sh` before each log copy; one game at a time; killed by
  PID only. `settings.cfg` backed up at each grant (md5 `6392ed7f...`) and restored byte-identical (`cmp`)
  before each release. No `*.pkm` mounted. Crash log 0 and no hang log in every run.
- The season switch was a scratch edit of `gameupdateloop.cpp` in the worktree, never committed (FAA-SPIKE's
  form). The game dir is left with a debug build of the branch carrying that scratch switch (summer, k 0.40,
  from the last re-shot); redeploy before trusting it.
- Not run: `tools/build.sh stress` (the coordinator's), resave gate, acceptance, castmetric.

## 7b. The GPU-cost stress legs (coordinator, 2026-10-16)

Perf exe `0e35f793...` (main `14c8b08d9`), the compact stress town (800 citizens, 3 sets, speed 10, 6000
frames, seed 31337, medium map), the user's `VideoMultisample=3` (4x), fixes OFF and every other row OFF
per leg (`STRESS_EXT`), fps UNCAPPED (RTSS / Afterburner / RTSSHooksLoader64 not running), machine load
CLEAN on every leg (`perf/results/20261016_17*_gfx_*/machine_load.txt`), one leg per row, 2026-10-16
17:10-17:25. The first attempt (16:54-17:09) ran the DEBUG exe the gate had left deployed (`--no-build`
skips the deploy: memory `feedback_no_build_skips_the_deploy`) and is parked under
`perf/results/_invalid_debug_exe_20261016/`; it is not evidence.

| Leg | sim ms / iter | draw ms / frame | frame ms | fps | private MB (end) | fps vs faithful |
|---|---|---|---|---|---|---|
| faithful (every ext off) | 0.16 | 0.98 | 1.11 | 904 | 646 | -- |
| AO Low (`ao_quality=1`, opacity 20) | 0.16 | 0.99 | 1.16 | 866 | 728 | -4.2% |
| AO Medium (2) | 0.16 | 0.96 | 1.19 | 838 | 716 | -7.3% |
| AO High (3) | 0.16 | 0.98 | 1.27 | 785 | 712 | -13.2% |
| foliage_aa mode 1 (smooth edges) | 0.16 | 0.96 | 1.08 | 924 | 673 | +2.2% (noise) |
| foliage_aa mode 2 (+ distance fill) | 0.16 | 0.98 | 1.11 | 900 | 663 | -0.4% (noise) |
| max_shadow_res (4096 at High) | 0.16 | 0.98 | 1.12 | 890 | 729 | -1.5% |

Reading: sim and draw CPU are unchanged by every row (the rows are GPU-side; the CPU columns move
within the run-to-run noise). The AO tiers cost 4 / 7 / 13% of the uncapped fps at this town and camera
(the frame is ~1.1 ms, so ~0.05 / 0.08 / 0.16 ms per frame of GPU time); the foliage rewrite is free at
this resolution (the per-bind mutex + hash lookup FAA-CORE noted does not show in draw ms); the 4096 sun
map costs ~1.5% fps and ~80 MB. "Lowest" (ASSAO quality -1) is not a tier the menu offers; not measured.

## 8. Nothing retail entered the tree

`git diff --stat 20486dadd..HEAD` reviewed: `src/ext/foliage_aa/foliage_aa.{h,cpp}`,
`src/render/dx11/foliage_aa/{cutout_rewrite.{h,cpp},dxbc.h,foliage_aa_backend.h,foliage_aa_stage.cpp}`,
`tests/unit/ext_foliage_aa_test.cpp`, this markdown and FOLIAGE_AA.md (plus main's merged docs). No DXBC blob or
dump, no `.srsl` / `.rsc` copy, no capture, no texture, no screenshot. The blobs read back from the captures,
the arms' outputs and every screenshot are in `C:/GOG Games/Banished/faa_tune/` and `rdoc_foliage/`; the
scratch tool is in the git-ignored `build/faatune/`.

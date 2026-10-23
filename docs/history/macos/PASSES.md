# Historical macOS measurements

Recorded on 2026-10-19. These notes describe earlier builds, not current build or test instructions. Local evidence and harnesses are not reproducible from this checkout. Later sections may supersede earlier findings.

# Metal extension passes — 2026-10-19

> Not shipped: the local tooling and result files cited here (local artifact “*,” docs_metal/*) are measurement artefacts from the author's machine and are not part of this repository; the numbers below are the recorded results of those runs.

## State

This is a partial port. Better trees' single-sample path is connected and ran
on a new game and the 2,000-citizen stress town. ASSAO's native command encoding
passes an actual GPU test, but **in-game AO is not connected**: `integration.mm`
still returns false from `AoBackend::Supported()` and defines an empty
`Graphics::ExtAoAfterOpaque`. That file is outside the initial change scope; a
request for permission for the small forwarding edit is pending.

True foliage edge smoothing is also unfinished: the Metal backend still has
single-sample targets. Just as on DX11 with MSAA disabled, mode 1 preserves the
cutout discard; mode 2 adds distance fill. No alpha-to-coverage claim is made.

## Files and implementation

- `src/render/metal/passes/tree_passes.{h,mm}`: cached in-memory rewrites, using
  the existing unchanged `dxbc.cpp`, `cutout_rewrite.cpp`, `receiver_rewrite.cpp`,
  and `edge_dither.cpp`; rewritten DXBC goes through `VideoMetal::Translate`.
  Conversion failure or a non-match keeps the original shader. A blend guard
  keeps transformed pixel shaders away from alpha-blended draws, as on DX11.
- `src/render/metal/passes/assao_pass.{h,mm}`: native Metal encoders, four-way
  depth deinterleave and normal generation, depth mips for high quality,
  quality -1/0/1/2 generation, blur ping-pong, result texture array, and RGB-only
  multiply application. Uses Intel ASSAO's existing extension-owned compiled
  shader headers via airconv; all 14 entries load as Metal functions. No fxc,
  shader downloads, LLVM rebuild, or translation of retail source is needed.
  Intel's MIT license remains at `src/render/dx11/ao/assao/LICENSE`.
- `src/render/metal/passes/run_passes.py`: copied/adapted bounded launch harness;
  uses the isolated executable/settings in `build/mac-passes`, an explicit
  `USERPROFILE=build/mac-passes/profile` redirect, dummy audio,
  background 640x360 client window, and 32-second launches. Only owned children
  are terminated. Captures go to `docs_metal/passes_*.png` (local, not shipped).
- `src/render/metal/backend.mm`: small includes, once-per-frame toggle snapshot,
  shader-bind selection, draw blend guard, and the prepared after-opaque bridge
  (ends the active encoder before invoking ASSAO).
- `CMakeLists.txt`: Apple source glob includes the new pass subdirectory and
  links the four unchanged portable rewrite implementations. DX11 selection is
  untouched. `VideoMetalData.h`, `shaders.mm`, SDL, and `local artifact “*”` (local, not shipped) were not
  edited by the initial pass implementation.

No retail shader blobs, source, or other retail assets were added. Runtime shader
transformation uses the user's install and the pre-existing local airconv cache.

## Selection

The existing registry remains authoritative: `ext.better_trees` and
`ext.ambient_occlusion` are class A and default ON; faithful profile forces OFF.
`ext.foliage_aa_mode` is 1 or 2 (default 2). `BetterTrees::Current()` is sampled
once per command frame, outside draw loops. No new environment flag or settings
key was added. The legacy backend request symbols in integration.mm remain
stubs; standard settings/menu changes are picked up by the frame snapshot.
Debug-overlay custom rewrite parameters are not wired on Metal.

## Build and tests

Configured `build/mac-passes` with the mac-arm64 cache's debug flavour:

```sh
cmake -S . -B build/mac-passes -G Ninja -DCMAKE_OSX_ARCHITECTURES=arm64 -DBANISHED_USE_CCACHE=OFF
cmake --build build/mac-passes -t OpenBA unit -j3
```

OpenBA and unit linked successfully. `TMPDIR=build/mac-passes/tmp` (absolute
path when running) and `BANISHED_DIR` set to the read-only Steam install:
**1,186 tests, 0 failures, 5 expected skips = 1,181 passed** on the final rerun
(the earlier baseline run was 1,178 total / 1,173 passed; concurrent processes
added tests).
Evidence: `docs_metal/passes_unit.log` (local, not shipped), `build-passes-build*.log`.

A standalone GPU check of the ASSAO pass was run once during development (its source has since
been removed from the tree). Result:

All eight GPU cases completed (Metal status 4). Flat depth darkened **0** pixels;
a one-unit raised square darkened **2,716 / 3,122 / 2,938 / 2,896** pixels at
qualities -1 / 0 / 1 / 2. Alpha remained unchanged in every case. The inspected
`docs_metal/passes_assao_q2.png` (local, not shipped) shows a white square with an occlusion halo
around its base. This verifies functional ASSAO on synthetic depth, not game
integration or visual parity.

## In-game evidence and limits

Sequential stress captures use seed 10000, camera pin town,90, 2,000 requested
citizens (CSV census 2,001), 10x speed, identical executable and isolated settings.
Each process ran approximately 32.3 seconds; window probes reported layer -1,
frontmost=false. Harness termination is SIGTERM, not a clean user quit test.

- `passes_stress_off_28.png`: extensions disabled.
- `passes_stress_billboard_28.png`: mode 1; at single sample this is the receiver
  rewrite and edge dither without foliage distance fill.
- `passes_stress_trees_28.png`: mode 2; receiver/dither plus distance fill.

Images were opened and visually inspected. Buildings, foliage, water and cast
shadows render. The tiny, foggy stress overview does not establish leaf-edge
smoothing or a clear isolated billboard-shadow improvement. The latter rewrites
change canopy shadow *reception*, as on Windows; they do not add a new caster.
The enabled log records successful receiver and cutout/dither conversions, with
no conversion/pipeline/command errors observed. This is functional evidence,
not a claim of visual parity. Earlier new-game captures used different maps and
are not a controlled comparison.

## Remaining integration edit (not applied)

The concrete minimal forwarding change in `src/render/metal/integration.mm` is:
include `passes/assao_pass.h`; make `AoBackend::Supported` reflect a live Metal
device and `DepthReadable` reflect the native readable depth; replace the empty
`Graphics::ExtAoAfterOpaque` with
`VideoMetal::Assao::AfterOpaque(context, frame, entry)`.
Native depth textures already have shader-read usage. The prepared bridge ends
the backend’s active encoder before the pass; subsequent game draws recreate their encoder
and reapply all bindings.

After authorization: apply that forwarding change, build, run AO off/on on the
same scene, inspect screenshots and validation logs, and measure its stress cost.
MSAA resource allocation/resolve/pipeline support must precede A2C smoothing.
Windows/Linux runtime regression was not run on this Mac.

## Evidence correction

Initial stress runs were invalid as A/Bs: the concurrently changed macOS
DocumentsDir implementation read ~/Documents instead of executable-local
UserData. All three initial stress logs reported mode 2. Their original timing
comparison is withdrawn. The harness now sets USERPROFILE inside the owned
build directory and verifies the actual extension state through startup logs.
The initial launches may have persisted game settings to the real Documents
directory; no attempt was made to remove or restore files outside the workspace.

## Corrected stress timing (verified startup settings)

The corrected logs all read `build/mac-passes/profile/Documents/Banished` and
confirm better_trees OFF / mode 1 / mode 2, respectively. Three CSV intervals
(world frames 240, 360, 480), excluding the first interval:

| Mode | Mean frame ms | Reciprocal FPS |
|---|---:|---:|
| Off | 48.037 | 20.82 |
| Mode 1: billboard receiver | 48.442 | 20.64 |
| Mode 2: receiver + distance fill | 47.873 | 20.89 |

The mode-1 delta is +0.406 ms / -0.17 FPS. Mode 2 measured slightly faster than
off; these differences are within run noise, so neither is an isolated GPU-cost
claim. Short debug runs, with video playback and other processes on the Mac, are
not a stable benchmark. AO cost remains unmeasured. Calculation:
`docs_metal/passes_stress_summary.json` (local, not shipped). Crash logs are empty and no shader or
pipeline errors were observed.

The corrected captures were opened and inspected. The same stress layout renders
in all three; fog and animation differ. Mode 1 logs a converted receiver shader;
no pixel edge-dither conversion was observed in that run. Mode 2 logs converted
cutout/fill shaders as well. Thus the pure edge-dither rewriter is connected, but
its matching/application to the shaders exercised here is not visually verified.
These captures do not prove smooth leaf edges or isolated billboard-shadow parity.

Visual defect still open: the corrected mode-2 capture shows blue/grey leaf
speckling that is substantially less apparent in off/mode 1. Distance fill is
therefore **not visually accepted** on Metal. The unchanged Windows rewriter and
its default 0.15 fill/floor are used; investigate the rewritten derivative/LOD
path and filtered cutout colour before claiming foliage parity. The existing
registry defaults were retained, so the default-on distance-fill mode can expose
this defect. This is a remaining port issue, not a successful smoothing result.


# Render-features update (2026-10-19)

Build dirs `build/mac-render` (debug) and `build/mac-render-perf`. Harness `local artifact “run_render.py”` (local, not shipped)
(isolated profile, dummy audio, background window; `--ms INDEX` = VideoMultisample, sample count = index+1:
0 none, 1 = 2x, 3 = 4x, 7 = 8x; `--set key=value`; `local artifact “cmp_crop.py”` for crops). Evidence: `docs_metal/r_*` (local, not shipped).

1. **AO is live.** integration.mm forwards `AoBackend::Supported/DepthReadable` and `Graphics::ExtAoAfterOpaque`
   to `VideoMetal::Assao::AfterOpaque`; main depth is created shader-readable. Selected by the same registry row
   as Windows: `ext.ambient_occlusion=1` (+ `ext.ao_quality`, `ext.ao_opacity`) in settings.cfg, off under the
   faithful profile. Checked: `r_ao_compare_crop.png` (soft darkening at building bases and under tree canopy;
   mean luminance -2.5 / -3.6 at opacity default / 30, 8% / 17% of pixels darker than 8 levels), also under 4x MSAA
   (`r_ao_ms4_crop.png`; MSAA depth is resolved to R32Float sample 0 by a small runtime MSL pass, the multiply is
   applied into the MSAA colour). Metal debug layer: no errors.
2. **MSAA.** Main colour/depth are 2DMultisample (Private); Options VideoMultisample 0/2/4/8 enumerated from
   `supportsTextureSampleCount`; pipelines keyed by sample count; Present and `RenderBufferData::Resolve`
   resolve on store; a sample change at runtime rebuilds the main targets (`[Metal] multisample Nx`). 4x verified
   on screenshots (`r_msaa_compare_crop.png`, smoother rock/tree edges), debug layer clean. 2x/8x not captured.
3. **Foliage AA.** With MSAA on, the cutout rewrite emits coverage and the pipeline uses alpha-to-coverage with
   the DX11 blend clone (colour*1, alpha 0), only on non-blended draws into multisampled targets; otherwise the
   retail shader runs. Mode 1 (A2C) gives clean soft leaf edges (`r_foliage_cmp2.png`, `r_w_*`). Mode 2 (distance
   fill) still thickens thin pale-blue branch sprites into a grey-blue haze. I could not show this is a Metal
   bug: sampler filters match DX11, the rewriter and its floor (0.15) are unchanged, and the effect is the
   documented fill (alpha*(1+lod*k)) acting on low-alpha texels with blue RGB, stronger at the tiny 640x360
   test size. (Superseded by the investigation below.)

   **Foliage mode 2 investigation (foliage investigation, build `build/mac-foliage`, harness `local artifact “run_foliage.py”` (local, not shipped)).**
   - Metal conversion checked: the rewritten DXBC (4 colour programs) was converted by airconv and the metallib
     disassembled (`metal-objdump -d`): `resinfo` -> `air.get_width/height_texture_2d(level 0)` as float,
     `deriv_rtx/rty` -> `air.dfdx/dfdy`, the lod maths (dot2, max/min, `* 1/256`, log2, `* 0.5`, max 0, `fma k,1`),
     the `>= floor` select, sharpen (`|dfdx|+|dfdy|`, max 1e-4, fdiv, saturate) and `o0.w = coverage` are all
     instruction-for-instruction the rewriter's emit. No Metal translation bug. Alpha-to-coverage (pipeline,
     colour*1 / alpha 0 blend) was already correct and mode 1 is clean with the same plumbing.
   - One real sampler gap found and fixed (backend.mm, `ImageBufferData::Create`): Metal samplers ignored
     `Configuration::textureFilter`, so anisotropic filtering (DX11 `FILTER_ANISOTROPIC`, cap 16) never applied.
     Now `maxAnisotropy = clamp(textureFilter, 1, 16)` for non-comparison `_filter == 2` samplers. It does not change
     the haze (checked at VideoTextureFilter=16) but the rewriter's lod estimate assumes aniso 16.
   - Cause of the haze: it is the design of the fill (alpha * (1 + lod*k), floor 0.15), not a Metal bug. At overview
     zoom lod is 4-5, so the factor is 1.6-1.75 and low-alpha texels (0.3-0.5, filtered RGB pale blue-grey) of the
     bare/dead tree twig cards are promoted to full coverage. Same maths runs on Windows (FAA-TUNE measured the
     same blue speckles there and added the 0.15 floor); it is just more visible on a Mac window.
   - Historical mitigation (superseded; Metal only, DX11 path and `Ext::BetterTrees::kDistanceFill*` untouched): `tree_passes.mm` used
     `kMetalFillK = 0.08`, `kMetalFillFloor = 0.40` (were 0.15 / 0.15). Fixed camera town,90, 4x MSAA, seed 10000:
     k 0.15 floor 0.30 still hazy; k 0.05 floor 0.15 mostly clean; k 0.08 floor 0.40 close to mode 1 with a
     slightly fuller canopy (crops `docs_metal/f_cmp_a.png` (local, not shipped), `f_cmp_b.png`, `f_cmp_640.png`; 640x360 checked).
     This tuning was superseded: Metal now uses the ruled shared strength/floor values
     0.15 / 0.15. The historical visual measurements above do not validate those restored values.
     Judged visually only (frames are not reproducible).
   - Unit: 1191 tests, 0 failures, 5 skipped.
4. **Billboard shadows.** Receiver rewrite + edge dither run in mode 1/2 (logs show conversions loaded); on
   1280x720 shots (`r_w_t1_28.png`) canopy shadows on grass look right. No Windows same-scene reference exists
   (the repo screenshots are different maps), so parity is judged visually only.
5. `getenv`: none remain under src/render/metal (already Opt()-based); nothing to change.

Stress town 640x360, perf build, 60 s, frame-throughput mean after the first third (noisy, other load on the
Mac): off 193.7 fps, AO 193.1, MSAA 4x 180.8, MSAA 4x + AO 222.5 (the last is noise). The harness is
CPU-bound at this size, so GPU cost of AO is not isolated: no measurable cost, no precise figure.
Unit: 1191 tests, 0 failures, 5 skipped.

## Shadow settings repair (2026-10-19, shadow investigation)

`src/render/metal/backend.mm`, built in `build/mac-shadow`. Two defects corrected:

- Settings Apply rebuilt dynamic resources only if the main window's pixel dimensions
  changed. `shadowScalar` changes `ImageBuffer::Width/Height()` immediately but left
  the Metal texture and RenderBuffer's cached extent at the old size. Apply now runs
  `ReleaseDynamic/RecreateDynamic` for settings changes even at the same window size.
  This recreates shadow attachments, samplers and render-buffer extents together.
  Shadow targets retain their own scaled dimensions and stay single-sample.
- Raster bias was passed straight from the serialized engine values, missing DX11's
  constant-bias conversion (`int(bias * 16777215)`) and slope scale (`*4096`). Metal
  now uses those rasterizer units. D32Float versus retail D24 precision still prevents
  a claim of exact depth/bias parity. The default scene had shadows before the fix;
  the owner's subjective default-appearance complaint is not independently resolved
  by a same-scene retail comparison.

The existing LESS comparison sampler, aniso exclusion for comparison samplers,
attachment/sample pipeline keys and per-draw cull/viewport/bias setup were intact.
`OPENBA_METAL_STATS` now also logs shadow texture creation (size/format/samples) and
one depth-only draw count every 120 frames; no new flags were added.

Evidence under `docs_metal/shadow/` (local, not shipped) (screenshots visually inspected, no pixel-diff claim):

| Run | Settings and result |
| --- | --- |
| `baseline2/` | Initial `build/mac-arm64/OpenBA`: default has projected shadows; Apply with scalar 2 loses them (`default.png`, `low.png`). Earlier `baseline/` also loses them after scalar 0. |
| `fixed/` | Startup, scalar 2, 0, 1: shadows retained in `default/low/high/medium.png`; logs confirm 512, 2048, 1024 maps, format 260 (Depth32Float_Stencil8), samples=1. |
| `quality/` | shaderDetail 0/1/2: 0 intentionally omits shadows (engine SunLight gate); 1 and 2 restore them. |
| `msaa/` | Off → 2x → 4x → Off, shadows retained; weather/fog obscures later captures. |
| `extensions/` | AO on, better_trees mode 1 (billboard receiver + cutout/dither rewrites loaded), 4x MSAA; scalar 2/0/1 retains shadows. Faithful runs above cover AO off. |
| `extensions-extra/` | With AO/billboard/4x, shaderDetail 3 and reflections 0/2 retain shadows. |
| `requested-window/` | Requests 1280x720 then 1920x1080 both clamp to 640x360 in background mode; shadow remains 1024. This is Apply-path evidence, **not actual high-resolution resize validation**. |

The companion `msaa-extensions/` run pauses simulation through LLDB to improve the
weather stability of the MSAA captures; settings still use the real Apply path.
Initial extension config is 4x, followed by 2x, 4x and Off.

Harnesses: `local artifact “run.sh”` (local, not shipped) and `sweep.py`, adapted from the prior pipeline investigation
harness. LLDB changes Configuration fields and calls `Device::ApplyChangedSettings()`;
no actual Options widget clicks are claimed. Each launch has an owned-PID EXIT trap,
38-second hard kill, dummy audio, background layer -1, appFrontmost=false captures,
and fresh /tmp HOME/USERPROFILE. Other processes' game processes are untouched.
`MTL_DEBUG_LAYER=1` and `METAL_DEVICE_WRAPPER_TYPE=1` enabled throughout.

Unit: `docs_metal/shadow/unit.log` (local, not shipped): **1198 total, 1193 passed, 0 failures, 5 expected
skips**. Build logs: `build/shadow-build.log`, `build/shadow-rebuild.log`,
`build/shadow-final-build.log`. Original/default appearance retail parity, actual
1280→1920 resize and long interactive play remain unverified.

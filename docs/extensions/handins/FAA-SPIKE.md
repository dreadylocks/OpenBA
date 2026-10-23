# FAA-SPIKE -- the foliage-AA spike: the go / no-go facts (hand-in, 2026-10-16)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

Block FAA-SPIKE of `docs/extensions/FOLIAGE_AA.md` §4, answering §5 S1..S8. Branch
`worktree-agent-a060ab93c6cbd31db` (from main `2b5afd78c`), commits prefixed `spike:`. **Nothing merges**
except this file, FOLIAGE_AA.md §6 and the pure tools named under "Kept" below. The experiment switches
are compile-time constants on the branch: `src/render/dx11/foliage_aa/faa_spike.h` (`kMode` 0..6,
`kDebugLayer`, `kDumpBlobs`, `kFillK`, `kFillAniso`, `kZeroAlphaBlend`), `src/game/Game/faa_spike_season.h`
(`kYearSeconds`) and `kFaaShadowBase` in `src/engine/graphics/sunlight.cpp`. No env flag, no settings row,
no `getenv`. All are back at "off" on the branch tip (mode 0 = the retail path plus null-cost seam calls).

**Verdict: GO on S1, S2, S3, S5, S6, S8; CHANGES THE PLAN on S4 (the blend clone) and S8 (the combo
mapping); S7 half done (MSAA-off look GO, the GPU-cost stress pair OWED to the coordinator).**

Machine / runs: Windows 10, NVIDIA, feature level 11_0, 2560x1377, the user's `VideoMultisample=3` (4x),
`VideoShaderDetail=3`, `VideoShadowScalar=0`. Seed 12345, `BANISHED_CAMERA_PIN=town,25` ("street") and
`town,120` ("overview"), `BANISHED_CAPTURE_INGAME=300`, `/profile:faithful` (every ext / fix off) except
the two S5 AO runs, which ran the user's own ext rows (AO on). Every run held `game_dir_lock.sh` as
`faaspike` (`assert-held` with the exe path before each log read). No `*.pkm` was mounted. `settings.cfg`
(`Documents/Banished/`) was backed up before the MSAA-off runs and restored byte-identical (md5
`f7fad13a...` before and after, `cmp` clean). The game dir now holds a debug build of the branch tip with
every switch off. Captures, PNGs and blob dumps are in `C:/GOG Games/Banished/rdoc_foliage/` only.

**A tooling defect found on the way (affects every capture made with `tools/trace_oracle/capture_arm.sh`
and `tools/dx11_state_diff/capture.sh`).** `MenuDLT::Draw` triggers a RenderDoc capture on its 10th draw
whenever RenderDoc is attached (`src/game/Game/menudialogloopt_stubs.cpp:497`), so every run under
renderdoccmd writes the **main-menu frame first** and the `BANISHED_CAPTURE_INGAME` frame second. Both
scripts keep the first stable `.rdc` (`ls -t ... | head -1` once a file is stable). They capture the menu
backdrop, not the game. My first 13 captures were menu frames (kept as `menu1_*.rdc`). FOLIAGE_AA's own
`forest1.rdc` is the menu backdrop too (577 draws, the same counters as my menu captures). The menu scene is
a fixed summer town with canopies and water, so the menu-frame S4 numbers below are still valid evidence.
`tools/foliage_aa/run_arm.sh` waits for the second file. Fixing the two shared scripts is the
coordinator's call. trace_oracle's noise-floor table was probably measured on menu frames.

---

## S1 -- summer and autumn captures: which programs draw the canopies, and what else discards? **GO**

**Season route.** A spike switch writes `Calendar::yearTime` once on the first drawn in-game frame and
runs `WeatherSimulation::Update(0)`. That is retail's own `DebugUI::OnDaySliderChanged` (0x1400607e0,
`debugui.cpp:176`). The leaf-loss fade is `WeatherSimulation +0x24` (`weather_sim.cpp:315`). It is bare at
yearTime 0 (the autostart new game starts there, which is why a new town's deciduous trees are bare),
full from 150 s to 2250 s, and bare again from 2400 s. Fall colour (+0x20) ramps from 1800 s to 2250 s.
Summer = 1200 s. Autumn = 2310 s: leaf loss 0.4 and full fall colour, so the season `mad` takes part in
the cutoff. FAA-TUNE can use the same switch for "all four seasons", or save a game per season.

**Captures** (`rdoc_foliage/`): `s1_summer_street.rdc`, `s1_summer_over.rdc`, `s1_autumn_street.rdc`,
`s1_autumn_over.rdc` (in-game frames, each with its `_menu.rdc` sibling), plus PNGs in `png/`. Per-draw
GPU counters come from `tools/foliage_aa/capture_summary.py`. It keys each pixel shader by the DXBC
checksum, the same key the exe's `[FoliageAA]` lines use.

| pass (target) | program (group, see S2) | summer street: draws / PS invocations / samples passed | summer overview | autumn street | autumn overview |
|---|---|---|---|---|---|
| main (2560x1377 ms4), variant v3 | G1 deciduous billboard `a437d0d2` | 7 / 20.1 M / 10.6 M | 38 / 22.9 M / 10.2 M | 7 / 24.8 M / **7.3 M** | 38 / 26.2 M / **2.5 M** |
| | G2 snow conifer `f299fbbb` | 8 / 6.5 M / 13.5 M | 52 / 10.6 M / 16.1 M | 8 / 6.5 M / 13.4 M | 51 / 10.6 M / 16.1 M |
| | G3 alpha-test props `15fe7ead` | 2 / 434 / 4 | 2 / 2.0 k / 1.0 k | 2 / 1.5 k / 625 | 2 / 4.8 k / 2.7 k |
| | G4 `ada6954f` | 1 / 157 / 4 | 1 / 2.7 k / 799 | 1 / 1.0 k / 78 | 1 / 4.0 k / 1.6 k |
| reflection (1280x688 ms1), variant **v0** | G2 `bc55ff72`, G1 `1d0afbb6`, G3 `ba79b59f` | 17+17+2 draws | 57+42+2 | 16+17+2 | 57+42+2 |
| shadow (2048 depth-only) | the depth programs (S2) | 14+14+1 | 58+42+1 | 14+13+1 | 56+39+1 |

- The canopies are G1 (deciduous) and G2 (snow-capable conifers). G3 and G4 are small props. Autumn's
  leaf loss cuts G1's samples passed by 31% at street level and by 76% at the overview. The season term
  is applied to an alpha the mip chain has already averaged down.
- **Nothing else discards in the main colour pass.** Every other main-pass program has no `discard`.
- Outside the main pass: two water-plane clip programs (`6f3b9c32`, `3239d9a9`) in the reflection pass
  (`lt r0.x, -v4.y, l(0)` / `-v3.w`, `mov o0.w, v4.y`: a clip on an interpolant, not a texture alpha), and
  the three depth programs in the shadow pass. The matcher rejects all five by construction (S2).
- **The reflection pass draws the same cutout programs at variant 0**, into a 1x target. The rewrite
  reaches it whether we want that or not (S4).

## S2 -- does every DETAIL0..3 variant contain the pattern? **GO -- 6 programs x 4 variants, one grammar**

**Run-time evidence** (`s1_summer_street.error.log`, mode 1):
```
[FoliageAA] running: blobs 148 identity 148/148 checksum 148/148 with-discard 48 matched colour 24 depth 0 unmatched 24 transformed 0 failed 0
```
That is 148 pixel-blob creations (99 distinct blobs). 48 contain a `discard`. **24 match: exactly 6
programs x 4 variants.** The 24 rejected are the 3 water-clip programs x 4 variants plus the 3
single-variant depth programs (counted per creation). Programs carry no name at create time:
`owner->_path` is empty in `PixelProgramData::PlatformCreate`. The match is the key, as §3 planned.

**The grammar FAA-CORE must accept** (all 24 matched blobs, from `fxc /dumpbin` of the dumped blobs):
The cutout shape, in prose (the instruction text is not reproduced; read it from the install with
`fxc /dumpbin`): after the declarations a `sample` of the colour texture through the interpolated uv; in the
deciduous-billboard group a season `mad` on the sampled alpha comes next; then an `add` of a constant to the
alpha, an `lt` of that result against zero, and a `discard_nz` on the `lt` result, each directly after the
previous one. No flow control appears anywhere in the 24 blobs, a single `mov` writes the output alpha once,
and a single `ret` ends the program. The scratch register carrying the alpha differs per group (see the table).
| group | variants v0 / v1 / v2 / v3 (checksum key) | rT | season mad | temps | textures |
|---|---|---|---|---|---|
| G1 deciduous billboard (`Normal_ps`) | `1d0afbb6` `d5503f8c` `1935b5ef` `a437d0d2` | r0.w | yes | 3 | t0, t3 (+t5) |
| G2 snow alpha-test conifer | `bc55ff72` `d384ccbf` `97870cc6` `f299fbbb` | r0.w | no | 3 / 3 / 4 / 4 | t0, t1 snow, t3 |
| G3 alpha-test (`Normal_psalpha`, FOLIAGE_AA's ps 2596) | `ba79b59f` `e9fd958d` `e27aa5b3` `15fe7ead` | r0.w | no | 3 | t0, t3 |
| G4 the same shape as G3 (a second plain cutout program) | `e5962a9e` `641764b4` `563dd64b` `ada6954f` | r0.w | no | 3 | t0, t3 |
| G5 snow + ao | `321cb39d` `44c4d692` `ee245469` `3796d76a` | **r1.x** | no | 3 / 3 / 4 / 4 | t0, t1, t3 |
| G6 snow + damage | `eeac69ad` `57ea6932` `ebd278eb` `9474d357` | **r1.x** | no | 3 / 3 / 4 / 4 | t0, t1, t2, t3 |

The two variations the matcher must take: **the test register** (r0.w, which overwrites the alpha, or a
separate r1.x), and **the optional season mad**. Everything else is fixed. In G1-G4 the `add` writes over
the alpha register, so the rewrite must **copy the alpha to a fresh temp before the add** (the spike's
first emit read r0.w after the add and was wrong; fixed before any run). Not matched, by design: the depth
programs `f7d47395` (cut **0.25**, DepthAlpha), `17cb63f8` (cut 0.5) and `9f58bc58` (season `add r0.w,
r0.w, -v1.z`). They put `mov o0.xyzw, r0.xyzw` between the add and the lt, and have no `mov o0.w, l(0)`.
The plan keeps them hard. Which variant is bound: `BindResources` passes `Configuration::shaderDetail` (the
"Shadow Quality" combo, `context_bind.cpp`): v3 at the user's settings in the main pass, **v0 in the
reflection pass**. All four must be rewritten. G5 and G6 drew in none of these frames.

## S3 -- identity re-emit byte-identical for every blob; a dropped discard accepted? **GO**

- **Identity:** `148/148` byte-identical (container parse, SHDR token walk, re-emit of the tokens, a fresh
  container with recomputed offsets, sizes and checksum). **Checksum:** our DXBC-MD5 reproduces the stored
  checksum for `148/148` (`src/render/dx11/foliage_aa/dxbc.cpp` `Checksum`).
- **The runtime enforces the checksum:** a synthetic blob with one checksum bit flipped gives
  `CreatePixelShader: Pixel Shader is corrupt or in an unrecognized format` (id 192) on a debug device
  (host harness, scratch only). So "accepted" really means our checksum is right.
- **Drop `discard_nz` on one program** (mode 2, `kDebugLayer` on, `s3_dropdiscard_dbg.error.log`):
  ```
  [FoliageAA] D3D11 debug layer ON
  [FoliageAA] transformed bc55ff728803445a v0 mode 2: 1760B -> 1748B CreatePixelShader hr=0x00000000
  [FoliageAA] transformed binds 1 of 42 pixel binds
  [FoliageAA] debug layer summary: 1027 message(s); id351(sev2)x1017 id353(sev2)x7 id391(sev2)x3
  ```
  There was no message at the create (it is drained before and after) and no new message id while the
  program was bound. The three ids are the faithful frame's own warnings, the same ones AOSPIKE saw.
- **The full part-1 transform** (mode 3, debug layer on, `s4_a2c_dbg.error.log`): 24/24 created with
  `hr=0`, the A2C blend clone created with `hr=0`, the same three faithful ids and nothing new.
- A malformed emit is caught before the driver: the rewriter re-parses its own output and checks
  identity + checksum (`cutout_rewrite.cpp`, end of `Transform`).

## S4 -- A2C with `RenderTargetWriteMask = RGB`: coverage from o0.w? RT alpha untouched? water identical? **CHANGES THE PLAN**

Method: one capture, a deterministic replay. For every A2C draw, every sample of the target is read before
and after the draw (`tools/foliage_aa/a2c_check.py`). In-game summer street (`s4_a2c_street.rdc`, 53 A2C
draws):

| target | draws | samples covered | alpha changed | covered samples whose alpha was non-zero (retail writes 0 there) | pixels with 1-3 of 4 samples |
|---|---|---|---|---|---|
| main 2560x1377 ms4 | 18 | 13,514,482 | **0** | 0 | **277,660** |
| reflection 1280x688 ms1 | 35 | 822,322 | **0** | **818,008** | 0 (1 sample) |

- **The coverage comes from o0.w:** 277,660 main-pass pixels are partially covered (1, 2 or 3 of 4
  samples). The menu frame shows the same: 205,064 such pixels over 80 draws.
- **The RT alpha is left untouched by the RGB mask:** 0 alpha changes in all 53 draws (and in the 80 of
  the menu frame).
- **But "untouched" is not "retail".** Retail writes `o0.w = 0` on every sample it keeps. Under the mask the
  old alpha stays. In the reflection pass that old alpha is the clear value (non-zero) on 818,008 covered
  samples. **The water reads the reflection's alpha:** the water program binds `_reflection_` at t2 (the
  RenderDoc binding names) and uses its `.w` as a factor, `mad r0.yzw, r0.yyzw, r2.wwww, r2.xxyz`. So with
  the plan's RGB mask, **the water under reflected trees changes**. The main pass carried 0 here (the
  frame copy's alpha, the plan's concern, stayed 0 in this frame), but the menu frame had main-pass covered
  samples with a non-zero old alpha too (13,773 on its first A2C draw).
- **The fix, measured:** the A2C clone uses **BlendEnable = TRUE, colour ONE / ZERO / ADD, alpha ZERO /
  ZERO / ADD, full write mask** (`kZeroAlphaBlend`, `s4_a2czero_street.rdc`). The coverage still comes from
  o0.w (it is taken before blending), the colour is written as before, and the alpha becomes 0 on every
  covered sample, which is retail's own value. Measured: alpha changed = 488,742 = exactly the covered
  samples with a non-zero old alpha; covered samples left with a non-zero alpha = **0**; main-pass partial
  pixels 291,017. Every retail blend state the 24 programs use has BlendEnable = FALSE (one clone was
  created per run, `blend=0 mask=0xf`).
- **The water is identical where the coverage is the same.** At a leaf edge the covered set differs from
  retail's on purpose (that is the AA), so there the water's input alpha follows the new coverage.
- The look (`png/cmp_street.png`: faithful left, A2C right, 3x crop): the stair-stepped canopy edge becomes
  a smooth 4-level edge, and the canopy interior is unchanged.

## S5 -- dropping the discard: depth per covered sample, early-Z, the AO resolve, SelectionMask? **GO**

- **Depth only on covered samples** (`tools/foliage_aa/a2c_depth_check.py`, 8 A2C draws, D24S8 per
  sample): 8,092,066 samples changed colour and depth together; 4,861 changed colour only and 1,112 depth
  only (0.07%: rewrites at an equal depth, or of an equal colour). So the A2C mask gates the depth writes
  per sample, as D3D11 specifies.
- **Early-Z / cost proxy:** without the discard, the PS invocations are +1.6% street (20.12 M -> 20.45 M)
  and +2.8% overview (22.9 M -> 23.5 M) on G1. Samples passed are -0.6% (10.59 M -> 10.52 M) and -0.7%: the
  sharpened alpha keeps the canopy area. The program gains 8 instructions and loses the discard (+196 bytes). The GPU time is
  S7's owed row.
- **AO:** the depth resolve reads **sample 0** (`ao_depth_resolve.hlsl:29`, `g_DepthMS.Load(.., 0)`). So
  AO sees a leaf-edge pixel as leaf exactly when sample 0 is covered: a 1-pixel binary edge under a soft
  colour edge. In the AO-on captures (`s5_a2c_ao_street.rdc` against `s5_faithful_ao_street.rdc`,
  `png/cmp_ao_street.png`) no halo or artefact is visible at street level. A min / max over samples is
  FAA-TUNE's option if one shows up.
- **SelectionMask** uses `OpaqueMaterial.srsl:Depth_ps`, the opaque depth program (no discard, colour
  write mask 0, stencil Replace). It is never matched, so the selection outline is untouched. The blend
  clone is bound only with a transformed program, so no other state is touched.

## S6 -- part 2 on ps_4_0: does resinfo + derivatives track the hardware lod? Where does k live? **GO (aniso form)**

Method: mode 5 writes `(iso lod, hardware lod, aniso lod) / 8` into o0.rgb for the transformed programs.
The hardware value comes from the SM4.1 `lod` instruction (opcode **108**: 107 is reserved, and fxc's own
`CalculateLevelOfDetailUnclamped` disassembles to it). The transformed blob is bumped to `ps_4_1`, with
the RDEF target patched as well. `CreatePixelShader` accepts it on FL 11_0. Compared on 3,166,626 canopy
pixels, overview (`tools/foliage_aa/lod_check.py`, `s6_loddebug_over.rdc`):

| formula (uv derivatives scaled by `resinfo` size) | mean (formula - hardware) | mean abs | worst |
|---|---|---|---|
| isotropic `0.5 * log2(max(|ddx|^2, |ddy|^2))` (the plan's) | +0.75 mip | 0.75 | 4.3 |
| aniso `0.5 * log2(max(Pmax^2 / 256, Pmin^2))` (max aniso 16) | +0.27 mip | 0.29 | 3.7 |

The isotropic form over-estimates by about 3/4 of a mip, because the sampler is anisotropic 16x and its
hardware picks a sharper mip. **FAA-CORE uses the aniso form** (six instructions more, all SM4.0). On
FL >= 10_1 the exact `lod` instruction works too (ps_4_1); the engine still allows FL 10_0, so it stays an
option, not the default. k scales whatever bias remains, so FAA-TUNE absorbs the last +0.27.

**Where k lives:** no free constant. `_PixelConstants_` is 5 rows. Row 0 is `(1/shadowRes, 0, 0, 0)`,
written whole by every `Context::SetLight` (`batchc_context_setlight.cpp:172-175`). Rows 2 and 3 (.w) carry
the light colour / position w. Row 4 is the weather vector. The 4 `mc_` rows are per material. Using any
of them means editing a retail body. **Bake k as an immediate in the emitted code** (the spike does:
`mad P.x, P.x, l(k), l(1)`) and re-create the transformed shaders when mode or k changes. That is 24
`CreatePixelShader` calls, the same work as at load, and it adds no cbuffer, no slot and no second
variant. The combo's two states are then one transformed program per blob, with k = 0 or the tuned value.

**Look** (`png/cmp_over.png`: faithful, part 1, parts 1+2 with k = 0.25 iso): faithful is the sparse
single-pixel dither the user reported. Part 1 smooths it. Part 2 fills the canopies at the overview.

## S7 -- the MSAA-off look with the discard kept; the GPU-cost stress pair -- **look GO; stress OWED**

- **MSAA off** (`VideoMultisample=0`, settings restored after): `s7_msaa0_faithful_over.rdc` (retail) and
  `s7_msaa0_fill_over.rdc` (mode 6: the discard kept, part 2 fill only, no A2C). See
  `png/cmp_msaa0_over.png` (and the menu-frame pair `menu1_s7_*`). The fill makes the distant canopies
  read as solid instead of dithered. **One artefact for FAA-TUNE:** a few blue-grey speckles inside the
  filled canopies. These are texels with a near-zero alpha whose RGB is not leaf colour, pushed over the
  cutoff by the fill. A floor on the fill (fill only where a > ~0.1) or a smaller k removes them. The same
  texels show at 4x MSAA, softened.
- **GPU cost row on vs off: NOT RUN** (the brief: the machine was not clean, another lane was building and
  running, and RTSS caps fps). **Owed to the coordinator. Recipe:** on this branch, build the **perf**
  exe twice, with `faa_spike.h` `kMode = 0` (faithful) and `kMode = 4` (parts 1+2), `kZeroAlphaBlend =
  true`, `faa_spike_season.h` `kYearSeconds = -1`. Run `tools/build.sh stress` once per leg on a clean
  machine with Afterburner, RTSS and RTSSHooksLoader64 stopped (memory `reference_rtss_*`), at the user's
  4x MSAA, then `tools/stress_report.py <row-on>.csv --baseline <faithful>.csv`. The rows to quote are draw
  ms / frame ms and fps. Note `kMode` is compile-time here, so both legs are separate builds of one tree.

## S8 -- shadow: which retail combo step gives which shadowScalar; does the PCF texelSize follow a 4096 map? **CHANGES THE PLAN (the mapping)**

- **The "Shadow Resolution" combo has three items, High / Medium / Low** (the kit's
  `StringTable.rsc:shadowResolution`, Level0..2). `OptionsDialog` writes the item index straight into
  `Configuration::shadowScalar` (`optionsdialog.cpp:918`, `:980`). Startup clamps it to 0..2
  (`device_startup.cpp:310`). The log, from `Width()` itself with the scalar stepped:
  ```
  [FoliageAA] S8 shadow step: VideoShadowScalar=0 (combo item 0) -> sun map 2048x2048, texelSize 0.00048828
  [FoliageAA] S8 shadow step: VideoShadowScalar=1 (combo item 1) -> sun map 1024x1024, texelSize 0.00097656
  [FoliageAA] S8 shadow step: VideoShadowScalar=2 (combo item 2) -> sun map 512x512, texelSize 0.00195312
  ```
  **The top step is High = scalar 0.** "Off / Low / Medium / High" (FOLIAGE_AA §7) is the *Shadow Quality*
  combo (`shaderDetail`: None / Low / Medium / High = DETAIL0..3 = the pixel-program variant, i.e. the
  PCF kernel), not the resolution.
- **The PCF texel follows:** `pc.texelSize.x` is `1/Width()`, set by `SetLight(..., dim)` with `dim =
  _depthTarget->Width()` (`lights_vtable.cpp:582-589`). In the capture with the base forced to 0x1000
  (`menu1_s8_shadow4096_over.rdc`) the main-pass cbuffer holds `texelSize.x = 0.00024414 = 1/4096`, against
  `1/2048` in a normal capture. The texel snap in `sun_camera.cpp:299-303` divides by the same `Width()`.
- **But a plain base change moves every step** (logged with base 0x1000: 4096 / 2048 / 1024). FAA-SHADOW
  must make only step 0 change. Either create the map at 0x1000 only when `shadowScalar == 0`, or keep base
  0x1000 and add one to the shift for steps 1 and 2. Either way the map is re-created through the
  existing `settingsChanged` path when the combo or the row flips.

---

## Kept / not kept, and the content rule

- **Kept for FAA-CORE (pure, no D3D, no retail bytes):** `src/render/dx11/foliage_aa/dxbc.{h,cpp}` (the
  container, the checksum, the SM4 token walk, the operand decode, the encoders) and
  `src/render/dx11/foliage_aa/cutout_rewrite.{h,cpp}` (the matcher of S2 and the emitters of modes 2-6).
  FAA-CORE takes these two as its starting point, plus unit tests on synthetic blobs. **Tools:**
  `tools/foliage_aa/capture_summary.py` (the per-shader counters, keyed like the exe log),
  `a2c_check.py` (S4), `a2c_depth_check.py` (S5), `lod_check.py` (S6), and `run_arm.sh` (a locked run that
  keeps the **in-game** capture, kills by PID, and logs its arm).
- **Not kept:** `faa_spike.{h,cpp}`, the seam lines in `shaderprograms.cpp` / `renderstatedata.cpp` /
  `devicedata.cpp`, the season switch in `gameupdateloop.cpp`, and the S8 log + base in `sunlight.cpp`.
- **Nothing retail entered the tree:** `git diff --stat 2b5afd78c HEAD` was reviewed. It holds C++ sources,
  Python / shell tools and this markdown only: no blob, no DXBC dump, no `.srsl` / `.rsc` copy, no capture,
  no texture. The disassembly quoted above is a few instructions of evidence. The blob dumps and the
  `fxc /dumpbin` listings stay in `C:/GOG Games/Banished/rdoc_foliage/blobs/`, outside the tree.

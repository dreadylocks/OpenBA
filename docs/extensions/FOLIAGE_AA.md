# FOLIAGE_AA -- `ext.foliage_aa` (class A): smooth alpha-tested edges (leaves, needles, twigs), the plan

Opened 2026-10-14 (night) from the user's screenshot (`trees_left_zoomed_in_right_zoomed_out.png`:
stair-stepped canopy edges up close, a single-pixel dither over whole canopies zoomed out) and one
RenderDoc capture of the faithful renderer at the user's max settings. Nothing is built. This sheet
is the plan for a future session; §1 is what the user asked for, §2 the facts, the rest derived.
Blocks hand in under `handins/FAA*.md`.

## 1. The ask and the rulings (the user, 2026-10-14 night)

1. Reduce the pixelation of tree leaves at max settings.
2. It is **another entry in the Options dialog's "Enhanced graphics" tab** (the tab AOMENU built,
   `src/ext/ambient_occlusion/options_tab.cpp`), next to the AO combo and slider.
3. Plan now, build in a future session.
4. **Menu: a combo**, Off / Smooth edges / Smooth edges + distance fill.
5. **The distance fill (§3 part 2) ships in the first version.**
6. **"Max shadow resolution"** is a second entry in the same tab (§7). The retail Options shadow
   resolution combo keeps its Off / Low / Medium / High scaling untouched.
7. **No game content is distributed.** This is an open engine project. The repo, the exe and the
   build outputs we would hand out contain only our own code and our own resources. No retail
   shader source (the kit's `.srsl`), no retail DXBC, no hand port of either, no retail texture,
   and nothing derived from them. Retail data is read from the user's own install at run time, as the
   engine already reads the `.pkg`. This rule shaped §3: the shaders are **transformed at load
   from the user's installed bytecode**, not ported and shipped.

## 2. Facts (the capture + the kit; file:line are the tree's on 2026-10-14)

**The capture.** `C:/GOG Games/Banished/rdoc_foliage/forest1.rdc` (debug exe at `7dbcaaf0e`, seed
12345, `BANISHED_CAMERA_PIN=town,45`, `BANISHED_CAPTURE_INGAME=300`; 2560x1377, 577 draws). The
autostart town was in **winter**, so the deciduous canopies are clipped bare by the season term
below. The conifers, twigs and every program listed here are in the frame, but **a summer capture
is FAA-SPIKE's first job** (the look checks need full canopies). Per-draw state:
`dump_state.py` (tools/dx11_state_diff), a summariser grouped by shader pair.

**The cause is the alpha test, not texture resolution.** Every cutout program is
`clip(alpha - 0.5)` -> DXBC `discard_nz`, so each pixel is all-leaf or all-background. MSAA is on
(the user's `VideoMultisample=3` = 4x; main colour + depth `2560x1377 ms4`) but MSAA only antialiases
triangle edges; a leaf edge is inside the card's triangle, so all four samples are kept or all are
killed together. `AlphaToCoverageEnable` is FALSE in every draw (retail-faithful,
`src/render/dx11/renderstatedata.cpp:339`). Zoomed out it gets worse: the mip chain averages alpha,
so in the small mips most texels sit near or below 0.5 and canopies turn into a sparse
single-pixel dither that shimmers as the camera moves (the right half of the screenshot).

**The kit's shader sources are a reference, never an input** (§1 ruling 7: read them as we read
Ghidra; copy nothing into the tree). The mod kit (`ToolKit/resource/Material/`, git-ignored, see
`docs/reference/binaries.md` "Modding toolkit") ships every `.srsl` (Shining Rock's shading
language, compiled by `Tools-x64.exe` to 4 DXBC variants, `DETAIL0..3`, = `kShaderVariantCount`).
The capture's DXBC matches the source line for line. The cutout programs:

| Pass | Program (`file:entry`) | Material | Cutoff | Captured as |
|---|---|---|---|---|
| colour | `BillboardMaterial.srsl:Normal_ps` | deciduous canopies: camera-facing leaf cards | `(a - season*move) - 0.5` | ps 2788 / 2596, vs 4260, NoCull, 256x256 BC3 9 mips |
| colour | `BillboardMaterial.srsl:Normal_psevergreen` | `BillboardEvergreenMaterial` | `a - 0.5` | |
| colour | `OpaqueMaterial.srsl:Foliage_ps` | `FoliageMaterial` | `a - 0.5` | |
| colour | `OpaqueMaterial.srsl:Normal_psalpha` | `AlphaTestMaterial` (props, twigs) | `a - 0.5` | ps 2596 (`e3b37064`, eids 2177-2198) |
| colour | `OpaqueMaterial.srsl:Snow_alphatest`, `SnowDamage_alphatest` | conifers with snow (season lerp) | `a - 0.5` | ps 2932 (eids 2085-2100, snow texture t1) |
| depth / shadow | `BillboardMaterial.srsl:Depth_ps` / `Depth_psevergreen` | the shadow casters of the above | `(a - season) - 0.5` / `a - 0.5` | ps 724 / 768 into the 2048 shadow map |
| depth / shadow | `OpaqueMaterial.srsl:DepthAlpha_ps` | AlphaTest / Foliage shadow casters | **`a - 0.25`** (retail: shadows are fuller than the visible canopy) | ps 700 (cutoff not disassembled; the source says 0.25) |

- `pc.userPosition.z` is the **season leaf-loss** term (the canopy thins through autumn, bare in
  winter); `pc.userPosition.y` the fall-colour lerp; `move` (lightFogMove.w) marks the swaying
  card vertices. The replacement must keep both exactly; it changes only what happens at the cutoff.
- Every opaque program writes **`o0.w = 0`** (`output.color.w = 0.0`). The alpha-tested ones too.
- **The main target's alpha is load-bearing.** The water shader (ps 3180, eids 2949-2958) reads a
  copy of the frame and blends `o0.xyz = lerp(frame.rgb, water, frame.a)`
  (`mad o0.xyz, r1.wwww, r0.xyzx, r1.xyzx`). Anything that writes a non-zero alpha into the main
  colour target changes the water. So **A2C must run with the alpha channel write-masked off**.
  (D3D11 takes the coverage from the shader's SV_Target0.a whatever the write mask is. FAA-SPIKE
  verifies this on this driver.)
- **The render state is shared.** `AlphaTestMaterial` / `FoliageMaterial` use
  `Material\Opaque\OpaqueMaterial.rsc:rs`, the SAME state object as every solid mesh (whose shader
  writes alpha 0: A2C on that state would make every building invisible). A2C cannot be a
  property of the retail RenderState; it has to be bound together with the replacement program.
- Programs carry no name at run time: `Video::ShaderProgram` is `_textures` / `_constants` /
  `_byteCode` (`src/engine/video/ShaderProgram.h:244-249`); the D3D object is made in
  `PixelProgramData::PlatformCreate` (`src/render/dx11/shaderprograms.cpp:197`) and bound per
  draw in `PixelProgramData::Bind` (`:223`). How the replacement is keyed is spike question S2.
- No runtime shader compiler in the exe (the AO blobs are fxc'd offline from OUR HLSL,
  `tools/ao_shaders.py`). Banished's DXBC is `ps_4_0`, SM4 tokens, one SHDR chunk per blob, the
  container checksummed (DXBC's modified MD5; the algorithm is public, e.g. in Wine's d3dcompiler /
  vkd3d-shader / DXVK). The blobs reach us as `Video::ShaderProgram::_byteCode[i]` from the user's
  installed `.pkg`, before `CreatePixelShader` (`shaderprograms.cpp:209`).
- **Textures** are not the problem. Leaf cards are 256x256 BC3 with a full mip chain, the sampler
  is anisotropic 16x (`VideoTextureFilter=16`), and the interior of a canopy is sharp in the
  screenshot. Higher-resolution leaf art is asset work, out of scope.
- **Shadows** (a separate candidate the user named): one 2048x2048 D24S8 sun map
  (`src/engine/graphics/sunlight.cpp:342-349`, `0x800` hard-coded; `VideoShadowScalar=0` = no
  down-shift, already the max), fitted to the bounding sphere of the WHOLE view
  (`src/engine/graphics/sun_camera.cpp:299-305`, texel = 2*radius*skew/2048), sampled 3x3 PCF at
  one texel spacing (`lighting.srsl` `GetShadowHigh`, `DETAIL3`). Zoomed out, a shadow texel covers
  many screen pixels, so the dappled self-shadow inside the canopy is blocky. That is a different
  artefact from the edge stair-steps; §7 keeps it as its own optional entry.

## 3. The shape

**How the replacement shaders come to exist (§1 ruling 7).** Not a port. At
`PixelProgramData::PlatformCreate`, when the row is on, a small **DXBC rewriter** recognises the
retail cutout pattern in the user's own installed bytecode and emits a transformed copy in memory:

The pattern (retail instruction text not reproduced here), in order: the `sample` of the colour texture, an
optional season `mad` on its alpha, an `add` of a constant to the alpha, an `lt` against zero, a `discard_nz`
on that result, the lighting and fog (left untouched), and a single `mov` writing the output alpha.
The rewriter keeps the `sample` and the season `mad`, inserts the part-2 LOD alpha scale after them, replaces
the `lt` / `discard_nz` pair with a screen-space-derivative coverage computation (`deriv_rtx` / `deriv_rty`
-> fwidth of the alpha; coverage = saturate(rT / max(fwidth, 1e-4) + 0.5)), and makes the final output-alpha
`mov` write that coverage instead of zero. The result lives in memory only, never written out.

plus the container fix-ups (instruction-length tokens, `dcl_temps` + 1 or 2, the chunk sizes, the
checksum). The exe ships the rewriter and the pattern; every byte of the result comes from the
user's install. A blob that does not match the pattern exactly is left alone (retail look), and logged
once. That covers mods too: a mod's own alpha material is either recognised (same compiler, same
pattern) or untouched.

**Part 1 -- alpha-to-coverage with a sharpened alpha.** The transformed program is bound with a
**clone of the draw's blend state** that has `AlphaToCoverageEnable = TRUE` and
`RenderTargetWriteMask = RGB` (§2: the main target's alpha feeds the water). With 4x MSAA the edge
resolves to 4 coverage levels, so a smooth silhouette with no sorting and no blending. Covered samples
still write depth, so AO and the Blended list keep working as today. The ramp is Golus' "sharpened
alpha", `(a - 0.5) / fwidth(a) + 0.5`.

**Part 2 -- distance fill (ships with part 1, §1 ruling 5).** Scale alpha up with the mip level so a
canopy keeps its mip-0 coverage at distance: `a *= 1 + max(0, lod) * k` (k from ~0.25, FAA-TUNE).
`CalculateLevelOfDetail` would need `ps_4_1`; staying on `ps_4_0`, the lod is computed from `resinfo`
(the texture size) and the uv derivatives, all SM4.0 instructions. Rescaling the mip alphas at
texture load (Castaño 2010) is the rejected alternative: the leaf textures are BC3, so it needs a
decode and re-encode in the loader, and it would also change the shadow pass.

**Menu (§1 ruling 4):** Off / Smooth edges (part 1) / Smooth edges + distance fill (parts 1+2).
`k = 0` is part 1, so it is ONE transformed program per retail blob with `k` in a constant. If no
free cbuffer slot is in reach, the spike picks two variants.

**When MSAA is off** (`VideoMultisample=0`), A2C does nothing. Then the rewriter leaves the
`discard` in place (retail look; part 2 still applies as a thicker cutoff), decided per device
creation from `Configuration::multisample`.

**Not changed.** The depth / shadow programs keep their hard cutoffs (a shadow map has no coverage
to blend); the season / fall-colour behaviour; the retail RenderState objects; the faithful
profile (with the row off nothing is rewritten, one null test per program bind).

```
src/render/dx11/better_trees/    the D3D side, the AO precedent (src/render/dx11/ao/)
  dxbc_rewrite.cpp/.h            container parse, SM4 token walk, the pattern match, the emit, the checksum
  foliage_aa_stage.cpp/.h        per-program transformed copies, the A2C blend-state clones
src/ext/foliage_aa/              the extension: Sync(), params (mode, k), the seam set/clear
src/ext/ambient_occlusion/options_tab.cpp   + the two combos (§7); move the tab out of ao/ now it holds 3 entries
tests/                           unit tests on SYNTHETIC SM4 blobs built in the test (no retail bytes)
```

- **Seam** (DESIGN §3 rule 5): one null-by-default static pointer, read in
  `PixelProgramData::PlatformCreate` (make the transformed `ID3D11PixelShader` alongside the retail
  one) and `Bind` (bind it plus the A2C clone of the current blend state). The faithful path pays one
  null test per program bind. Live toggle: the copies are made for every matching program at
  create time when the row is on, or lazily on the first bind after it is turned on.
- **Settings**: the bool row `ext.foliage_aa` (Extensions dialog checkbox, as every row) plus the
  valued key `ext.foliage_aa_mode` (1 = smooth edges, 2 = + distance fill) through AOMENU's
  valued-key code. The combo is the master; Off = the bool off.
- **Device reset**: the transformed shaders and blend clones are re-created with the device.

## 4. Blocks

| Block | Owner | What | Done when |
|---|---|---|---|
| **FAA-SPIKE** | one opus agent | §5's facts, in a worktree, nothing merged but the hand-in | `handins/FAA-SPIKE.md` answers every row of §5 with evidence; §6 filled |
| **FAA-CORE** | one opus agent | the DXBC rewriter + its unit tests (synthetic blobs), the seam, the A2C blend clones, the bool row + mode key, parts 1 and 2 | `tools/build.sh run` clean; a SUMMER capture shows every §2 colour program transformed, A2C on, RT alpha untouched (water identical); `trace_oracle` differs ONLY in those draws; faithful stress unchanged, row-on stress recorded; gate at 0. **DONE 2026-10-16 on its branch (not merged): `handins/FAA-CORE.md`** -- seam `VideoDX11::g_extFoliageAA` (program + render-state bind); row OFF == faithful (trace_oracle 0 on every screen); summer capture: 24 / 24 blobs transformed, A2C + clone, RT alpha 0 on every covered sample, only the cutout draws differ; mode 1 / 2, live toggle and device lost clean. Stress pair OWED to the coordinator |
| **FAA-SHADOW** | one agent (small) | "Max shadow resolution" (§7): the row, the `sunlight.cpp` sites, device reset | the sun map is 4096 with the row on and the retail Shadow Resolution combo at High (`shadowScalar` 0), unchanged at Medium / Low (1024 / 512); memory delta in the hand-in. **DONE 2026-10-16 on its branch (not merged): `handins/FAA-SHADOW.md`** |
| **FAA-MENU** | one agent (sonnet: AOMENU is the pattern) | the foliage combo + the shadow entry in the Enhanced graphics tab, Apply / Cancel | both change the live frame; settings.cfg round-trips**DONE 2026-10-16 on its branch (not merged): `handins/FAA-MENU.md`** -- tab moved to `src/ext/enhanced_tab/` (each extension contributes its rows), foliage combo (Off / Smooth edges / + distance fill) and Max shadow resolution Off / On, row icons; also fixed: a live sun-map toggle under `ext.render_pipe` deadlocked (resize on the render thread) |
| **FAA-TUNE** | one opus agent, then the user by hand | the ramp width, `k`, the MSAA-off fallback, street level vs overview, all four seasons | DONE 2026-10-16: `handins/FAA-TUNE.md`; the user's ruling: k 0.15, floor 0.15, mode 2 the look, row OFF by default. Follow-up FAA-SHADOW2: the shadow entry offers 4096 and 8192 |

FAA-CORE, FAA-SHADOW and FAA-MENU can run in parallel once the spike is in (at most 4 agents;
`--jobs 2`; one game run at a time; merge main before the final build; the coordinator runs the gate).
**Every hand-in states that nothing retail entered the tree** (`git diff --stat` reviewed for
blobs, `.srsl` / `.rsc` copies, captures, extracted textures).

## 5. The spike's questions

| # | Question | Why | Evidence |
|---|---|---|---|
| S1 | A summer (and an autumn) capture of a forest edge at street level and at the overview zoom: which programs draw the canopies, how many draws / pixels each, and does anything other than §2's table `discard` in the colour pass? | the match list is complete | two `.rdc` (kept in the game dir, never committed) + the summariser output; how to get the season (a save, or a seed + month flag) |
| S2 | Does every DETAIL0..3 variant of every §2 colour program contain the exact pattern of §3 (`sample` -> optional season `mad` -> `add -0.5` -> `lt` -> `discard_nz`, and the `mov o0.w, l(0)`)? List the register / swizzle variation the matcher must accept. | the rewriter's grammar | a `[FoliageAA] matched N / M blobs` line per program, the unmatched ones disassembled |
| S3 | A rewriter that changes NOTHING (parse -> re-emit -> checksum) yields byte-identical blobs for every program blob the game loads; then one that only drops `discard_nz` is accepted by `CreatePixelShader` (debug layer clean). | proves the container + checksum code before any semantic change | the identity count, the debug-layer log |
| S4 | A2C with `RenderTargetWriteMask = RGB` on this driver: is the coverage from `o0.w` honoured and RT alpha left untouched? Water identical? | §2: the alpha is load-bearing | RT alpha before / after one replaced draw (a replay compare on ONE capture is deterministic) |
| S5 | Dropping `discard`: depth per covered sample only (verify), early-Z, the AO depth resolve seeing leaf-edge samples, the `SelectionMask` pass? | correctness around the canopy | captures with the AO row on and off |
| S6 | Part 2 on `ps_4_0`: does the `resinfo` + derivative lod track `CalculateLevelOfDetail` closely enough? Where does `k` live (a free constant in `_PixelConstants_`, an extra cbuffer slot, or two variants)? | the combo's third state | the formula, a capture |
| S7 | GPU cost row on vs off in the stress town, RTSS stopped, machine load CLEAN; the MSAA-off look with the discard kept | class A must be cheap; the fallback | the two stress rows, screenshots at `VideoMultisample=0` |
| S8 | Shadow: which retail combo step maps to which `shadowScalar`, and does the PCF `texelSize` constant follow a 4096 map? | FAA-SHADOW | a log line per step, one capture with the row on |

## 6. Decided by the spike (FAA-SPIKE, 2026-10-16; evidence in `handins/FAA-SPIKE.md`)

- **6.1 The match list (S1, S2).** 6 colour programs x 4 variants = 24 blobs, all one grammar:
  `sample r0.xyzw, v1.xyxx, t0, s0` -> optional `mad r0.w, -cb0[4].z, v2.w, r0.w` (deciduous billboard
  only) -> `add rT.c, r0.w, l(-0.5)` -> `lt rT.c, rT.c, l(0)` -> `discard_nz rT.c`, then `mov o0.w, l(0)`
  once and `ret` last, with no flow control. rT is r0.w (four programs, overwriting the alpha: copy it to
  a new temp before the add) or r1.x (two). Nothing else discards in the main colour pass. The water-clip
  programs (reflection pass) and the three depth programs are rejected by the same grammar. Programs carry
  no name at create time (`_path` is empty): the match is the key. The reflection pass binds the same
  programs at **variant 0** into a 1x target, so rewrite all four variants.
- **6.2 The container (S3).** Parse -> re-emit -> checksum is byte-identical for 148/148 blob creations.
  The runtime rejects a wrong checksum, so ours is verified. A dropped discard and the full part-1
  transform create with `hr=0` and add no debug-layer message. Start from the spike's
  `src/render/dx11/better_trees/dxbc.{h,cpp}` + `cutout_rewrite.{h,cpp}`.
- **6.3 The blend clone (S4; changes §3).** Not `RenderTargetWriteMask = RGB`. The RGB mask leaves the
  OLD alpha under the leaves, and the water reads the reflection target's alpha (`_reflection_.w`): 818k
  covered samples differed from retail in one street-level frame. Use **AlphaToCoverage + BlendEnable,
  colour ONE / ZERO / ADD, alpha ZERO / ZERO / ADD, full mask**. The coverage still comes from o0.w and the
  alpha gets retail's 0 (measured: 0 covered samples left non-zero). Every retail state these programs
  use has blending off.
- **6.4 Depth / AO / selection (S5).** Depth is written per covered sample (99.93% of changed samples
  change colour and depth together). The AO resolve reads sample 0 (a binary 1-pixel edge, nothing visible
  at street level). SelectionMask is the opaque `Depth_ps`, never matched.
- **6.5 Part 2 (S6; changes §3).** Use the **aniso lod** `0.5 * log2(max(Pmax^2 / 256, Pmin^2))` from
  `resinfo` + `deriv_rtx/rty` (+0.27 mip against the hardware `lod`); the plan's isotropic form reads
  +0.75 mip high. **k is an immediate** in the emitted code. No `_PixelConstants_` slot is free (row 0
  is all written by `SetLight`), so a mode / k change re-creates the 24 shaders. The combo's third state
  costs no second variant set. FAA-TUNE: a fill floor (fill only where a > ~0.1), because the fill exposes
  near-zero-alpha texels whose RGB is not leaf colour (blue-grey speckles, visible with MSAA off).
- **6.6 MSAA off (S7).** Keep the discard and apply part 2 as a thicker cutoff (screenshots in the game
  dir). The GPU-cost stress pair is owed to the coordinator (recipe in the hand-in).
- **6.7 Shadow (S8; changes §7).** "Shadow Resolution" is three items, High / Medium / Low =
  `shadowScalar` 0 / 1 / 2 = 2048 / 1024 / 512. The PCF `texelSize` (1/Width()) and the texel snap follow
  a 4096 map (`1/4096` measured). A plain base change shifts every step, so FAA-SHADOW makes the map 4096
  only when `shadowScalar == 0`. §7's "Off / Low / Medium / High" is the Shadow Quality combo
  (`shaderDetail`, the DETAIL variant), not the resolution.
- **6.8 Tooling.** The menu loop captures its own 10th frame whenever RenderDoc is attached, and
  `trace_oracle/capture_arm.sh` / `dx11_state_diff/capture.sh` keep that FIRST file. Their captures (and
  §2's `forest1.rdc`) are the main-menu backdrop, not the game. `tools/foliage_aa/run_arm.sh` keeps the
  in-game one. Fixing the shared scripts is open.

## 7. "Max shadow resolution" (§1 ruling 6), and what stays out

- **`ext.max_shadow_res`** (the row; class A, OFF, labelled "Max shadow resolution"; FAA-SHADOW,
  `handins/FAA-SHADOW.md`). The sun map is created `0x800` raw with flag bit 6 (`sunlight.cpp`
  InitFields and Set), and `Width()` shifts it right by `Configuration::shadowScalar`
  (`src/engine/video/buffers.cpp`). The retail **Shadow Resolution** combo has three items, High /
  Medium / Low = scalar 0 / 1 / 2 = 2048 / 1024 / 512 (spike S8; "Off / Low / Medium / High" is the
  Shadow *Quality* combo, `shaderDetail`, untouched). With the row on, **only High changes**: the raw
  size is `0x1000` when `shadowScalar == 0`, so High builds 4096 and Medium / Low keep 1024 / 512. The
  PCF `texelSize` (`1/Width()`) and the texel snap (`sun_camera.cpp`) divide by `Width()`, so they
  follow (1/4096 measured). The seam is `Graphics::SunLight::_extMaxShadowRes` (a null-by-default
  `const bool*` beside `_extShadowFitView`); a row toggle or a combo change re-creates the map through
  retail's own rebuild (`Device::settingsChanged` -> `DeviceData_Resize` -> `RecreateDynamic`), as does
  a device reset. Cost at High: the D24S8 depth goes 16 -> 64 MB (+48 MB of video memory; measured +58 MB
  dedicated) and 4x the shadow fill. The B8G8R8A8 colour target is not built on DX11 (DeviceData slot 6,
  "supports a colour shadow target", is `xor al,al; ret`); where a backend builds it, it follows the same
  size (+48 MB more).
- **Two sizes: `ext.max_shadow_size`** (FAA-SHADOW2, `handins/FAA-SHADOW2.md`; the user, 2026-10-16: "4096 AND
  8192"). A discrete valued key, 4096 | 8192, default 4096 (any other number or a malformed value reads as
  4096; `ValueKey::choices`). The bool row stays the master: off = retail's 2048 at High. With the row on the
  raw size at High is the key (`0x1000` / `0x2000`); **Medium / Low keep 1024 / 512 whatever the key says**.
  The engine reads it through a second seam beside the bool, `SunLight::_extMaxShadowSize` (`const int32_t*`,
  null under the faithful profile). The Enhanced tab's combo is Off / 4096 / 8192 (Off = the row off, the key
  kept; `Ext::SetMaxShadowResAndRecord`), the Extensions dialog's checkbox is the bool alone, the tweak
  overlay has the same three-way combo. A size change rebuilds the map through the same main-thread request
  as the toggle (`SetValueAndRecord` -> `ExtRequestMaxShadowResSync`). 8192 x 8192 D24S8 is 256 MB: the
  dimension always fits (the device is FL 10_0 at least, 8192; 16384 at FL 11_0), the memory may not, so the
  first time 8192 is asked for a throwaway depth image of the map's descriptor is created and deleted; if that
  Create fails the map is built at 4096 with one `[ShadowRes]` stderr line and the key is left as the user set
  it. `texelSize` and the texel snap follow `Width()` (1/8192 measured).
- **Shadow PCF width**: a wider kernel would need the same bytecode rewrite on every shadow
  receiver. Possible after FAA-CORE (the rewriter exists) but not ruled; not in this row.
- **Higher-resolution leaf textures**: that would be asset distribution, out under §1 ruling 7. A
  user can drop their own loose textures in, as with any mod; we ship none.

## 8. Rulings recorded

The first sheet's three questions are ruled (§1 items 4-6), plus the content rule (§1 item 7).
Next: FAA-SPIKE.

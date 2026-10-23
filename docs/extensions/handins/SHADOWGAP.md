# SHADOWGAP: the lit band between a boulder and its shadow (2026-10-20)

> **Verdict: RETAIL behaviour, not a port regression and not an extension.** Retail shows it (the user
> confirmed it in retail, `rockmshadow.PNG`, and this session's retail capture shows the same boulder),
> our `/profile:faithful` shows it, and the all-extensions profile (8192 sun map) shows it at the same
> width. Nothing changed in game or engine code. A `fix.` / `ext.` row is proposed below for the user's
> ruling; it is not built.

Base `gpuframe` 487011762 (branch `worktree-agent-a1a9fa08d83d546b9`). Report: `rocknotonground.PNG`
(user, 2026-10-20), a boulder in a birch forest whose cast shadow starts below its bottom edge.

## 1. Screenshots (session scratchpad, not in the tree)

Scratchpad = `C:\Users\USPC\AppData\Local\Temp\claude\C--code-GIT-untitled-rev-eng-project\2a850983-1e23-4e43-8367-f0bb106761ee\scratchpad\`

| File | What it shows |
|---|---|
| `retail_03.png`, `retail_03_crop2.png` | RETAIL `Application-x64.exe` (DX9 interface, see §5), the same pause-menu scene as the user's `rockmshadow.PNG`: the same boulder, lit ground under its base, shadow below |
| `L1_faithful_z12.png`, `L1_faithful_z12_crop.png` | ours, `/profile:faithful` (every ext and fix off, 2048 sun map), seed 12345, pin `33.5,33.2,12`: the boulder row east of the start town. Lit wedge between the left boulder's bottom edge and its shadow; the small stone on the right has a detached shadow |
| `L2_allext8192_z12.png`, `L2_allext8192_z12_crop.png` | ours, the user's per-entry profile (every ext on, `ext.max_shadow_size=8192`), same pin: the same wedges, same width, sharper edges |
| `X_faithful_cull_n_crop.png`, `X_faithful_cull_b_crop.png` | EXPERIMENT (temporary local hack, reverted, never committed): faithful with the shadow pass's `CULL_FRONT` replaced by `CULL_NONE` / `CULL_BACK`. The shadow meets the base everywhere, and shadow acne (diagonal moire stripes) appears on the ground and the rocks |

## 2. Bisect

| Leg | Sun map | Gap |
|---|---|---|
| retail (DX9) | 2048 | present (user: also present in retail) |
| ours `/profile:faithful` | 2048 | present |
| ours per-entry, every ext on | 8192 | present, same width |
| ours faithful, shadow cull NONE (experiment) | 2048 | gone, acne |
| ours faithful, shadow cull BACK (experiment) | 2048 | gone, acne |

The per-extension bisect (max_shadow_res 4096 / 8192, better_trees, billboard_shadow, ambient_occlusion)
was not run: the faithful baseline already has the gap and the all-on leg does not widen it, so no
extension introduces it. The gap is the same at 2048 and 8192, so it is not measured in shadow texels.

## 3. Mechanism

Read from a RenderDoc capture of the faithful frame (`scratchpad\rdc\sg_faithful.rdc`, D3D11 pipeline
state per draw, `scratchpad\rs_dump.py`) and the render-state conversion:

- **The shadow pass draws the rocks with `CullMode = FRONT` and NO depth bias** (DepthBias 0,
  SlopeScaledDepthBias 0; the rock draws at events 316/320/325, position-only VS). The terrain chunks are
  drawn the same way. The main pass draws the same meshes with `CULL_BACK`. These values come from the
  material render states in the user's installed `.rsc` data, converted faithfully at
  `src/render/dx11/renderstatedata.cpp:364` (cull) and `:368-372` (bias); the shadow pass itself is
  `SunLight::DrawShadowMap`, `src/engine/graphics/lights_vtable.cpp:625`. Only two render states in the
  whole run carry a bias (0.0001 / 1677 D24 units, slope 0.41). In the shadow pass that one is used,
  with `CULL_BACK`, only by other meshes (event 338, a swaying-vertex mesh, and 362), not the rocks.
- **The receivers compare unbiased.** The terrain VS writes `shadowProjection[0] * worldPos` (no normal
  offset) and the PS takes a 3x3 `sample_c_lz` PCF against that depth (installed shaders, read in the
  capture). The bias matrix (`lights_vtable.cpp` `BiasMatrixA`) has no depth offset.
- So retail avoids shadow acne by culling front faces in the shadow pass instead of biasing: only the
  faces that point AWAY from the sun write depth. A ground point is shadowed only if the ray toward the
  sun crosses one of those faces. The boulder meshes are open at the bottom and rest on (or in) the
  terrain; where the shadow-side wall's bottom rim stands above the ground, a ray from the ground just
  past the base enters the shell through the open bottom and leaves through a sun-facing face, which is
  culled, so no back face is crossed and the ground stays lit. That is the band: its width is the rim's
  height times the cotangent of the sun elevation, a world-space amount, which is why 8192 does not
  change it. Drawing both faces (the experiment) closes it immediately and shows the acne the culling
  was there to prevent.

## 4. What a row would do (for the user's ruling; NOT built)

`fix.shadow_contact` (or `ext.shadow_contact`), class A (render only, no sim or save effect), under
`src/ext/shadow_contact/`, seam in `renderstatedata.cpp`'s rasterizer build for shadow-pass states
(a cull-FRONT, depth-writing state):

- In the sun shadow pass, draw opaque casters with `CULL_BACK` (sun-facing faces, so the base rim is
  closed by the sun-facing wall) or `CULL_NONE`, and add a depth bias to replace the acne protection the
  culling gave: a small constant term plus a slope-scaled term, scaled to the sun map size
  (`ext.max_shadow_res` changes the texel 4x, so one value does not fit both).
- Visual cost / risk: acne or light leaking if the bias is too small; peter-panning (a new, smaller gap)
  if it is too large; tuning needed at 2048 / 4096 / 8192 and at low sun. Terrain self-shadowing on hill
  faces changes slightly (terrain is a caster with the same state). Fill cost of the shadow pass rises
  modestly (both faces or the other face set). Foliage (alpha-tested, `CULL_NONE` already) is untouched.
- Alternative with less risk: keep `CULL_FRONT` and add only a negative slope-free constant bias toward
  the light for rock / building materials, which pulls the back-face depth forward. It narrows the band
  but cannot close it where the rim is high.

## 5. Notes

- Retail's DX11 interface (`VideoInterface=1`) crashes at boot on this machine (AV at
  `VideoDX11-x64.dll+0x260f`, `ImageBufferData::AttachColorBuffer` with a null swap-chain colour
  buffer; two minidumps in the scratchpad; the two crashed processes, PIDs 91888 and 167824, could not
  be killed). It ran with `VideoInterface=0` (DX9) and `VideoMultisample=0`; the retail registry key was
  exported first and re-imported after (VideoInterface=1, VideoMultisample=3 again). The DX9 path culls
  from the same `.rsc` render states.
- `BANISHED_CAMERA_PIN=33.5,33.2,12,180` (yaw 180) rendered only the sky on this pin, while yaw 90 and
  the default framed the ground. Not investigated (harness or a camera edge case).
- `BANISHED_SCREENSHOT_AT` wrote no `.bmp` on the perf exe in this session; window captures were used.
- Game dir left as found: `OpenBA.exe` md5 a4f18159... restored, `settings.cfg` restored byte for byte
  (md5 ce0b31b8...), lock released.

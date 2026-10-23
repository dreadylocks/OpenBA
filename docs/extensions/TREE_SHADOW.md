# TREE_SHADOW: the deciduous canopy's rotating shadow edge (diagnosis and design)

Opened 2026-10-16 from the user's screenshot (`C:/Users/USPC/Downloads/tree.PNG`). In it, an oak
or birch canopy has a straight light/dark boundary across its upper part (the user's dashed
line), and the boundary turns as the camera orbits. This is phase 1, diagnose and design only:
nothing is built. The kit's `.srsl` is cited as a reference by file and function name, and none
of it is copied here (FOLIAGE_AA §1 ruling 7).

## 1. Verdict

**Billboards are the cause, but the cards cast their shadow correctly.** Retail already does what
SpeedTree and Unity do: in the sun shadow pass the cards face the sun, with a roll that depends
only on the sun direction. So the canopy's cast shadow on the ground does not depend on the
player camera.

The defect is on the receiving side. In the colour pass the same cards are rebuilt facing the
player camera, and each fragment then looks up the shadow map at that viewer-facing position.
The canopy is therefore compared against a sun-facing copy of itself that sits somewhere else.
Two things follow:

- Each card is cut through its centre by its own sun-facing twin. The half of the card that lies
  farther from the sun is shadowed by the card itself.
- Every viewer-facing card lies in the screen plane, so across the canopy the depth along the sun
  direction changes linearly on screen. The lit/shadowed boundary is therefore a **straight line
  in screen space**: the screen-plane trace of a plane perpendicular to the sun. Its angle is set
  by where the sun projects onto the screen, so it **rotates as the camera orbits**. That is the
  screenshot.

## 2. Evidence

**The card basis comes from a constant.** The kit's `Material/Billboard/BillboardMaterial.srsl`,
function `Billboard()`, takes the card's right, up and forward axes from `gc.worldToCamera` rows
0, 2 and 1. The sway term and the tilt term (`forward * cameraNormal * 0.5`) use the same matrix,
with `cameraNormal = worldToCamera3x3 * normal`. It needs no other camera input: `cameraPosition`
is not used. The vertex shaders `Depth.base` (shadow caster) and `Normal.vs` (colour) both call
it. `Normal.vs` then computes **both** `output.position` and
`output.shadowProjection = lc.shadowProjection[0] * worldPosition` from that one billboarded
`worldPosition`.

**Which camera is bound in each pass** (static read):

- `SunLight::AddDrawItems` (`src/engine/graphics/wave28_lane_partsys.cpp:889`) passes
  `BeginDrawGroup(_renderBuffer, sceneCam, sunCamera, 1, 1)`. The sun camera goes into the
  group's **`_cullCamera`** slot.
- `Renderer::DrawItemList` (`src/engine/graphics/batchc_gfx_leaves.cpp:689`) always calls
  `context.SetCamera(view, proj)` from **`group._cullCamera`**.
- `Context::SetCamera` (`src/engine/video/context.cpp:456`) writes `_inverseView` (=
  `gc.worldToCamera`) and `_viewProjection`.
- `SunLight::DrawShadowMap` (`lights_vtable.cpp:625`) ends in `DrawItemList(context, a4, 0, a5)`,
  so the shadow draws run with the **sun camera's** `worldToCamera`. The main pass runs with the
  player camera's.
- The sun camera's basis is `Transform3::FromLookAt(origin, _direction)`
  (`src/engine/graphics/sun_camera.cpp`, `GetSunCamera`). It depends on the sun direction only.
  The view-dependent parts (the fitted sphere's centre and radius, the texel snap) touch only the
  translation and the frustum, and `Billboard()` reads neither.

**The capture agrees** (`C:/GOG Games/Banished/rdoc_foliage/s1_summer_street.rdc`, faithful,
summer, seed 12345, `town,25`). `_GlobalConstants_` (cb0, ResourceId 223) bytes 64..127
(`worldToCamera`):

| Draw | Pass | VS | worldToCamera rows 0..2 (xyz) | Projection |
|---|---|---|---|---|
| eid 655 (oak cards, 52 instances, 2048 D24S8, no RT) | sun shadow | 953 (4 inputs + instance id) | (-0.5, -0.366, -0.785) / (0.866, -0.211, -0.453) / (0, -0.906, 0.423) | orthographic (w column 0,0,0,1) |
| eid 1061 (the same cards, 2560x1377 ms4) | colour | 945 | (1, 0, 0) / (0, 0.814, 0.581) / (0, -0.581, 0.814) | perspective |

The shadow draw's matrix is the sun basis (one axis horizontal, as `FromLookAt` gives). The colour
draw's matrix is the player camera (yaw 0, pitch about 35°). The colour draw samples the 2048 sun
map at t2.

**Which models use billboards.** Per the kit's `Models/.../MaterialInstance/*.rsc`:

- `BillboardMaterial` (deciduous `Normal_ps` / `Depth_ps`): `OakTree`, `BirchTree` and the eight
  fruit and nut trees.
- `BillboardEvergreenMaterial` (`Normal_psevergreen` / `Depth_psevergreen`): **Blueberry only**.
  The conifers are not billboards (they are Foliage / Snow alpha-test meshes) and are untouched by
  anything below.

Both billboard materials share **one** vertex shader, `BillboardMaterial.srsl:Normal_vs` (and
`Depth_vs`), so blueberry bushes have the same defect at a smaller scale.

## 3. What others do, and how each option fits this defect

Research summary (sources at the end):

- **Light-facing shadow cards.** Unity rotates SpeedTree billboards to face the light in the
  shadow-caster pass. The 3ds Max and POV-Ray answers use a second, invisible, light-facing set of
  planes. Banished **already does this**, so it is not the fix.
- **The receiver mismatch is a known, usually unsolved, artefact.** Unity has a tracker entry,
  "billboard shadowy artifact appears on tree billboard at certain camera viewpoint", closed
  *Won't Fix*. Unreal users report tree cards "cast shadows onto themselves", worked around with
  shadow offsets or by not casting at all.

| Option | Removes the rotating edge? | Ground shadow | Cost | Verdict |
|---|---|---|---|---|
| (a) Light-facing cards in the shadow pass | no: retail already does it | unchanged | 0 | already the case |
| (a') Viewer-facing cards in the shadow pass (bind the player's `worldToCamera` for billboard shadow draws: a pure constant seam, no shader rewrite) | yes: caster == receiver | **worse**: the cast shadow follows the camera. Cards seen side-on from the sun thin to slivers, so the canopy's ground shadow swells and shrinks as you orbit | ~0 | rejected (moves the artefact onto the ground) |
| (b) Fixed world basis (sun azimuth or crossed planes) for the caster | no: the receiver is still viewer-facing | stable | 0 | rejected |
| (c) Proxy caster (ellipsoid or octahedral impostor) | no: same receiver mismatch, and the terminator becomes a curve on a plane | rounder | medium + a new mesh | rejected |
| (d) More bias, PCF or blur on the canopy | no: the line moves or softens, but stays straight and rotating (the receiver depth is still a linear sheet on screen) | unchanged | small | rejected as a fix |
| **(e) Receiver-consistent lookup**: keep the viewer-facing card for the clip position, and compute the shadow lookup from the same card rebuilt **sun-facing**, exactly as the caster was | **yes** | **unchanged (the caster is not touched)** | ~20 VS ALU per canopy vertex | **recommended** |

How (e) looks:

- **Inside the canopy:**
  - A fragment at card-local (a, b) looks up the shadow at the caster's own (a, b). Its card can
    no longer shadow itself, and the depth matches the stored depth up to the retail bias, as for
    any flat surface.
  - Shadow from cards above it, from neighbouring trees and from buildings is kept.
  - The dappling becomes a fixed function of the sun, the same at every camera angle.
- **Through the day:** the pattern follows the sun as the cast shadow does today, slowly and
  smoothly. No camera dependence is left.
- **Brightness:** the canopy gets **brighter** on average than retail, because the half-card
  self-shadow and the dark band under the line are gone. Its top is lit as a sun-facing surface.
  The user's look check should compare summer and autumn.
- **DepthAlpha 0.25:** not involved. That cutoff belongs to `OpaqueMaterial.srsl:DepthAlpha_ps`
  (Foliage and AlphaTest casters). The billboard casters use `Depth_ps` at
  `(a - season) - 0.5` (FOLIAGE_AA §2), and (e) leaves every caster unchanged.
- **ext.max_shadow_res:** independent. A sharper map makes the retail line crisper, and (e)
  removes it at any size.
- **ext.foliage_aa:** independent. FAA rewrites pixel shaders; (e) rewrites one vertex shader.
  They compose, with no shared blob. (RETIRED 2026-10-17, handins/BETTER-TREES.md: the edge dither
  needs the billboard pixel shaders FAA rewrites, so both rows became the one row `ext.better_trees`
  and the two pixel rewrites are chained on one blob in one stage.)
- **ext.eye_camera:** independent. It changes the shadow fit, not the sun basis.

## 4. The seam for (e)

### 4.1 The sun basis into the colour pass, with no new cbuffer

The colour-pass vertex shader needs the shadow pass's exact `worldToCamera`. It has a free carrier:
**`LightConstants.textureProjection`** (cb2 rows 6..9 in `constants.srsl` order: color,
ambientColor, position, nearPlane, radius, texelSize, textureProjection, shadowProjection[4]).

- No kit shader reads it: a grep of `ToolKit/resource/Material/**/*.srsl` finds only its
  declaration.
- No engine body writes it. It is `Context::LightStaging::_unwritten60` (+0x60..+0x9f,
  `src/engine/video/Context.h:554`), zeroed by `Context::Reset` and uploaded whole with the light
  block (`context_bind.cpp:386`, dirty bit 0x2000).

**Engine-side seam** (DESIGN §3 rule 5, null by default):

- What: a `static const bool* SunLight::_extBillboardShadow` (the name is for the coordinator to
  pick).
- Where: read in `SunLight::DrawLight` (`src/engine/graphics/lights_vtable.cpp:566`), the
  colour-pass light setup, right next to `WriteShadowMatrix`.
- What it writes when on:
  `_unwritten60 = Matrix(cullCamera._cameraToWorld expanded as DrawItemList does).Inverse()`. That
  is the same `cullCamera` object the shadow pass bound, and the same `Math::Matrix::Inverse` that
  `Context::SetCamera` runs, so the floats are bit-identical to the shadow draw's
  `gc.worldToCamera`. It then sets `_dirty |= 0x2000`.
- Faithful cost: one null test per sun light per frame.
- Mods: a mod shader that reads `textureProjection` under the sun would see this value instead of
  zero, which is a negligible risk (the row is default OFF). Put it in the hand-in.

`SunLight::DrawLight` runs in pass 2 (the emit). Under **ext.render_pipe** that is the render
thread, and it reads only the packet's draw group camera plus a `const bool*`, so no new
cross-thread state is added.

### 4.2 The vertex-shader rewrite (over the user's installed DXBC, FAA's pattern)

The basis is baked into the VS from `cb0[4..6]`, and `Normal_vs` uses one `worldPosition` for both
outputs, so a constant swap alone cannot split them. A rewrite is needed:

- **Where:** at `VertexProgramData::PlatformCreate` (`src/render/dx11/shaderprograms.cpp:127`),
  with the replacement bound in `VertexProgramData::Bind` (`:173`). It mirrors FAA's
  `PixelProgramData` seam and reuses `src/render/dx11/better_trees/dxbc.{h,cpp}` (container parse,
  SM4 token walk, emit, checksum).
- **Match:** a VS with an output register written by `dp4` against `cb2[10..13]`
  (`shadowProjection[0]`) whose input slice contains reads of `cb0[4..6]` (`worldToCamera`) in the
  `Billboard()` shape: two `normalize`s (dp3 / rsq / mul) of `row + forward * cameraNormal * 0.5`,
  then the `mad`s into the position.
  - This is `Normal_vs` DETAIL1..3. DETAIL0 has no `shadowProjection`, so there is nothing to do.
  - `Depth_vs` has no `cb2` reads and is left alone, so the caster stays retail.
  - Any blob that does not match exactly is left alone and logged once.
- **Transform:** a **dataflow-slice clone with operand substitution**.
  - Take the backward slice of the `shadowProjection` output.
  - Clone every instruction in it that depends, directly or transitively, on `cb0[4..6]`. That
    covers `cameraNormal`, the sway offset (its `dot(cn, cn)` is basis-invariant but cloned for
    safety), `right`, `up` and the final position. In the clones, `cb0[4..6]` becomes `cb2[6..8]`
    and the destinations are fresh temps.
  - Repoint the four `shadowProjection` `dp4`s at the cloned position.
  - `output.position`, the lighting normal, fog and texcoords keep using the original
    viewer-facing values.
  - Fix-ups: `dcl_temps` grows by about 4, plus the instruction lengths, chunk sizes and checksum.
    `cb2` is already declared at 14 or more rows (indices 3 and 10..13 are used), so rows 6..8 are
    in range. The spike verifies the declared size.
- **Billboard identity:** the match is the key (no names at create time; FAA-SPIKE §6.1).
- **Evergreen:** because the VS is shared, the rewritten VS reaches **Blueberry** too. Two choices
  for the coordinator:
  1. Accept it (recommended): the same defect, the same fix, a tiny model.
  2. Exclude it: bind the rewritten VS only when the `VideoProgram`'s pixel program is the
     deciduous `Normal_ps`. FAA's matcher already tells the two apart by the season `mad`.
     `Depth_psevergreen` and every conifer are untouched either way.
- **Device reset:** the replacement `ID3D11VertexShader` is re-created with the device, as FAA's
  are.

### 4.3 The extension surface (the FAA / AO precedent)

- **Row:** `ext.billboard_shadow` (name to confirm), class A, default OFF.
  - It changes only the colour-pass shading of billboard canopies: no sim, no save, no caster.
  - It is a bool row; no valued key is needed.
- **Code layout:**
  - `src/ext/billboard_shadow/`: `Sync()`, the seam set and clear, and `enhanced_entry.cpp` for
    the Enhanced graphics tab (`src/ext/enhanced_tab/`), as `src/ext/foliage_aa/enhanced_entry.cpp`
    does.
  - `src/render/dx11/better_trees/` (beside the foliage files): the slice-clone
    rewriter and its stage.
  - Unit tests on **synthetic** SM4 blobs built in the test, with no retail bytes (the FAA-CORE
    rule).
- **Menu:** one checkbox or Off/On combo in the Enhanced graphics tab, "Stable tree shading", plus
  the settings.cfg key through the registry, as every row has.

## 5. Plan

| Block | Owner | What | Done when | Effort |
|---|---|---|---|---|
| **TS-SPIKE** | opus, worktree, nothing merged but the hand-in | Disassemble `Normal_vs` DETAIL1..3 from a capture (VS 945 in `s1_summer_street.rdc`; the street and overview variants). Confirm the `Billboard()` slice shape and the `cb2` declared size. Confirm cb2 rows 6..9 are zero in every colour draw today. Then a throwaway hard-wired rewrite of the one VS, with the `DrawLight` write. | Hand-in shows: (1) the instruction list to clone and the substitution, (2) a camera-pin pair at two yaws, faithful vs spike, where the faithful line rotates and the spike's does not, (3) no acne on top cards (bias adequate), (4) cast ground shadow unchanged (trace_oracle: only the canopy colour draws differ) | 0.5 day |
| **TS-CORE** | opus | The generic matcher and slice-clone rewriter in the dx11 tree, synthetic-blob unit tests, the `VertexProgramData` seam, the `SunLight::DrawLight` seam, the row, device-reset handling, render_pipe on and off | `tools/build.sh run` clean. **Row off == faithful** (`tools/trace_oracle` 0 on every screen, the FAA-CORE standard). Row on: `[BillboardShadow] matched 3 / 3 blobs` (or 6 if the variants are duplicated per material), only the billboard colour draws differ, the camera-pin yaw pair shows a stable canopy, live toggle and device lost are clean, the stress pair is recorded (CLEAN machine, RTSS stopped), unit tests pass, gate 0, nothing retail in `git diff --stat` | 1 to 1.5 days |
| **TS-MENU** | sonnet (FAA-MENU is the pattern) | The Enhanced-tab entry, Apply / Cancel, settings.cfg round trip | The checkbox changes the live frame, the key round-trips | 0.25 day |
| **TS-LOOK** | the user | Summer, autumn and winter (bare canopies: the season cutoff runs in the caster and receiver alike), street and overview, with foliage_aa and max_shadow_res on and off | the user's ruling (default stays OFF) | by hand |

Verification notes:

- Frames are not reproducible, so a pixel diff measures nothing. The row-off check is
  trace_oracle. The visual claim is a **camera-pin pair at two azimuths** of the same canopy, row
  off vs row on, where the row-on terminator must not turn (skill `camera-pin`).
- One game run at a time with the game-dir lock, `--jobs 2`, and `tools/boot_state.sh --wait 300`.

## 6. Recommendation

Build option (e), the receiver-consistent shadow lookup:

- Leave the caster alone (it is already correct and view-independent).
- Feed the colour pass the shadow pass's exact sun `worldToCamera` through the unused
  `textureProjection` light constant (one engine seam in `SunLight::DrawLight`).
- Rewrite only `BillboardMaterial.srsl:Normal_vs` so that `shadowProjection` uses the sun-facing
  rebuild of the card.

This removes the rotating straight edge and the half-card self-shadow while keeping retail's ground
shadow byte for byte. Class A, default OFF, about 2 days including the spike.

Do **not** "fix" the caster: making shadow cards face the viewer (a') moves the camera dependence
onto the ground shadow.

## 7. Status (2026-10-16)

**Approved and built.** The coordinator approved (e) with three decisions: blueberry shares the fix;
the row is `ext.billboard_shadow` "Stable tree shading", class A, default OFF; the carrier and the
seam are as in §4.1.

TS-SPIKE, TS-CORE and TS-MENU are done on branch `worktree-agent-af0b0326b58439bc0` and are **not
merged**. The evidence is in `handins/TS-CORE.md`:

- **One shader.** The game has one billboard colour vertex shader (`a88a6da1`, its DETAIL variants
  byte-identical). Its `cb2` is declared with 14 rows. The rewrite clones 45 slice instructions,
  seeded by 8 `mov`s, with temps 8 -> 16.
- **The carrier.** The sun's worldToCamera reaches the colour pass bit-identical to the shadow
  pass's.
- **The look.** The camera-pin pair at yaw 0 and yaw 90 shows retail's turning cap line, and no
  line with the row on.
- **No acne** on the top cards.
- **trace_oracle.** With the row on, only the vertex shader of the 8 billboard colour draws differs
  (0 findings once mapped back). With the row off, the result is faithful (0 findings against
  FAA-CORE's base).
- **Clean runs:** the live toggle, device lost under render_pipe, the gate run, unit 1232/0, and
  acceptance.

The look ruling (TS-LOOK, §5) is the user's.

**TS-DAPPLE (2026-10-16 night, the coordinator):** TS-FIX2's margin removed the self-shadow altogether and the user
ruled the flat crowns wrong ("I like the og game's look, it's just the hard clipping on the crowns"). The lookup
now keeps retail's own position and scatters each card's cut per card (a hash of the card centre as a depth
offset in half extents): the dapple stays, the straight edge dissolves. Three immediates (basis / margin / jitter,
shipped 0 / 0 / 0.5) are the overlay's live sliders. `handins/TS-CORE.md` "TS-DAPPLE".

## Sources

- Unity SpeedTree manual: billboards are rotated to face the light in the shadow caster pass.
  https://docs.unity.cn/560/Documentation/Manual/SpeedTree.html
- Unity tracker, "billboard shadowy artifact appears on tree billboard at certain camera
  viewpoint" (Won't Fix).
  https://issuetracker-mig.prd.it.unity3d.com/issues/billboard-shadowy-artifact-appears-on-tree-billboard-at-certain-camera-viewpoint
- Unity forum, tree billboards aligned to camera direction.
  https://discussions.unity.com/t/unity-tree-billboards-rotate-with-camera/593924
- Autodesk 3ds Max, particle trees (a separate light-facing shadow-only plane set).
  https://help.autodesk.com/cloudhelp/2016/ENU/3DSMax-Tutorial/files/GUID-A6D2C10C-6AEC-4167-9310-C3F41F036BCB.htm
- POV-Ray newsgroup, animation with billboards (camera-facing visible set plus light-facing
  invisible set). https://news.povray.org/4a2ae3c5%40news.povray.org
- Unreal forum, tree cards self-shadowing (shadow offset or no-cast workarounds).
  https://forums.unrealengine.com/t/lightmass-disable-self-shadowing-on-static-meshes/381408
- Unreal forum, SpeedTree camera-facing leaves.
  https://forums.unrealengine.com/t/speedtree-for-vr-2/43877

**TS-FIX (2026-10-16 night).** The user's check found the row-on crowns lit on the wrong side. The
§1 / §4 lookup at the sun-facing twin T is self-consistent, but it carries the drawn card offset
from the viewer's axes onto the sun card's axes, which point elsewhere. So every card was lit on its
lower half.

The lookup is now P' = P moved along the light onto the card's caster plane. It keeps retail's
light-space xy and takes the caster's depth. Details and evidence: `handins/TS-CORE.md` "TS-FIX".

**TS-FIX2 (2026-10-16 night).** The user's second check still showed a soft self-shadow band on the
sun side. The plane solve was confirmed to be the cause: at a sun-side crown top it looked the
shadow up 0.48 world units behind the crown's own caster.

The lookup now keeps retail's light-space xy and takes the card centre's depth moved toward the
light by the card's half extent: `P + (dot(C - P, L) - |C - P|) L`. That cannot self-shadow at any
heading.

The §4.1 carrier (`textureProjection` and its `SunLight::DrawLight` seam) is no longer used, and both
were removed. The row is now a single D3D-side seam. The details, the pixel debug and the
five-heading contact sheet are in `handins/TS-CORE.md` "TS-FIX2".

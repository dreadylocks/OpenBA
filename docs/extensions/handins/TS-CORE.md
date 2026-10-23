# TS-CORE -- `ext.billboard_shadow` "Stable tree shading" (hand-in, 2026-10-16)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/render/dx11/billboard_shadow/` and `src/render/dx11/foliage_aa/` moved to `src/render/dx11/better_trees/` (file names kept); `tests/unit/ext_billboard_shadow_test.cpp` and `ext_foliage_aa_test.cpp` merged into `ext_better_trees_test.cpp`, cases `ext_bbs_*` / `ext_faa_*` renamed `ext_better_trees_*`.)

Blocks TS-SPIKE + TS-CORE + TS-MENU of `docs/extensions/TREE_SHADOW.md` §5. The design is option (e),
TREE_SHADOW.md §4, which the coordinator approved, with these decisions: blueberry shares the fix; the
row is `ext.billboard_shadow`, class A, default OFF; the carrier is `textureProjection` (cb2 rows
6..9); the engine seam is a null-by-default `const bool*` read in `SunLight::DrawLight`.

- **Branch:** `worktree-agent-af0b0326b58439bc0`, fast-forwarded to main `cf16b65ff` first.
- **Commits:** `2a49a0174` (the TREE_SHADOW.md design) and `d8f3d731f` (the code). Not merged.
- **Spike verdict: PASS.** The spike's prototype is the general rewriter itself. It met every
  done-condition, so the CORE work went on from the same code.

## 1. What was built

| Piece | Files | What |
|---|---|---|
| The rewriter (pure) | `src/render/dx11/billboard_shadow/receiver_rewrite.{h,cpp}` | See "The rewriter, in detail" below. |
| The stage (D3D) | `src/render/dx11/billboard_shadow/billboard_shadow_stage.{h,cpp}`, `billboard_shadow_backend.h` | See "The stage, in detail" below. |
| D3D seam | `src/render/dx11/shaderprograms.cpp` | `VideoDX11::g_extBillboardShadow`, null by default. With the row off, `VertexProgramData::Bind`, `PlatformCreate` and `~VertexProgramData` each pay one atomic load and a test. |
| Engine seam | `src/engine/graphics/SunLight.h`, `sunlight.cpp`, `lights_vtable.cpp` | `SunLight::_extBillboardShadow` (`const bool*`, null by default and under the faithful profile). See "The engine seam, in detail" below. |
| Registry row | `src/ext/registry.{h,cpp}`, `registry_boot.cpp` | `ext.billboard_shadow`, class A, default OFF. Label "Stable tree shading". Description "Leaf cards read the shadow where they cast it: no line turning with the view." (77 characters, sized like the other rows). Boot installs the engine seam once (null under faithful) and runs `BillboardShadow::Sync`. Every toggle (`SetAndRecord`) runs `Sync` too. |
| The extension | `src/ext/billboard_shadow/billboard_shadow.{h,cpp}` | `Sync()` is `BbsBackend::Request(Ext().billboard_shadow)`. |
| Menu (TS-MENU) | `src/ext/billboard_shadow/enhanced_entry.cpp`, `src/ext/enhanced_tab/enhanced_tab.{h,cpp}`, `src/ext/ui/Dialog/EnhancedOptions.rsc` | A fifth row "Stable tree shading" in the Enhanced tab: combo Off / On (string table `offOnItems`; table height 4 -> 5). Apply calls `SetAndRecord` when the row changes. This follows the FAA-MENU pattern. |
| Unit tests | `tests/unit/ext_billboard_shadow_test.cpp`, `tests/unit/sources.cmake` | See "The unit tests, in detail" below. |
| Harness | `src/game/Game/camera_input.cpp`, `src/app/options.h` (comment only) | `BANISHED_CAMERA_PIN` takes an optional yaw: `x,y,dist,yaw` or `town,dist,yaw`. It pins the heading for the azimuth pairs. This extends an existing flag; no new flag was added. |
| Run script | `tools/billboard_shadow/run_arm.sh` | A copy of `tools/foliage_aa/run_arm.sh` that writes to `rdoc_tree/` under lane `tsspike`. |

### The rewriter, in detail

**What it matches.** A blob must meet all of these:

- It is a vs_4_x program and its container round-trips byte-identical.
- It has no flow control, no indexable temps, no extended opcodes, and `ret` appears exactly once,
  as the last instruction.
- There is one SHADOW OUTPUT: the output register written from `cb2[10..13]`.
- That output's component-exact backward slice reads `cb0[4..6]`, and never reads `cb0` with a
  relative index.
- `cb2` is declared with at least 10 rows and `cb0` with at least 8.

**What it emits.**

- `dcl_temps T` becomes `2T`.
- At the first slice instruction that depends on `cb0[4..7]`, it inserts `mov r[N+T], r[N]` for every
  temp.
- From that point, it clones every slice instruction beside the original. In the clones:
  - temps are renamed, including the temps inside relative indices (`cb3[r8.w + k]`);
  - `cb0[4..7]` becomes `cb2[6..9]`;
  - the instructions that write the shadow output are replaced by their clones.
- Every emit re-parses itself and checks its checksum and instruction count, or it is dropped.

### The stage, in detail

- It keeps one copy per `VertexProgramData` variant that matches.
- Copies are made at `PlatformCreate` once the row is on, or lazily at the first bind.
- A copy is bound only while `SunLight::_extBillboardShadowLive` is set. That flag is the emit's
  latch: `DrawLight` sets it, on the same thread, right after it writes the rows.
- A device change releases every copy; they are re-made lazily.
- An off request is applied at the next vertex bind: everything is released and the seam is cleared.
- One mutex guards the state.
- Each unmatched candidate is logged once: `[BillboardShadow] untouched <hash> v<i>: <why>`.

### The engine seam, in detail

When the row is on, `DrawLight` writes into `Context::LightStaging::_unwritten60` (cb2 rows 6..9).
What it writes is the sun group camera's `view.Inverse()`, built exactly as `DrawItemList` builds its
`view` and inverted exactly as `Context::SetCamera` inverts it. So the floats are the shadow draws'
`gc.worldToCamera`, bit for bit. It then sets dirty bit 0x2000 and the latch. When the row goes off,
it zeroes the rows once.

**The mod caveat.** No retail shader reads `textureProjection` and no retail body writes it; in
retail it is always zero. A mod's vertex shader that read `lc.textureProjection` while the sun light
is bound would see the sun camera's worldToCamera instead of zero, but only while this row is on.
Turning the row off restores zero. The row is OFF by default, so a player who never turns it on
sees no difference.

### The unit tests, in detail

The blobs are synthetic vs_4_0 programs. There are three tests:

- **Match and meaning.** Three filler lengths, each run with and without a renamed relative `cb3`
  read. A tiny SM4 interpreter runs the original and the rewritten program and checks:
  - `o0` and `o1` are unchanged;
  - `o3` equals the original's `o3` with `cb0[4..7] := cb2[6..9]`;
  - `o3` really moved.
- **Refusals.** It must refuse:
  - a blob with no shadow output;
  - a shadow output that does not depend on the camera rows;
  - `cb2` declared too small;
  - flow control;
  - a relative `cb0` read;
  - a pixel shader with the same body;
  - a broken checksum.
- **The registry row.**

## 2. The exact rewrite (spike question: instructions, cb2 size)

The game has one billboard colour vertex shader: `BillboardMaterial.srsl:Normal_vs`, checksum
`a88a6da1754867b0`, 4260 B. The four DETAIL variants are byte-identical: one hash, three copies
rewritten (v1..v3). DETAIL0 has no shadow output and is refused by the same grammar, as are
`Depth_vs` and every other vertex shader. The run logs no other candidate.

Its declarations are `cb0[12]`, `cb2[14]` and `cb3[208]` (dynamic). Rows 6..9 are inside `cb2[14]`,
so no declaration had to grow.

The log line:

```
[BillboardShadow] rewrote a88a6da1754867b0 v1: 4260 B -> 5740 B (temps 8 -> 16, shadow o3,
  45 slice instructions cloned from #34, 12 worldToCamera reads -> cb2[6..9])
```

What RenderDoc's disassembly of the bound copy shows (eid 1101 of `ts_on_y0.rdc`, 153 lines):

- 8 seeding `mov`s after instruction 20.
- Each `Billboard()` instruction is paired with its clone. Two examples:
  - `mad r12.xyz, _lc_.textureProjection[0].xyzx, r11.xxxx, r12.xyzx` beside
    `mad r4.xyz, _gc_.worldToCamera[0].xyzx, r3.xxxx, r4.xyzx`;
  - `mov r13.x, cb3[r8.w + 0].w` beside `mov r5.x, cb3[r0.w + 0].w`.
- The clip position still comes from `r0`, as in retail.
- `o3` now comes from `r8`: `add o3.xyzw, r8.xyzw, _lc_.shadowProjection[0][3].xyzw`.
- The driver accepted it: `CreateVertexShader` returned S_OK, with 0 create failures in every run.

## 3. Evidence, per done-condition

**Machine and setup**

- Windows 10, NVIDIA, 2560x1377, 4x MSAA, `VideoShaderDetail=3`, sun map 2048.
- Debug exe md5 `0161c766...`. That build is commit `d8f3d731f`; the commit changed no code after
  the build.
- Every run held `game_dir_lock.sh` as `tsspike`, checked with `assert-held` against this build.
- `settings.cfg` was backed up at the grant (md5 `b365a548...`) and restored byte-identical at the
  release, checked with `cmp` and md5. The game had written `VideoSettings=true` on the last run;
  that was put back.
- Arm configs:
  - row off and row on: every `ext.*` and `fix.*` row 0 except `ext.billboard_shadow`, plus
    `GameShowWeather=false`;
  - faithful: `profile=faithful`.
- Common settings: seed 12345, `BANISHED_FORCE_SPEED=10`.
- Captures, PNGs and logs are in `C:/GOG Games/Banished/rdoc_tree/` and were never committed.

**Why weather is off.** The first summer frames came out overcast with rain, which drops the sun
shadow to near nothing. Two runs at the same seed and frame came out with different weather, so it
is not deterministic. `GameShowWeather=false` (retail's own Options checkbox) keeps the sun out for
the look pairs. `[BillboardShadow]` behaves the same with weather on (frame 20000 run, `ts_on_y0`).

| Done-condition | Result |
|---|---|
| **Spike: the instructions and the cb2 size** | §2. |
| **Spike: the carrier is free** | Row off: cb2 rows 6..9 are all zero in a main-pass draw (eid 806 of `ts_off_y0_f12k`). Row on: they hold the sun basis. At canopy colour draw eid 1101, `lc` bytes 96..159 are hex-identical to the shadow-pass billboard draw eid 668's `gc` bytes 64..127 (`01 00 00 bf 8c 75 e1 be ...`). The colour pass therefore reads the caster's exact matrix. |
| **Spike: the two-angle camera-pin pair** (`town,25,0` and `town,25,90`, summer frame 12000, crops of the same oak and birch grove: `crop_{off,on}_y{0,90}.png`) | Row off shows the user's defect at both yaws. Every canopy has a bright cap above a sharp, nearly straight boundary, with the rest dark. The boundary runs one way at yaw 0 and another way at yaw 90, so it turns with the view. Row on, at both yaws: no cap and no line, only card-sized dappling lit from the sun side, with the same pattern from both headings. The ground shadows of the canopies are visibly the same off and on (the caster is retail's). |
| **Spike: acne on the top cards** | None. A 2x zoom of the top cards row on (`zoom_on_y0.png`) shows lit card patches and no texel-scale stripes or speckle. Retail's depth bias is enough: the receiver position is the caster position, so they differ only by ulp-level differences in instruction order between `Depth_vs` and the clone. |
| **Spike: trace_oracle, only the canopy colour draws change** | Row on vs row off, control off/off2, all at `town,25,0` frame 12000. **Raw:** S1 66 shared / 3 one-sided per side, S2 28 / 2 per side, S3 / S4 / S5 / S6 = 0. The one-sided keys are the rewritten vertex shader `DXBC:5740:a632eac3` in 8 draws, which pair with the two billboard pixel shaders (deciduous 2788 B, evergreen 2596 B). **Mapped** (each rewritten vertex shader replaced by the retail one, `build/ts/map_vs.py`, not in the tree): **0 findings on all six screens**. S1 69/69, S2 30/30, S5 64/64, S6 5/5, every S3 verdict field 0.0%. Nothing but the vertex shader of those 8 draws differs. |
| **Row off == faithful** | `profile=faithful` capture of this exe (frame 2000, `town,25`) vs FAA-CORE's base `faa_off_base` (control: `faa_off_base` / `faa_off_ours`): **0 findings on every screen**. S1 69/69 shared, S2 30/30, S5 64/64, S6 5/5, S3 every verdict field 0.0%. The log shows `ext.billboard_shadow=0` and 0 `[BillboardShadow]` lines. |
| `tools/build.sh run` (the gate's faithful autostart) | Clean: autostart new game, frame 10130 in game, crash log 0 bytes, `ext.billboard_shadow=0`, 0 `[BillboardShadow]` lines. |
| **Live toggle, one session** (`ts_live_toggle.error.log`) | Main menu, row off at boot, `BANISHED_AUTOMENU_BUTTON=4` opens Options; the Enhanced tab is the restored tab, and real mouse clicks drove it. Off -> On: `request on: seam set`, `Apply: item=1 active=1`, `rewrote ...`, `first rewritten bind`. On -> Off: `request off`, `stage reset (off): 72 blobs seen, 3 rewritten, 0 create failures, 3202 of 86453 vertex binds rewritten`, `stage off: copies released, seam cleared`. Off -> On again works. Crash log 0. The menu backdrop's autumn orchard shows the terminator bands with the row off (`live_off_c.png`) and an even, dappled canopy with it on (`live_on_c.png`). |
| **Device lost, row on, render thread on** (`ts_devlost_user.error.log`) | The user's own settings (render_pipe, AO, foliage_aa, max_shadow_res 8192, everything on) plus the row, `BANISHED_FORCE_DEVICE_LOST=3000`. Injected on the render thread, `recreate OK`, `stage reset (device changed): 120 blobs seen, 3 rewritten, 0 create failures, 3269 of 94873 vertex binds rewritten`, `device-lost round trip COMPLETE`, then `first rewritten bind` on the new device. Ran to frame 17353, crash log 0. |
| **render_pipe on and off** | Off: every look and oracle arm (`ext.render_pipe=0`). On: the device-lost run above (`ext.render_pipe=1`, `DrawLight` and the vertex binds both on the render thread; 3269 rewritten binds before the loss, rebinds after). |
| Unit tests | `tools/build.sh unit`: **1232 tests, 0 failures**, including the three `ext_bbs_*` tests. |
| Acceptance | `tools/acceptance/acceptance.sh`: 0 lines off target. |
| Enhanced-tab entry, settings round trip | `[EnhancedTab] Enhanced tab added: tabs=5 entries=4`, `[BillboardShadowMenu] combo=... item=0`. Apply records `ext.billboard_shadow` through `SetAndRecord` (Persistence) and the next Open reads the live value. The loose `.crs` were compiled by `tools/ext_resources.sh` (font check passed) and deployed. |
| **settings.cfg round trip** (`ts_roundtrip.error.log`) | Combo On + Apply. The game rewrote `settings.cfg` itself, and the key moved into its sorted place as `ext.billboard_shadow=1`. A fresh boot on that file logged `ext.billboard_shadow=1 class A`, `request on: seam set`, the rewrite, `first rewritten bind`, and opened the combo at `item=1`. Crash log 0. |
| **Stress pair** (perf exe `5317e685...`, `STRESS_EXT`, compact town, speed 10, 6000 frames, RTSS / Afterburner not running) | Both legs CLEAN per the runner's machine-load check: `ts_bbs_off2` (`perf/results/20261016_200741_*`) and `ts_bbs_on` (`20261016_200442_*`). Steady state, off -> on: sim 0.16 -> 0.16 ms/iter, draw CPU 0.97 -> 0.98 ms, frame 1.10 -> 1.11 ms, **911 -> 901 fps (-1.1%, min window 893 -> 864)**. This is within run-to-run noise at ~900 fps: about 45 extra vertex-shader instructions per vertex on the billboard draws only. A first off leg (`20261016_200234_ts_bbs_off`, 843 fps) ran while another agent's compiler was active (BUSY) and is not used. |

## 4. The look, for TS-LOOK

- **Brightness.** With the row on, the canopy is darker on average than retail's top cap, and
  brighter than retail's dark lower band. The light falls in patches where a card is not covered,
  toward the sun, by another card. That is the light-facing caster's own self-shadow, now
  consistent.
- **The menu orchard.** In autumn the row-on canopies read as evenly lit.
- **The summer oaks** at `town,25`: noticeably more interior shade than retail's lit cap.
- **The user's ruling** may want this, or a bias toward lighter. A future tweak could push the
  receiver a fraction of a card toward the sun: one immediate in the clone. It is not built.
- **Seasons.** The season cutoff runs identically in caster and receiver (the pixel shaders are
  untouched). Winter's bare canopies have nothing to shade.

## 5. Nothing retail entered the tree

- `git diff --stat main..HEAD`: 24 files, all source, docs, tests and one script. No blobs, no
  `.srsl` / `.rsc` copies, no captures, no textures.
- The test blobs are assembled token by token in the test.
- The only `.rsc` touched is our own `src/ext/ui/Dialog/EnhancedOptions.rsc`.
- TREE_SHADOW.md cites the kit's `BillboardMaterial.srsl` by file and function and quotes none of
  it.
- The rewriter reads the user's installed bytecode at run time and writes nothing out.

## 6. Merged with main (`3395b162a`)

main had moved: `ext.seamless_loops` (block SEAMLESS) had been merged. The conflicts were all
"both sides added a row", and both were kept:

- the registry rows and fields;
- the boot seam installs;
- the Enhanced-tab contributions;
- `EnhancedOptions.rsc`, where the tab now has six rows, "Stable tree shading" last.

Checks after the merge:

- The debug build is clean, and the unit tests are **1239 / 0**.
- Resources were recompiled and deployed. The menu shows six rows, with the row restored as On from
  settings (`entries=5`).
- An in-game run under the user's settings plus the row (render_pipe on): `rewrote ...`,
  `first rewritten bind`, frame 7495, crash log 0.

`settings.cfg` is back to md5 `b365a548...`. The game dir holds this branch's debug exe and its
loose `.crs`.

## TS-FIX -- the canopies were lit on the wrong side (2026-10-16 night)

### The report

The user checked the build after the merge (`40926d998`) and compared row off with row on at the
same spot (`C:/Users/USPC/Downloads/treeshadow.png`):

- With the sun from the east, the evergreens and the retail crowns were lit on the east.
- With the row on, the deciduous crowns were lit on their lower / west side.

### Checks 1-3 from the coordinator's list

All three passed. The rewritten shader computes exactly the caster's card. The defect was in the
design.

**1. The carrier holds the shadow pass's matrix.** I compared the uploaded buffers, not the C++
matrices.

- Colour-draw `lc` bytes 96..159 were already hex-identical to the shadow draw's `gc` bytes 64..127
  (§3).
- Re-read from `ts_on_y0_f12k.rdc` (eid 1116), the card code's reading of the carrier is:
  - R = (W0.x, W1.x, W2.x) = (-0.500, 0.866, 0)
  - f = (W0.y, W1.y, W2.y) = (-0.496, -0.286, -0.820), equal to `lc.nearPlane`, the sun direction
  - U = (W0.z, W1.z, W2.z) = (-0.710, -0.410, 0.573)
- That is an orthonormal sun basis, with no transpose and no inverse.

**2. The clone reads the same rows in the same roles.** The disassembly of the bound copy pairs every
`_gc_.worldToCamera[k].c` with `_lc_.textureProjection[k].c`, with the same k, the same component
and the same operand position (§2).

**3. The caster has no extra sign.** `Depth_vs` and `Normal_vs` share `Depth.base` (the kit's
`BillboardMaterial.srsl`), and NoCull does not move vertices.

### The cause: the twin is the caster's point, but not the fragment's

The spike looked the shadow up at T(a, b) = C + a R' + b U', the caster's own point at the drawn
fragment's card offset. That is self-consistent: no card shadows itself. But it carries the offset
(a, b) from the viewer's axes onto the sun card's axes, and the two frames are unrelated.

With the capture's numbers:

- The viewer's up U_v = (0, 0.579, 0.815) points north.
- The sun card's up U = (-0.710, -0.410, 0.573) points up and AWAY from the sun.

So the upper half of every drawn card read the shadow map at points displaced toward the crown's
shadowed side (higher cards nearer the sun cover them in light space), and the lower half read
points displaced toward the sun. Every card was lit on its lower side, whatever the sun's azimuth.
The effect turns with the view the same way, so the user saw it as a mirror about the sun.

### The fix (`receiver_rewrite.{h,cpp}`)

The lookup is now the drawn position P moved along the light direction onto the card's caster plane:

```
P' = P - f * ((P - T) . n) / (f . n),    n = 0.5 (N . R) R + 0.5 (N . U) U - f
```

**What P' keeps and takes.**

- It keeps P's light-space xy: retail's own lookup point, physically where the fragment is.
- Its depth is that of the card's own caster at that point. So the half-card self-shadow and the
  straight terminator stay gone, and the sun side of the crown is lit, because its casters are the
  front ones.

**Where n comes from.** n is the caster plane's normal, derived from the card code's construction
(right' ~ R + 0.5 (N . R) f, up' ~ U + 0.5 (N . U) f). Then n . right' = n . up' = 0 for an
orthonormal (R, f, U), which the capture shows.

**How the rewriter finds its inputs.**

- **N** is the temp source of the first camera-dependent instruction (`mul r4.xyz, r3.yyyy, cb0[5]`,
  so r3). It must stay unwritten until the lookup.
- **P** is the single temp the `lc.shadowProjection[0]` instructions read with replicated swizzles
  (r0).
- **The insertion point** is right before the first shadow-matrix instruction (#93). The block is 21
  instructions in three extra temps, so `dcl_temps` is 8 -> 19 and the blob grows 4260 -> 6320 B.

**New refusals.**

- The card code's `0.5` tilt must be present, or the blob is refused.
- A blob that already reads `lc.textureProjection` (a copy this rewriter made) is refused.

**The unit test** now checks the meaning, using a random orthonormal sun basis and an identity
shadow matrix:

- the shadow output equals the P' formula;
- it differs from P;
- it moved only along f (its R and U components equal P's).

### Evidence (debug exe `6266f5b0...`, lock `tsfix`, settings.cfg md5 `a12735f9...` restored byte-identical)

| Item | Result |
|---|---|
| The rewrite | `rewrote a88a6da1754867b0 v1: 4260 B -> 6320 B (temps 8 -> 19, ..., lookup projected along the light from P r0 onto the caster plane (N r3) at #93)`, 0 create failures. |
| Sun (this frame) | `lc.nearPlane` = (-0.504, -0.291, -0.813). The sun is about 30° from +x toward +y, elevation 54°. On screen: from the upper left at `town,25,0` (house shadows fall right) and from the right at `town,25,90` (house shadows fall left). |
| **Sun-consistency shot** (evergreens and deciduous trees in frame) | Row on: `rdoc_tree/tsfix_on_y90.png`; row off: `rdoc_tree/tsfix_off_y90.png`; crops `cropfix_on_y90.png` / `cropfix_off_y90.png`. With the sun from the right, the conifers are lit on their right, and with the row on the oak crowns are lit on their upper right and shaded lower left. That is the same side as the conifers and as the row-off cap, now without the cap's straight line. |
| **The two-heading pair** | yaw 0: `cropfix_{on,off}_y0_right.png` (full frames `tsfix_{on,off}_y0.png`); yaw 90: the crops above. Row off: the bright cap and its sharp line, turning with the view. Row on: no line at either heading, and the lit side follows the sun (upper left at yaw 0, upper right at yaw 90), not the screen. |
| Acne | `zoomfix_on_y90.png` (2x): none; lit leaf patches only. |
| trace_oracle, on vs off | Rewritten vertex shader `DXBC:6320:3d540d93` mapped back to retail: **0 findings on all six screens** (S1 69/69, S2 30/30). Only the vertex shader of the 8 billboard draws differs. |
| **Row off == faithful** | `profile=faithful` frame-2000 capture vs FAA-CORE's `faa_off_base` (control base / ours): **0 findings on every screen** (S1 69/69, S2 30/30, S5 64/64). |
| Gate run (`tools/build.sh run`, faithful autostart) | frame 10614, crash log 0, 0 `[BillboardShadow]` lines. |
| render_pipe on + device lost (user settings + row) | `stage reset (device changed): ... 3262 of 91681 vertex binds rewritten`, `device-lost round trip COMPLETE`, rebinds after, crash log 0. |
| Unit / acceptance | 1239 / 0; 0 lines off target. |

**The spike's mistake.** It judged "the line no longer turns" but never checked the lit side against
the sun. From now on, every look pair for this row carries the sun direction and a conifer in frame.

## TS-FIX2 -- a conservative margin instead of the plane solve (2026-10-16 night)

### The report

The user's second check (`treeshadow2.png`, new left, old right) still failed. Trunk shadows fall
to the lower right, so the sun is upper left. The birch crown's sun side carried a wide, soft dark
band, and only its lower front was lit: "it looks like there's self shadowing still". The user also
said that without turning the camera the agent could not judge the angle. TS-FIX had been checked at
two headings only, and (found this time, see the harness note) its "yaw 0" arms had not been pinned.

### The formula (`receiver_rewrite.{h,cpp}`)

Keep the fragment's own light-space xy (the texel retail samples). Take the depth of the card
centre, moved toward the light by the card's own half extent:

    P_lookup = P + (dot(C - P, L) - k |C - P|) L,      k = 1

- `P`: the drawn (camera-facing) vertex position. Retail feeds it to `lc.shadowProjection[0]`.
- `C`: the card centre. It is the camera-independent temp source of the first camera-dependent `add`
  that writes P's xyz (retail: `add r0.xyz, r0.xyzx, r2.xywx`, disassembly line 62). The rewrite copies it into a
  new temp just before that add.
- `L`: `lc.nearPlane.xyz` (cb2 row 3), the sun direction, unit length, pointing from the sun to the
  ground. Retail's own lighting reads it as `-dot(N, L)`.
- `|C - P|`: the vertex's distance from its centre, that is, the corner offset times the card
  scale. At a corner it is the card's half-diagonal. It is exactly "the scale the card code applies
  to the corner offsets, times the half-diagonal", per vertex, with no constant tied to the kit's
  card sizes.

So `depth(P_lookup) = depth(C) - k |C - P|`. The card the shadow pass drew faces the sun, apart from
the card code's tilt. Each axis leans toward the view row by at most `0.5 / sqrt(1.25) = 0.447`, so
the caster's own depth stays within `0.447 (|w.x s.x| + |w.y s.z|) <= 0.632 |C - P|` of its centre.
The camera-facing card's axes are not quite orthogonal either (cos <= 0.2), which raises the bound to
0.71 of `|C - P|`. Therefore k = 1 can never self-shadow, at any heading. A card is still shadowed by
casters more than its half extent in front of its centre: the outer cards of the same crown, the
neighbouring trees.

**Units, checked in the capture.** At every canopy draw, cb2 rows 10..12 have `.w = 0` and row 13
has `.w = 1`: the projection is orthographic. The depth column is `(-0.00194, -0.00112, -0.00322)`
(`t2_on_y0`). Its length is 0.00392, and normalised it is `(-0.495, -0.286, -0.821)` = L. So depth is
linear along L at 0.00392 per world unit: a move along L changes only the depth, and r is in world
units.

**Emitted.** One `mov r8.xyz, r0.xyzx` before that add, then six instructions before the first
shadow-matrix instruction: `add`, `dp3`, `sqrt`, `dp3` with `lc.nearPlane`, `mad`, `mad`. The three
shadow-matrix reads of P are pointed at the result. `dcl_temps` 8 -> 10, and the copy grows from
4260 B to 4468 B (the plane version was 6320 B, temps 19). The log line:
`rewrote a88a6da1754867b0 v1: 4260 B -> 4468 B (temps 8 -> 10, shadow o3, centre saved at #74, lookup = P r0 moved along the light to the centre's depth minus 1.00 |C - P| at #93)`.

**Dropped: the sun-basis carrier.** The rewrite no longer reads cb2 rows 6..9 and no longer clones
the camera slice, so these are removed:

- the `SunLight::DrawLight` write: `ExtWriteBillboardShadowBasis`, `SunLight::_extBillboardShadow`
  and `_extBillboardShadowLive`, plus its install in `registry_boot.cpp`;
- the stage's latch check.

The row is now one seam (`VideoDX11::g_extBillboardShadow`). cb2 is retail's again with the row on:
`textureProjection` stays zero, so the mod caveat of §1 is gone. The rewritten shader reads only
constants that retail supplies to the retail shader, so the stage binds it whenever it exists.
The matcher also refuses a relative `gc` read now, since which rows it reads is unknown.

### Pixel debug: a fragment at the top of a sun-side crown

The fragment is pixel (2560x1377 back buffer) (170, 120) at `town,25,0`, frame 12000, on the
big birch at the upper left. The sun is upper left, so this is the crown's sun-side top. The same
draw primitive (93) and the same shadow texel (1045.6, 474.3) of the 2048² sun map were taken in two
captures. One is from this build (`t2_on_y0`). The other is from main's build `f21ffac9...`, the
TS-FIX plane solve, run once with the same pin and seed (`plane_on_y0`). The tool is
`build/ts/pixdbg.py` (RenderDoc DebugPixel on the last passing fragment, plus `PickPixel` on the map;
not in the tree).

| Build | Lookup depth (v3.z) | Map depth at the texel | Lookup minus map | PCF lit fraction (9 taps) |
|---|---|---|---|---|
| TS-FIX2 margin | 0.513101 | 0.516223 | **-0.00312** (0.80 world units in front of the caster) | **1.0** |
| TS-FIX plane solve | 0.518131 | 0.516232 | **+0.00190** (0.48 units behind the caster) | **0.0** |
| retail, row off (`t2_off_y0`, prim 103, texel 1043.4, 474.1) | 0.517590 | 0.516476 | +0.00111 | 0.0 |

Read with 0.00392 depth per world unit. Both texels have the same xy (to 0.02 texel), so the
margin's xy really is retail's. Only the depth moved.

At the crown's sun-side top, the plane version looked the shadow up half a world unit behind the
crown's own caster, so that fragment was fully shadowed. That is the user's band. The margin version
lands 0.8 units in front and is fully lit.

The plane solve fails because its caster plane uses the card normal N reconstructed per vertex. On
the top cards, N tilts away from the light, so the plane falls behind the caster that the shadow
pass really drew.

### The yaw sweep

The sweep covers `town,25` at yaw 0 / 90 / 180 / 270, plus a low-pitch street-level view (`town,20,45`,
min zoom, PitchUp held to the manual-pitch limit). Each heading has a row-on and a row-off capture
at frame 12000, summer, `GameShowWeather=false`, seed 12345, every other ext / fix row 0. Every
frame has conifers.

The sun from the log (`lc.nearPlane` of each capture) ranges from `(-0.495, -0.286, -0.821)` to
`(-0.511, -0.295, -0.808)` (the sun moves a little over the day): the sun stands about 30° from +x toward +y, elevation 54° (z is up).

**Contact sheet:** `C:/GOG Games/Banished/rdoc_tree/TSFIX2_contact_sheet.png`. It has one row per
heading, row off on the left and row on on the right, and each crop holds deciduous crowns next to
conifers. The full frames are `rdoc_tree/t2_{on,off}_{y0,y90,y180,y270,street}.png`.

What the sheet shows, heading by heading (read by the agent, plus 1:1 crops for yaw 0 and yaw 180):

- **yaw 0** (sun upper left). Off: the birch shows the dark smudged band across its middle. On: the
  birch is lit evenly from the sun side, and its far (lower-right) edge is slightly darker from
  retail's N·L.
- **yaw 90** (sun from the right of the frame). Off: dark streaks and patches through the birch.
  On: evenly lit, no band.
- **yaw 180** (sun from the lower left). Off: only small lit caps on the upper left of each oak; the
  rest is dark. On: the oaks are lit on the left (the conifers' lit side). They darken gently to the
  right and bottom, toward the shadowed ground, which is retail's N·L term, and they keep the
  shadows cast by their neighbours.
- **yaw 270.** Off: curved dark terminators inside each crown. On: even, lit.
- **street level, yaw 45.** Off: the deciduous crown at the right is mostly dark. On: lit, with
  the same neighbour shadows as the ground.

At every heading the row-on crowns are lit on the side where the conifers are lit. No band, line or
gradient follows the heading: what darkening is left points away from the sun at every heading.

**Trade-off, said plainly.** With k = 1, cards in the same crown rarely shadow each other. The
crowns now read as evenly lit leaf masses, shaded by retail's N·L and by the shadows of other
trees, rather than as dappled. That is the price of "cannot self-shadow at any heading". k is one
constant (`BbsRewrite::kMargin`). The tilt bound allows going down to about 0.71 to bring back some
inner-crown shading. That is not done here; it is a look call for the user.

**Harness note (the camera pin).** `BANISHED_CAMERA_PIN` runs in `PlayerToolManager::UpdateCamera`.
That function returns early while the application is paused, and an unfocused window pauses it. Two
row-off arms of the first sweep, and TS-FIX's `tsfix_on_y0` / `tsfix_off_y0`, never pinned: no
`[CameraPin]` line, and the frame is the new-game default camera. `build/ts/arms3.sh` now brings the
window to the front once RUNNING (`build/ts/pitchkey.ps1`) and checks for the `[CameraPin]` line.
Every sweep arm above has it.

### Evidence (debug exe `0cfc7e32...`, lock `tsfix2`, settings.cfg md5 `8ec17148...` restored byte-identical)

The user's file had `ext.billboard_shadow=1` since the last session. It was backed up at the grant
and put back with `cmp` identical.

| Item | Result |
|---|---|
| Unit | `tools/build.sh unit`: **1239 tests, 0 failures**. `ext_bbs_match_and_meaning` now checks `P' = P + (dot(C-P, L) - k |C-P|) L`, the depth `depth(C) - k |C-P|`, and the unchanged xy across L, over 6 random seeds × 6 program shapes. It expects temps 6 -> 8 and that the copy is not rewritten twice. |
| trace_oracle, row on vs row off | `t2_on_y0` vs `t2_off_y0`, control `t2_off_y0` / `t2_off2_y0`, all pinned at `town,25,0`, frame 12000. The rewritten VS `DXBC:4468:e98cc719` maps back to retail `DXBC:4260:14dfd352` (8 draws). Mapped: **0 findings on all six screens**, 262 aligned pairs. cb2 contents no longer differ (no carrier). |
| Row off == faithful | `profile=faithful` frame-2000 capture (`t2_faithful_f2k`) vs FAA-CORE's `faa_off_base` (control base / ours): **0 findings on every screen**. |
| Acceptance | `tools/acceptance/acceptance.sh`: 0 lines off target. |
| `tools/build.sh run` (faithful autostart) | New game autostarted, frame 21656, crash log 0, `ext.billboard_shadow=0`, 0 `[BillboardShadow]` lines. |
| Live toggle, one session (`rdoc_tree/t2_live_toggle.error.log`) | Main menu, row off, Options -> Enhanced, real clicks. Off -> On: `request on: seam set`, `Apply: item=1`, `rewrote ...`, `first rewritten bind`. On -> Off: `stage reset (off): 72 blobs seen, 3 rewritten, 0 create failures, 3089 of 83402 vertex binds rewritten`, `stage off: copies released, seam cleared`. Off -> On again: `request on`, `first rewritten bind`. Crash log 0. |
| Device lost, render_pipe on (`rdoc_tree/t2_devlost.error.log`) | The user's settings with the row on and `ext.render_pipe=1`, `BANISHED_FORCE_DEVICE_LOST=3000`. Injected on the render thread, `recreate OK`, `stage reset (device changed): 120 blobs seen, 3 rewritten, 0 create failures, 3265 of 94760 vertex binds rewritten`, `device-lost round trip COMPLETE`, then `first rewritten bind` on the new device. Ran to frame 11198, no crash log. |

## TS-TWEAK -- the margin k as a live slider in the debug overlay (2026-10-16 night)

The user wants to choose k by eye. **Debug flavour only**, behind `BANISHED_TWEAK_OVERLAY` (the
overlay's own CMake option, so it is OFF for og / perf / release and Linux). k has **no settings.cfg
key**: the user picks a value, and that value then becomes the constant `BbsRewrite::kMargin` in
`receiver_rewrite.h`. The overlay's bottom lines print `bbs kMargin` to copy from, and its "Write to
settings.cfg" button logs it in `[TweakOverlay] constants: ... bbs margin`.

### What was added

FAA's k is the model:

- `src/ext/tweak/ext_tweaks.{h,cpp}`: `Ext::Tweak::Bbs { margin = BbsRewrite::kMargin }`, with
  `BbsValues()` / `SetBbs()` (copied under a mutex, the same pattern as `Ao` / `Faa`).
- `Ext::BillboardShadow::Sync` (`#if BANISHED_TWEAK_OVERLAY`): `BbsBackend::RequestMargin(Tweak::BbsValues().margin)`
  before `Request(row)`, so turning the row on builds the copies with the current k.
- The stage (`#if BANISHED_TWEAK_OVERLAY`): `RequestMargin` records the k (main thread). The next
  `BindVertex` (on the binding thread, under the stage mutex) sees `margin != reqMargin` and goes
  through the existing reset path, `ReleaseAll(s, "margin changed")`, the one a device change uses.
  The copies are then re-made with the new immediate, lazily, once per program: one re-create per
  change, never a per-frame rebuild. `AppliedMargin()` feeds the overlay's stage line. The rewrite
  log is keyed per blob per k, so each new k logs its `rewrote ... minus <k> |C - P|` line once.
- `receiver_rewrite.{h,cpp}`: `TransformWithMargin(m, out, k)` (`#if BANISHED_TWEAK_OVERLAY`). Both it
  and `Transform(m, out)` call one internal `TransformK`, and `EmitMargin` writes `-k` as the
  immediate. The unit tests and the non-overlay builds call only `Transform` (k = `kMargin`).
- The overlay (`src/diag/win/tweak_overlay.cpp`) has a new **Billboard shadow** panel:
  - the row's checkbox (`billboard_shadow` also joined the Rows panel);
  - the slider "Billboard shadow margin k (self-shadow-free >= 0.71)": 0.0 .. 1.5, snapped to 0.05,
    default 1.0, applied on release (`Act::BbsSync` -> `Ext::BillboardShadow::Sync` on the main thread);
  - a **default** button beside the slider;
  - the line "stage: copies made with k ... (BbsRewrite::kMargin 1.00)".

  **Reset defaults** resets the k too.
- `docs/reference/tweak_overlay.md`: the panel row, and the row added to the Rows list.

The perf and release builds keep only the constant. The only lines that changed outside
`#if BANISHED_TWEAK_OVERLAY` restructure code without changing what it does: `Transform` now calls
`TransformK(m, out, kMargin)`, `EmitMargin` takes k as a parameter, and the stage's log names a local
`k`. That is the same code with the constant passed in, but I did not run the `objcopy
--strip-debug` byte comparison of the perf exes, so it is not proven byte-identical. The perf flavour
builds clean.

### Evidence (debug exe `6aac4646...`, lock `tstweak`, settings.cfg md5 `8ec17148...` restored byte-identical)

Runs were in game, autostart, seed 12345, `town,25,0`, with every other ext / fix row 0 and the
weather off. Overlay input was real: `build/ts/altf10.ps1` (Alt+F10), `build/ts/drag.ps1` (mouse
drags on the slider) and `click.ps1`.

| Item | Result |
|---|---|
| Unit / build | 1239 / 0 tests. The debug and perf flavours build (no new warnings). |
| Drag across the range (`rdoc_tree/tw_slider.error.log`) | Seven changes (six drags, then the button): 1.00 -> 0.45 -> 0.00 -> 1.50 -> 0.70 -> 0.45 -> 0.50 -> 1.00 (**default**). The first drags of the session landed on the panel background and moved the window instead; they changed nothing and logged nothing. Every change logged exactly one `request margin k <v>`, one `stage reset (margin changed): ... 3 rewritten, 0 create failures`, and one `margin k <a> -> <b>: copies re-created`. A k seen for the first time adds one `rewrote ... minus <k> |C - P|` line. No re-create between releases, no create failure, crash log 0 bytes. |
| Device lost at a non-default k (`rdoc_tree/tw_devlost.error.log`) | Set k = 0.45 with the slider, then `BANISHED_FORCE_DEVICE_LOST=40000`. Injected, `recreate OK`, `stage reset (device changed): 80 blobs seen, 3 rewritten, 0 create failures`, `device-lost round trip COMPLETE`, `first rewritten bind` on the new device, and the overlay's DX11 backend re-initialised. The stage line afterwards still read "copies made with k 0.45" (`tw_devlost_c.png`), so the k survives the device change. **Reset defaults** then logged `request margin k 1.000`, the reset, `bbs margin 1.00` in the constants line, and one re-create. Crash log 0 bytes. |
| Crop pair k = 0.5 / 1.0 | `C:/GOG Games/Banished/rdoc_tree/TSTWEAK_k050_k100.png`, from the overlay-closed screenshots `tw_k050.png` / `tw_k100.png` of one session (the sun moves a little between them). At k = 0.5 the oak crowns (upper right) get some inner-crown depth back: their lower and inner cards darken where outer cards lie in front. The birch (upper left) begins to show faint soft patches on its sun side. At k = 1.0 the crowns are evenly lit, as in the TS-FIX2 sheet. This is in line with the tilt bound: below about 0.71 a card can start to catch its own caster. |

## TS-DAPPLE -- retail's self-shadow kept, the cut scattered per card (2026-10-16 night, the coordinator)

The user's third check pass (TS-FIX2 merged, the k slider in hand): "nothing actually looks good ... I like the
og game's look, it's just the hard clipping on the crowns that's off". TS-FIX2 (and every margin k) removes the
self-shadow altogether, which is why the crowns went flat. What reads wrong in retail is only that every card's
cut lies on one plane (perpendicular to the sun, through each card centre), so the cuts line up into one straight
edge that turns with the camera; the half-card self-shadow itself is retail's dapple.

**The lookup now** (`receiver_rewrite.h` "THE LOOKUP"): the fragment's own texel, the depth moved along the light by
a per-card amount:

    d = C - P,  r = |d|,  h = frc(dot(C, K)),  K = (12.9898, 78.233, 37.719)
    e = margin + jitter (2 h - 1)
    P' = P + (basis dot(d, L) - e r) L

`basis` 0 measures from P's depth (retail's), 1 from the card centre's (basis 1 / margin 1 / jitter 0 is TS-FIX2);
`margin` shifts every cut toward the light; `jitter` moves each card's cut to a pseudo-random place across its
card (the hash of the card centre, stable per card and per frame, so no flicker). With 0 / 0 / 0 the lookup is
retail's exactly (P' = P). **Shipped: basis 0, margin 0, jitter 0.5** (`BbsRewrite::kDefaultParams`). 10
instructions + the centre mov, `dcl_temps` + 3 (4260 -> 4524 B). The three are the debug overlay's live sliders
(the "Billboard shadow" panel; a "retail (0/0/0)" button), each applied on release through the stage's reset path.

**Evidence** (`rdoc_tree/DAPPLE_j50_sheet.png`, `DAPPLE_j50_zoom.png`, `DAPPLE_j100_zoom.png`,
`DAPPLE_m25j50_zoom.png`; yaw 0 / 90 / 180 / 270 at `town,25`, frame 12000, summer, weather off, seed 12345, every
arm pinned (`[CameraPin]` line), retail left / row on right, 1:1 crops of one crown per heading): at every heading
the retail straight edge (the lit cap over a hard diagonal) is gone; jitter 0.5 keeps the crown's lit-cap / dark-
interior structure closest to retail; jitter 1.0 is the most uniform mottle; margin 0.25 + jitter 0.5 brightens the
caps but leaves a soft gradient where the edge was. The user picks the final values by eye (the sliders).

**Gate** (main, this commit): unit 1239 / 0 (the meaning test mirrors the hash in the interpreter's float order),
acceptance 0, castmetric 2, abi_audit OK, faithful run clean, resave PASS == golden. Row off is untouched (the
rewrite body is the only change). Nothing retail entered the tree.

**The user's ruling (2026-10-16 night, by eye with the sliders, orbiting):** margin -0.05, jitter 0.4 ("0.35 -> 0.4 jitter
look the best"); a few sharp edges remain on small crowns made of few large cards (`billbordedge.PNG`: within one
card the cut is still a straight line, the jitter only moves it; a per-card TILT of the cut would need a
camera-independent per-vertex hash, i.e. the vertex's uv input in the matcher -- a possible follow-up, not taken).
**Locked in: `kDefaultParams{0, -0.05, 0.4}`.**

## TS-HASH -- the "per-vertex uv hash" follow-up checked, premise found false (2026-10-17, the coordinator)

**Question.** The ruling above left one residual: inside a single large card the cut is still a straight line.
The idea was that the hash of the card centre C is one value per card, so a per-card TILT would need a
camera-independent per-VERTEX seed (the uv / userData input) in the matcher.

**Finding: the hash is already per vertex.** Read from the user's own vertex buffer in the capture
`rdoc_tree/tsfix_off2_y0.rdc` (canopy colour draw eid 1004, the billboard VS `a88a6da1...`, stride 24:
position int16x4, texcoord int16x4, normal unorm8x4, userData int16x2):

- every card is four vertices whose `userData` (the corner weights, `Billboard()`'s `weights`) is
  (+5836, +5836), (+5836, -5836), (-5836, -5836), (-5836, +5836), i.e. symmetric +-0.71 card units;
- but their `position` is NOT one shared centre: the four base positions form a small rectangle, 0x1c x 0x2e
  model units/512 = 0.055 x 0.090 (times the instance scale), with the corner pattern of the weights.
  Example card at buffer offset 0x9d8: (ffec, fff5, 0b68) / (ffec, fff5, 0b3a) / (ffd0, fff5, 0b3a) /
  (ffd0, fff5, 0b68).

The "centre" C the rewrite saves (the instance-transformed position before the sway and corner offsets) is
therefore different at each corner by 0.05..0.09 world units at unit scale, and `frc(dot(C, K))` with
K = (12.9898, 78.233, 37.719) moves by 0.7..4.3 cycles between adjacent corners: the four corners of one card get
four unrelated jitter values. What the card interpolates is the bilinear blend of four random corner offsets,
so the cut already carries a random slope (and a slight hyperbolic bend) per card. A uv / userData seed would
produce a different random per-vertex value with the same statistics: **it changes nothing structurally.**
The sentence "stable per card" in TS-DAPPLE above is wrong on that point (it is stable per VERTEX and per
frame, which is what matters for flicker).

**Why the residual stays with any vertex-side term.** Across a planar card the shadow lookup depth is the
interpolation of four corner values; a straight (or bilinear) function of the card coordinates can only cut
the caster's constant depth along a line or a shallow hyperbola. On a large card the depth slope across the
card (r |L in-plane|) dominates the +-0.4 r corner spread, so a long, nearly straight self-shadow edge always
remains somewhere in the card. The vertex stage is exhausted.

**What would break the straight line: a per-TEXEL term, which lives in the pixel shader.** The billboard PS
(`a437d0d2...`, 51 instructions) reads the interpolated `shadowProjection` as `v3.xyz` (the compare depth is
`v3.z`, nine `sample_c_lz` PCF taps) and never reads `v3.w`. The sketch, not built:

- VS (this rewriter): write `o3.w = -(r * |lc.shadowProjection[0] row 2 .xyz|)`, the card half extent in depth
  units, negative so a retail VS (w = +1, orthographic) yields amplitude 0 in the PS below;
- PS (a new rewriter): copy `v3` into a temp, `z += (hash(floor(uv * Q)) - 0.5) * noise * max(-w, 0)` with uv =
  the diffuse texcoord input (`v1.xy`, camera-independent, attached to the card so the pattern is world-stable),
  retarget every `v3` read to the temp; `dcl_input_ps v3.xyz -> .xyzw` and the ISGN read mask byte;
- Q sets the blob size (about 8 atlas texels at Q = 64), `noise` the amplitude in half extents; both sliders.
  The hard cut inside a card becomes a leaf-scale jagged edge; shadows from neighbouring cards and trees get
  the same jagged edge (only billboard pixels are affected).
- **Cost that needs the user's ruling:** ext.foliage_aa already rewrites the same billboard pixel shaders
  (`cutout_rewrite`), and TREE_SHADOW §3 promised the two rows share no blob. A PS term means chaining the
  two rewrites on one blob (FAA's output as this row's input, under one of the two stages), i.e. a second
  pixel-side seam or a shared one. Not taken without the user's say.

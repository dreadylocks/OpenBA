# SHADOWCONTACT: a bias cannot close the rock shadow gap (2026-10-21)

> **Verdict: no `fix.shadow_contact` row was built.** Under the ruling's constraints (bias only, no
> cull change, retail everywhere else, no perf cost) no bias closes the gap. A caster depth bias
> leaves the gap exactly as it is. A receiver offset closes it only by sliding every shadow in the
> game toward its caster. The gap comes from where the rocks are placed: on a slope, a boulder's
> open bottom edge sits above the ground on its downhill side (§3). The user asked "are the rocks
> floating above the terrain?", and in effect, yes. The levers that would work are listed in §4
> for a new ruling.

Base `gpuframe` 29f4aeccb. Spec: `SHADOWGAP.md` §3-§4. Experiment code was temporary. It is not
committed; the patch is kept in the session scratchpad as `sc/sctune_experiment.patch`. With it,
both levers could be retuned live in one paused game: the shadow pass's rasterizer bias for the
CULL_FRONT casters with zero bias, and a world-space offset added to the receivers' shadow matrix.

## 1. The sign argument, and why a depth bias is the wrong lever

The sun pass draws casters with front faces culled. Only faces pointing away from the sun write
depth, and the lit pass compares the receiver's light-space depth against that depth (shadowed when
the receiver is farther). Moving the stored depth toward the light needs a negative D3D11
`DepthBias` / `SlopeScaledDepthBias`. A positive one widens the gap. That much matches the brief.

But a depth bias changes only the depth a caster writes, never **which shadow-map texels** it
covers. The lit band is ground where the light ray toward the sun passes **under** the rock's bottom
rim, enters the open shell and leaves through a sun-facing face, which is culled. No away-facing
face lies on that ray, so the band's texels hold no rock depth at all, and nothing written there can
be biased. Receiver-side, a depth offset along the light direction also keeps the same texel. Only
a **lateral** offset changes coverage, and it moves every shadow, not just the rock's.

## 2. Measured (2048 map, seed 12345, pin `33.5,33.2,12`, game paused, sun about 54° high)

Paused frames were checked to be static: two captures 5 s apart, mean pixel difference 0.001.

| Lever | Value | Gap | Side effects |
|---|---|---|---|
| caster `SlopeScaledDepthBias` | -4 | unchanged | self-shadow streaks on the rock faces |
| caster `DepthBias` | -30000 (about 0.0018 depth) | unchanged | the same streaks |
| caster bias (retail's one biased state, negated) | -1677 / -0.5 | unchanged | faint streak on one boulder |
| receiver offset +z (lift) | 0.05 / 0.1 / 0.15 / 0.2 | the whole rock shadow slides toward the rock; the band narrows only as the far end shortens by the same amount | tree and pine shadows shift visibly at 0.3 |
| receiver offset +z | 0.4 | the band is closed, but the shadow is about half its retail size | every shadow and the hill self-shadow move by d·cot(elevation) |
| receiver offset −z | 0.15 / 0.3 | wider band, longer shadows, ground darkens | — |

The "how much of the gap closes" table the brief asked for is therefore: **0 %** for any caster
bias at any value. For the receiver offset, the band only closes by moving the whole shadow, with
the far end shrinking by the same amount, at about 0.4 world units for this boulder. Not tested:
4096 / 8192 (a coverage problem does not depend on the texel size, and §1 holds at any size), a
wide terrain view of the bias legs (they did nothing at the rock), and a low sun. On low sun: the
sun direction differs from launch to launch (z = -0.75 / -0.81 / -0.88 in three boots), so the time
of day moves and a low sun could be reached by running unpaused. It was not needed for the verdict.

Perf: nothing shipped, so the cost is zero. Either lever would have cost one state or constant per
pass, with no added draws.

## 3. Why the rim floats: placement, faithful

`NaturalResourceComponent::PlaceAtCell` (`src/game/Action/action_base_cnr.cpp:253-298`, retail
0x140126540) and the worldgen copy `StartTown_PlaceResourceCell` (`src/game/Game/starttown.cpp:4464-4534`)
set the rock's z to `Map::GetTerrainPos` at the **cell centre**. The x / y jitter (`RandFloatTol() *
_f60`) is applied afterwards without re-sampling the height. Pitch is hard 0, so the rock is not
tilted to the slope. The boulder meshes' bottom ring sits at the model origin: in the capture
`scratchpad/rdc/sg_faithful.rdc`, event 509, the int16 z range is -27..586 with the low ring at -27..13.
So on a slope the downhill half of a 2-unit boulder floats by about slope × half-width (plus the
jitter error), and the band is the ground under that edge. From above, the rock body hides the
air gap. Its shadow does not hide it. Retail places rocks the same way, which is why the user sees
the band in retail too. Trees use the same code, but their trunks are thin, so it does not show.

## 4. Levers that would work (for a new ruling)

1. **Sink the rock at placement time** (render transform only): put z at the lowest terrain height
   under the footprint, or lower it by a fraction of the local slope. This removes the cause. The rock
   would sit slightly deeper (a visible change at the base), shadows stay retail everywhere else, and
   there is no perf cost. It must not touch the serialized transform (a fix never changes the save), so
   it would apply at the render side, where the instance matrix is built, keyed on the rock template.
   Needs a check that the transform is not saved or used by the sim.
2. **Rock materials only: CULL_NONE in the shadow pass, plus a positive slope bias**, i.e. SHADOWGAP §4
   limited to the rock material instead of every caster. This closes the band (SHADOWGAP's
   experiment). Acne would be confined to rock surfaces and controlled by the bias. It is a cull change,
   which the ruling excluded.
3. Leave it as retail.

## 5. Screenshots (session scratchpad `sc/`, not in the tree)

Scratchpad = `C:\Users\USPC\AppData\Local\Temp\claude\C--code-GIT-untitled-rev-eng-project\2a850983-1e23-4e43-8367-f0bb106761ee\scratchpad\sc\`

| File | What |
|---|---|
| `stk1.png` | rock row: off / caster slope -4 / caster const -30000 / lift +0.3 / lift −0.3 / off |
| `stk5.png` | left boulder, 2x: receiver lift 0 / 0.2 / 0.4 / 0.6 (the shadow slides under the rock as a whole) |
| `stk6.png` | birch and pine shadows lift 0 vs 0.3 (the global shift), and the mild caster bias leg |
| `low_off.png` | dist-4 camera: the boulders are open shells (camera near-clip shows the inside) |

Game dir left as found: `OpenBA.exe` md5 a4f18159… redeployed, `settings.cfg` restored byte for byte
(md5 ce0b31b8…), lock released.

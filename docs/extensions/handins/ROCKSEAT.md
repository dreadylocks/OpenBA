# ROCKSEAT: rocks and ore drawn seated on the slope (`fix.rock_seat`, 2026-10-21)

> **Built: `fix.rock_seat` (tier 1, default ON, off under `/profile:faithful`), render only.** Both
> variants the user asked for are behind the one row, picked by the valued key `ext.rock_seat_mode`
> (1 = sink, 2 = tilt). **Recommended and default: 2 (tilt).** Both close the lit band under the
> SHADOWGAP boulders at 8192 and at 2048. On a real hillside, sink buries the small ore pieces
> (one almost disappears in the close view), while tilt keeps every rock's size and leans it with
> the ground. The user picks; switching is one settings key.

Base `gpuframe` c99c60f7c. Diagnosis: `SHADOWGAP.md`, `SHADOWCONTACT.md` §3 (placement at the cell
centre, pitch 0, open-shell meshes). Retail entry: `docs/retail_bugs.md` §7. Inventory row F9.

## 1. What changes, and where

| Site | Retail | With the row on |
|---|---|---|
| `NaturalResourceComponent::PlaceAtCell` (0x140126540, `src/game/Action/action_base_cnr.cpp`) | `meshInstance.SetTransform(transform)` | `Ext::RockSeat::SetMeshTransform(model)` |
| `StartTown_PlaceResourceCell` (`src/game/Game/starttown.cpp`, the worldgen copy) | the same | the same |
| `ModelComponent::OnAdded` (0x140124a30, `src/game/Component/modelcomponent.cpp`) | `mesh->SetTransform(transform)` before the scene add | `Ext::RockSeat::SetMeshTransform(this)` |
| `GameUpdateLoop::SerializeAllBody` load arm (`gameupdateloop_serialize.cpp`), after the post-load passes | nothing | `Ext::RockSeat::SeatAll(m_entityList)` |

`SetMeshTransform` falls back to retail's exact call for anything that is not a rock, for a model with
no mesh, and while no terrain is loaded. A "rock" is an entity whose `NaturalResourceDescription` is
not animated (`_b64`, the falling-tree flag) and is mined with the pick axe (`_u80` = ToolType 1;
`Citizen.rsc`'s tool order Axe, PickAxe, Scythe...; confirmed at run time, every seated model printed
tool=1). In the shipped data that is `NaturalResourceRock` (stone) and `NaturalResourceOre` (iron).
Every other natural resource was checked and left alone: trees (Axe, thin trunks, tilting them would be
wrong), herbs, berries, mushrooms, roots and crops (Scythe, alpha-tested foliage that has no open rim).

**The load pass is needed.** On a load the entity list is read BEFORE the WorldGeometry, so
`OnAdded` (inside `ModelComponent::Persist`) runs with no heightmap and takes the retail arm (26,525
fallbacks on Gate.sav). `SeatAll` then re-seats every in-scene rock (5,271 on Gate.sav) and rebuilds
its node pose with `MeshInstance::Update(Time 0)`, exactly as `OnAdded` does after its SetTransform:
a rock never ticks, so without the Update the draw kept the pose built from the unseated transform
(found by the load screenshot; the first build seated the transform and nothing moved on screen).

## 2. Render only: the readers checked

The fix never writes `ModelComponent::transform` (+0x148). It writes only the model's
`Graphics::MeshInstance` (the `Geometry::_transform` the scene draws, culls and shadows with).

| Reader | Reads | Affected? |
|---|---|---|
| Save: `ModelComponent::Persist` (0x140124b70) | `transform`, `yaw`, mesh group / index, then `MeshInstance::Serialize` | no: `MeshInstance::Serialize` (0x1800ff680) streams the animation state and the material list, never the Geometry transform |
| Map grid, pathing | `MapComponent` tiles (`PlaceAtCell` writes tile, 1x1); `MapDescription _pathType = Obstacle` per cell | no: cell based |
| Harvesting | work actions target the cell; `RemoveResource` (0x1401260f0) spawns the raw material at the cell | no |
| Picking / selection | rock templates have no PickingComponent (`map`, `model`, `naturalresource` only); the map's picking scene is terrain / floors | no |
| Minimap / overhead | `_addToOverhead`, `_mapColor` per cell | no |
| Status icons | none on natural resources | no |
| Other game code reading `meshInstance._transform` | `tutorialhighlightdialog.cpp` (a stderr diagnostic) and `ext/eye_camera` (citizens only) | no sim reader |
| Sim reading `model->transform` of a rock | unchanged value | no |

**Resave gate.** `tools/resave_gate/resave_gate.sh Gate` (faithful, perf exe): NEW-GOLDEN on the first
run in this worktree (the golden is not in the tree here) with md5 `971aaec2cf95...`, the invariant
golden, then **PASS** (resave == golden, 23,453,453 bytes, crash log 0). With the fix ON
(`GATE_PROFILE=` per-entry, class B rows and mods off, `fix.rock_seat=1`, tilt): the resaved image
is **byte-identical** to the same profile with `fix.rock_seat=0` (both md5 `1a94f8e2...`; that
profile differs from the faithful golden through the other class A rows / fixes, not this one). The
user's mods (betterFields, DebugMenu) were disabled for the gate runs through the temporary
settings.cfg (two stuck retail processes from SHADOWGAP hold the `.pkm` files, so they could not be
parked).

## 3. The math (`src/ext/rock_seat/rock_seat_math.{h,cpp}`, pure, unit-tested)

- **Footprint.** The bottom ring is an ellipse in the model's z = 0 plane inscribed in the mesh's
  local AABB (`Geometry::_localBounds`, copied from GraphicsMesh +0x80 by SetMesh) times the instance
  scale (`_color[0]`, 1.0 for rocks): centre ((min+max)/2), semi-axes ((max-min)/2), 16 samples.
- **Heights.** `Map::GetTerrainPos(out, {x, y}, 1, null)`, the query PlaceAtCell itself uses, without
  the floor refine. The rock's jittered x / y is used (retail took the height before the jitter).
- **Sink (1).** z = min over the ring (turned by the rock's yaw) and the centre of the terrain
  height. Rotation unchanged. No ring point is above the ground; the lowest touches it.
- **Tilt (2).** Least-squares plane z = a + sx dx + sy dy through the 16 ring heights and the centre
  (2x2 normal equations, centred, in double; degenerate input = flat). Normal n = normalize(-sx, -sy, 1).
  Tilt = the minimal rotation taking up onto n (axis up x n), capped at 30 degrees (ore spawns up to
  about 41). In the engine's quaternion convention `Rotate(v)` is the textbook rotation by the
  conjugate (`transform.cpp`: `(q * v) * conj(q)` with the reversed product), so the tilt's vector
  part is negated; and `(a * b).Rotate(v) == a.Rotate(b.Rotate(v))`, so the result is `tilt * yaw`
  (yaw kept). Seat: z = min over the ring of (terrain under the turned point - the point's height
  above the origin), and the centre height, minus `TiltSink(f)` = min(0.06, 0.12 x the mesh height)
  (`kTiltSink`, `kTiltSinkFraction`; section 7). A boulder goes 0.06 under its lowest ring point, a
  0.26-tall ore piece 0.031.
- Unit tests (`tests/unit/ext_rock_seat_test.cpp`, 9 cases): footprint from bounds (and the empty
  box), plane fit recovers slopes and refuses collinear input, the normal is perpendicular to the
  plane, `TiltTo(n).Rotate(up) == n` and the cap, the engine product order, flat ground keeps
  retail's transform (x / y / rotation exact), sink on an 8 degree slope (no ring point floats, the
  lowest one touches), tilt on a slope (up = normal, heading kept within 6 degrees, every ring point
  exactly `TiltSink(f)` under the plane), the sink's height cap, the row and the mode key. `tools/build.sh unit perf`:
  **1216 tests, 0 failures**, 1 skipped (the kit cross-check, needs ToolKit output).

## 4. Screenshots (session scratchpad, not in the tree)

`R` = `C:\Users\USPC\AppData\Local\Temp\claude\C--code-GIT-untitled-rev-eng-project\2a850983-1e23-4e43-8367-f0bb106761ee\scratchpad\rs\`

New game, seed 12345, the user's settings (every extension on) with only the keys named changed,
perf exe, paused. The sun direction differs from launch to launch (SHADOWCONTACT §2), so shadow
lengths differ between legs; look at the contact, not the length.

| View | off (retail) | sink (1) | tilt (2) |
|---|---|---|---|
| SHADOWGAP boulder row, pin `33.5,33.2,12`, 8192 map, crop | `R\row_off_8192_crop.png` | `R\row_sink_8192_crop.png` | `R\row_tilt_8192_crop.png` |
| same, full frame | `R\row_off_8192.png` | `R\row_sink_8192.png` | `R\row_tilt_8192.png` |
| boulder row, 2048 map (`ext.max_shadow_res=0`), crop | `R\row_off_2048_crop.png` | `R\row_sink_2048_crop.png` | `R\row_tilt_2048_crop.png` |
| iron-ore hillside, pin `-96,58,22`, crop | `R\hill_off_crop.png` | `R\hill_sink_crop.png` | `R\hill_tilt_crop.png` |
| same, full frame | `R\hill_off.png` | `R\hill_sink.png` | `R\hill_tilt.png` |
| hillside close, pin `-95.5,57.5,7` | `R\close_off.png` | `R\close_sink.png` | `R\close_tilt.png` |
| a LOADED game (Gate.sav autoload), boulder row | `R\load_off_crop.png` | | `R\load_tilt_crop.png` |
| citizen near the row, running (not paused) | | | `R\cit_tilt.png` |

What they show:
- **Boulder row (both sizes).** Off: the lit wedges between the left boulders and their shadows, as in
  SHADOWGAP. Sink and tilt: the shadows meet the rock bases; the gap is gone at 8192 and at 2048.
- **Hillside (ore).** Off: every piece's shadow is detached from it, clearly floating. Sink: shadows
  attached, but the small pieces lose much of their height into the uphill ground. Tilt: shadows
  attached, every piece keeps its size and leans with the slope; no base floats, none looks buried.
- **Close view.** Sink buries the small flat ore piece in the middle almost completely (a speck
  remains); tilt shows it whole. This is the deciding image.
- **Loaded game.** Tilt is applied after a load too (5,271 rocks re-seated on Gate.sav).
- **Citizen.** No citizen was caught mining: a new game gives no gather orders and the menu driver
  cannot drag the gather tool. `R\cit_tilt.png` has a citizen walking near the seated row. Clipping
  risk is low by construction: citizens stand on the terrain at the work cell (the sim position,
  unchanged), the tilt is at most 30 degrees about the rock's own base and moves the origin by at most
  a few tenths of a unit vertically, never sideways.

## 5. Perf

Nothing per frame. Per rock: 17 (sink) or 34 (tilt) bilinear heightmap samples, at placement, at
scene add and once in the post-load pass (one `FindComponent` per entity: 27,472 entities, 5,271
rocks on Gate.sav, plus one `MeshInstance::Update(Time 0)` per rock). The faithful path pays one bool
test at each of the four sites. Not timed separately (well under the load's noise).

## 6. Notes, limits

- A fix carries no class in the registry (`Class::None`); it behaves as class A: no save or sim effect,
  live toggle. The row is on the front-end Extensions dialog; a toggle takes effect at the next new
  game or load (rocks already in the scene are not re-seated live).
- `ext.rock_seat_mode` is the first valued key owned by a fix (the values test now accepts a fix owner).
  Under the faithful / all profiles every valued key stays at its default (registry rule), so `all`
  gives tilt.
- Terrain edited after placement (a road or building flattening a neighbouring cell under a rock's
  outer ring) is not followed; retail's centre height has the same staleness.
- Rocks can also move UP: the jittered position sometimes stands on higher ground than the cell centre
  (retail buried that side). Seating puts the lowest ring point on the ground, so nothing floats.
- Game dir left as found: `OpenBA.exe` md5 `6a1eac9d...` redeployed, `settings.cfg` restored byte for
  byte (`fc /b`: no differences, md5 `5266a21f...`), lock released.

## 7. Follow-up (2026-10-21): "minor minor sink + tilt"

The user picked tilt and reported a small lit gap left under the boulder row on tilt (the lower-left
edge of a large boulder where its base meets a small rock, `row_tilt_8192_crop.png`). Ruling: keep
tilt, add a minor sink. Tried `kTiltSink` 0.02 / 0.06 / 0.10 / 0.15 (perf exes, new game and Gate.sav
load, the boulder row at 8192, the close-up and the hillside):

- **The hairline along a boulder's lower edge does not depend on the sink.** At 4x zoom the same thin
  lit line runs along the right boulder's lower-left edge at 0.02, 0.06, 0.10 and 0.15. A floating rim
  would make a band whose width is the rim height times cot(sun elevation), shrinking by about 0.1 world
  units (several pixels at this zoom) between 0.02 and 0.15; it does not move. It is the contact
  itself: the sun pass draws only the away-facing faces (cull FRONT, no bias), so along the line where a
  buried face meets the ground the stored depth equals the receiver's and the 3x3 PCF lets a texel's
  worth of light through. Any object resting on the ground has it; no placement closes it.
- **The pockets of lit grass between a boulder base and a neighbouring small rock** do shrink with the
  sink (some at 0.06, more at 0.10 / 0.15): there the ground curves between ring samples and the rim
  stood a little above it.
- **Small ore pieces disappear quickly.** In the close-up the flat ore piece is visible at 0.02 and 0.06,
  mostly buried at 0.10 and nearly gone at 0.15.

**Chosen: 0.06, capped at 12 % of the rock's height** (`TiltSink(f) = min(kTiltSink, kTiltSinkFraction *
f.height)`, the height from the mesh's local AABB). Boulders (about 1.1 tall) get the full 0.06; ore
pieces (0.26 .. 0.4 tall) get 0.03 .. 0.05 and stay clearly visible on the hillside. More ring samples or
the mesh's real bottom vertices were not needed: the remaining line is the shadow contact, not a missed
edge (the vertices are not on the CPU either: the mesh keeps only its AABB there).

Unit: 1216 tests, 0 failures, 1 skipped (`ext_rock_seat_footprint_from_bounds` now also checks the cap).

Comparison image for the user (columns off / tilt 0.02 / tilt + 0.06 capped; rows boulder row 8192,
close-up, hillside; new game seed 12345, so the sun differs per column):
`R\rockseat_compare2.jpg`. Game dir left as found again (`OpenBA.exe` md5 `6a1eac9d...`,
`settings.cfg` byte-identical, lock released).

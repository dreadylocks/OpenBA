# EYECAM hand-in (2026-10-11)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: namespace `EyeCamera` -> `Ext::EyeCamera`; the `[EyeCam]` stderr prefix is `[EyeCamera]`.)

The first extension: `ext.eye_camera`, class A (no serialized state, no sim effect), default OFF,
display name "Eye-level camera", description "Follow a citizen at eye level, looking where they
look." Spec: `INVENTORY.md` §5.1. Static gate only: the game was NOT run by this block
(coordinator's rule). Everything below about how it looks is unverified until the by-hand run (§7).

## 1. What exists

| File | What |
|---|---|
| `src/ext/eye_camera/eye_math.h` / `.cpp` | The pure math, standard C++ only: the conventions, the constants (§4), `ModelYawFromQuaternion`, `Forward`, `ComputePose` (feet + heading + eye height + distance -> eye and look-at), `ZoomDistance`, `BlendFactor`, `Smooth` + `Snap` / `Step`. |
| `src/ext/eye_camera/eye_camera.h` / `.cpp` | The engine glue: `EyeCamera::Active()`, `Apply(pc)` (Update step 6), `ApplyFrame(pc, alpha, fovY, far)` (Draw step 4), `Zoom(zoomDir)` (camera input). The weak reference, the follow detection, the model sample, the terrain clamp. |
| `src/ext/registry.h` / `.cpp` | `Ext::Extensions::eye_camera` and the table row (Tier::Extension, Class::A, default false, formatVersion 0). |
| `tests/unit/ext_eye_camera_test.cpp` | 6 tests of the pure math (§6). `tests/unit/sources.cmake` links `eye_math.cpp`. |

No hook vtable, no function pointer, no getenv, no `BANISHED_*` flag, no door, no new cast.

## 2. The three seams (the only edits outside `src/ext`)

Each is `if (Ext::Ext().eye_camera && EyeCamera::Active()) <ours> else <retail's lines, unchanged>`,
with an `// EXT SEAM` comment; the toggle is read once per seam call, never in a loop.

| Site | Lines | Ours | Retail's arm |
|---|---|---|---|
| `src/game/Game/gameupdateloop.cpp`, `GameUpdateLoop::Update` step 6 (once per update tick) | 977-989 | `EyeCamera::Apply(*playerCtrl)` | `CalcTransform` -> `AsTransform3` -> `camera.SetCameraToWorld` |
| `src/game/Game/gameupdateloop.cpp`, `GameUpdateLoop::Draw` step 4 (once per frame) | 1335-1340 | `EyeCamera::ApplyFrame(*m_playerController, alpha, 40, 512)` | `UpdateCamera(40, 2.0, 512)` (0x140098660) |
| `src/game/Game/camera_input.cpp`, `PlayerToolManager::UpdateCamera` (R17's file) | 172-176 | `zoomDir = EyeCamera::Zoom(zoomDir)` (moves the eye's distance, returns 0) | nothing: retail's zoom path below runs with zoomDir as computed |

Plus one include line per file. **The Draw seam does more than the near plane** INVENTORY §5.1
named: it also puts the eye transform on the camera, at the renderer's `alpha`. Reason: step 6
runs once per update tick, the renderer draws the citizen interpolated between its previous and
current transform at `alpha`; a camera set only at step 6 would sit at the tick's transform while
the citizen is drawn behind it, a visible judder at a 0.1 near plane, at every game speed. Step 6
still sets the eye too (the last frame's), so the tick's later readers (picking, the audio
listener) see the eye camera rather than retail's for one tick.

## 3. How the follow engages and disengages

`EyeCamera::Active()` is the whole test (eye_camera.cpp):
1. Retail's follow flag, `PlayerToolManager +0x38` (FollowDialog::OnOpened sets it, OnClosed
   clears it; nothing else writes it). Off -> disengage, retail's camera.
2. The frontmost open `FollowDialog` in `UserInterface::_dialogs` (walked tail -> head, skipping
   anything in `_removedDialogs`). The frontmost one because the UI updates back to front, so its
   `OnUpdate` writes retail's target last: we follow the citizen retail's camera follows.
3. On a NEW dialog (weak-ref identity differs) we latch it and its citizen's entity
   (`dialog->citizen->_entity`) as weak references; the dialog's raw `citizen` pointer is read only
   at that moment. The smoothing is reset and the first pose SNAPS to the citizen.
4. The latched entity must still resolve (below) and not be marked for removal
   (`_entityFlags` bit 10). Otherwise `Active()` is false: retail's follow (its FollowDialog keeps
   writing PlayerController's target).

Engaged = the follow dialog is open and the extension is on. Disengaged by everything that ends
retail's follow today: the dialog's close button (id 100), Escape (key 8, FollowDialog::OnUpdate),
the citizen panel closing (CitizenUI::OnClosed closes the follow dialog), or the toggle switched off
in the Extensions dialog (live, class A). On disengage the camera is retail's on the very next
step: `PlayerController`'s target / yaw / pitch / distance and `PlayerToolManager`'s +0x3C / +0x40
were never written by this code, so retail's camera is exactly where retail's follow left it.
Retail's own writes continue meanwhile (FollowDialog::OnUpdate moves the target to the citizen,
the camera input still smooths pitch / distance toward their unchanged targets, Q/E still turn
retail's yaw): all of that is retail's state, invisible while the eye owns the camera.

**Must-not kept:** no write of `PlayerController::transform / target / yaw / pitch / distance`
(Serialize 0x1400988c0) nor of `PlayerToolManager` +0x3C / +0x40 (Serialize 0x140102920). The +0x00
transform IS written, by retail's own `CalcTransform()`, which `Apply` still calls at step 6 so
the serialized +0x00 stays exactly what retail would store. The only other write is the camera
member (+0x34, not serialized) through `SetCameraToWorld` and `UpdateCamera`.

**The weak reference** (R1): `{pointer, InternalFactory slot index, generation}` taken from the
object; it resolves only while `InternalFactory::_objects[index]` still holds that pointer with
that generation (`~Internal` swap-removes the slot, so a destroyed object is never found there),
and the pointer is dereferenced only after the slot names it. Tested every step (every
`Active()` call, three per frame).

## 4. Constants (`eye_math.h`)

| Constant | Value | Why |
|---|---|---|
| eye height | 0.88 units | The shipped male citizen mesh (`data0.pkg`, `Models\Citizen\CitizenMaleMesh_resource.crs`, bounding box read from the compiled resource: z 0.002 .. 0.946, x +-0.45 T-pose, y -0.072 .. 0.089) is 0.946 units tall, so 1 unit is ~1.85 m and a 1.6 m eye is ~0.87; eyes at 0.93 of the height = 0.88. A constant, not the live bounds (an animated bound moves with the arms); children use it too (see §7). |
| face forward | 0.10 | the first-person eye sits just ahead of the face (the mesh's front is ~0.09) |
| distance | 0 .. 3.0, default 0.9, step 0.3 | 0 = first person; 3.0 units ~5.5 m over the shoulder; the wheel / ZoomIn / ZoomOut keys move it by sign (zoom in = closer). Kept across follows for the session. |
| rise | 0.30 per unit backed off | the head sits ~17 deg under the view centre at any distance |
| look ahead | 6.0 units, level at eye height | |
| near plane | 0.1 (retail 2.0) | far stays 512, fov stays 40 |
| smoothing | position tau 0.06 s, heading tau 0.25 s, distance tau 0.10 s | exponential, `1 - exp(-dt/tau)` on the real frame time (steady_clock), so frame-rate independent (unit-tested); the heading turns the short way round |
| snap | a jump > 4 units, or > 0.5 s without a frame | teleport / reload / the toggle off and on |
| terrain clearance | 0.15 above the heightmap | a backed-off eye over a slope (`Map::GetTerrainPos` with flag 1: the heightmap only, clamped to the map) |

The heading: the model's rotation is the move tick's pure Z rotation
`(0, 0, -sin(Y/2), cos(Y/2))` with `Y = heading + CharacterDescription::_f24`
(citizenai_executor.cpp, CharacterComponent_MoveTick_1094c0); `ModelYawFromQuaternion` inverts it
and `_f24` is subtracted, giving the heading whose `Vector3::FromYawPitch(H, 0)` is the walking
direction. The position and the rotation are `ModelComponent::meshInstance._transform` (the
Geometry's InterpolatedState) lerped prev -> current at the renderer's `alpha`: the model's
interpolated node-to-world for the root, not the sim cell and not `ModelComponent::transform`
(the per-tick gameplay copy).

## 5. INVENTORY §5.1's risks and what I did

| Risk | What I did |
|---|---|
| Near plane 2.0 clips at eye height | 0.1 while engaged (the Draw seam calls retail's `UpdateCamera(40, 0.1, 512)`); retail's 2.0 otherwise. Depth precision: 512/0.1 = 5120, worth a look at distant terrain / water edges (§7). |
| Terrain chunk / status-icon culling, `statusIconDist`, assume a top-down camera | Not changed. Culling uses the camera's own frustum, which is now the eye's: things behind the eye are culled, which is right. Status icons are placed by their own distance setting from the camera; at eye level more icons may be in range and big in view (§7). PerfX GEOM_CULL / ANIM_CULL (perf build only) cull off-frustum citizens: the followed citizen is in front of the eye or contains it, so it is not culled; the 6-unit margin covers its first frame. |
| `CalcWindFactor` / the audio scene use \|camera - target\| | Not changed (audio only, class A stays class A). The listener is (target.xy, camera z) and the wind factor is \|camera - target\|: at eye level both are near the citizen, so ambient wind is quieter and nearby 3D sounds are louder. Arguably the right sound for the view; a by-hand listen (§7). |
| The followed citizen enters a building (model hidden) | Hidden = `ModelComponent::modelId < 0` (out of the scene). `ApplyFrame` skips the sample and keeps the last eye transform; when the citizen reappears the smoothing resumes (a walk out of a door is under the 4-unit snap, so it glides; farther, it snaps). Hidden at the moment of engaging: retail's follow until the model shows. |
| The followed citizen is removed (R1) | Weak reference tested at every `Active()`; dead or marked for removal -> `Active()` false -> retail's follow, which then ends the usual way (CitizenUI closes the follow dialog). The dialog's raw `citizen` pointer is never read after the latch. |
| Step 6 runs per sim step; high game speed | Step 6 runs once per update tick (outside the speed loop), and the eye is rebuilt per FRAME at the renderer's alpha from the Geometry's interpolated transform, so it moves exactly as the drawn citizen does at every speed. (PerfX GEOMXF_LASTONLY, perf build: the geometry push happens on the last speed iteration only -- the same transform the renderer draws.) |
| R17 (camera_input.cpp's pan basis from yaw) | Not fixed (the brief). The hook does NOT depend on it: it reads no basis and replaces only the zoom direction; while engaged, WASD still pans retail's target, which FollowDialog::OnUpdate overwrites each frame (retail's follow). |

## 6. Results

- `tools/build.sh build debug --jobs 2`: exit 0. Warnings: the clean baseline build of the base
  (a6d61e20d) has 109; the incremental build after this block's edits printed 1, in
  `src/game/Dialog/residenceui.cpp:243` (unused `endRow`, pre-existing, recompiled because
  registry.h changed). None in a new or touched file: 0 new warnings.
- `tools/build.sh unit debug --jobs 2`: `1019 tests, 0 failures` (6 new `ext_eye_camera_*`:
  wrap_degrees, yaw_from_model_rotation (against `Transform::FromYawPitchOffset`, the move tick's
  quaternion, q / -q, and a lerped transform), forward_matches_engine_heading (against
  `Vector3::FromYawPitch` / `Heading`), pose, zoom, smoothing (snap, frame-rate independence, the
  short way round +-180, the teleport snap)).
- `tools/acceptance/acceptance.sh`: `acceptance: 0 line(s) off target`.
- `python tools/castmetric.py src | tail -1`: `# castmetric total: 2` (no cast added; the one
  `static_cast<FollowDialog*>` is a checked hierarchy downcast after `IsA`, not counted).

## 7. What the coordinator must look at by hand (`tools/build.sh run-menu`, then a new game)

1. Extensions dialog: a fourth row "Eye-level camera", class A, "live", off by default; on, OK.
   `ext.eye_camera=1` in settings.cfg. The gate (`tools/build.sh run`, `/profile:faithful`) logs
   `ext.eye_camera=0 class A`.
2. Click a citizen, press Follow: the view drops to the citizen's eyes slightly behind the head
   (distance 0.9), looking where they walk; no snap when they turn (heading smoothed), no judder
   while they walk at speed 1, 2, 3 (alpha interpolation).
3. Mouse wheel / zoom keys: toward first person (0) and back out to ~3 units over the shoulder;
   retail's zoom must NOT have changed when the follow ends.
4. The eye height looks like a person's (0.88 units): if it is at the knees or above a roof, the
   unit scale guess is wrong (the constant is in eye_math.h).
5. First person (distance 0): the citizen's own head / hat / carried tool -- does any of it fill the
   view? Near plane 0.1: any clipping of the ground at the feet, z-fighting on distant terrain,
   water, roads.
6. Close the follow three ways: the dialog's close button, Escape, closing the citizen panel.
   Each must give back retail's camera at retail's pitch / distance over the citizen (the same
   view the faithful follow ends with), with no jump in zoom.
7. The citizen enters a house / workplace: the view holds at the door; when they come out it
   glides back to the eyes.
8. The followed citizen dies (or is removed some other way) while followed: retail's follow
   takes over, no crash, no frozen camera.
9. Toggle the extension off in the Extensions dialog (pause menu) while following: retail's follow
   at once; on again: the eye snaps back.
10. Save while following with the extension on, load in the faithful profile: the camera
    loads where retail's would (target at the citizen, retail's pitch / distance). A resave diff
    of that save against one made with the extension off at the same moment shows only the
    sim's own drift, never the camera block.
11. Children: the eye stays at the adult 0.88 (children are scaled meshes); judge whether that
    reads wrong.
12. Status icons / particles / the wind at eye level: anything huge, missing or loud.
13. Two citizen panels both following: the frontmost follow dialog wins (as retail's target).

## 8. Stress comparison (the coordinator fills)

| Profile | simIters/frame or fps | crash / hang log | exit |
|---|---|---|---|
| faithful (`/profile:faithful`, label eyecam_faithful) | sim 0.17 ms/iter, update p95 2.17 ms, 140 fps | none | 0 |
| everything on (`/profile:all`, label eyecam_all; nothing followed) | sim 0.17 ms/iter, update p95 2.33 ms, 140 fps | none | 0 |

The stress town follows no citizen, so with the extension on and nothing followed the only cost
is `Ext::Ext().eye_camera` plus the follow-flag test, three times a frame.

## 9. EYECAM2 (the user's look)

The user looked on 2026-10-11 (late). Engage, the three exits, the return to retail's camera and the
zoom all work. Three defects, one commit each. Static gate only: this block did not run the game.
Line numbers in "root cause" are in the EYECAM version (main a4db56fd5).

### 9.1 Children got the adult eye height

**Root cause.** `eye_camera.cpp:144` (`BuildPose`) passed the constant `kEyeHeight` (0.88) to
`ComputePose` for every citizen. Children are not a different mesh: they are the same mesh drawn at
an age scale. `CitizenComponent::SetupYoung / SetupAdult / SetupElder` (0x140115110 / 0x1401151a0 /
0x140115230, `citizenaicomponent.cpp` `CitizenComp_SetupAgeMesh`) store a uniform factor, 1.0 /
0.83 / 0.66 (a child gets 0.66), into the model's `MeshInstance::_color[0..2]` (+0x10c..+0x114);
`MeshInstance::AddDrawItems` hands that vector to the vertex shader as the w column of the instance
transform (`Context::SetTransformScale`, `meshinstance_draw.cpp:283`). Nothing in the eye read it.

**Fix** (commit 1). `SampleCitizen` reads `model->meshInstance._color[2]` (the height factor) with
every sample; `ComputePose(feet, heading, modelScale, distance)` puts the eye at
`kEyeHeight * modelScale` and the face offset at `kFaceForward * modelScale` (the rise per unit
backed off is unchanged). No child factor is hard-coded: the live value is read, so a citizen who
ages while followed gets the new height on the next sample. Unit test
`ext_eye_camera_pose_child_scale`.

**Re-check.** Follow a child: the eye at a child's head (about 0.58 units at 0.66), the view lower
than an adult's; follow an adult: unchanged.

### 9.2 No shadows at the default distance (0.9); they came back only near 3

**Root cause.** The shadow map is rendered from the orthographic camera
`Graphics::SunLight::GetSunCamera` (0x180107010, `src/engine/graphics/sun_camera.cpp`) fits to the
VIEW camera (draw group 0's camera, `sun_camera.cpp:161`), and the fit assumes a view that looks
down at the ground:
- each frustum corner ray that points up does not hit the ground plane and is extruded 1000 units
  (`sun_camera.cpp:191` and the far-corner arm), so at eye level the two upper corners sit ~800
  units ahead and the fit's centroid is ~200 units in front of the eye, its radius ~800;
- the box half-size is `radius * sqrt(|forward.z|)` (`sun_camera.cpp:290` `skew`, `:322` `w`):
  the view's forward axis is nearly level at eye height, so `skew` is ~0.2 at distance 0.9 (the
  eye 0.27 above the head, looking 6.8 ahead) and 0 in first person.

A transcription of the fit (scratch Python, same arithmetic, same shipped clamp bug) with the eye
over flat ground: at 0.9 the box half-size is 162 and the citizen sits 172 from the box centre
across the sun's axis, OUTSIDE the box: no shadow is drawn near the eye. At 1.5 it is 201 (inside),
at 3.0 it is 260 (inside, but with one shadow texel ~0.25 units at 2048: blurry). That is the
user's report: shadows only as the eye backs off toward 3. Retail's own camera (pitch 35-70,
distance 15-60) gives half-sizes 24-60 with the target well inside. Nothing else culls shadows:
the caster cull uses the sun camera, which is the box above.

**Fix** (commit 3). The fit is fed a different view, retail's fit itself unchanged:
- `EyeCamera::DrawScene` (eye_camera.cpp) runs retail's `Renderer::Draw` with the eye's camera
  exactly as before, after pointing `Graphics::SunLight::_extShadowFitView` at a synthetic view:
  the eye's camera moved to `ShadowFitPose` (eye_math.h) -- looking at the ground 16 units ahead of
  the smoothed feet, from 48 units back along the smoothed heading, 55 degrees below level. The
  fit of that view: a box ~50 units across from just behind the citizen to ~50 ahead, the size
  retail's fit gives at a middle zoom. The pointer is null again when `Draw` returns.
- The engine end, `SunLight::AddDrawItems` (`wave28_lane_partsys.cpp`): `_extShadowFitView ==
  nullptr` -> retail's line `GetSunCamera(renderer)`, unchanged; else
  `ExtShadowFitSunCamera`, which puts the fit view in draw group 0's camera for the one
  `GetSunCamera` call and restores the real camera at once (the main pass, the `BeginDrawGroup`
  that follows and every other reader see the eye's camera). `src/engine` does not include
  `src/ext`, so the engine end is a NOT RETAIL static data pointer (`SunLight.h`), null by default;
  no function pointer, no indirect call.
- The new seam (the fourth): `GameUpdateLoop::Draw` step 7, `if (Ext::Ext().eye_camera &&
  EyeCamera::Active()) EyeCamera::DrawScene(...) else m_renderer->Draw(...)` (retail's call
  unchanged). Faithful path cost: one null test per sun light per frame in `AddDrawItems`.

Unit test `ext_eye_camera_shadow_fit_pose` (the fit view's target, distance and pitch).

**Re-check.** At the default distance and in first person: the citizen's own shadow, buildings'
and trees' shadows on the ground ahead, at retail's sharpness. Turning: the box turns with the
heading (shadows must not pop or crawl visibly while walking; the fit snaps to shadow texels as
retail's does). Far ahead (past ~50 units) and toward the horizon there are no shadows -- the same
as anything outside retail's box; judge whether the edge reads wrong (the constants are in
eye_math.h, `kShadowFit*`). Retail's camera after the follow ends: shadows exactly as before.

### 9.3 An idle citizen kicked the view back to retail's follow until it walked again

**Root cause.** Not the heading: `SampleCitizen` reads the model's transform, never the move tick or
the path, so an idle citizen's heading simply holds. The cause is the weak reference,
`eye_camera.cpp:64-74` (`Resolve`): it treated the factory slot index as the object's identity
(`if (slot != w.ptr) return nullptr;`, line 71). `Object::InternalFactory::RemoveObject`
(0x1800c0110, `internal_class.cpp`) swap-removes: the table's LAST object moves into the freed slot
and gets a new `_factoryIndex`. The follow dialog is a young object, so it is often the last entry
once the transient objects created after it (a walk's, a task's) are gone; the next death of any
older object moves it. Then:
- `Active()` line 170 saw `Resolve(g_dialog) != dialog`, disengaged and re-latched. With the
  citizen visible that was a silent snap; with the citizen inside a building (model out of the
  scene: a worker who stopped at the workplace) the first-pose sample at line 190 failed and the
  camera went to retail's follow until the model came back (they walked out);
- the same move of the citizen's entity made `Resolve(g_entity)` fail (line 180): retail's follow
  for good, until the dialog itself moved and re-latched.

**Fix** (commit 2). `FollowSlot` (eye_math.h): when the hint slot no longer names the object, the
table is searched by pointer value (nothing dereferenced; only after a slot names it is the
generation read). Missing from the table, or a reused address under another generation = destroyed,
and the reference is cleared for good. The search runs only when the object moved (rare, then the
hint is updated). The first pose of an engagement with the model hidden takes
`ModelComponent::transform` (the gameplay copy a hidden citizen keeps: the door it went in by), so
the eye engages and stays engaged. The only ways back to retail's camera are now: the follow
dialog closed (close button, Escape, the citizen panel closed), the toggle off, the entity removed
or marked for removal. Unit test `ext_eye_camera_idle_citizen_stays_followed`: a replay of
RemoveObject's swap moving the followed object (it resolves at its new slot), an unrelated removal,
the object's own removal (-1); and an idle model holding its heading and feet exactly over 1000
steps.

**Re-check.** Follow a citizen through a work cycle: walk, stop, work, go into the workplace or
home, come out. The eye must never hand back to retail's view while the follow dialog is open; at
a stop it holds the last heading; inside a building it holds at the door and glides back when
they come out. Follow in a busy town for a few minutes (many objects dying: the old swap-remove
case). Closing the follow, Escape, and a citizen's death still hand back exactly as before (§7
items 6, 8).

### 9.4 Results

Branch `worktree-agent-ac8bac3e180bfba55`, base main 05d029338. Commits: c1cacd849 (9.1), b276e8abb
(9.3), d536990ec (9.2).
- `tools/build.sh build debug --jobs 2`: exit 0. The clean build of the base printed 109 warnings
  (the baseline, as in §6), none in a touched file; the incremental build after all three commits
  printed 0. 0 new warnings.
- `tools/build.sh unit debug --jobs 2`: `1022 tests, 0 failures` (3 new: `ext_eye_camera_pose_child_scale`,
  `ext_eye_camera_idle_citizen_stays_followed`, `ext_eye_camera_shadow_fit_pose`).
- `tools/acceptance/acceptance.sh`: `acceptance: 0 line(s) off target`.
- `python tools/castmetric.py src | tail -1`: `# castmetric total: 2`.
- `python tools/abi_audit.py`: OK (the new `SunLight` static is not a layout field).
- The game was not run (the coordinator gates, the user looks).

The seams are now four (§2's three plus GameUpdateLoop::Draw step 7), and the engine end of the
fourth in `SunLight::AddDrawItems`. No serialized field is written; no getenv, no flag, no door,
no function pointer.

### 9.5 Open doubts

- The age scale is read from `MeshInstance::_color[2]` as the HEIGHT factor. The three setters write
  the same value to x, y and z and the draw hands it to the vertex shader as the w column of the
  instance transform; this block did not read the shader to prove it multiplies the model-space
  position about the feet. If a child's eye lands in the chest or above the head, that reading is
  wrong (the user's look decides).
- The shadow fit view's constants (16 ahead, 48 back, 55 degrees) are chosen from the fit's
  arithmetic, not from a frame. Shadows beyond ~50 units ahead are not drawn while the eye is
  engaged; the depth range is retail's (the box's 1..256 along the sun).
- `ExtShadowFitSunCamera` writes and restores draw group 0's camera around one call; the copy is
  `Camera`'s own copy ctor / `operator=` (the 3 pad bytes +0xd5..+0xd7 round-trip through a copy
  that does not write them: unread padding).
- Defect 3's root cause is inferred from the code (no run): the weak reference was the only way
  `Active()` could return false with the dialog open and the entity alive. Whether the user's
  "stopped" citizens were hidden (inside a workplace) or visible is not known; both paths are
  fixed.

## 10. EYECAM3 (the idle drop, second look)

The user looked again on 2026-10-11 (late): EYECAM2's child height and shadows are good; the idle
drop is "mostly fixed" -- following a citizen who stands still, the view SOMETIMES still leaves the
eye for retail's third-person follow. EYECAM2's cause (slot reuse) was inferred, not reproduced.
This block did not run the game either: the cause below is again INFERRED; the new trace is there so
the next look names the test that fired. §3's engagement rules are replaced by §10.2.

### 10.1 Every exit while the FollowDialog is open (the EYECAM2 code, main 31424ac88)

`Active()` ran four times per frame (Update step 6, Draw steps 4 and 7, the zoom input). It handed
back to retail when:

| # | Test | Can an idle citizen trigger it? |
|---|---|---|
| 1 | Follow flag `PlayerToolManager +0x38` false | **No.** Retail's writers (Ghidra: every `MOV byte [reg+0x38]` after a load of `GameUpdateLoop +0x120`, and every +0x38 store in 0x1400fe000..0x140104000): the ctor 0x1401025c0 (0), `FollowDialog::OnOpened` 0x140069340 (1), `FollowDialog::OnClosed` 0x140069320 (0). Nothing clears it when the target stops or its data changes. It IS false with a dialog open after two FollowDialogs were open and one closed (one byte for all of them) -- a user action, not idleness. |
| 2 | No FollowDialog in `UserInterface::_dialogs` (minus `_removedDialogs`) | **No.** The list is only appended (`UserInterface::Update`, drained from `_addDialogs`) and unlinked (`CloseDialog`); nothing reorders it (no bring-to-front). CitizenUI never re-creates its FollowDialog: `followDialog` (+0xE0) is written only by the Follow button (0x1400650f0), `CitizenUI::OnClosed` and `FollowDialog::Close`; the panel's per-frame layout update does not touch it. |
| 3 | The latched dialog's weak ref no longer equals the frontmost dialog | Not a drop: it re-latched and snapped. With one dialog open its identity cannot change (row 2); only its table entry could (row 4). |
| 4 | The entity's weak ref: not found in `InternalFactory::_objects` by pointer, or its `_generation` changed -- then cleared **for good** (retail's follow until the dialog was reopened) | **The candidate.** `_generation` is written only by `AddObject`, `~Internal` and a load (and `Internal::operator=` / the copy ctor, which nothing in the port calls: no object file references `_ZN6Object8InternalaSERKS0_` or `_ZN6Object8InternalC1/C2ERKS0_`). So the drop needs a LIVE entity missing from the table. Idleness does not cause that directly; what can is the documented swap-remove family (memory `reference_internalfactory_swapremove_wild_write`): an object freed without `~Internal` leaves a stale entry; a later `RemoveObject` that moves that entry writes `_factoryIndex` into whatever now lives at the freed address; that object's own `~Internal` then swap-removes the WRONG slot, which can hold any live object -- the followed entity included. INFERRED: not reproduced, and how often the port frees an Internal without its destructor today is not measured. It fits "sometimes, not every time" and "stays in retail's follow". |
| 5 | Entity marked for removal (`_entityFlags` bit 10) | **No.** Only `ComponentEntity::MarkForRemoval` sets it (death, demolition, leaving); the component flag words `RefreshEntityFlags` ORs in do not carry 0x400. |
| 6 | First pose of an engagement: `SampleCitizen` false (no `ModelComponent`) | **No.** A citizen's ModelComponent lives as long as the entity; a hidden model takes the gameplay transform since EYECAM2. |
| 7 | The toggle off | User action; `Active()` is not even called. |

Nothing else returns false: `ApplyFrame` / `DrawScene` run only after `Active()` said true, and the
other camera writers (`OpenPauseMenu`, `CenterOnEntity` in toolbardialog.cpp) are one-shots that the
next Draw step 4 overwrites. Retail itself never ends a follow on idleness: `FollowDialog::OnUpdate`
(0x140069250) copies the model position to the camera target and closes on Escape (key 8), nothing
else.

### 10.2 The fix (commit 52b05b8f8)

The identity is the FOLLOWED ENTITY. Every step `Active()` reads it from the frontmost open
FollowDialog -- `dialog->citizen->_entity`, exactly the read retail's `FollowDialog::OnUpdate` makes
every frame, so the eye dereferences nothing that faithful body does not -- and compares it by
pointer with the latched entity. The decision is the pure `EyeCamera::DecideFollow` (eye_math.h):

- no FollowDialog open -> hand back (`no-follow-dialog`);
- the frontmost FollowDialog has no citizen -> hand back (`no-citizen`);
- its entity was seen marked / gone before -> hand back (`entity-dead`), never dereferenced again
  (forgotten once no FollowDialog is open);
- a NEW entity (not the latched one) not in InternalFactory's table -> hand back (`entity-gone`):
  the table is asked only before a new entity's first dereference;
- its entity marked for removal -> hand back (`entity-marked`);
- the latched entity -> stay (a different FollowDialog on the same citizen only re-attaches);
  a different one -> latch it (engage, snap).

A latched entity is no longer asked of the table: row 4 cannot hand back any more. The follow flag
is reported in the trace, not tested (row 1: with two follows open, closing one no longer ends the
other's eye). Unit test `ext_eye_camera_follow_identity_is_the_entity`: a latched entity missing
from the table for 1000 steps stays engaged and the table is never asked; the same entity in another
dialog keeps; a new entity latches; the three hand-backs; a dead entity is never asked (mark or
table) again; a new entity missing from the table is refused without a dereference.

### 10.3 The trace (stderr = `standalone_error.log`, unconditional)

```
[EyeCam] engage entity=<p> dialog=<p> from=<p|null>     a follow engaged (from: the entity it switched from)
[EyeCam] reattach entity=<p> dialog=<p> from=<p>        same citizen, another FollowDialog
[EyeCam] disengage reason=<r> entity=<p> dialog=<p> dialogOpen=<0|1> followFlag=<0|1>
[EyeCam] engage refused reason=no-model entity=<p> dialog=<p>
[EyeCam] note entity=<p> not in InternalFactory (kept: its FollowDialog still targets it)
```
`<r>` is one of `no-follow-dialog`, `no-citizen`, `entity-dead`, `entity-gone`, `entity-marked`.
`disengage` prints only on the engaged -> retail edge. The `note` line prints at most once per
follow (the table is searched only when the slot hint misses) and is the evidence for row 4: if it
appears during an idle follow, row 4 was the old drop and the eye now stays.

### 10.4 What the user re-checks

1. Follow a citizen through a long idle stretch and a work cycle (busy town, several minutes). The
   eye must never hand back while the follow dialog is open. Then grep `standalone_error.log` for
   `[EyeCam]`: one `engage`, no `disengage` until you closed the follow (`reason=no-follow-dialog
   dialogOpen=0`). A `note ... not in InternalFactory` line confirms the inferred cause (and that the
   object table lost a live entity: a port defect of the swap-remove family worth its own hunt).
2. If the view still drops: the `disengage reason=` line names the test; send it.
3. Close the follow (button, Escape, the citizen panel), switch to another citizen's follow, and a
   followed citizen's death: each hands back as before (`no-follow-dialog`, an `engage ... from=`,
   `entity-marked`).

### 10.5 Results

Branch `worktree-agent-a1fc3778d5238ba7c`, base main 31424ac88. Commits: 52b05b8f8 (code + test),
then this hand-in.
- `tools/build.sh build debug --jobs 2`: exit 0; the incremental build of the final change printed
  0 warnings (the first, partial build printed none in a touched file).
- `tools/build.sh unit debug --jobs 2`: `1023 tests, 0 failures` (1 new).
- `tools/acceptance/acceptance.sh`: `acceptance: 0 line(s) off target`.
- `python tools/castmetric.py src | tail -1`: `# castmetric total: 2`.
- The game was not run. No new seam, no serialized field written, no getenv, no flag, no door.

### 10.6 Open doubts

- Row 4 is the only test an idle citizen could reach, and the mechanism that drops a live entity from
  the table is inferred from the documented swap-remove family, not observed. If the next look still
  drops and the trace names another reason, this analysis missed it.
- Not ext's to fix, seen while reading: `CitizenUI::OnClosed` (citizenui.cpp, its
  `SA_DialogEntityAlive` early return) skips closing the FollowDialog when the citizen panel is
  closed because the citizen is marked for removal (`EntityList_CloseDialogsForDyingEntities`). The
  FollowDialog then stays open with a dangling `citizen`, which `FollowDialog::OnUpdate` keeps
  reading after the entity is freed (retail closes it unconditionally, and its strong data-source
  reference keeps the entity alive meanwhile). The eye is safe (it saw the mark and never touches
  that entity again) but the game's own follow reads freed memory. Inferred from the code, not run;
  worth a row in `docs/known_port_issues.md`.
- A NEW follow whose entity is (wrongly) missing from the table is refused (`entity-gone`) until
  every FollowDialog closes: the same row-4 defect at engage time, rarer; the trace says so.

## 11. The idle drop, observed (coordinator, 2026-10-11 night)

The user's run with EYECAM3's trace: every drop is `[EyeCam] disengage reason=no-follow-dialog ...
dialogOpen=0 followFlag=0`, and each one pairs with an `[EditTool] deselect -> closed info dialog`
line in standalone_debug.log at the same moment. So the FollowDialog was closed by the citizen
panel's own close (CitizenUI::OnClosed), and the panel was closed by `EditTool::Update`'s
off-display rule (retail 0x1400fcebc-0x1400fcec8: an unpinned info panel whose entity yields no
screen rect is deselected and closed). The tree's citizen projection (`TryProjectEntityModelPos`)
supplies that rect for an on-screen citizen; at eye level the followed citizen is AT or BEHIND the
camera, so the projection fails whenever they stop and the smoothing swings the view onto them.
EYECAM2's and EYECAM3's causes were real defects but not THIS one; the trace settled it.

Fix (`edittool.cpp`, one seam in the design's shape): while `ext.eye_camera` is engaged on this
panel's entity (`EyeCamera::FollowedEntity()`), a failed projection leaves the panel where it is
instead of closing it. Retail's rule is unchanged for every other entity and when the extension
is off. The user re-checks: follow a citizen through several stops; `[EyeCam] disengage` should
appear only when the follow is closed by hand.

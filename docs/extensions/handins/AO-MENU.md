# AO-MENU -- `ext.ambient_occlusion` on the main-menu backdrop (hand-in, 2026-10-17)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/ambient_occlusion/ao.{h,cpp}` moved to `ambient_occlusion.{h,cpp}` (namespaces `AmbientOcclusion` / `Ext::AO` / `AoMath` -> `Ext::AmbientOcclusion` / `Ext::AmbientOcclusion::AoMath`), `tests/unit/ext_ao_test.cpp` to `ext_ambient_occlusion_test.cpp`.)

AO.md §7 row 7.8 ("ruled game-only for now, one arm site to change"), the user's call of 2026-10-17:
the row on = AO on the menu backdrop too, the way `ext.better_trees` and `ext.max_shadow_res`
already apply there. Branch `worktree-agent-a4b8af7efe972ccf7` from main `9afd16b48`. Nothing is
pushed or merged; the lead merges.

## 0. Short version

- **Works.** With the row on, the main-menu backdrop gets the same ASSAO pass as the game's scene
  (captures in §3). The Options dialog opened AT the menu changes it live: Enhanced > Ambient
  occlusion High + Apply turns it on behind the open dialog (`[AO] ext.ambient_occlusion ON`,
  `[AoMenu] Apply: quality=3 opacity=30`, the ASSAO targets allocated), no restart.
- **One arm site added, nothing else.** `MenuDialogLoopT::DrawScene_PhaseA` arms
  `Graphics::Renderer::_extAo` around its `Renderer::Draw` exactly as `GameUpdateLoop::Draw`
  step 7 does. The seam's consumer, `Ext::AO::Current()`, `AoMath::FrameFor`, the backend and the
  settings are untouched.
- **Row off = retail.** `Enabled()` is one load per menu frame, the seam stays null, the menu's
  `Draw` is the retail call. Row off at the menu: no `[AO]` resource line at all (§4).
- **Cost.** Menu, 2560x1377, 4x MSAA, High / 20, fps uncapped (`BANISHED_FPS_CAP=off`, the
  watchdog's frame counter over 15 s): off 676 fps (1.48 ms), on 589 fps (1.70 ms) = **-13%, +0.22
  ms per frame**; the same -13% AOTUNE §7.2 measured for High in game. Under the default limiter
  the menu sits at its 216 fps ceiling either way.
- **Hand-off game <-> menu.** Row on, new game, quit to the main menu from the pause menu: the
  menu loop's new instance draws with AO, no `[AO]` error line, crash log empty, hang log none.
- Unit 1152 / 0 / 0. Acceptance 0 lines off target.

## 1. The seam at the menu

`src/game/Game/menudialogloopt.cpp`, `MenuDialogLoopT::DrawScene_PhaseA` (the menu loop's draw,
after SunLight / fog / shader detail, right at its `renderer->Draw`):

```cpp
const bool aoArmed = AmbientOcclusion::Enabled() && AmbientOcclusion::Arm(*transform);
renderer->Draw(*context, alpha, *gScene, camera);
if (aoArmed) AmbientOcclusion::Disarm();
```

- Arm before, Disarm after, once per frame, nothing in a loop: the same three lines as
  `gameupdateloop.cpp` step 7. `Arm` reads `Ext::AO::Current()` exactly once (the one-read-per-frame
  rule holds: the menu loop and the game loop never draw in the same frame).
- `ao.h`'s header comment now names both arm sites. The minimap stays the one scene that never
  gets AO.
- The menu never uses `ext.render_pipe`'s packet path (`MainHalf` / `BuildFrame` are the game
  loop's); it calls `Renderer::Draw` on the main thread, so the static seam is the right one.

### The distance stand-in

None was needed. The menu loop owns a real `PlayerController` (`_transform`, allocated in the
scene load at `menudialogloopt.cpp` ~line 525) with the same `yaw` / `pitch` / `distance` /
`target` the game's controller has; `UpdateCamera_PhaseA` runs its `CalcTransform`
(eye = target - FromYawPitch(yaw, pitch) * distance), so `_transform->distance` IS the
camera-to-target distance `AoMath::FrameFor` scales the radius with. `Arm(*transform)` takes it
unchanged. The menu scenes set it to 25 or 30 world units (the `[MenuDLT::UpdateCamera] ... dist=`
log line), near `kZoomMin` (20), so the backdrop gets the street-level end of the radius curve
(~1.6..1.8 units, between `kRadiusStreet` 1.5 and `kRadiusOverview` 5.0 at 130) and the fog
fade follows the menu scene's own fog range (`FadeFromFog` reads `Context::_fogParams`, which the
menu draw sets right before).

### The exclusions still hold

The consumer's test (`wave27_lane_gfxbig.cpp` pass 2) is unchanged: `k == 0 && extAo &&
e->_shader == 0 && e->_renderBuffer == context._defaultRenderBuffer`. The water reflection is
shader 2 on its own render buffer (excluded by the test, not by the loop); the minimap paint is
`GameUpdateLoop::Draw` step 3, before the game's arm, and the menu has no minimap. The menu scene
passes the test (AO.md §6.5 already knew: target = the default buffer, shader 0), which is why one
arm is all it takes.

## 2. Runs (debug exe, the user's settings.cfg backed up and restored, md5 `0C2F116F...` before and after)

| Run | Settings / flags | What it checked |
|---|---|---|
| r1 | row 0 | the plain menu, capped: `menu_off_before.png`; the `[AO]` lines are the three `[Ext]` rows only |
| r2 | row 0, `BANISHED_FPS_CAP=off` | fps off: 10142 frames / 15 s |
| r3 | row 1 (High / 20), `BANISHED_FPS_CAP=off` | fps on: 8829 frames / 15 s; `menu_on_after.png`; `[AO] ... ON`, ASSAO created at 2560x1377 |
| r4 | row 0, `BANISHED_AUTOMENU_BUTTON=4` (Options opens on the Enhanced tab, `GameLastOption=4`) | the live Apply at the menu, real mouse input (the scratch `ui.ps1`, by PID, as AOTUNE §1.1): combo -> High, strength dragged to 30, Apply; then Ok |
| r5 | row 1 (what r4's Ok wrote), `BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_BUTTON=5` | new game, 600 frames, quit to the main menu; `menu_after_quit_from_game.png` |

Every run was stopped by PID (`taskkill //PID`), one game process at a time; the lead's menu look
was not running when the first started.

## 3. Captures (`C:/GOG Games/Banished/ao-menu/`, not in the tree)

The menu scene is picked at random per launch, so the clean before / after pair is NOT the same
scene; the same-scene pair is r4's, behind the open dialog:

| File | State |
|---|---|
| `menu_off_before.png` | r1, row off (the mill scene) |
| `menu_on_after.png` | r3, row on from boot, High / 20 (the orchard scene): contact darkening under the cabbages, the fence and the house bases |
| `menu_dialog_before_apply.png` | r4, row off, Options > Enhanced open at the menu (the winter scene) |
| `menu_dialog_combo_open.png` | r4, the Ambient occlusion list open (Off / Low / Medium / High) |
| `menu_dialog_after_apply.png` | r4, **same scene, same frame of the dialog**, after High / 30 + Apply: the wall bases, eaves and chimney roots of the winter houses darkened live behind the dialog |
| `menu_live_on_dialog_closed.png` | r4 after Ok |
| `menu_after_quit_from_game.png` | r5, the menu drawn by the loop's second instance after the quit from a game (the market scene), AO on |

The run logs sit beside them (`r1_off_capped.*.log` .. `r5_game_quit_to_menu.*.log`). A RenderDoc
capture of the menu frame was not taken (optional in the brief); the live Apply's `[AO]` resource
lines and the visible darkening stand for it.

## 4. Row off = no AO resource

r1 / r2 / r4-before-Apply `standalone_error.log`: the only `ambient_occlusion` lines are the
`[Ext]` boot rows (`ext.ambient_occlusion=0 class A` and its two valued keys). No `[AO] main
depth -> readable`, no `ASSAO 1.0 created`, no target allocation: the backend is never asked.

## 5. Cost (menu frame, on vs off)

Debug exe, 2560x1377 window, `VideoMultisample=3` (4x), High / 20, every other row the user's (all
on), `BANISHED_FPS_CAP=off`, machine not CLEAN (other agents building; the two runs back to back
under the same load). Frames from the watchdog's state line (integer seconds, so +-7%):

| | frames / 15 s | fps | ms / frame |
|---|---|---|---|
| row off (r2) | 10142 | 676 | 1.48 |
| row on, High (r3) | 8829 | 589 | 1.70 |

-13% / +0.22 ms: the same ratio as AOTUNE §7.2's in-game High (-13.2%). It is GPU time (the menu
frame's CPU side is unchanged: one `Enabled()` load and one `Arm`). With the default frame limiter
the menu renders at its 216 fps ceiling with the row on or off.

## 6. Files

- `src/game/Game/menudialogloopt.cpp` -- the arm / disarm around the menu's `Renderer::Draw`, the
  `ao.h` include.
- `src/ext/ambient_occlusion/ao.h` -- the `Arm()` comment names both sites.
- `docs/extensions/handins/AOCORE.md` ("These never get AO"), `docs/extensions/AO.md` §6.5 and row
  7.8 -- one line each.
- This hand-in.

## 7. Left

- A RenderDoc capture of the menu frame with the row on (optional; the resource lines are in the
  log). `tools/trace_oracle/capture_arm.sh` captures the in-game frame by rank, not the menu's.
- The cost table is from a loaded machine; a CLEAN re-cut if the number matters. The ratio is the
  in-game one.
- The user's look at the backdrop with the row on (the fade / radius were ruled for the game's
  zoom range; the menu sits at its street end).

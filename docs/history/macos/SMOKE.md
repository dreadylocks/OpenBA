# Historical macOS measurements

Recorded on 2026-10-19. These notes describe earlier builds, not current build or test instructions. Local evidence and harnesses are not reproducible from this checkout. Later sections may supersede earlier findings.

# In-game smoke tour of the Metal port (2026-10-19)

> Not shipped: the local tooling and result files cited here (local artifact “*,” local artifact “*”) are measurement artefacts from the author's machine and are not part of this repository; the numbers below are the recorded results of those runs.

57 unattended launches of the real game (build `build/mac-smoke`, a private copy of the tree at that moment).
Every launch: small background window (`OPENBA_BACKGROUND=1`, 640x360 points unless a row says 960x540), silent
(`SDL_AUDIODRIVER=dummy`), `USERPROFILE`/`HOME`/`TMPDIR` redirected to `/tmp/smoke/<name>/` (the real
`~/Documents/Banished` was never touched), hard timeout, only its own PID is killed, `pgrep` after every run
(nothing left running; the runner prints `leftover OpenBA from this run: 0` each time).

Result of the tour: **no crash, no hang, empty `standalone_crash.log` in all 57 runs, no ERROR / assert /
Metal-validation line in any `standalone_error.log`** (the runner greps `ERROR|assert|validation|MTL|fatal|HUNG|exception`;
`error_flagged.txt` is empty in every run). Exit code 143 means "stopped by the harness with SIGTERM at the time
limit" (the run was still healthy: `boot_state=RUNNING`); the one run that quits itself (`s6_pause_quit_game`) exits 0.
**Not covered: toolbar menus, building-placement ghosts, hand-drawn roads/fields** (see "Tooling gap").

Evidence per run is in `local artifact “<name>” :: ` (local, not shipped) (`result.txt`, `shot_*.jpg`, `error_flagged.txt`, `error_tail.txt`,
`standalone_error.log.gz`, `standalone_crash.log`). Contact sheets (several runs on one image, easiest to look at):
`local artifact “*.jpg”` (local, not shipped). Screenshots are `screencapture -l <window id>` of the engine window (works without
focus), re-encoded as 960 px JPEG to keep the repo small.

## Scenario table

| # | Scenario (run name) | How | Result | Evidence | Notes / suspected cause |
|---|---|---|---|---|---|
| 0 | Baseline new game (`base_newgame`) | `BANISHED_AUTOSTART=1 BANISHED_CAMERA_PIN=town,90` | PASS | `base_newgame/shot_32s.jpg` | Terrain, trees, water, houses, toolbar all correct. |
| 1 | Climate mild / fair / harsh (`s1_climate_mild`, `base_newgame`, `s1_climate_harsh`) | `BANISHED_CLIMATE=` | PASS | `_sheets/s1_new_game_variants.jpg`; log line `[NewGameDialog::StartGame] picks: ... climate[2]="Harsh"` | Harsh run shows rain/fog (washed-out scene), mild shows sun. |
| 1 | Terrain Valleys / Mountains (`s1_terrain1`, `s1_seed777_mtn_harsh`) | `GameLastTerrain=1` in the throwaway settings.cfg | PASS | log `terrain[1]="Mountains"`; sheet | Only **two** terrains exist (Valleys, Mountains). `s1_terrain2` (`GameLastTerrain=2`) falls back to Valleys: out-of-range value is clamped, not a bug. |
| 1 | Map size small / medium / large (`s1_size_small`, `base_newgame`, `s1_size_large`) | `BANISHED_MAP_SIZE=` | PASS | log `size=2`; large run reached frame 3506 in 47 s (world generation takes longer than small) | Large map: winter start, fine. |
| 1 | Difficulty hard (`s1_diff_hard`) | `BANISHED_DIFFICULTY=hard` | PASS | `_sheets/s1_new_game_variants.jpg` (middle right) | Citizens with status icons and no houses, rain: correct for the hard start. |
| 2 | Zoom extremes (`s2_zoom_min`, `s2_zoom_max`) | `BANISHED_CAMERA_PIN=town,20` / `town,130` | PASS | `_sheets/s2_camera_extremes.jpg` | Close-up houses, cattle, smoke: textures and shadows correct; max zoom: terrain, trees, river correct. |
| 2 | Camera rotation 90 / 180 / 270 (`s2_yaw90/180/270`) | `town,45,<yaw>` | PASS | same sheet | No seams or shadow flips. |
| 2 | Pitch + yaw + zoom sweep, 52 s (`s2_sweep`) | `town,60,0,sweep` | PASS | `s2_sweep/shot_*s.jpg`, sheet | Pitch 0..1, zoom +/-25 and a full rotation: no flicker, no black quads. Frames ~1 per tool shot only; not a per-frame check. |
| 2 | Toolbar menus, building ghosts, road/field placement by hand | needs mouse/keyboard | **NOT RUN** | `input_trace` evidence below | Tooling gap, see below. Buildings (incl. crop field, orchard, pasture) are covered as finished objects by row 2b. |
| 2b | 16 demo buildings placed by the real build path (`s4_demo_buildings_far/close`) | `BANISHED_BUILD_DEMO_*=1` (all 16) | PASS | `_sheets/s4_demo_tornado_save_load.jpg`; log `[startTown::*] demo ... ent=` for all 16 | Quarry, market, school, tavern, well, hospital, cemetery, chapel, crop field, orchard, pasture, trading post, fishing dock, hunter, gatherer, forester all appear with textures. Status "?" icons are normal. |
| 2b | Stress town, 2000 citizens, winter (`s4_stress_2000`) | `BANISHED_STRESS=1 ...CITIZENS=2000 /profile:faithful`, 54 s | PASS | `s4_stress_2000/shot_48s.jpg` | Dense town, snow, status icons, notification buttons bottom-left all drawn. |
| 3 | Pause menu opens (`s3_pause_open`) | `BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_QUIT=0` | PASS | `_sheets/s3_menus_pause.jpg` | See W1 (menu taller than the 640x360 window). |
| 3 | Save (round trip, step 1) (`s4_save`) | `BANISHED_SAVE_TEST=600` (retail autosave handler) | PASS | `Auto.sav` 4.17 MB written; log `[SaveTest] operation 4 -> 0` | |
| 3 | Load that save (round trip, step 2) (`s4_load`) | `BANISHED_AUTOLOAD=1`, the Save folder of `s4_save` copied in | PASS | `s4_load/shot_32s.jpg` shows the same founding town | |
| 3 | Save dialog and Load dialog, front end + in game (`s3_pause_save`, `s3_pause_load`, `s3_fe_load`, `s4_big_save`) | pause button 3 / 2, front-end button 2 | PASS, see W2 | `_sheets/s3_menus_pause.jpg`, `_sheets/s4_tutorial_bigwindow.jpg` | Dialogs open and draw; the selected empty save slot is a solid black square (W2). |
| 3 | Options, all 5 tabs: Game, Video, Audio, Input, Enhanced (`s3_opt_tab0..4`, `s4_big_options_video`) | pause button 4; tab chosen by `GameLastOption=N` in settings.cfg (the dialog restores the last tab) | PASS | `_sheets/s3_dialogs_options.jpg`; at 960x540 the tab strip is visible (`_sheets/s4_tutorial_bigwindow.jpg`) | Text, checkboxes, sliders, combos correct on every tab; Video shows Renderer "Metal", Adapter "Apple Metal". At 640x360 the top rows and the tab strip are cut off (W1). |
| 3 | Front-end Options / Mods / Achievements (`s3_fe_options`, `s3_fe_mods`, `s3_fe_achievements`) | `BANISHED_AUTOMENU_BUTTON=4/7/8` | PASS | `_sheets/s3_menus_pause.jpg` | |
| 3 | Pause menu Mods, Achievements, Tutorials list, Extensions, Map (`s3_pause_mods/achievements/tutorial/extensions/map`) | pause buttons 7, 8, 12, 13, 11 | PASS | `_sheets/s3_menus_pause.jpg`, `_sheets/s3_dialogs_options.jpg` | Map dialog shows town name, seed 4242, Valleys, Small, Fair, Easy (matches the launch). |
| 3 | Pause menu Quit Game / Quit to Main Menu (`s6_pause_quit_game`, `s6_pause_quit_to_menu`) | pause button 6 / 5 | PASS | `s6_pause_quit_game/result.txt` exit=0; `s6_pause_quit_to_menu/shot_34s.jpg` (menu with the winter scene) | |
| 3 | Tutorial game (`s4_tutorial`) | `BANISHED_AUTOTUTORIAL=1` | PASS | `_sheets/s4_tutorial_bigwindow.jpg` | Tutorial dialog with arrow, greyed toolbar. |
| 3 | Goals dialog | pause button 10 | **NOT RUN** | | Only valid in a scenario game; at the front end it faults on a null world by design (menu-driver skill). |
| 4 | Fire (`s2_fire`) | `BANISHED_FORCE_FIRE=2 ..._FRAME=600` | PASS | `_sheets/s2_disasters_snow.jpg`; log `2/2 house(s) now report a non-zero burn timer` | Flames and black smoke drawn, fire status icon bottom-left. |
| 4 | Tornado (`s2_tornado`, `s4_tornado_cam`) | `BANISHED_FORCE_TORNADO=1` | PASS (weak) | `s4_tornado_cam/shot_26s.jpg` | Tornado icon appears and the swirl/dust cloud enters at the screen edge; the tornado was never centred on screen (its start tile is random per run), so the swirl mesh itself was not inspected closely. |
| 4 | Sickness (`s2_sick`) | `BANISHED_FORCE_SICK=4` | PASS | log `[Sick] infecting 4/4`; plague icon bottom-left | |
| 4 | Crop infestation (`s2_infest`, `s4_infest`) | `BANISHED_FORCE_INFEST=1` | INERT | log `[Infest] ... every one of them is already cureable` | Not a failure: the town has no living crop plants at that frame, so there is nothing to infest. Needs a longer run with a planted field. |
| 4 | Snow cover 1.0 and 0.5 (`s2_snow_full`, `s2_snow_half`) | `BANISHED_FORCE_SNOW=` | PASS | `_sheets/s2_disasters_snow.jpg` | White ground, snowy roofs, pine foliage change; half snow blends correctly. |
| 4 | Rain / fog / night | observed, not forced | PASS (observed) | `s1_climate_harsh`, `s1_diff_hard`, `s2_seasons_50x/shot_32s.jpg`, `_sheets/s2_seasons_panel.jpg` | Rain streaks and a dimmed grey scene appear on their own. The dim scene at 32-40 s of the 50x run is night or fog; I could not tell which from the log (no flag forces the time of day). |
| 4 | Seasons at 50x (`s2_seasons_50x`) | `BANISHED_FORCE_SPEED=50`, 60 s | PASS | `_sheets/s2_seasons_panel.jpg` | Only ~40% of a year passed (1800 frames), so no winter in this run; winter is covered by rows 1 (small/large map winter starts), `s5` (below) and the snow rows. The first shot is the loading screen (fine). |
| 4b | Building info panel held open (`s4_panel_hold`) | `BANISHED_PANEL_HOLD=ResidenceComponent` | PASS | `s4_panel_hold/shot_32s.jpg` | "Wooden House" panel, Occupants/Inventory tabs, rows, icons, close button all drawn. (`s2_panel_hold` with `ConsumeProduceComponent` printed nothing: no such building at the start, harness stays silent.) |
| 5 | 5-minute run at 10x, normal start (`s5_10x_5min`) | `BANISHED_FORCE_SPEED=10`, `SMOKE_FULL=1`, 298 s | PASS | `_sheets/s5_10x_5min.jpg`; boot_state `frame=34538 uptime=293`; crash log 0 | Summer -> autumn (yellowing foliage) -> winter (snow, snowy roofs) all drawn; thirst status icon appears at 270 s. 112 fps reported by the frame limiter at the start. |

## Watch items (visual or layout observations, none is a crash)

- **W1 (layout, test artefact, not a port bug):** `OPENBA_BACKGROUND=1` forces a 640x360 window. The retail UI is laid out
  for a larger screen, so at that size the Options / Load / Save / Mods dialogs are cut off at the top and bottom (Options
  loses its tab strip), the pause menu's last buttons run past the bottom edge and the "Banished" logo sits behind the
  "New" button. At 960x540 (`OPENBA_WINDOW_WIDTH=960 OPENBA_WINDOW_HEIGHT=540`, still background and non-activating) the
  main menu, the Options tab strip and the Save dialog all fit and look right (`s4_big_menu`, `s4_big_options_video`,
  `s4_big_save`). Use 960x540 for any future dialog check. A real player at a small window size would see the same
  clipping; whether retail does too (it enforces a minimum window size) is untested.
- **W2 (possible, unverified):** in the Save dialog the selected empty slot is a solid black square with a white dashed
  circle, while the other empty slots are dark grey with a grey circle. `SaveLoadDialog` fills the "new save" slot with the
  container's `spriteSaveLoadEmpty` sprite (`src/game/Dialog/saveloaddialog.cpp` ~line 425) and the current-game thumbnail
  is not drawn. Probably the same as retail, but compare against a retail screenshot; the earlier investigation on screenshots /
  readback (`src/render/metal`) is the owner if it turns out wrong.
- **W3:** `s4_tornado_cam`: the tornado swirl was not inspected centred (see table).

## Tooling gap: toolbar menus, building ghosts, roads, fields by hand

Placement tools need a mouse position and clicks. Posting events straight to the game's PID with `CGEventPostToPid`
(so the real cursor and the frontmost app stay untouched; source in `local artifact “inject.swift”` (local, not shipped)) **does not work**: the
background window runs with `NSApplicationActivationPolicyProhibited` (`src/render/metal/background.mm`), SDL never
gets keyboard or mouse focus, and `OPENBA_INPUT_TRACE=1` showed 36 consecutive samples of `focus=0 mouse_px=0.0,0.0`
while a move/click/scroll/key sequence was being posted. Typing or clicking for real would steal focus from the
video the owner is watching, so I did not.

What the existing automation does cover: front-end and pause-menu buttons (ids in `.claude/skills/menu-driver`),
the options tab (via `GameLastOption`), world setup, camera pins, disasters, snow, speed, held building panels,
and finished buildings (`BANISHED_BUILD_DEMO_*`).

Suggested patch to close the gap (not applied, it touches `src/diag/` and the shared `src/app/options.h`):
add a harness `BANISHED_SMOKE_TOOL=<templatePath>[,frame]` next to `src/diag/food_panel_harness.cpp` that, at the given
frame, does exactly what that file already does for the Pasture case (`StartTown_ResolveTemplateByPath("Template\\X.rsc")`
then `entityUI->SelectTool(tmpl)`), then moves the tool's cursor cell to the founding-cluster centre every frame so the
tool draws its ghost; one run per template (house, road, crop field, pasture, orchard, storage, market). Opening a
toolbar menu is the same idea through `ToolbarDialog`'s action handler (`ToolbarUtility_HarnessDriveController`,
`src/game/Dialog/toolbardialog.cpp`). A second option is a non-activating build with real hit-testing, which is a
window-owner decision.

## How to rerun

```
cmake -S . -B build/mac-smoke -G Ninja -DCMAKE_OSX_ARCHITECTURES=arm64 -DBANISHED_USE_CCACHE=OFF
cmake --build build/mac-smoke -t OpenBA -j3
BANISHED_AUTOSTART=1 BANISHED_CLIMATE=harsh local artifact “run_scenario.sh” (local, not shipped) my_run 36        # <= 60 s (SMOKE_FULL=1: <= 330 s)
python3 local artifact “sheet.py” out.png local artifact “shot_*.jpg” (local, not shipped)                   # contact sheet (needs Pillow)
```
`local artifact “run_scenario.sh”` (local, not shipped) header lists its knobs (`SMOKE_SHOTS`, `SMOKE_SETTINGS`, `SMOKE_SEED_SAVES`, `SMOKE_ENGINE`).
Scenario flags are ordinary environment variables; every flag used here is declared in `src/app/options.h`.

## Gates at the end of the tour

- `unit` built in `build/mac-smoke` (it failed to compile at 15:03 because concurrent
  `tests/unit/display_geometry_test.cpp` was mid-edit; it built at 15:47): **1193 `ok` lines, 0 failures**
  (`TMPDIR=build/mac-smoke/tmp BANISHED_DIR=<Steam install> build/mac-smoke/unit`).
- No source file was edited by this tour. Added: `local artifact “smoke” :: ` (local, not shipped) (`run_scenario.sh`, `sheet.py`, `inject.swift`) and
  `docs/macos/SMOKE.md` plus `docs/macos/smoke/` (local, not shipped).

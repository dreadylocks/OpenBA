# FAA-MENU hand-in (2026-10-16)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/enhanced_tab/shadow_res_entry.cpp` moved to `src/ext/max_shadow_res/enhanced_entry.cpp`; `EnhancedTab::FindIn` is now `Ext::FindElement` (`src/ext/ui/find_element.h`); the Options dialog's `EXT ARM` comments are `EXT SEAM`.)

The foliage and shadow entries of the Options dialog's "Enhanced" tab (docs/extensions/FOLIAGE_AA.md §3 "Menu",
§4, §7), built on AOMENU's pattern. Branch `worktree-agent-aa72cca21bfaf8a8f`, on main 8e88908b2 (main had not moved
at the final `git merge main`). Commits: `c22afd945` (tab move, entries, icons), `6437e9caf` (the deadlock fix, the
sprite-sheet lines).

## 1. What shipped

| Item | Where |
|---|---|
| Tab moved out of ao/ | `src/ext/enhanced_tab/enhanced_tab.{h,cpp}` (git mv of `options_tab.*`, namespace `Ext::EnhancedTab`). It holds the AddTab call, the dialog key and a list of `Contribution {name, open, apply, close}`; `src/game/Dialog/optionsdialog.cpp` calls `Ext::EnhancedTab::Open / Apply / Close` (the three arms are AOMENU's, only renamed). The resource file is `src/ext/ui/Dialog/EnhancedOptions.rsc` (git mv of `AoOptions.rsc`; `tools/ext_resources.sh` follows). |
| AO rows (no behaviour change) | `src/ext/ambient_occlusion/enhanced_entry.cpp`: AOMENU's combo + slider code, moved verbatim into a contribution |
| Foliage edges combo | `src/ext/foliage_aa/enhanced_entry.cpp`: Off / Smooth edges / Smooth edges + distance fill = `FoliageAA::Current().mode` 0 / 1 / 2; Apply = `FoliageAA::Set` (Off turns the bool row off and keeps the stored mode key; 1 / 2 set the key and the row on) |
| Max shadow resolution Off / On | `src/ext/enhanced_tab/shadow_res_entry.cpp` (the row has no code directory): a combo (the tab's kit has no checkbox in the page), Apply = `SetAndRecord(max_shadow_res)` |
| Row icons | `tools/ext_icons/faa.py` (our own 12 x 12 pictograms: a leaf; a tree and its shadow), `tools/ext_sprites.py` regenerates, the two `ExtensionsEntry_*` lines in `ExtensionsSprites.rsc`; the 18 existing PNGs regenerate byte-identical |
| Live-toggle fix | see 3 |

Persistence is AOMENU's: Apply / OK record the keys into the settings map, retail's `WriteSettings` flushes; Cancel
writes nothing. The Extensions dialog's checkbox for `foliage_aa` is the same bool, so it mirrors the combo.
No getenv, no new `BANISHED_*` flag.

## 2. Evidence (debug exe, game-dir lock `faamenu`, settings.cfg backed up and restored byte-identical (`cmp`), no .pkm)

Route: `BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=900 BANISHED_PAUSEMENU_BUTTON=4` (Options in game, `GameLastOption=4` opens the
Enhanced tab), real mouse input. 2560x1377 client, client origin at screen -6,132.

- **Tab shows the entries**: four rows, "Ambient occlusion / AO strength / Foliage edges / Max shadow resolution"; stderr
  `[EnhancedTab] Enhanced tab added: tabs=5 entries=3`. Screenshots `build/faamenu/tab.png`, `list.png`, `list2.png` (git-ignored).
- **Live, foliage**: combo -> "+ distance fill", Apply: `[FoliageAAMenu] Apply: mode=2`, `[FoliageAA] request mode 2 ... seam set`,
  `transformed ... coverage fill` lines; combo -> Off, Apply: `stage reset ...`, `stage off: retail state restored, seam cleared`.
- **Live, shadow**: Off -> On, Apply: `[Ext] ext.max_shadow_res requested=1`, `[ShadowRes] sun map 4096x4096 (raw 4096, VideoShadowScalar=0, ...)`;
  On -> Off: `sun map 2048x2048`. No hang with `ext.render_pipe` on (see 3).
- **OK persists**: fresh run, set "Smooth edges" + shadow On, OK: settings.cfg `ext.foliage_aa=1 / ext.foliage_aa_mode=1 / ext.max_shadow_res=1`;
  next run's tab opens on Smooth edges / On (`reopen.png`).
- **Cancel discards**: state off/off in the file, edit to "Smooth edges" + On, Cancel: file unchanged (`ext.foliage_aa=0`, `ext.max_shadow_res=0`).
- **Extensions dialog**: both rows show their new icons (`ext3.png`); unticking "Smooth foliage edges" there + OK: `ext.foliage_aa=0`,
  `[FoliageAA] stage off`, mode key kept at 1 (the mirror).
- **Gates**: `tools/build.sh run-menu` (RUNNING, crash log 0 bytes) and `tools/build.sh run` (`/profile:faithful`, new game reached
  `[Vitals]`, `[AutoStart] sequence fired`, crash log 0 bytes), rows off. `tools/build.sh unit`: 1225 tests, 0 failures. No stress run.

## 3. Defect found and fixed: sun-map toggle deadlock under render_pipe

With `ext.render_pipe` on (the user's settings), flipping Max shadow resolution live hung the exe (exit 4, `standalone_hang.log`): the main
thread waits in `RenderPipe::Handoff::WaitIdle`, the render thread sits in `DeviceData_Resize -> Device::AdjustDisplay -> SDL_SetWindowPosition`
(a window move needs the main thread). Cause: `SunLight::ExtSyncMaxShadowRes` raised `Device::settingsChanged` inside `AddDrawItems`, i.e. in a pipelined
frame after the tail's sequential decision, so the render thread's `BeginCommands` ran the resize. (Retail's own combos raise it between frames.)
Fix: `SetAndRecord` for `max_shadow_res` calls the new `SunLight::ExtRequestMaxShadowResSync()` (main thread, between frames), which runs the same
descriptor rewrite + `settingsChanged` through the last light the sync saw (cleared in `ReleaseFields`); the next frame is sequential and the resize
runs on the main thread. This also covers the Extensions-dialog checkbox. Files: `src/engine/graphics/SunLight.h`, `sunlight.cpp`, `src/ext/registry_boot.cpp`.

## 4. Mouse-script recipe (nothing of it is in the tree)

`build/faamenu/ui.ps1` (git-ignored; AOTUNE's SetCursorPos / mouse_event script, by PID) and `run.ps1` / `runext.ps1` (launch + wait for the
`[EnhancedTab]` line). Client coordinates at 2560x1377: foliage combo 1376,599; its list Off / Smooth edges / + fill at 1344,628 / 652 / 673;
shadow combo 1376,632 (the dialog widens a few px once the long item is chosen); its list Off / On 1290,660 / 1293,685; Apply 1330,905; OK 1463,905
(1569,946 in the Extensions dialog); Cancel 1221,905. Extensions dialog: `BANISHED_AUTOMENU_BUTTON=13`, mouse wheel at 1280,717 to scroll,
the foliage row's checkbox at 1501,806.

## 5. Notes

- Choosing the long item widens the combo column by a few pixels (cosmetic).
- Icons are programmer art like the others.
- `git diff --stat` holds no game content: C++ sources and headers, one `.rsc` (our own names, inheriting the kit by reference), two small
  PNGs from the script (our pictograms), a Python tool file, and this markdown. No retail sprite, shader, string table, font or `.rsc` copy.
- The worktree needed `third_party/SDL` copied from the main checkout (untracked, not committed).

## 6. Retail Shadow Resolution combo under `ext.max_shadow_res` + `ext.render_pipe` (coordinator's test)

Scenario: `ext.max_shadow_res=1`, `ext.render_pipe=1`, in game, Options > Video, the retail Shadow Resolution combo High -> Medium -> High, Apply each.
**Before the second fix it hung** (exit 4, `standalone_hang.log`: `ResizeTrace #3` on a second thread id, the render thread in `DeviceData_Resize`):
`ApplyVideoSettings` stored the new `shadowScalar` and raised `settingsChanged` on the main thread, but the sun-map descriptor still said 0x1000, so the
per-frame sync in `AddDrawItems` found the mismatch after the tail's sequential decision and raised it again from the pipelined frame.
Fix at the root: `OptionsDialog::ApplyVideoSettings` (src/game/Dialog/optionsdialog.cpp) calls `SunLight::ExtRequestMaxShadowResSync()` right after it stores
`shadowScalar`, before `ApplyChangedSettings`, so the descriptor rewrite for a retail combo change also happens on the main thread and the rebuild lands
at the right size; the per-frame sync stays as the logging / fallback path and finds nothing to do.
After: High -> Medium -> High, no hang, crash log 0 bytes, every `ResizeTrace` entry on the main thread's id, `[ShadowRes]` evidence:
`sun map 1024x1024 (raw 2048, VideoShadowScalar=1, ext.max_shadow_res=1)` then `sun map 4096x4096 (raw 4096, VideoShadowScalar=0, ext.max_shadow_res=1)`.
Not covered: a failed `ApplyChangedSettings` rolls `shadowScalar` back after the descriptor was rewritten (the per-frame sync would then raise from the render thread; a rollback of this combo is not reachable from the UI).
Note for lanes: another lane's deploy replaced the game-dir exe mid-session once (the log showed the old `[AoMenu] Enhanced tab added` line); check the exe before reading a log.

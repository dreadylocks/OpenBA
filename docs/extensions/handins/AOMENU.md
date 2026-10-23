# AOMENU hand-in (2026-10-14)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `Ext::AO::Current` / `Set` are `Ext::AmbientOcclusion::Current` / `Set`; `EnhancedTab::FindIn` is `Ext::FindElement` (`src/ext/ui/find_element.h`); the Options dialog's `EXT ARM` comments are `EXT SEAM`; `src/ext/ambient_occlusion/ao.{h,cpp}` moved to `ambient_occlusion.{h,cpp}` (namespaces `AmbientOcclusion` / `Ext::AO` / `AoMath` -> `Ext::AmbientOcclusion` / `Ext::AmbientOcclusion::AoMath`), `tests/unit/ext_ao_test.cpp` to `ext_ambient_occlusion_test.cpp`.)

The menu half of `ext.ambient_occlusion` (docs/extensions/AO.md §3 "Settings", §4, §6.3): the
registry's first **valued keys**, `Ext::AO::Current()` / `Set()` (the `params.h` contract with
AOCORE, unchanged), and a fifth Options tab **"Enhanced"** added at run time by
`UI::TabElement::AddTab`. Started from the spike's method (a) only (AOSPIKE S6, commit 988c47ada):
its `AoOptions.rsc` pair and its AddTab call shape; method (b) (`OptionsExt.rsc`, the
`mainmenudialog.cpp` arm, `ao_spike_ui.h`, the stderr logging) was not taken. Branch
`worktree-agent-a39cb53d4ec6958df`, on main 53ac9b9c4.

## 1. What exists

| File | What |
|---|---|
| `src/ext/registry.h` / `.cpp` | `Ext::Values` (third member of `State`), `ValueKey` {id, owner row, min, max, default, slot}, the table `kValueKeys`: `ao_quality` 1..3 default 2, `ao_opacity` 0..100 default 100 (owner `ambient_occlusion`). `ParseValue` / `ClampValue`, `Val()`, `Value()`, `SetValue()`. `Resolve` reads them under the per-entry profile; `LoadReport` gains `malformedValues` / `clampedValues`. The bool row `ambient_occlusion` (Tier::Extension, Class::A, default off, "Ambient occlusion", text as AOCORE's, byte for byte, after `quick_screens`). |
| `src/ext/registry_boot.cpp` | `SetValueAndRecord(k, v)`: `SetValue` then `Persistence::Set("ext.", id, "%i")`; one `[Ext]   ext.<id>=<v> (min..max, of <owner>)` boot line per key; the summary line counts valued keys. No Sync hook added (AOCORE adds `AmbientOcclusion::Sync()` to InitFromSettings and SetAndRecord). |
| `src/ext/ambient_occlusion/settings.cpp` | `Current()`: quality = row on ? `ext.ao_quality` : 0, opacity = `ext.ao_opacity` (rebuilt per call, `thread_local` storage). `Set(p)`: quality > 0 records `ao_quality` (clamped 1..3); always records `ao_opacity` (clamped); the row follows `quality > 0` through the registry's own `SetAndRecord` (only when it changes), so its Sync hooks fire and the Extensions checkbox mirrors it. |
| `src/ext/ambient_occlusion/options_tab.h` / `.cpp` | The tab: `Offered()`, `Open(dialog, tabs)` (load the two descriptions, `AddTab`, find `comboAoQuality` / `sliderAoOpacity` by name in the returned page, fill from `Current()`), `Apply(dialog)` (-> `Set`), `Close(dialog)`. The two element pointers live in a file-static side struct keyed by the dialog instance, not in `OptionsDialog` (sizeof 0x1C0 stays asserted). |
| `src/ext/ui/Dialog/AoOptions.rsc` | NEW names only: `aoStrings`, `aoQualityLevels` (Off / Low / Medium / High), `buttonEnhanced` + `labelEnhanced` (sprite `OptionsEnhanced` from `ExtensionsSprites.rsc`), the page `enhancedOptions` (a 2 x 2 table in `gameOptionsTable`'s shape), `comboAoQuality` (`SharedElements.rsc:textCombo`, `_initialData` = the levels), `sliderAoOpacity` (`SharedElements.rsc:slider`, readout `Options.rsc:number`). |
| `src/ext/ui/Dialog/ExtensionsSprites.rsc` (+ 2 PNGs) | `OptionsEnhanced` (16 x 16 white-with-alpha, the retail tab icons' form: a ball in a soft contact shadow) and `ExtensionsEntry_ambient_occlusion` (the Extensions row icon; AOCORE confirmed it adds none). |
| `tools/ext_sprites.py`, `tools/ext_icons/ao.py` | `options_enhanced()`; the row pictogram. Re-running regenerates every other PNG byte-identical. |
| `tools/ext_resources.sh` | TARGETS `:137-138` (`AoOptions.rsc:buttonEnhanced` / `:enhancedOptions`), deploy glob `:181` (`Dialog/AoOptions_*.crs`), header `:49`. 9 `.crs` compiled, 0 problem lines. |
| `tests/unit/ext_registry_values_test.cpp` | 5 tests: table shape (ranges, defaults == initialisers, ids valid / not shared with bool rows, owner registered), parse + clamp + malformed (13 malformed spellings), Resolve (absent / in range / clamped / malformed, counters separate from the bool rows'), the profile rule (faithful / all, settings key and command line), SetValue + lock + the `%i` round trip through `IO::SettingsFile`. Globbed by CMake; `registry.cpp` was already in `sources.cmake`, so no list edit. |

Parse rule (`ParseValue`): an optional `-` and 1..9 digits, nothing else. A well-formed number
is CLAMPED into range (`ok`); anything else (empty, `+5`, `0x10`, `1.5`, `5x`, ten digits) is
malformed = the key's default, counted. **Profile rule**: `faithful` and `all` leave every valued
key at its default (a number has no "retail" or "on" value); the toggles follow the profile as
before.

No getenv, no `BANISHED_*` flag, no door, no hook vtable, no edit to `params.h`.

## 2. Edits outside `src/ext` (`src/game/Dialog/optionsdialog.cpp`)

| Line | What |
|---|---|
| 50 | `#include "ext/ambient_occlusion/options_tab.h"` |
| 709-718 | In `OnOpened`'s tab block (the last block of the function): `Ext::AO::OptionsTab::Open(this, tabOptions)` BEFORE the restore, then the restore's clamp `lastTab > tabOptions->_tabCount - 1` (was `> 3`, see §3). |
| 728-730 | `OnClosed` (retail `RET 0`): `Ext::AO::OptionsTab::Close(this)`. |
| 1059-1061 | `OnApply` (OK calls it too): `Ext::AO::OptionsTab::Apply(this)` after `ApplyGameSettings`, before retail's `WriteSettings` which flushes the three keys. |

Cancel needs no arm: nothing is recorded before Apply, and the next `Open` re-reads `Current()`
(retail's `OnCancel` re-reads settings.cfg into the map; the registry's in-memory values were
never changed).

**Visibility ruling.** The tab is offered whenever the build has the extension, NOT gated on the
row's value: the player turns AO on from this tab (combo Off -> Low). The one exception is the
gate: under `/profile:faithful` from the command line (`Ext::GateProfileForced()`, what
`tools/build.sh run` and the resave gate pass) the tab is not added, so the dialog is retail's
four tabs, like the main menu's missing Extensions button.

## 3. The `lastOption` clamp: the PORT's, not retail's

Retail `OptionsDialog::OnOpened` (0x1400742d0), its tail, objdump of `Application-x64.exe`:

```
1400750c1  call  [FindElement]            ; "tabOptions"
1400750ca  mov   [rsi+0x1b8], rax
1400750db  mov   rax, [0x1401c3de0]       ; g_gameSettings
1400750e2  mov   ebx, [rax+0x1c]          ; lastOption (settings[7])
1400750ec  call  [CheckPtr]               ; bare RET
1400750fb  call  [TabElement::SetActiveTab]  ; edx = ebx, unclamped
```

No compare, no null test: retail passes the stored value straight to `SetActiveTab`. The
`if (lastTab < 0 || lastTab > 3) lastTab = 0` was the port's own guard. Per the brief it is
replaced outright by `_tabCount - 1` (both arms: with four tabs it is the old `> 3`); the comment
at :714-716 says it is the port's. A saved `GameLastOption=4` therefore opens the Enhanced tab
when the tab exists, and tab 0 under the gate (verified, §4 run 3).

## 4. Verification (runs under `tools/game_dir_lock.sh`, mods parked, settings.cfg backed up and restored)

Debug exe, `tools/build.sh deploy`; `tools/build.sh unit`: **1212 tests, 0 failures** (5 new).
Clicks delivered with real mouse input (a scratchpad `SetCursorPos` / `mouse_event` script; the
menu driver has no Options-dialog tokens), screenshots `tools/capture_window.ps1`, saved under
the worktree's `captures/` (git-ignored):

Run 1 (`BANISHED_AUTOMENU_BUTTON=4`, settings `GameLastOption=4`, no ao keys, row off):
1. `aomenu_1_tab_restored.png`: Options opens ON the fifth tab "Enhanced" (lastOption 4
   restored); combo **Off** (row off -> quality 0), slider 100. `[AoMenu] Enhanced tab added:
   tabs=5 ... quality=0 opacity=100`.
2. `aomenu_2_combo_open.png`: the dropdown lists Off / Low / Medium / High.
3. `aomenu_3_edited.png`: High, slider dragged to 90. OK -> `[Ext] ext.ao_quality=3`,
   `ext.ao_opacity=90`, `ext.ambient_occlusion requested=1 active=1`; settings.cfg then held
   `ext.ambient_occlusion=1`, `ext.ao_opacity=90`, `ext.ao_quality=3`, `GameLastOption=4`.
4. `aomenu_4_reopened_after_ok.png`: Options reopened from the menu: High / 90, tab 4.
5. Low + 80, **Cancel**, reopen: `aomenu_5_reopened_after_cancel.png` High / 90; no `[Ext]`
   write logged; settings.cfg unchanged.
6. Combo Off, OK: `ext.ambient_occlusion requested=0`, `ext.ao_quality` stays 3.
   `aomenu_6_extensions_row_off.png`: the Extensions dialog's new row "Ambient occlusion"
   (its icon, class A) shows Disabled. Ticking it there + OK: `requested=1`; reopened Options
   shows **High / 90** (`aomenu_7_after_checkbox_on.png`): the stored tier survived the
   checkbox round trip (item 4 of the brief).
7. Exit from the menu: clean (no crash log).

Run 2 (`BANISHED_AUTOSTART=1 BANISHED_PAUSEMENU=600 BANISHED_PAUSEMENU_BUTTON=4`): the in-game
pause menu's Options opener shows the same tab, High / 90
(`aomenu_8_ingame_pause_options.png`); Apply and OK in game logged
`[AoMenu] Apply: quality=3 opacity=90`; the world kept running; Exit clean (QUITTING, no crash).

Run 3 (`/profile:faithful` on the command line, `GameLastOption=4` still saved): four tabs, no
`[AoMenu]` line, the clamp opened tab 0 (`aomenu_9_gate_faithful_no_tab.png`).

`git merge main`: main had not moved from 53ac9b9c4 (the base), so the runs above are the final
build's.

## 5. What is not verified / open

- **The live frame.** This branch has no AO pass (AOCORE's); "the combo and slider change the live
  frame" is verified only up to `Current()` returning the new values the moment Apply / OK
  returns. The pass picking them up next frame is AOCORE's side of `params.h`, to be seen after
  the merge.
- **Merge notes for the coordinator.** (1) The `ambient_occlusion` row and field are on both
  branches, identical text: keep one. (2) AOCORE's temporary
  `src/ext/ambient_occlusion/params_stub.cpp` must be deleted (it defines `Current` / `Set` too).
  (3) AOCORE's `AmbientOcclusion::Sync()` calls in `registry_boot.cpp` sit beside mine; no
  overlap (I added none).
- Only the debug exe was run; perf / release not built. No `tools/build.sh run` gate run (the
  coordinator's); run 3 is the gate's profile on the menu route only.
- The slider moves in its description's steps (a drag moved it 10 at a time, as retail's UI
  scale slider does); any 0..100 value is accepted from settings.cfg.
- `Apply` re-records the two keys even when unchanged (two `[Ext]` lines per Apply); the row is
  written only on a change.
- The two icons are programmer art (the user replaces the PNGs).
- Linux bed not run.

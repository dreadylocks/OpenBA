# UI scale default on Retina

Facts (verified by running):

* `_uiScale = GameSettings::uiScale / 100` is layout units per device pixel. A SMALLER value
  gives a BIGGER UI. The earlier note in an earlier input/window note ("raise to 200") had the direction
  backwards: 200 makes the UI smaller still. On a density-2 window the physical-size match
  is uiScale 50 (= 100 / density).
* Retail's slider range is 75..150, hard-coded in `OptionsDialog` (code, not Options.rsc).
* Change: on first run (no `GameUIScale` key in settings.cfg) and pixel density > 1,
  `GameSettings::Load` sets uiScale to `MacDisplay::DefaultUiScalePercent()` (50 at density 2).
  Density 1 and any saved value are untouched. The Metal backend records the density
  (`src/platform/common/display_density.h`).
* The Options slider range is retail's divided by the density (38..75 at density 2) so 50 is
  reachable; identity at density 1.
* Caveat: a value saved at density 2 (e.g. 50) is a large UI if the same settings.cfg is later
  used at density 1.

## Crisp text on a 2x window

On a Retina display (native pixel mode is always on) the UI draws at ratio 2, above the 1x variant range
(`VariantLow..VariantHigh`, 2/3 to 4/3 of a font's height). `ext.sharp_ui` then asks
`Ext::Resources::InitFonts` for a Retina variant (`PickRetinaVariantHeight`, heights up to
`RetinaHigh` = 8/3 of the base: Font 2x, FontLarge 96 px), rasterised at run time from
`FrancophilSans.ttf`. 1x picks are unchanged.

The engine looks for the typeface by name (`FrancophilSans.ttf`): beside the executable first,
then `~/.fonts`, `~/.local/share/fonts`, `/usr/local/share/fonts`, `/usr/share/fonts`. It does
not search `~/Library/Fonts` or `/Library/Fonts`, and it accepts no other font. The file is not
in the tree (licence; see `tools/release/FrancophilSans_README.txt`): drop it next to the
executable. Without it the log shows one `[ExtRes] font variants off: typeface FrancophilSans
absent` line and the trace shows `retail(bad N)`: retail's glyph sheets scaled 2x (soft text).

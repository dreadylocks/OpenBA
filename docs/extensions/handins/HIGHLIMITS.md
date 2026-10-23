# HIGHLIMITS hand-in (2026-10-20)

Branch `ext-high-limits`, one PR, based on `main`. The task: let the town's resource limits be set above 999,999 as an OpenBA extension.
**Result: `ext.high_limits`, one class B toggle (locked for the game, recorded in the save, no per-town state), off by default: the limit boxes take 0..999,999,999 and 9 typed digits. Built, unit-tested and looked at in a running game (section 6).**

## 1. Where the cap is
The stored limit (`CitizenBehavior::professionResources[i].maxCount`) is an `int32` that the game saves, loads and compares with no upper bound. Nothing but the spinner that sets it stops a bigger value, and the spinner stops at 999,999 for two reasons, both in the UI:
* `ResourceLimitUI::OnCreated` (`resourcelimitui.cpp`) gives the row's "editLimit" spinner `SetMinMax(0, 999999)`;
* the spinner's edit field takes only six typed characters (the description `spinnerEditSmall6`: `_maxLength` 6, `_maxValue` 999999).

## 2. What it does
Two UI limits, both on the one controller that serves every limit spinner (the Town Hall's Production tab, the Limits window, and the dialog of each production building that shows its limit):
* the range: `SetMinMax(0, 999999)` becomes `SetMinMax(0, 999999999)`;
* the typed-character limit of the spinner's edit field (`EditElement::_maxLength`, 6 from the description `spinnerEditSmall6`) becomes 9. Retail stops at 999,999 on both input paths: the up arrow is disabled at the field's maximum (`SpinnerElement::Update`) and `NumberEditElement::SetValue` clamps to it, and typed text is cut at six characters and clamped to the range. So both paths need the new range, and only typing needs the longer digit limit.

## 3. Where
| file | change |
|---|---|
| `src/ext/high_limits/high_limits.{h,cpp}` (new) | the retail and high values (`kRetailMax`, `kHighMax`, `kRetailDigits`, `kHighDigits`), `Active()`, `MaxValue()`, `MaxDigits()` |
| `src/ext/high_limits/spinner.cpp` (new, exe only) | `ApplyToSpinner`: the edit field's `_maxLength`, then `SetMinMax` |
| `src/game/Dialog/resourcelimitui.cpp` | the one seam, in `ResourceLimitUI::OnCreated`: `if (Active()) ApplyToSpinner(spinner); else spinner->SetMinMax(0, 999999);` (retail's call kept in the else arm) |
| `src/ext/registry.{h,cpp}` | the row (class B, version 1, OFF) |
| `tools/ext_icons/highlimits.py`, `Entry_high_limits.png`, `ExtensionsSprites.rsc`, `ext_sprites_data.h` | the row icon (programmer art, class B amber) |
| `tests/unit/ext_high_limits_test.cpp` (new), `tests/unit/sources.cmake`, `docs/extensions/LIST.MD` | the tests and the list row |

No other engine change and no new setting keys beyond `ext.high_limits`.

## 4. Why class B
No per-town state (the limits are retail's ints in retail's save), so not C. But a limit above 999,999 is a value the faithful spinner cannot show or edit, and a game saved with one would play the same in faithful mode while its dialog could no longer display it, which is the better_camera ruling's shape (the extension lets the player write an out-of-domain value into a faithful path). So the row is locked for the game and recorded in the save as a pseudo-mod.

## 5. Behaviour notes
* **Display width.** The edit field's description gives it a 42-pixel minimum, which grows for a longer typed value (so only the row being typed in widened and the columns went uneven). With the row on, every limit box is raised to at least 64 pixels (`kHighMinWidth`, room for nine digits at FontSmall) in `ApplyToSpinner`, so all boxes are at least that wide and the window sizes to them. The width is raised with `max`, never lowered: a description that asked for a wider box keeps it. Set through the public `_minWidth` field, since `Element::SetMinSize` is declared but not defined in this port; `OnCreated` runs before the first layout.
* **Other spinners** (trade quantities, workers, the Foods/Priority pages) are separate controllers and keep their own ranges.
* **Separate from this PR:** `ResourceLimitUI::IdxFor` accepts a config's resource index only below 11 (`resourcelimitui.cpp`), where retail indexes the ledger by whatever the config holds, so the limit rows of `Custom0..9` (indices 11 to 20) are wrong in this port. That is its own fix and its own PR; this extension does not depend on it and does not change `IdxFor`.

## 6. Verified / not verified
* VERIFIED on this branch (based on `main` 6dfc487, MinGW on Windows): `OpenBA.exe` and the unit suite build; unit 1191 / 0 failures / 6 skipped. The ABI audit and the serializer check were not run (the maintainer runs them). The 3 new tests: the registry row (class B, off, the faithful profile turns it off); the values are consistent (retail 999,999 is exactly 6 digits, 999,999,999 exactly 9 and fits an int32); the values follow the row, and the faithful profile wins over the toggle.
* VERIFIED in a running game (2026-10-20, a Windows run by the author): a nine-digit value typed into the Limits window (it displays in full; before the width change the box widened only for that row); with the width change the boxes look right in the Limits window, the Town Hall and one production building's dialog.
* VERIFIED in a running game (2026-10-20, a Windows run by the author): the arrows past 999,999; a limit above 999,999 saved and reloaded, and the higher limit was remembered.
* VERIFIED in a running game (2026-10-20, a Windows run by the author): a save written with a limit above 999,999 loads in retail (the retail exe, not this one) without trouble, and retail brings the limit back down to its own maximum of 999,999.
* To try it: build, turn **Higher limit values** on in the Extensions menu, start a town, open the Town Hall's Production tab, click a limit box and type 5000000.

## 7. Rules

* **No game content (DESIGN.md section 3, rule 7).** The extension adds no retail data of any kind. The row's icon (`Entry_high_limits.png`, from `tools/ext_icons/highlimits.py`) is our own programmer art. The numbers it changes are three integers (a range, a digit count and a pixel width) set on a spinner the game has already built from the user's own install.
* **Hook rules.** One seam, in `ResourceLimitUI::OnCreated`, which runs once per limit row when a dialog is built. It is not in a hot loop, reads no toggle per frame and adds no vtable or function-pointer table. Retail's `SetMinMax(0, 999999)` is kept unchanged in the else arm.
* **Performance.** Not measured with the stress town: the only change on the faithful profile is one branch when a limit dialog is built (rule 6 asks for faithful against everything-on; this row has no per-frame work to measure).
* **Not run:** the Linux and macOS builds, `tools/build.sh run` (the gate), the resave gate and the ABI audit. The maintainer runs the Windows gate and the serializer check (AGENTS.md section 7). The row touches no serialized class, and the only save change is the pseudo-mod record every class B row writes.

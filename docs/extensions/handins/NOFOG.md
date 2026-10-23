# NOFOG hand-in

`ext.no_fog`: takes the fog out of the picture. One class A toggle, **off by default**, live (no restart, not held by the game's lock), no serialized state.

## 1. What it does

Each frame the game blends the lighting record of the month (and, while weather is shown, of every live weather effect) into one `EnvironmentSetting`: ambient colour, light colour, fog colour, fog near, fog far, fog maximum, and the sun's heading and pitch (`WeatherSimulation::BlendLighting`, retail 0x140093ac0). The fog colour's fourth channel is the fog maximum. The lighting shader mixes `min((fogNear' + viewDepth) * fogScale, fogMax)` of the fog colour into each lit pixel, so zoomed far out the mix reaches the maximum (0.95 by default) and the far map fades into haze.

With the row on the blended record's fog maximum is 0, so nothing is mixed in whatever the depth, in every weather. The fog colour, near and far are left alone. Rain and snow keep their particles and their darker light; only the haze goes. (The game's own show-weather setting removes the particles and keeps the distance fog; this is the other half: the weather stays and the map stays readable.)

## 2. The seam

One, at the end of `WeatherSimulation::BlendLighting` (`src/game/Game/weather_sun.cpp`):

```
out->fogMax = Ext::NoFog::FogMax(out->fogMax, ::Ext::Ext().no_fog);
```

Both readers of the blended record go through that function: the shipped draw path (`WeatherSimulation::DrawUpdate`) and `ext.render_pipe`'s recorded path (`render_pipe.cpp`). The row is read on each call, which is once per drawn frame, so a toggle takes effect at the next frame. With the row off `FogMax` returns the bits it was given, so the record is retail's own: the seam is one load and one store of the same value. The call goes through the tested helper, so the unit test covers what the seam does.

## 3. Class

Class A: the value is rebuilt every frame from the month and the weather, only drawing reads it (the simulation does not), nothing is stored, and no serialized field can hold anything retail could not. A save made with the row on loads unchanged in retail and with the row off.

**Default OFF is a second class A exception** beside `eye_camera` (DESIGN.md section 2 says class A is on by default): how the map looks is the player's choice. This PR therefore touches two files outside the extension, `docs/extensions/DESIGN.md` (the table cell) and `tests/unit/ext_registry_test.cpp` (the default-on rule). Your call: if you would rather it ship on, those two edits go away and the row's default flips.

## 4. Where

| file | change |
|---|---|
| `src/ext/no_fog/no_fog.h` (new) | the constant and the pure `FogMax(retail, on)`; the header comment is the design |
| `src/game/Game/weather_sun.cpp` | the seam and its includes |
| `src/ext/registry.{h,cpp}` | the row (class A, version 0, OFF) |
| `src/ext/ui/Dialog/ExtensionsSprites.rsc`, `Sprite/Extensions/Entry_no_fog.png`, `src/ext/resources/ext_sprites_data.h`, `tools/ext_icons/nofog.py` | the row icon (programmer art, ours), baked with `tools/ext_sprites_bake.py` |
| `tests/unit/ext_no_fog_test.cpp` (new), `tests/unit/ext_registry_test.cpp` | the tests; the default-on rule gains the exception |
| `docs/extensions/LIST.MD`, `docs/runtime_port/STATUS.md`, `docs/extensions/DESIGN.md` | the list row, the status row, the table cell |

No game content ships (DESIGN.md hook rule 7): the icon is drawn by `tools/ext_icons/`, and the extension changes one runtime value. Inspired by Nemo Jr.'s No Fog mod, which removes the fog by replacing the environment resources; this extension needs none of its data.

## 5. Verified and not verified

* **Built and tested** on Windows (MinGW), release flavour, on `main` dd8a060: unit 1282 / 0 failures / 6 skipped (the kit and font skips), `OpenBA.exe` builds. `tools/acceptance/acceptance.sh` (0 lines off target) was run on the earlier base 30d98f2 and not repeated after the rebase. Tests: the row (class A, off by default, key, profile), the toggle live under the game lock (`EntryLocked` is false, `SetRequested` takes effect at once), the fog maximum (retail's bits with the row off, 0 with it on).
* **Performance (hook rule 6):** not measured with the stress town. The only change on the faithful profile is one load and one store of the same value per drawn frame, and the row adds no per-frame work beyond that.
* **Looked at in a Windows run by the author, on this build:** the same scene zoomed out in clear weather, rain and snow, once with vanilla, once with the community no-fog mod and once with the row on. In clear weather the mod and the row look the same, with the haze gone. In rain and snow both remove most of the haze, but the mod keeps a light veil (its rain block carries `_fogMax = 0.4`), where the row removes all of it while the particles and the weather's darker light remain. Zero was chosen on purpose.
* **Not run:** Linux, macOS, the resave gate (nothing is saved).

# macOS input and window behaviour

> Session log (2026-10-19 builds). Current facts are in [../../reference/macos_input_window.md](../../reference/macos_input_window.md). The numbers are the recorded results of those runs.

What the Mac port does with the mouse, keyboard and window, why, and how it was checked.
Code: `src/platform/sdl/{input,pump,display}_sdl.cpp`, `src/platform/common/{mac_keymap.h,display_geometry.*}`.
Everything below is on top of the platform layer the port shares with Windows and Linux; the
shared behaviour (what retail did, what SDL does differently) is documented in the banners of
those files.

## For players: keys and buttons

| You want | On a Mac |
|---|---|
| Quit | Cmd+Q, the Dock menu, or the red close button. All three end the game cleanly through the same path as Windows' close box (`SDL_EVENT_QUIT` / `WINDOW_CLOSE_REQUESTED`). Cmd+W closes the window the same way. |
| Minimise | The yellow button or Cmd+M. The game keeps its state and sizes its back buffer to 0 while minimised, as on Windows. |
| Full screen (this session) | Control+Command+F, or the green button / Window menu. macOS native full screen (its own Space). If the SDL menu entry is unavailable (the window is not resizable, e.g. when the game already runs the borderless "fullscreen" below) the pump handles the same chord itself. |
| Full screen (saved) | Options > fullscreen. This is the game's own setting: a borderless window pinned to the display and sized to the back buffer; it is saved in settings and restored next launch. |
| Rotate camera | Middle mouse button drag, or **Option + left-button drag** on a trackpad (no middle button). |
| Zoom | Mouse wheel / two-finger scroll. The direction does not depend on the "natural scrolling" system setting (the port un-flips `SDL_MOUSEWHEEL_FLIPPED`; retail's wheel meant one thing). |
| Right click | Right button, or two-finger click / Control-less secondary click as set in System Settings. Control+click stays a plain left click. |
| Edge scroll | Push the cursor against any edge of the game window; the cursor is confined to the window while it is focused, so the edge is reachable. |
| F1-F12 | On a laptop the top row is brightness / volume by default: hold **fn** (or turn on "Use F1, F2, etc. keys as standard function keys" in System Settings > Keyboard). |
| Ctrl-shortcuts | **Command acts as Control.** Cmd+key is delivered to the game as Ctrl+key; physical Control still works. (The Windows-key slot is cleared.) |
| Home / End / Page Up / Page Down / Delete | fn+Left / fn+Right / fn+Up / fn+Down / fn+Delete, which macOS already sends as those keys. Alternative: hold **Right Option** and press Left / Right / Up / Down / Backspace. |
| Insert | **Right Option + Return**. (The Help key of a full keyboard is Insert too.) |
| Scroll Lock / Print Screen / Pause | **Right Option + F10 / F11 / F12**, or F14 / F13 / F15 on an Apple extended keyboard. |

Right Option is consumed by the layer when it maps something; held alone it is still Right Alt.
The layer is applied to the DirectInput-style key read and to the legacy `GetAsyncKeyState`
path alike (`EffectiveKeyState` in `input_sdl.cpp`). The rules live in one pure header,
`src/platform/common/mac_keymap.h`; keys are matched by physical position (scan code), so
non-US layouts behave like the US layout, as on Windows.

## Mouse and Retina (points vs pixels)

SDL gives window sizes/positions and the cursor in **points**; the engine (back buffer,
`GetClientRect`, the UI, the cursor) works in **pixels**. They differ only when the window
is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY` (always, on macOS; density 2 on a
Retina display); without it density is 1 and nothing below changes anything.

The rule: *every screen coordinate the engine sees is in pixels of the window's display*
(`value_in_points * density`), and every value handed to SDL is divided back. Done in one place
per function, all through `display_geometry.*` (unit-tested):

* `GetCursorPos`, `ScreenToClient`, `ClientToScreen`, `GetWindowRect`, `AdjustWindowRect`,
  `SetWindowPos`, `GetMonitorRect`, `GetPrimaryDisplayRect`, `RectOnAnyDisplay`,
  `GetCurrentDisplaySize` (default resolution = display size in pixels, not points) and
  `ClipCursor` (window-relative points, floor/ceil so the clip never shrinks).
* Before this pass only `ScreenToClient` / `ClientToScreen` knew about density, in
  inconsistent units (a persisted window position, the monitor rect and the "fullscreen"
  size mixed points and pixels, so a Retina fullscreen came out half size).
* **Edge snap:** the OS reports a cursor pinned to the right/bottom edge at the first
  sub-pixel of the last point, one pixel short of the last pixel at density 2.
  `CursorPointToPixel` snaps a cursor in the last point to the last pixel, so edge scrolling
  works on the right and bottom edges.
* **Rotate drag** runs in SDL relative mode (raw mickeys; the cursor is hidden for the drag
  and put back at release). The mickey scale is SDL's raw delta, the same on Retina and
  non-Retina, so camera speed does not change with density.
* **Cursor:** `Cursor::Show(true)` is `SDL_ShowCursor`. The game's cursor image is an
  `SDL_CreateColorCursor` (32x32 points) plus a nearest-neighbour 64x64 alternate image
  (`SDL_AddSurfaceAlternateImage`), so Retina shows it crisp at the right size. The "no
  cursor" state before the game cursor exists is a transparent cursor, so the OS arrow never
  shows over the client area. A window that loses focus gets the system cursor back
  automatically. **Root cause of the invisible cursor (2026-10-19):** the Metal
  `DeviceData::SetImageBuffer` (retail slot 8, which builds the cursor from the 32x32
  ImageBuffer) was an empty stub, so only the transparent "no cursor" was ever set. It now
  reads the texels and calls `Platform::Cursor::SetFromTexels` (src/platform/common/cursor_image.h;
  retail's alpha/blue classification in `TexelsToPlanes`, unit-tested against slot 8's loop).
* **Mixed-DPI setups:** one density (the game window's) is used for all displays' bounds. On a
  Retina laptop + non-Retina external display the other display's rect is scaled by the wrong
  factor; the placement probe is only approximate there (known limitation).

## Window

* **Windowed resize:** the framed window is resizable (`SetFramed`, `AdjustDisplay`); a live
  resize raises `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` -> `WM_SIZE` -> `Video::Device::SizeWindow`
  -> back-buffer reset, same as Windows.
* **Position memory:** the client origin is saved after a *user* move (`SDL_EVENT_WINDOW_MOVED`
  -> `WM_EXITSIZEMOVE` -> `MoveWindow`) and restored by `AdjustDisplay`, with retail's drift
  quirk kept. Positions are now saved in pixels (points at density 1, so non-Retina settings
  files are unchanged; a settings file written by an earlier build that ran in 1x mode holds
  point values and the window reopens at half the saved offset once).
* **`BANISHED_BACKGROUND`** still short-circuits `SetWindowPos` (small unobtrusive test
  window); that branch is untouched, so it also disables window-position/fullscreen placement
  on purpose.
* **UI scale:** the engine has one: Options > UI scale (`GameSettings::uiScale`). A SMALLER number
  means a BIGGER UI (the value is layout units per device pixel). On Retina (density 2) the default
  is now 50 when no saved choice exists, which gives the same physical size as the original on a
  1x display; see `docs/macos/UI_SCALE.md`.
* **Quit:** SDL installs the standard macOS application menu (Quit Cmd+Q, Hide Cmd+H) and a
  Window menu (Close Cmd+W, Minimize Cmd+M, Toggle Full Screen Ctrl+Cmd+F). Quit and close
  reach the same pump arms as the Windows close box; no extra code was needed or added.
* No `window_sdl.cpp` change was required.

## How it was checked

* `unit`: +9 tests (`tests/unit/mac_keymap_test.cpp`): every layer rule, the layer targets
  reach a DIK, the chord predicate, points/pixels round trips (including 1.5x), the cursor
  edge snap, Option-click. 1187 run, 0 failures, 5 skipped (baseline 1178 run, same skips).
* `tools/input_window/run_harness.sh`: compiles the **shipping** `pump_sdl.cpp`,
  `input_sdl.cpp`, `display_sdl.cpp` and `window_messages.cpp` against SDL's dummy video driver
  (no window, no sound) and drives them with `SDL_PushEvent` and SDL's internal key injection:
  Quit / close / minimise / resize / focus / move / wheel / middle-click become the right
  `WM_*` tuples; `PostQuitMessage` is delivered once; Ctrl+Cmd+F toggles full screen, ignores
  key repeat and a plain Cmd+F, and gives the game no key; real `ReadKeyboard` and
  `AsyncKeyState` return the Mac-layer results as DIK codes (Cmd -> LCTRL, Right Option +
  arrows/Return -> Home/Insert, F13-F15, fn+PageUp untouched, nothing stuck after release);
  `static_assert`s pin `mac_keymap.h`'s numbers to SDL's real enums. 33 checks.
* Boot smoke: the full `OpenBA` built in `build/mac-input` ran 22 s with `BANISHED_BACKGROUND=1`
  and a dummy audio driver, state RUNNING, 1959 frames.

## Not verified (needs a person at the keyboard)

* A real Retina window: density 2 conversions are covered by the pure tests and by reading the
  call chain, not by a physical run (the harness's dummy driver is density 1; opening a visible
  Retina window while someone watches a video was ruled out).
* `SDL_SetWindowMouseRect` confinement and the real edge-scroll feel (the dummy driver does
  not implement mouse rects).
* The native full-screen animation, Space switching and its interaction with the Metal layer's
  resize (renderer is concurrent area).
* Physical Apple extended keyboard keys (F13-F15) and the Option layer on hardware.

## Opening size (windowed mode)

Bug (owner's first hands-on run): a default windowed launch on a Retina display opened a 1x1 point
window. Cause: the Metal backend's `CreateDevice` (src/render/metal/backend.mm, the renderer PR) called
`SDL_SetWindowSize` with the engine's windowed back buffer, which is `Configuration::windowWidth/Height`
(-1 until a geometry is saved), clamped to 1; the 10x10 creation window became 1x1 and nothing
reopened it. Only a window size forced from outside bypassed it (that switch has since been removed: the size comes from settings.cfg).

Fix: `Platform::Window::ApplyInitialWindowGeometry` (src/platform/darwin/window_darwin.cpp), called from `CreateDevice` for windowed,
non-background, no-override starts. Size = `InitialWindowPoints` (display_geometry.h): saved size
(stored in PIXELS) / density, else the configured video size as POINTS (1280x720 points = 2560x1440
px on Retina), a zero/negative request falls back to 1280x720, never below 640x360 points, never
above the usable screen less the title bar. Position = the saved origin if it is still on a display,
else centred on the primary display's usable area. Measured: before 1x33 outer; after 1280x752 outer
(1280x720 client) at 224,149 on a 1728-point screen; saved 2000x1200 px reopens as 1000x600 points
(round trip); unit tests display_geometry_initial_window_*.

## Changing the resolution in Options (2026-10-19)
* Units: the Resolution list is in engine PIXELS (`BuildResolutionList`, `display_geometry.h`): the usual
  candidate sizes that fit the usable screen (less a 32-point title bar, times the pixel density) plus the
  display's own pixel size. A windowed client of w x h pixels is a w/density x h/density point window.
* Metal had no resize path: `BeginCommands` just cleared `Device::settingsChanged`, so Apply changed the
  engine's idea of the client size (mouse mapping) while the window and drawable stayed put. Now
  `ApplyEngineSizeChange` (backend.mm) runs `Device::AdjustDisplay` when the engine size differs from the
  window or fullscreen flipped, publishes the REAL pixel size as back buffer / `windowWidth/Height`, then
  `ReleaseDynamic`/`RecreateDynamic` and `RebuildMainTargets(w, h)` (also repoints the default render buffer's
  recorded size, its aliases and the ASSAO depth).
* A stale 2560x1440 depth made the AO pass skip/blend wrongly: image cropped to the old size and darkened.
* `SetWindowPos` (display_sdl.cpp) clamps a framed window to the usable screen and slides it on screen, so
  the display-sized mode (e.g. 3456x2234) becomes a 3456x1920 window instead of overflowing. Fullscreen is a
  borderless window of the chosen mode at the monitor origin (the display-sized mode fills the screen).
* First run: `VideoWidth` is read as points for the opening window; `CreateDevice` rewrites the configuration
  to the actual pixel size when it is a listed mode so the dialog agrees with the window.

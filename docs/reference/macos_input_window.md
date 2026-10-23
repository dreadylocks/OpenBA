# macOS: input and window behaviour

Current facts. Dated measurements and bug write-ups are in
[../history/macos/INPUT_WINDOW.md](../history/macos/INPUT_WINDOW.md).
Code: `src/platform/sdl/{input,pump,display}_sdl.cpp`, `src/platform/darwin/window_darwin.cpp`,
`src/platform/common/{mac_keymap.h,display_geometry.*,display_density.*}`; tests
`tests/unit/mac_keymap_test.cpp` and the display_geometry tests.

## Keys and buttons

| You want | On a Mac |
|---|---|
| Quit | Cmd+Q, the Dock menu, or the red button; same path as the Windows close box. Cmd+W closes the window. |
| Minimise | Yellow button or Cmd+M. The back buffer is sized 0 while minimised, as on Windows. |
| Full screen (session) | Ctrl+Cmd+F or the green button: native macOS full screen. |
| Full screen (saved) | Options > fullscreen: a borderless window pinned to the display; saved in settings. |
| Rotate camera | Middle button drag, or Option + left-button drag. |
| Zoom | Wheel / two-finger scroll; independent of the "natural scrolling" setting. |
| Right click | Secondary click as set in System Settings. Control+click stays a left click. |
| Edge scroll | Push the cursor against a window edge (the cursor is confined to the focused window). |
| F1-F12 | Hold fn, or enable "Use F1, F2, etc. keys as standard function keys". |
| Ctrl shortcuts | Command acts as Control; physical Control still works. |
| Home / End / PgUp / PgDn / Delete | fn + Left / Right / Up / Down / Delete, or Right Option + Left / Right / Up / Down / Backspace. |
| Insert | Right Option + Return. |
| Scroll Lock / Print Screen / Pause | Right Option + F10 / F11 / F12, or F14 / F13 / F15. |

Right Option is consumed when it maps something; held alone it is Right Alt. The rules are in
`mac_keymap.h`, applied to both the DirectInput-style key read and `GetAsyncKeyState`
(`EffectiveKeyState`). Keys are matched by scan code, so non-US layouts behave like US.

The Option-click rule: the event pump decides whether an Option + left press is a camera rotate (middle
button) and pairs the release with the same decision, so a click never leaves a stuck button.

## Points and pixels

SDL reports window sizes, positions and the cursor in points; the engine works in pixels. The window is
created with high pixel density, so on a Retina display density is 2. Every screen coordinate the engine
sees is `points * density`, and every value handed to SDL is divided back, in one place per function
through `display_geometry.*`: `GetCursorPos`, `ScreenToClient`, `ClientToScreen`, `GetWindowRect`,
`AdjustWindowRect`, `SetWindowPos`, `GetMonitorRect`, `GetPrimaryDisplayRect`, `RectOnAnyDisplay`,
`GetCurrentDisplaySize` and `ClipCursor`. The density is read live through `display_density.h`
(`Platform::Window::PixelDensity`).

* A cursor pinned to the right or bottom edge is snapped to the last pixel so edge scrolling works.
* Rotate drag uses SDL relative mode; camera speed does not depend on density.
* The game cursor is an SDL colour cursor (32x32 points) with a 64x64 alternate image, crisp on Retina.
* Mixed-DPI setups use the game window's density for all displays; other displays' bounds are approximate.

## Window geometry

* A framed window is resizable; a live resize flows `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` -> `WM_SIZE` ->
  `Video::Device::SizeWindow` -> back-buffer reset, as on Windows.
* The client origin is saved in pixels after a user move and restored on the next launch.
* Opening size (`Platform::Window::ApplyInitialWindowGeometry`, `src/platform/darwin/window_darwin.cpp`,
  size from `InitialWindowPoints` in `display_geometry.h`): the saved size (pixels) divided by density, else
  the configured video size as points, never below 640x360 points and never above the usable screen less
  the title bar. Position is the saved origin if still on a display, else centred on the primary display.
* The Options resolution list is in engine pixels (`BuildResolutionList`): common sizes that fit the usable
  screen plus the display's own pixel size. A framed window is clamped to the usable screen; fullscreen is a
  borderless window of the chosen mode at the monitor origin.
* `BANISHED_BACKGROUND=1` gives a small unobtrusive window and skips window-position and fullscreen placement.
* UI scale: see `docs/macos/UI_SCALE.md`.

## Not verified on hardware

Density-2 conversions are covered by unit tests and a dummy-driver harness, not by a visible Retina run;
`SDL_SetWindowMouseRect` confinement, the native full-screen animation and an Apple extended keyboard's
F13-F15 are unchecked.

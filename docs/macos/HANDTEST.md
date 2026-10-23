# Hands-on test of the Mac port (mouse, keyboard, window)

This is for the owner: one script, about 3 to 5 minutes, and a result you can send back
without taking any screenshots. Background of what is being tested: `docs/reference/macos_input_window.md`.

## What you type

```
cd openba_mac
tools/macos/handtest/run_handtest.sh
```

That is all. The game opens in a normal window (not full screen) with sound on. The terminal
prints a numbered list; do the steps in order. Do not worry about doing them perfectly: the
report says exactly what it saw and what it did not.

1. **Start a new game** (any map) and wait for the town.
2. **Edges**: push the mouse pointer slowly against the left, right, top and bottom edge of
   the window. The view should scroll each time.
3. **Click** one toolbar button and click it again to close it.
4. **Rotate**: hold the middle mouse button and drag. No middle button? Hold **Option** and
   drag with the left button. (Do both if you can.)
5. **Zoom**: scroll the wheel (or two fingers on the trackpad) up and down.
6. **Keys**: press W, A, S, D, Q, E, F1 and Space, one at a time.
7. **Page Up**: hold the **right** Option key and tap the Up arrow.
8. **UI scale**: Options, change "UI scale" (for example 100, then 150, then back), close it.
9. **Full screen**: press Ctrl+Cmd+F, wait 3 seconds, press Ctrl+Cmd+F again.
10. **Resize**: drag a window corner to make it bigger, then smaller.
11. **Quit**: press **Cmd+Q**. The test ends and the analysis starts by itself.

Afterwards it asks five yes/no questions about things only your eyes can judge (did the
buttons get bigger, did the view scroll, ...). Answer `y`, `n`, or `s` if you did not try it.
If the game freezes, press Cmd+Q anyway, or wait: it stops waiting after 15 minutes.

Safe to run: the game works inside a throwaway folder `run/handtest-<date-time>/` and your
real saves and settings are not touched (the script redirects `USERPROFILE` there and starts
it with a fresh windowed 1280x720 setting). The game always runs in native Retina pixel mode on a Retina display. Other options: `--engine PATH`,
`--game-dir DIR`, `--max-seconds N`, `--no-questions` (see the top of the script).

## What you get

A table in the terminal, also saved as `handtest_result.txt` in that folder:

```
PASS  Mouse reaches every screen edge            left, right, top and bottom all touched (pixel space)
PASS  Key W -> DIK 0x11                          scancode 26 reached the game as 0x11
FAIL  Ctrl+Cmd+F toggles full screen (twice)     entered full screen but the drawable stayed 1280x720
...
2 thing(s) did not work out of 24 checked:
  - Ctrl+Cmd+F ...
```

* **PASS**: the game saw it as expected. **FAIL**: it did not (the line says what it saw
  instead). **WARN**: odd but not necessarily broken. **SKIP**: could not be checked (for
  example the pixel-density check on a non-Retina window).
* Then a short plain-language summary. If anything is FAIL, **send me the whole folder
  `run/handtest-...`** (or zip it). It holds everything needed to find the cause:
  `input_trace.log` (every event), `handtest_result.txt`, `handtest_meta.txt` (exit code and
  your answers), `standalone_metal_frames.csv` (frame times) and the engine logs.

What each row checks:

| Row | Passes when |
|---|---|
| Mouse reaches every screen edge | the pointer positions, in pixels, touched all four window edges (so edge scrolling can work, including the right and bottom edge) |
| Mouse coordinates are in pixels | on a Retina window the pixel coordinates went beyond the window width in points (a half-size mapping would fail) |
| Window points x density = drawable pixels | every size the window reported agrees with the drawable size |
| Left click / Middle-drag / Option+drag / Scroll wheel | the button or wheel events arrived (wheel both directions) |
| Key X -> DIK | the key arrived and the game's key table would read the right DirectInput code, after the Mac layer (Command acts as Control, Right Option layer, ...) |
| Right-Option+Up -> Page Up | the Mac layer turned it into Page Up (DIK 0xC9) |
| Ctrl+Cmd+F toggles full screen | the window entered full screen, the drawable size changed, and the second press left it again |
| Resize the window | the windowed drawable size really changed |
| Game window had focus / No key left pressed | focus was seen; no key press without a release (a lost key-up is a stuck key) |
| Cmd+Q quits cleanly | a quit request arrived, the process wrote its last line, and the exit code was 0 |
| Frame rate | average fps and slowest 1 percent of frames from the frame log |
| You said: ... | your yes/no answers |

## How it works (for whoever diagnoses)

* **The logger**: `src/diag/darwin/input_trace_darwin.cpp`, switched on by the option `BANISHED_INPUT_TRACE=1`
  (field `Opt().inputTrace` in `src/app/options.h`). macOS only (compiled out
  elsewhere). `main()` calls `Diag::InputTrace::Start()` after `SDL_Init`; it installs an SDL
  event watch only when the option is set, and a once-a-second timer hands the S line to the
  main thread (SDL_RunOnMainThread), so no SDL window call is made off the main thread.
  With the option off nothing is started.
  Writes `input_trace.log` next to `standalone_debug.log`.
* **Line format**: `<tag> t=<seconds> key=value ...`; tags `H` header, `M` mouse move (at
  most 20 per second), `B` button, `W` wheel, `K` key, `E` window event, `Q` quit/close
  request, `S` once-a-second summary (window/drawable size, density, full-screen flag, focus,
  extreme pointer pixels of the last second, so no edge touch is lost to rate limiting), `X`
  last line at process exit. Key lines carry the SDL scancode, the DIK of the raw key and
  `eff_dn` / `eff_up`: the DIK codes that went down/up after the Mac key layer, computed with
  the same `ApplyMacKeyLayer` + `kSdlScancodeToDik` the game's `ReadKeyboard` uses. Mouse
  `x,y` are SDL points, `px,py` pixels (points x pixel density).
* **Frame rate** comes from `standalone_metal_frames.csv` (the script sets `BANISHED_METAL_STATS=1`);
  the logger itself has no frame counter because it does not hook the game's frame loop.
* **Analyzer**: `tools/macos/handtest/analyze.py <input_trace.log> [--meta handtest_meta.txt]
  [--frames standalone_metal_frames.csv] [--json]`. Exit 0 = no FAIL, 1 = a FAIL, 2 = no trace.

## How the pipeline itself was tested (no human, no window)

`tools/macos/handtest/run_pipeline_test.sh` (needs any finished build in `build/mac-hand`, or
pass another build dir):

1. compiles `trace_harness.cpp` against the **shipping** logger and `pump_sdl.cpp` on SDL's
   dummy video driver (no window, no sound), and performs the entire checklist with synthetic
   events (edges, click, middle and Option drag, wheel, all keys, the Ctrl+Cmd+F chord through
   the real pump twice, a resize, Cmd+Q);
2. runs `analyze.py` on that trace: every row must PASS (exit 0);
3. `corrupt_trace.py` makes 13 broken copies, one defect each (right edge unreachable, wrong DIK
   for W, Page Up layer missing, full screen never entered / not left / drawable unchanged, no
   clean quit, no quit request, no header, no wheel, no click, never resized, size mismatch);
   the analyzer must FAIL each (exit 1);
4. `test_analyze.py`: 12 unit tests on hand-written traces (Retina and not, each defect, exit
   code of a crash, stuck key, frame log, manual answers, garbage lines).

`run_handtest.sh --background` is the developer self-check of the whole script on the real game:
a small unobtrusive window, no checklist, stops itself (default 25 s). With nobody touching it,
the analyzer correctly FAILs the input rows; the point is that the real exe writes the trace
and frame log and the table is produced.

## Not verified

* A real person's input through the real macOS event path (the event watch sees SDL's
  event queue, which the Cocoa driver and the synthetic harness both feed; the window and
  focus events of a live run were seen in the trace, key/mouse events from a physical device
  were not).
* Real Retina (density 2) in the logger: only unit-tested on synthetic density-2 traces.
* The real macOS full-screen transition: the harness used the dummy driver's fullscreen,
  where the size change and the enter event arrive in the same instant. On a real Mac they are
  an animation apart; the analyzer looks at the last full-screen size before the first leave,
  not at one instant, but this is untested on hardware.
* Whether Cmd+Q reaches the engine as `SDL_EVENT_QUIT`: it should (the SDL application menu), so
  the row requires a quit line, not a Q key line.

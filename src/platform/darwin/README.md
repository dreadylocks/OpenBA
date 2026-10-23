# src/platform/darwin — macOS twins

macOS implementations of the platform seams live here. The replacement rule:

- On Apple, CMake globs `src/platform/posix/*.cpp`, `common/`, `sdl/` (as on Linux)
  **and** `src/platform/darwin/*.cpp` (`CMakeLists.txt`, the `PLATFORM_SRCS` block).
- A file `darwin/<name>_darwin.cpp` **replaces** `posix/<name>_posix.cpp` whole-file:
  the POSIX twin is removed from the build and the Darwin twin is compiled instead.
  There is no per-function mixing — the Darwin twin defines every function its POSIX
  twin did (copy the POSIX file and change what macOS needs).
- A Darwin file with no POSIX twin of the same `<name>` is simply added.
- The unit test applies the same substitution (`CMakeLists.txt`, the `UNIT_TREE_SRCS`
  twin block), so `tools/build.sh unit` on a Mac links the same twins as the game.
- Seams first, then twins: platform-specific behaviour goes behind a `Platform::` hook
  declared in `src/platform/common/platform_*.h` (for example `platform_app.h`,
  whose win / posix bodies are empty), never behind `#if defined(__APPLE__)` in
  `src/app`, `src/game` or `src/engine`.

Diagnostics use the same rule independently of `PLATFORM_SRCS`: on Apple,
`src/diag/darwin/boot_state_watchdog_darwin.cpp` and `crash_handler_darwin.cpp`
replace their `src/diag/posix/*_posix.cpp` twins in `DIAG_POSIX_SRCS` for `OpenBA`.
Their CMake selection lives in the Metal/CMake part (2/3); parts 1/3 and 2/3 land together.
The unit source substitution also handles diagnostic twins when a diagnostic source
is listed. Currently the unit target links neither watchdog nor crash handler; it
supplies its own `_purecall` in `tests/unit/posix_crt_stubs.cpp`.

Application startup and shutdown use `Platform::App` hooks in `platform_app.h`.
Window setup and mode queries use `Platform::Window` hooks in
`platform_window_hooks.h`; pixel density is queried live through
`display_density.h`. The Darwin platform implementation owns those behaviors.

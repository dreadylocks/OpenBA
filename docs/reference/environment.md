# Dev environment (reference)

(The executable was formerly named `reimpl-x64.exe`; the Linux target `reimpl-x64`.)

## Fresh clone checklist
1. `git submodule update --init` (SDL3 at `third_party/SDL`; CMake refuses to configure without it).
2. MSYS2 UCRT64 GCC (`C:\msys64\ucrt64\bin`) and the pip `cmake` + `ninja` on PATH (Toolchain below).
3. `BANISHED_DIR` (default `C:/GOG Games/Banished`): the game directory `tools/build.sh deploy` / `run`
   copy the exe into; the unit tests also read `WinData/data0.pkg` from it and skip the tests that
   need it when it is absent (also tried: `/mnt/c/GOG Games/Banished`).
4. The Banished Modding ToolKit 1.0.7 unpacked at `<repo>/ToolKit/` (gitignored; `ToolKit/bin/x64/Tools-x64.exe`
   and `ToolKit/resource/`): `tools/ext_resources.sh`, run by deploy, fails without it.
5. The MCP venvs `.mcp.json` points at are gitignored: `tools/ghidra_mcp/.venv` (+ the GhidraMCP bridge,
   skill `ghidra`) and `tools/renderdoc_mcp/.venv`; recreate them before using those servers. The renderdoc
   entry needs an ABSOLUTE `RENDERDOC_MODULE_PATH` (Win32 AddDllDirectory rejects a relative one): export
   it, or edit the `${RENDERDOC_MODULE_PATH:-...}` default in `.mcp.json`.
Then `tools/build.sh unit` (no game needed beyond item 3's skips) and `tools/build.sh run` (the gate).

## Toolchain
- GCC 13.1 (MSYS2 UCRT64) at `C:\msys64\ucrt64\bin\`; C++17, static-linked runtime
- The build is CMake + Ninja through `tools/build.sh` (2026-10-11; the gate kit, the stress runner and the
  resave gate all call it; build directories `build/cmake-<flavour>`). The Makefile and its `mingw32-make`
  route were deleted the same day (history: `docs/history/`, the STATUS log).
- MSYS2 packages are outdated (stale signatures): avoid `pacman -S` for new installs
- Linux (WSL2 Ubuntu 24.04, g++ 13.3, cmake 3.28, ninja): the same CMakeLists builds `OpenBA` and
  `unit` in every flavour; `tools/linux_gamedir.sh` + `tools/linux_run.sh` run it over dxvk-native
  (`tools/fetch_dxvk_native.sh`) and lavapipe; the source copy lives in the WSL home (`~/banished-main`,
  rsync of the working tree) because a build over `/mnt/c` is slow; ccache makes a re-copy's build near-free
- Python 3.9.10 system-wide; 3.10.11 in the project venv `tools/ghidra_mcp/.venv/`
- `uv` v0.9.24; Node.js + npx (for the vscode MCP server)
- JAVA_HOME `C:\Program Files\Java\jdk-24` (for Ghidra)
- CMake 4.4 + ninja from pip (`C:\Users\USPC\AppData\Roaming\Python\Python312\Scripts`, on PATH)
- Cross-build from macOS (the neighbour-towns fork, 2026-10-19): `brew install mingw-w64 cmake ninja`
  (mingw-w64 14.0.0, GCC 16.2, UCRT by default; CMake 4.4.4, ninja 1.13.2), then
  `tools/build.sh build [flavour] --cross` -> `build/cross-<flavour>/OpenBA.exe` through
  `tools/toolchains/mingw-w64-x86_64.cmake`. Build, deploy (a plain copy to `BANISHED_DIR`) and
  clean only: the exe runs on the Windows PC, not on the Mac. Not the reference compiler (GCC 13.1).

## Third-party (pinned submodules; run `git submodule update --init` after a clone)
- **SDL3** at `third_party/SDL`: tag **`release-3.4.18`** (the current stable, 2026-10-02),
  commit `829a65d769d935c4852f8159e964312c0957260a` (D4_RULES §3 Q4: SDL3 only, static; the
  revised SDL plan §B). First pinned at `release-3.2.30`, moved the same day. `.gitmodules` has `shallow = true`. CMake builds it from source inside each build directory
  (`build/cmake-<flavour>/third_party/sdl3`, static, with the options below; the Makefile used to build it
  once into `build/third_party/install`). GCC 13.1 ucrt64 builds it as is, no fallback:
  ```
  cmake -S third_party/SDL -B build/third_party/sdl3 -G Ninja -DCMAKE_BUILD_TYPE=Release
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
        -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF
        -DSDL_HIDAPI=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF -DSDL_DIALOG=OFF -DSDL_TRAY=OFF
        -DCMAKE_INSTALL_PREFIX=<repo>/build/third_party/install
  cmake --build build/third_party/sdl3 -j2 --target install
  ```
  SDL_HAPTIC must stay OFF while SDL_JOYSTICK is OFF (configure fails otherwise).
  `SDL_NOTIFICATION` does not exist in this tag. The exe links `libSDL3.a` plus the system
  libraries the installed `sdl3.pc` lists, read at link time (with joystick / HIDAPI off that
  list has no `hid` / `mincore`). The unit tests and the resave tool do not link SDL (D4_RULES C7). The CMake twin
  builds the same tree with `add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)`.
  To move the pin: `git -C third_party/SDL fetch --depth 1 origin tag <tag>`, check the tag
  out, commit the submodule, update this entry, delete `build/third_party`.
- **Dear ImGui** at `third_party/imgui` (vendored files, not a submodule): tag **`v1.92.9b`**, commit
  `f1cc2ae15e53a861a874c3034aae6798fde194ab`, MIT; the core + `imgui_impl_sdl3` + `imgui_impl_dx11`, unmodified
  (`third_party/imgui/README.banished.md` has the file hashes). Compiled into the **debug flavour only** (CMake
  option `BANISHED_TWEAK_OVERLAY`) for the Alt+F10 tweak overlay, `docs/reference/tweak_overlay.md`; the debug exe
  then imports `d3dcompiler_47.dll`. Also vendored, all flavours: `third_party/wmadec` (FFmpeg's WMA v2 decoder,
  `EXTRACT.md`) and the ASSAO copy under `src/render/dx11/ao/assao/` (its `README.md`).

## VS Code debugging
- Configurations live in `.vscode/launch.json`: "Launch Standalone (debug, autostart new
  game)" sets `BANISHED_AUTOSTART`; "Launch Standalone (debug)" stops at the menu
- Launch AND inspect with the `vscode-debugger` MCP (`start_config`, `mcpManaged: true`):
  a session started through the `vscode` MCP's `start_debug_session` is invisible to it
- `vscode` MCP is for general editor operations (terminal, tasks, open files)
- Run `tools/build.sh deploy` first: the preLaunchTask may not run through MCP
- `workspaceFolder` must use a lowercase drive letter (`c:\...`)

## Ghidra
See the `ghidra` skill (`.claude/skills/ghidra/SKILL.md`) and `docs/reference/HOW_TO_START_GHIDRA.md`.

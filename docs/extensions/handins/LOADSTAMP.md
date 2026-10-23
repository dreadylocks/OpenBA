# LOADSTAMP -- `[LoadTime]` stage stamps and a multi-thread sampler (hand-in)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/draw_gather/gather_threads.{h,cpp}` moved to `src/ext/gather_threads/` (namespace `Ext::GatherThreads`).)

Block 1 of the loading-speed session. Branch `worktree-agent-ad4b51da84f2ce11d`, base main
`b0ccc82bc`. Static gate only: the game was NOT run (the coordinator runs it). Boundary list taken
from `LOADRESEARCH.md` §3 (all 14 rows covered) plus the package remounts and the save-read steps.

Nothing here changes game behaviour: every stamp is one `fprintf(stderr)` that reads no game state
except, in four places, a pointer it already holds (`typeid` of a loop, `m_savedState != nullptr`,
`IsSaving(&stream)`). No new flag, no `getenv`, no guard, no reorder. New-game world and resave are
unaffected by construction (prints only); the coordinator's gate run is the proof.

## 1. The `[LoadTime]` line

```
[LoadTime] <stage> t=<ms since process start, 1 decimal> thread=<name>[ <detail>]
```

* `t`: `Platform::PerfCounter` against an origin captured by `src/diag/load_time.cpp`'s static
  initialiser (before `main`, a few ms after process creation). The sampler's per-sample ms use the
  same origin, so a stamp's `t` can be fed straight to `sample_report.py --from/--to`.
* `thread`: `main`, `loader`, or another registered role (`persist`, `audio`, `pathfinder`, `path`,
  `render`, `cull`, `thread`), else `tid<N>`.
* A stamp marks the START of the step it names; a step lasts until the next stamp on the same thread.
* Always on, `fprintf(stderr)` -> `standalone_error.log`. It survives the perf / release flavours
  (`src/app/log_strip.h` macros away `printf` only; `check_log_strip.py` passes). The existing
  `[BootTime]` / `[Stall]` / `[ChunkedSceneLoader]` lines are `printf` and vanish in perf; these do not.
* Volume: ~45 lines for boot to menu, ~45 for a new game, ~35 for a save load (plus `ser.*` on each
  save / autosave). None is in a per-frame path; the three "first frame / first update" stamps cost a
  static pointer compare per frame.

Helper: `src/diag/load_time.{h,cpp}` (`LoadTime::Stamp(stage, detail)`, `LoadTime::StampType(stage,
typeid(x))` which demangles the dynamic type, `LoadTime::NowMs`, `LoadTime::CounterToMs`). Portable:
`Platform::PerfCounter` / `CurrentThreadId` + `abi::__cxa_demangle`.

## 2. Stage list (file:line in this branch)

### Boot to main menu (main thread unless noted)

| stage | where | marks the start of |
|---|---|---|
| `main` | `src/app/main.cpp:1003` (WinMain), `:1155` (POSIX main) | the logs are open |
| `engine.startup` | `src/engine/engine/engine_lifecycle.cpp:444` | Engine::Startup (allocator, config, timer) |
| `engine.filesystem` | `engine_lifecycle.cpp:495` | `FileSystem::Startup` (package mount #1) |
| `engine.mods` | `:518` | `Modification::RefreshList` |
| `engine.factory` | `:526` | `Object::Factory::Startup` (+ internal/external factories) |
| `engine.window` | `:558` | `Application::PlatformStartup` (the window) |
| `engine.videomodes` | `:605` | `EnumerateVideoModes`, audio / input configuration, PlatformConfigure |
| `engine.device` | `:646` | `Video::Device::PlatformStartup` (D3D11 device + swap chain) |
| `engine.audio` | `:657` | `Audio::Device::PlatformStartup`, input device, achievements |
| `engine.startup.done` | `:681` | the rest of GameMain up to MainLoop::Run |
| `gamemain.run` | `src/game/Game/game_main.cpp:685` | MainLoop::Run |
| `loop.startup <Type>` / `loop.started <Type>` | `src/engine/engine/mainloop_run.cpp:277/279` (Run's first loop), `:251/253` (every transition in MainLoop::Update) | a loop's `Startup()` and what follows it; `<Type>` = `GameLoaderLoop`, `InnerLoaderLoop`, `GameLoadDialogLoopT`, `MenuDialogLoopT`, `GameUpdateLoop` |
| `loader.mount <Type>` | `src/engine/engine/loops.cpp:419` | `Loader::Load`'s `Modification::SetupFileSystem` (the loop to be loaded) |
| `fs.remount` / `fs.remount.done` | `src/engine/io/modification_setupfilesystem.cpp:319/517` | an actual package remount (`_dirty` set); absent when SetupFileSystem returns early |
| `loader.kick` | `loops.cpp:423` | `Thread::Start` of the loader |
| `loader.run <Type>` | `loops.cpp:996` (loader thread) | the loader job |
| `loader.resources` | `loops.cpp:1005` (loader) | `UpdateLoop::LoadResources` (the loop's .rsc list, e.g. GlobalResources) |
| `loader.load` | `loops.cpp:1011` (loader) | the loop's slot-6 `Load` (GameUpdateLoop::Load for a game) |
| `loader.done` | `loops.cpp:1018` (loader) | the closing DeviceData Slot4(false) |
| `splash.fadeout <Type>` | `src/game/Game/splash_loop_stubs.cpp:435` | a splash's fade-out (loader idle, delay elapsed, world ready) |
| `splash.transition <Type>` | `splash_loop_stubs.cpp:476` | the splash hands over (SetNextLoop) |
| `menu.scene.a1` | `src/game/Game/chunked_scene_loader.cpp:54` | LoadSceneA1 (MenuScene.rsc) |
| `menu.scene.b` | `chunked_scene_loader.cpp:83` | the chunked PhaseB (500 elements per splash tick) |
| `menu.scene.a3` / `menu.scene.done` | `chunked_scene_loader.cpp:136/140` | particles; scene complete |
| `menu.opened` | `src/game/Dialog/mainmenudialog.cpp:433` | the menu dialog's OnOpened (next to `[MainMenu] OnOpened`) |
| `menu.update.first` | `src/game/Game/menudialogloopt_stubs.cpp:321` | the first Update of each MenuDialogLoopT (menu interactive) |

### New game

| stage | where | thread |
|---|---|---|
| `newgame.click` / `newgame.queued` | `src/game/Dialog/newgamedialog.cpp:676/737` (StartNewGame ENTER / EXIT) | main |
| `menu.stageC` | `menudialogloopt_stubs.cpp:400` | main: fade settled, SetNextLoop |
| `loop.*`, `loader.*`, `fs.remount*` | as above | main / loader |
| `gul.load new` | `src/game/Game/gameupdateloop_load.cpp:722` | loader |
| `gul.resources` | `gameupdateloop_load.cpp:747` | steps 1-4 (resource Finds) |
| `gul.subsystems` | `:752` | steps 5-27 (subsystem objects) |
| `gul.skymesh` | `:1004` | steps 28-30 |
| `gul.worldgen` | `:1128` | `WorldGen_BuildTerrain` |
| `wg.base` `wg.coastline` `wg.ranges` `wg.paths` `wg.rivers` `wg.stamp` `wg.erosion` `wg.carve` `wg.smooth1` `wg.smooth2` | `src/game/Game/worldgen.cpp:225-250` | the f390 heightmap pipeline, one stamp per stage |
| `wg.quantize` | `worldgen.cpp:551` | float -> int16 heights |
| `wg.blend` | `:556` | cf440 splat texture |
| `wg.upload` | `:560` | `UploadHeightImage` (GPU) |
| `wg.mesh` | `:562` | `BuildTerrainMesh` |
| `wg.water` | `:570` | `BuildWaterMesh` |
| `wg.polylines` | `:620` | the feature polylines copy |
| `wg.teardown` | `:776` | heightfield scratch free (BuildTerrain EXIT follows) |
| `gul.tilegrid` | `gameupdateloop_load.cpp:1181` | `MapTileGrid_Build` |
| `tg.classify` `tg.fillzones` `tg.pfseed` `tg.regionseed` `tg.shore` | `src/game/Game/maptilegrid.cpp:77/83/85/87/90` | its five steps |
| `gul.starttown` / `gul.starttown.done` | `gameupdateloop_load.cpp:1207/1220` | camera + `StartTown_SpawnEntities` |
| `gul.finalize` | `:1425` | steps 33+ (camera, scene globals, audio) |
| `gul.load.done` | `:1468` | the world-built signal |
| `game.update.first` | `src/game/Game/gameupdateloop.cpp:530` | main: first GameUpdateLoop::Update |
| `game.draw.first` / `game.draw.first.done` | `gameupdateloop.cpp:1267/1535` | main: the first Draw and its end (first frame) |

(GameUpdateLoop::Startup, i.e. the HUD bring-up, is `loop.startup GameUpdateLoop` ..
`loop.started GameUpdateLoop`.)

### Load save

| stage | where | thread |
|---|---|---|
| `load.click` | `src/game/Dialog/saveloaddialog.cpp:1047` | main: SetOperation(load) |
| `save.info` | `src/engine/save/saveload_update.cpp:660` | main: `BeginGetSaveGameInfo` |
| `save.read` | `saveload_update.cpp:768` | main: mods re-ordered, the file read queued on the persistence worker |
| `save.read.io-done` | `saveload_update.cpp:811` | main: first UpdateLoad tick with the worker idle |
| `save.read.done` | `saveload_update.cpp:813` | main: `_minOperationTime` also passed (the gap to `io-done` is the min-time floor) |
| `load.chain` | `saveloaddialog.cpp:650` | main: the loop chain is built |
| `menu.stageC`, `loop.*`, `loader.*`, `fs.remount*` | as above | |
| `gul.load save` | `gameupdateloop_load.cpp:722` | loader |
| `gul.saverestore` | `:1302` | `SaveRestore(load)` |
| `ser.retrieve` / `ser.open` | `gameupdateloop_serialize.cpp:811/813` | saved image -> stream; `CompressedMemoryStream::Open` |
| `ser.enter load` `ser.reftable` `ser.core` `ser.entities` `ser.worldgeom` `ser.citizens` `ser.map` `ser.rest` `ser.postload` `ser.done` | `gameupdateloop_serialize.cpp:551/560/623/673/675/679/681/683/699/707` | SerializeAll: reference table, then `[PreloadRefs]` (reftable..core), the core, the big subsystems, PostLoad |
| `gul.saverestore.done` | `gameupdateloop_load.cpp:1306` | |
| `gul.finalize`, `gul.load.done`, `game.*` | as above | |

The `ser.*` stamps also fire on every save and autosave, with detail `save`.

## 3. The sampler (`BANISHED_SAMPLE=<Hz>`, unchanged switch)

* `src/diag/diag_threads.{h,cpp}` (portable): a lock-free roster {tid, role letter, busy-word pointer}.
  * Every `System::Thread` registers itself in `System::Thread::ThreadFunc` just before its first
    `Run()` (`src/engine/system/thread.cpp:204`; the object is fully constructed there), the role read
    from `typeid(*self).name()`: Engine::Loader `L`, IO::PersistenceWorker `S`, Audio::Stream `A`,
    PathfinderThread `F`, ext.path_workers PoolThread `P`, ext.render_pipe RenderThread `R`, other `T`.
    The busy word is `&self->_runFlags` (bit 0 = mid-Run). After the first call it is one
    `thread_local` test per Run.
  * The cull helpers (`src/ext/draw_gather/gather_threads.cpp:53`) register as `C`; the main thread as
    `M` from `load_time.cpp`'s static initialiser. (Unit build: `tests/unit/sources.cmake` links
    `diag_threads.cpp` for gather_threads.)
* `src/diag/win/sampler.cpp`: each tick it opens newly registered threads (`OpenThread`
  SUSPEND_RESUME | GET_CONTEXT | QUERY_INFORMATION) and samples:
  * the main thread every tick (as before);
  * any other registered thread on a tick where it is mid-job (busy bit 0; the loader during a whole
    load, waits included) OR its `QueryThreadCycleTime` moved since the last tick; an exited thread is
    dropped. A parked worker writes nothing.
* New line format, `standalone_samples.txt`:
  `@<role><tid> <ms since process start> <tag> <n> <frames...>`, e.g. `@L14212 5321.4 99 12 E:0x1400...`.
  `tag` is the main thread's SIMPROF phase as before; 99 on every other thread.
* `tools/sample_report.py`: reads both formats; prints samples per thread; `--thread M|L|...|loader|
  main|<tid>|all` (default: the main thread, so old invocations give the old report) and
  `--from <ms> --to <ms>` (same clock as `[LoadTime] t=`).

Known limit: a pool thread object freed between the sampler's liveness check and its busy-bit read
would be read after free (allocator memory stays mapped; window of a few instructions, only at
path-worker pool teardown).

## 4. How to read a run

```
grep "\[LoadTime\]" standalone_error.log
```
gives the timeline; difference consecutive `t=` per thread. For a profile of one window, e.g. the
new-game world build:
```
BANISHED_SAMPLE=1000 tools/build.sh run      # or the perf exe
grep -E "LoadTime\] (gul.worldgen|gul.tilegrid)" standalone_error.log        # -> t=A, t=B
python tools/sample_report.py standalone_samples.txt --exe build/cmake-perf/OpenBA.exe \
       --thread loader --from A --to B
```
Boot's silent 3.7 s: read the `loop.*` / `splash.*` / `menu.scene.*` / `loader.*` lines between
`engine.startup.done` and `menu.opened`. Save load: `load.click` .. `save.read.io-done` is the
persistence worker (sample `--thread persist`), `io-done` .. `save.read.done` is the min-operation
floor, `menu.stageC` is the fade.

## 5. What was run (static gate, this worktree; the game was not run)

* `tools/build.sh build debug --jobs 2`: OK, 0 warnings in any touched file.
* `tools/build.sh build perf --jobs 2`: OK; the touched files' perf warnings are the pre-existing
  printf-strip ones (unused locals feeding stripped `printf`s), none from this block.
* `tools/build.sh unit debug --jobs 2`: 1172 tests, 0 failures.
* `python tools/castmetric.py src`: total 2 (unchanged; no new cast).
* `python tools/abi_audit.py`: OK. `python tools/check_log_strip.py`: OK.
* `tools/acceptance/acceptance.sh`: 0 lines off target (R14-R16 need a new-game log dir).
* `tools/sample_report.py` checked on a hand-made file mixing old and new lines (default = main,
  `--thread L` / `loader` / tid / `all`, `--from/--to`).
* Not checked here: the Linux build (no WSL run from this worktree). The new code is portable
  (`Platform::` seams, `<cxxabi.h>`, `thread_local`); `src/diag/win/sampler.cpp` stays Windows-only.

One slip caught by the perf build's `-Wmisleading-indentation` and fixed before the commit: a stamp
inserted under a brace-less `if` (the `MapTileGrid_Build` call). Every stamp site was then scanned for
a brace-less control statement above it: none left.

For the coordinator: the worktree had no SDL submodule checkout; `git submodule update --init` was
run here (it touches nothing tracked). Expect `[LoadTime]` in `standalone_error.log` on every run,
~45 lines per route; the gate's log greps are unaffected (new tag).

# LOADPROGRESS: the boot loader frames restored, and ext.load_progress (issue #13, 2026-10-22)

> Branch `issue13/loadscreen`, based on `main` 30d98f24c. Windows only (debug + perf builds, unit,
> runs below). Not built or run: Linux, macOS. The coordinator runs the full gate and the resave gate
> on the merged tree. Machine load during every timing below: BUSY (three other agents compiling and
> running); the coordinator re-times on a clean machine.

Issue #13: with a ~12 GB mod set the exe showed no window and gave no feedback for about a minute at
boot. Two parts: a faithful restoration (`behaviour:`), then a class A extension the user asked for.

## 1. Part 1, faithful: the loader loops return at once

**What retail does.** `LoaderLoop::Startup` (Runtime 0x1800dc260) is a three-instruction tail call to
`Loader::Load`; the main loop then runs `LoaderLoop::Update` (`SleepThread(14)` + the advance test)
and `LoaderLoop::Draw` (a black clear) + Present every frame while the loader thread works.

**What the standalone did.** `GameLoaderLoop::Startup` and `InnerLoaderLoop::Startup` called
`WaitForEngineLoader` (`Opt().syncEngineLoader`, `BANISHED_SYNC_ENGINE_LOADER`, default on): the main
thread sat in `Thread::WaitForFinish` for the whole load, so no frame, no message pump, no window.
Both reasons it was added are gone: every allocator door is locked (the comment kept in
`loader_loop_stubs.cpp` names them), and the second loader thread that raced the Engine::Loader for
the one package handle was deleted in 09a74987b (`docs/history/endgame_b/LOADER_READ_SPIN.md`).

**Change.** The wait, the option and its reader are deleted; only the skip-loader investigation
option is left in the two overrides. Every route that wraps a load in a `GameLoaderLoop` (boot, new
game `newgamedialog.cpp:722`, save load `saveloaddialog.cpp:697`, return to menu
`mainmenudialog.cpp:243`, tutorial `tutorialui.cpp:118` and `tutorial_next_scenario.cpp:92`, the
world-setup reload `worldsetup_reload.cpp:136`) now draws black frames during the outer loop's load,
as retail does. Read for anything that assumed a parked main thread: `MainLoop::Update` tears the
previous loop down (Shutdown, delete, its `_loadBit` clean) BEFORE the next loop's Startup kicks the
loader, and the advance waits on the loader's run flag, so no Startup ever overlaps a load. The outer
loops' frames run Input / Audio / the pump / a clear: retail's exact concurrency. A window message
that resets the device reaches `LoaderLoop::OnVideoError`, retail's `WaitForFinish`.

**The window.** `BootWindow` (`src/app/boot_window.h`) used to reveal the deferred window on the
Shining Rock splash's second Draw (with a 4 s watchdog in its Update). It now reveals on the first
frame `MainLoop::Run` completes, whichever loop drew it (`BootWindow_MarkFrame`, declared in
`mainloop_run.cpp` the way `BootState_MarkFrame` is). Frame's tail has already presented by then;
`ext.render_pipe` only pipelines `GameUpdateLoop` frames, never a loader loop's. At boot that is the
outer loader loop's first black frame, about one frame after mount #1. What the deferral still buys:
the window never appears unpainted during `Engine::Startup`; its first content is a presented frame
(black, as retail's). The splash-only reveal and the watchdog are gone (the reveal now runs on every
frame of every loop, so a watchdog has nothing left to catch).

**Proof.**

| Check | Result |
|---|---|
| 24-boot loader-exclusivity control (LOADER_READ_SPIN.md), vanilla new-game route, debug, `/profile:faithful`, `BANISHED_AUTOSTART=1 PAUSEMENU=600 PAUSEMENU_QUIT=120` (DebugMenu + betterFields mounted) | **24 / 24 exit 0, 0 defects**: `[ExtLoad] calls=5695 openFileFail=0 openCompFail=0 badMagic=0 retNull=0` in all 24 (one boot lost its stdout log to the shared game dir and was re-run). September's rate with two readers: 11 / 24. |
| Vanilla window | visible at 1.40 s (debug), was ~1.9 s |
| MegaMod 7-part ON, perf, default profile, 3 boots | window visible at **2.1-2.8 s** (was 48-50 s), `IsHungAppWindow` **never** true (0 of ~520 polls per run; the sync build also 0, it had no visible window to hang), menu reached every time |
| MegaMod counters (gdb, at the menu) | p1 `calls=376431 openFileFailed=10 openCompFailed=0 badMagic=0 returnedNull=10`, `cacheMiss=376431`: the sync build's numbers. (One base boot read 376440 / 4040973: the dump is taken a few seconds after the menu opens, so the menu's own loads can land in it.) |
| `tools/build.sh unit` | 1281 tests, 0 failures, 1 skipped (the kit cross-check) |
| `tools/build.sh run` (gate by hand) | in town at 10.8 s, 50 s of play, no crash (debug, faithful) |

**Timing: the cost is real, and it is retail's.** MegaMod ON, perf, default profile, interleaved,
time to `menu.update.first`:

| Build | Run 1 | Run 2 | Run 3 | Mean | Big load (`loader.resources` -> `loader.load`) |
|---|---|---|---|---|---|
| base 30d98f24c (main thread parked) | 55.2 s | 55.7 s | 56.9 s | 55.9 s | 44.3 s (run 2) |
| Part 1 (frames during the load) | 64.5 s | 64.2 s | 64.8 s | 64.5 s | 52.4 s (run 2) |

+8.6 s, all of it inside the loader thread's big load. The sampling profiler (2000 Hz, one pair,
`scratchpad/issue13/loadscreen/s_p1`, `s_base`) says why: on the loader thread
`RtVideoDeviceLock` is **8.2 %** of the load with frames on, absent from the parked run's top list,
and `ntdll` self time grows 8.2k -> 14.9k samples (the critical-section wait). The main thread holds
`Video::Device`'s lock from `ContextData::BeginCommands` through `Finish`'s `Present`
(`src/render/dx11/contextdata.cpp`, retail 0x180001720 / 0x1800017c0: "Present, then drop the device
lock"), and Present is **15 %** of the main thread's time while the loader keeps the GPU busy
(VSync is off in this settings.cfg, so it is the GPU queue, not the vblank). Every `ImageBuffer` /
`VertexBuffer` / `IndexBuffer` `PlatformConstruct` on the loader takes the same lock
(`buffers_lock.cpp`, retail's inline Enter / Leave). The other 85 % of the main thread is
`Application::SleepThread(14)`, retail's busy wait (`SwitchToThread` until 14 ms passed,
0x18009f850), which costs a core but no lock. Retail pays all of this too: its own MegaMod boot read
for 77 s (`runner/c3_retail_on`). A way to win the 8 s back without leaving retail's behaviour
on the faithful profile would be a class A row that lets a loader loop's frame skip its Present (or
present less often) while the loader runs; not built here.

## 2. Part 2, ext.load_progress (class A, default ON)

**The user's ruling.** No plain black screen during the long boot load: show the existing loading
screen (the logo + spinner) with a "Loading mod X of Y" line, and hold it until the load finishes.

**What it shows.** `Dialog/Loading.rsc`, the game's own loading screen (the axe-man spinner and
"Loading..."), with its `descText` line saying what the loader thread is on:
`Loading game data` while `Game/GlobalResources.rsc` loads, `Loading mod X of Y` while the X-th
enabled mod's `Resources.rsc` loads, and `Loading game data` again after the first list (the
remaining boot lists are re-stamps of resident data). With MegaMod that is honest about where the time
goes: the GlobalResources closure itself takes a few seconds, the mod lists the rest (mod 3 of 9 at
6.6 s, 5 of 9 at 21 s, 8 of 9 at 70 s in a debug boot). Screenshots (in-process frame captures, the
session was locked so desktop captures were black): `loadscreen_20s_ext_on.png` ("Loading mod 5 of
9", debug, MegaMod ON, 20 s in) and `loadscreen_20s_ext_off.png` (the same 20 s under
`/profile:faithful`: every pixel 0, retail's black), in `scratchpad/issue13/loadscreen/`.

**When.** Only at boot, only when at least one mod is enabled (with none the boot is unchanged and the
log says so), only on the two outer loader loops; it ends at the first splash's Startup, so the boot
from the Shining Rock splash on is retail's. The other `GameLoaderLoop` routes keep their short black
frames (their own Loading screen follows within a frame or two).

**Units, and why no percentage.** The counter is the enabled-mod ordinal `LoadResources` is on, out
of the enabled mods it walks: exact, cheap (one relaxed store per mod, a handful per load) and the
unit the user asked for. A percentage would need an honest denominator (the closure's resource count
is unknown until it is loaded; the last boot's totals per mod set would be a guess on the first boot
and after any mod change), so none is shown. The bytes-read counter in the packed `ReadFile` arm was
dropped (readpath / largepkm told).

**One reader, unchanged residency.** Nothing is loaded while the loader runs. The dialog and its
fonts / sprites are `game_main.cpp`'s main-thread preload of `Dialog/Loading.rsc`, done before
`MainLoop::Run` (row 1295 / INVENTORY R14 already own that preload); the string table
(`Dialog/ExtensionsStringTable.rsc`, two new keys) is provided from the exe and lives for the process
(KITFREE.md §2), and the menu loads it anyway for the Extensions button. Measured:

| Route | Part 1 | Part 2, ext on |
|---|---|---|
| Vanilla new game (DebugMenu + betterFields: the ext engages), default profile, debug, 2 boots each | `calls=5703 oF=8 oC=0 bM=0 rN=8 created=5695` | identical, both boots |
| MegaMod boot to the menu, perf, gdb | `calls=376431 cacheMiss=376431 returnedNull=10` | `calls=376431 cacheMiss=376431 returnedNull=10`; FindOrLoad +84 cache HITS (our table and dialog lookups) |

The private UI is freed at `BootEnd` in `DialogLoaderLoopT`'s own order (CloseDialog, the
UserInterface door's destructor, the storage). The faithful profile never installs it (the resave
gate runs faithful).

**Seams** (DESIGN.md hook rule 5; Names table, per-object flag row):

| Seam | Where | Null path |
|---|---|---|
| `Engine::LoaderLoop::_extFrame` | `loops.cpp`: `LoaderLoop::Update` after retail's body (`kExtUpdate`), `LoaderLoop::Draw` first (`kExtDraw`, true = drawn, the black clear skipped) | one null test per frame |
| `Engine::UpdateLoop::_extLoadProgress` | `updateloop_loadresources.cpp`: the enabled count before the mod walk, the ordinal per mod, `lists` at the end (relaxed atomics) | one null test per mod |
| `Ext::LoadProgress::BootBegin` | `game_main.cpp`, after the splash preloads, before `MainLoop::Run` | returns at once with the row off, under the faithful profile, or with no mod enabled |
| `Ext::LoadProgress::BootEnd` | `GameLoadDialogLoopT::Startup`, first statement | no-op unless installed |

**Code.** `src/ext/load_progress/load_progress.{h,cpp}` (the screen: a `UserInterface` from the door,
a `LoaderDialog` over the preloaded dialog, the splash's UI scale, the text through `SetText` +
`SetParameter`, the draw = `DialogLoaderLoopT::Draw`'s body over our UI), the registry row
(`load_progress`, "Mod loading screen"), two string keys, the row icon
(`tools/ext_icons/loadprogress.py`, baked), `tests/unit/ext_load_progress_test.cpp` (the row, and the
text rule `TextFor`). No platform or backend code: it draws through `Video::ContextData` and the UI,
so the Metal backend is untouched.

**Open.** If INVENTORY R14 is restored (the main-thread splash preloads dropped), `BootBegin` must
load `Dialog/Loading.rsc` itself, on the main thread before `MainLoop::Run`. The screen's first frame
comes after mount #1 (`SetupFileSystem`, ~0.5 s with MegaMod) and the InnerLL's mount #2 (~2 s, no
frames, retail's) still freezes the spinner once.

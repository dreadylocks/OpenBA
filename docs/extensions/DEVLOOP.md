# DEVLOOP: rebuild, relaunch, carry on where you were

Opened 2026-10-25. A proposal: nothing is built yet and nothing here is ruled. It is a developer
tool, not a player feature. It ships in no release, changes no game behaviour and needs no
`ext.*` row. Player-facing updates are out of scope.

**The goal.** You change code on the Mac or the PC, and the running game on either machine (or
both) picks up the new build and puts you back in the same town, looking at the same spot, with no
clicks. It is a fast **restart**, not an in-process hot swap (§1 says why), and it needs no code
signing on either platform (§5).

## 1. Why a restart and not a hot swap

- **A reloadable game library** (game code in a DLL / dylib, reloaded in place) needs game state
  that survives the old code being unloaded. Banished's world is retail's polymorphic objects:
  every live component's vtable points into the module that built it. Unloading that module
  leaves every object pointing at freed code.
- **Function patching** (Live++, jet-live: compile the changed file, load it, overwrite the start
  of each old function with a jump to the new one) avoids the vtable problem. But Live++ needs
  MSVC or clang-cl (the Windows build is MSYS2 GCC), jet-live has no Windows port, and on Apple
  Silicon rewriting code pages fights the kernel's code-signing checks. A home-grown patcher is
  weeks of work and fails in subtle ways (statics, inlined callers, changed layouts).
- **A restart runs the real code.** Everything already loads a save faithfully (the resave gate
  depends on it), so "save, quit, relaunch, load" is correct by construction. It costs the
  incremental build plus one load per change.

## 2. Facts it rests on

- **`BANISHED_AUTOLOAD=<name>`** loads `<user dir>/Save/<name>.sav` from the front end; `=1` loads
  the autosave `Auto` (`src/app/options.h`, `Opt().autoload` / `autoloadName`;
  `tools/resave_gate/resave_gate.sh` uses it).
- **`BANISHED_SAVE_TEST=<frame>`** runs retail's autosave at that frame through
  `AutoSaveDialog::Autosave` (`src/diag/saveload_harness.cpp`). That path is
  `SetSaveName("Autosave", 2)`, and `Save::SaveLoad::UpdateSave` writes `<SaveDir>/Auto.sav`
  **asynchronously**. The harness polls `Save::SaveLoad::GetOperation` until it returns to 0. It
  is a frame trigger, not a request from outside the process.
- **`BANISHED_CAMERA_PIN=x,y[,dist[,yaw]]`** (or `town[,dist[,yaw]]`) frames the camera at boot
  (skill `camera-pin`). Nothing writes the current camera out in that format today.
- **`BANISHED_PAUSEMENU` / `BANISHED_PAUSEMENU_BUTTON=6`** quits the game through the pause menu
  (`src/diag/pausemenu_harness.cpp`). That is the clean in-game quit route the harness already
  drives.
- **`BANISHED_USER_DIR`** replaces `<Documents>/Banished` as a whole (settings.cfg, `Save/`,
  `Cache/`). A dev loop runs against its own user dir, so its saves never touch your real ones.
- **Windows:** `tools/build.sh deploy` copies `build/cmake-debug/OpenBA.exe` into the game dir and
  fails with "exe busy" while the game runs. The exe is locked until the process exits.
  `tools/boot_state.sh --wait` tells a healthy boot from a modal or a hang. Exit 3 means
  `standalone_crash.log`, exit 4 means `standalone_hang.log`.
- **Mac:** `tools/macos/make_app.sh` builds `dist/OpenBA.app` and ad-hoc signs it
  (`codesign --sign -`, no hardened runtime). `tools/macos/run_openba.sh --game-dir DIR` runs it.
  Logs go to `~/Library/Application Support/OpenBA/Run/`.

## 3. The loop

```
 edit ──► build (ninja, ccache) ──► stamp file changes
                                        │
            watcher ◄───────────────────┘
               │ 1. ask the running game to save + quit    (stage 1; stage 0 just kills it)
               │ 2. wait for the process to exit           (timeout → kill)
               │ 3. swap in the new build                  (Windows: copy exe; Mac: swap .app)
               │ 4. relaunch: AUTOLOAD=<slot> CAMERA_PIN=<saved pin> USER_DIR=<dev dir>
               │ 5. boot_state --wait: in game? crash? modal?  → one line to the terminal
               └─► back to waiting
```

**The stamp.** The watcher never watches the exe itself: ninja and `make_app.sh` write it in
several steps, and a watcher that fires mid-write copies a half-linked binary. The build wrapper
writes `build/devloop.stamp` (the commit, the time, the build's path) as its last step. That one
small file appearing is the signal.

### Stage 0: script only, no exe change

`tools/devloop.sh` (bash; runs under MSYS2 on Windows and natively on the Mac):

1. Wait for the stamp to change.
2. Kill the game.
3. Swap in the build (§4).
4. Relaunch with `BANISHED_AUTOLOAD=1` (the last autosave), a fixed `BANISHED_CAMERA_PIN` from
   `devloop.cfg`, and `BANISHED_USER_DIR=<dev user dir>`.

It reuses only flags that exist. You lose the play since the last autosave and the camera falls
back to the fixed pin. Killing the process during an autosave can leave a torn `Auto.sav`, so the
script keeps a copy of the last good one and restores it if the load fails.

### Stage 1: a clean handover (one harness flag)

**`BANISHED_DEVLOOP=<dir>`**, in the harness section of `src/app/options.h` beside the autoload
and pause-menu drivers, read by a new `src/diag/devloop_harness.cpp`:

- **On a request.** Once a second, at a frame boundary, in game only (never in a menu, a modal or a
  load), the harness checks for `<dir>/request`. When it is there:
  1. Delete the request.
  2. Autosave through `AutoSaveDialog::Autosave`, as `BANISHED_SAVE_TEST` does.
  3. Write `<dir>/camera.txt` as a `BANISHED_CAMERA_PIN` value: target x,y, distance, yaw.
  4. **Wait for the write:** poll `Save::SaveLoad::GetOperation` until it returns to 0. The save
     is asynchronous, and quitting earlier tears it.
  5. Quit through the pause-menu route (button 6, Quit Game), so settings.cfg is written as on any
     quit.
- **The watcher** writes `request` instead of killing, then waits up to a timeout (default 30 s)
  for the process to exit. Only then does it kill. A game sitting in a menu or a modal ignores
  the request, so the timeout is the fallback.
- **Relaunch** passes `BANISHED_CAMERA_PIN="$(cat <dir>/camera.txt)"`, so you come back to the
  spot you left.

### Saying it is a restart, not a crash

A game window that vanishes without warning looks like a crash, so every restart announces itself,
before and after:

- **Before the window closes (stage 1).** The harness shows a non-blocking banner for about two
  seconds before it saves: `New build <commit>: saving and restarting…`. It must not be a modal:
  a modal blocks the frame loop, and the harness's own request would then never run. The same text
  goes to `standalone_error.log` as `[DevLoop]`.
- **Before the window closes (stage 0).** The game cannot be told, so the watcher posts an OS
  notification as it kills the game: `osascript -e 'display notification …'` on the Mac, a
  PowerShell balloon tip (`System.Windows.Forms.NotifyIcon`, no module needed) on Windows. It also
  prints the same line in its terminal.
- **After the relaunch.** Once the save has loaded, the banner says
  `Reloaded <commit> (<build time>)`, so you can see the new code is the one running. The
  watcher's terminal line says the same.
- **A real crash still looks like one.** The watcher tells its own restart from a death by the
  exit code. A clean quit after a request is exit 0. Exit 3 (`standalone_crash.log`), exit 4
  (`standalone_hang.log`), or any exit it did not ask for is reported loudly, with the log's first
  lines. It is not relaunched over, so the crash stays on screen to read.

### Stage 2: the other machine

To edit on one machine and run on the other: **build on the machine that runs the game.** Copying
binaries across is what drags in signing and quarantine questions (§5). Moving source is plain git:

- The running machine has a checkout on a `devloop` branch. You push to it from the editing
  machine: an ssh remote straight into that checkout's bare twin, or any shared remote.
- The watcher there sees the ref move (`git fetch` every few seconds, or an ssh `touch` of a
  trigger file from a post-push hook). It resets the checkout to the pushed commit, builds, writes
  the stamp, and the normal loop takes over.
- Both machines can run their own loop on the same branch, so one push updates both games.

No server, no upload and no artifact store. CI's Windows artifact takes minutes and needs a
GitHub sign-in, which is too slow for this.

## 4. Swapping the build

| | Windows (MSYS2 bash) | Mac |
|---|---|---|
| Build | `tools/build.sh build debug` | `tools/macos/make_app.sh <staging>.app` (never the `.app` that is running) |
| After exit | `cp -f` the exe into the game dir (the copy in `deploy()`, without the build) | rename the old `.app` to `.old`, the staging `.app` into place; delete `.old` after a good boot |
| Relaunch | `cd <game dir> && env … start OpenBA.exe` | `env … tools/macos/run_openba.sh --game-dir <dir>` |
| Health | `tools/boot_state.sh --wait 300` | the run log in `~/Library/Application Support/OpenBA/Run/` (boot_state on the Mac: §6) |

**Mac: never overwrite the binary inside the running `.app`.** The kernel caches a binary's
code-signature pages. Rewriting the file in place under a live process can get that process (or
the next launch) killed. Building into a staging bundle and renaming whole bundles avoids it.

## 5. Why no signing is needed

- **Windows.** SmartScreen and "Windows protected your PC" act only on a file carrying the
  Mark of the Web (the `Zone.Identifier` stream a browser, Explorer or a mail client adds to
  downloads). An exe that ninja links, or that a script copies locally, has none. Building on the
  running machine (stage 2) means no exe ever crosses a network.
- **Mac.** Apple Silicon refuses to run unsigned code, but an ad-hoc signature is enough for your
  own machine, and `make_app.sh` already adds one. Gatekeeper's notarisation check applies only to
  quarantined files (downloaded by a browser or another quarantine-aware app). A local build is
  never quarantined. Without hardened runtime there is no library validation either, so
  `libairconv.dylib` and any later dylib load as built.
- **What would need signing:** shipping builds to other people, which this loop never does.

## 6. Open points

- **The repo rule on instruments.** `CLAUDE.md` / `DIRECTION.md` §2 say no new instruments or
  `BANISHED_RT_*` flags. `BANISHED_DEVLOOP` is a harness flag (AGENTS.md §3 allows those, named,
  with the script that sets them), but it is a new instrument. Stage 0 needs nothing new. Stage 1
  is the fork's call.
- **The save slot.** Retail's autosave path is hard-wired to `SetSaveName("Autosave", 2)` →
  `Auto.sav`, which is fine inside a dev user dir. A dedicated `DevLoop.sav` would need the same
  call sequence with another name: read what the `2` selects before relying on it.
- **The banner's surface.** The in-game banner needs a non-blocking text surface the harness can
  write to (retail's in-game message line, or a plain overlay drawn by `src/diag`). Find which
  exists before building a new one. `src/ext/load_notice.h` is a modal, so it does not fit.
- **The camera's state.** The pin takes target, distance and yaw, not pitch. Coming back at the
  default pitch is acceptable for a first version. Writing the pin out means reading the live
  camera where `src/game/Game/camera_input.cpp` applies `Opt().cameraPin`, in the other direction.
- **boot_state on the Mac.** `tools/boot_state.sh` reads `$BANISHED_DIR/standalone_boot_state.txt`
  (default `C:/GOG Games/Banished`). On the Mac the run files land in
  `~/Library/Application Support/OpenBA/Run/`. Pointing `BANISHED_DIR` there may be enough: check
  that the state file is written there and the script's process check works on macOS.
- **Profiles.** The loop runs your own settings.cfg (extensions as you set them), not the forced
  faithful profile the gate uses. Pass `/profile:faithful` through `devloop.cfg` when you are
  iterating on faithful code.
- **What it costs per change.** Unmeasured. It is the incremental build and link plus one save
  load. Measure both once stage 0 runs before deciding whether anything faster is worth chasing.

## 7. Running it (`tools/devloop.sh`)

Built 2026-11-22: stages 0, 1 and 2 in one script, bash, the same file on the PC (MSYS2 or Git
Bash) and the Mac. Settings are per machine in `~/.config/openba/devloop.cfg`;
`tools/devloop.cfg.example` lists every key.

```bash
tools/devloop.sh watch     # leave running: follows origin/claude/devloop, builds, restarts the game
tools/devloop.sh build     # local edit: build + stamp; a running watch restarts the game into it
tools/devloop.sh restart   # one restart without a watcher
tools/devloop.sh launch | stop | status | config
```

**What one push does.** Every watcher polls `git fetch` (10 s). When `origin/<branch>` differs from
the commit in `build/devloop.stamp`, it fast-forwards the checkout (only when that is clean, or
already at that commit because this machine pushed it), builds, writes the stamp, and restarts:
`request` -> the game saves and quits -> the exe (PC) or the whole `.app` (Mac, built into
`dist/OpenBA.staging.app`) is swapped -> relaunch with `BANISHED_DEVLOOP` + `BANISHED_AUTOLOAD=1`
-> wait for `state` = `running <commit>`. A failed build is reported once and leaves the old game
running until the next push.

**What it does not hide.** A crash (exit 3), a hang, any exit nobody asked for, or a relaunch that
does not reach `running` is printed with the log's first lines and an OS notification, and is not
relaunched over. A game that does not answer `request` in 30 s (a menu, a modal, an exe without the
harness) is killed, and `Auto.sav` is restored from the copy taken before the request.

**Measured on the PC** (debug, the user's own 5 MB town): save and quit 3 s, relaunch to
`running` 32 s; a one-file rebuild about 60 s.

**Windows notes.** Git Bash puts its own `/mingw64/bin` first and the UCRT64 GCC then fails with no
message; the script puts `/c/msys64/ucrt64/bin` first itself. The game is started from bash in the
background (not `start`), so the watcher reads its exit code. An autoload can stop at a save/load
modal (`modsOutOfDate` with an old modded save): `EXTRA_ENV="BANISHED_MODAL_ANSWER=2"` answers it.

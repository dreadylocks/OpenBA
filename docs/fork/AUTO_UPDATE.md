# Updates: OpenBA checks, downloads and installs its own builds

Opened 2026-11-25. The design below was agreed with the maintainer on 2026-11-25: signed manifests,
the Mac builds and publishes both required platforms, Linux only if cheap. It is built as described:
extension `ext.auto_update`, code in `src/ext/auto_update/`. §14 records how it was tested.

**The goal.** A player installs one update-capable build by hand: the zip from the releases page,
with the usual first-run prompts (Gatekeeper, SmartScreen). After that, getting a new build takes
at most a restart of the game. The player downloads, unzips, installs and builds nothing. Windows
and macOS are required. Linux came almost free and is included (§11).

This is not the dev loop (`docs/extensions/DEVLOOP.md`). The dev loop rebuilds from source and
needs the repo and the toolchain on the machine. A player has neither. Two parts of it are reused:
its save-and-quit sequence (§7) and its rule never to overwrite the binary inside a running `.app`
(§6). Its §1 also gives the reason there is no hot reload here: game objects' vtables point into
the module that built them, so the code cannot be swapped under a live world. A restart that
reloads the same town (§7) is what this design offers in its place.

## 1. What it rests on (checked on `main`, 77a86958f)

- **Network.** `Platform::HttpPost` and the WebSocket client exist. There is no GET yet. Each
  platform uses the OS's own HTTP and TLS stack: WinHTTP on Windows and Network.framework on the Mac,
  where HTTP/1.1 is written by hand (`websocket_frames.h`). On Linux the stack is sockets plus OpenSSL.
- **Packages.** Windows ships `OpenBA-v<ver>.zip`: one statically linked `OpenBA.exe` plus fonts and
  texts. The player unzips it into the Banished folder, and the exe runs from there. The Mac ships
  `OpenBA-v<ver>-mac-arm64.zip`, an ad-hoc-signed `OpenBA.app`. Its launcher (`Contents/MacOS/OpenBA`,
  `packaging/macos/launcher.mm`) links `~/Library/Application Support/OpenBA/Run/OpenBA` to the
  bundle's engine and `execv`s it. Saves, logs and settings live outside the bundle. Each package
  already has a `.sha256` beside it. Sizes at v1.1.1: Windows 4.7 MB, Mac 12.8 MB, Linux 9.5 MB.
- **Releases.** GitHub Releases on `dreadylocks/OpenBA`. The latest is v1.1.1 (2026-10-25). `main`
  says `VERSION 1.1.2`, which was never released. `.github/workflows/release.yml` builds and
  publishes all three platforms on a `v*` tag, but Actions is off on the fork. The Mac can also
  cross-build the Windows exe (`tools/build.sh build release --cross`, mingw-w64).
- **The relay does not refuse a different build.** A different `engineVer` or `simBuild` only
  *warns* (`{"t":"compat"}`). It refuses only a different protocol version or different retail data
  (`tools/neighbour_server/src/index.js` `compareCompat`, `NEIGHBOUR_PROTOCOL.md`). `simBuild` always
  differs between Windows and Mac, because it hashes the compiler and the target. "The same build"
  therefore means the same `engineVer` (`OPENBA_VERSION`).
- **Boot.** Both entry points (`WinMain`, POSIX `main`) start with `CrashReport::KeepLastCrash()`,
  then reopen the logs. The settings are read later (`Ext::InitFromSettings`).

## 2. The whole flow

```
 launch ─► AtLaunch (first line of main, before the logs reopen)
             ├─ a verified build is staged and newer?  ─► install it, start it   (§6)
             ├─ the last new build never reached the menu? ─► roll back           (§8)
             └─ otherwise carry on booting
 boot ... settings read ─► background check (§3): manifest ─► newer? ─► download ─► SHA-256 +
                           signature ─► unpack into staging ─► "ready"
 menu / game / lobby  ─► "OpenBA 1.2.1 is ready" + [Restart now] (§7)
 next launch or Restart now ─► AtLaunch installs it
 first menu frame of the new build ─► "started" (confirms it; the old copy is kept one more round)
```

## 3. Where and when it checks

- **At launch**, on a background thread once the settings are read, so it never delays the boot.
- **Every two hours while the game runs.** Players leave towns running.
- **In the Multiplayer lobby**, when the relay's `compat` warning shows the neighbour runs another
  `engineVer`. At most once per five minutes.

One check is a single HTTPS GET of a file of about 400 bytes. A failed check (offline, GitHub down)
is one `[Update]` log line, and the game tries again at the next check. Nothing waits on it.

**Which builds check.** Only builds made by the release script (§10) know where to look: CMake
`OPENBA_UPDATE_FEED`, empty by default, like `OPENBA_CRASH_DSN`. Dev builds, `tools/build.sh deploy`
and the dev loop never replace themselves. A release build would otherwise overwrite your own
build on the next start.

## 4. Finding the newest release

The feed is the release asset `openba-update-<platform>.txt`, fetched by GitHub's stable link:

```
https://github.com/dreadylocks/OpenBA/releases/latest/download/openba-update-macos-arm64.txt
```

GitHub redirects `latest/download/<asset>` to that asset on the newest release that is published
and not a pre-release. It needs no API call, no token and no JSON, and it has no 60-an-hour limit.
WinHTTP follows the HTTPS redirect by itself. Platforms are `windows-x64`, `macos-arm64` and
`linux-x64`.

**The manifest** is one small text file per platform:

```
openba-update 1
version=1.2.1
commit=77a86958f3c1
platform=macos-arm64
url=https://github.com/dreadylocks/OpenBA/releases/download/v1.2.1/OpenBA-v1.2.1-mac-arm64.zip
size=12812809
sha256=<64 hex>
signature=<128 hex: Ed25519 over every byte above this line>
```

- `url` names the package on that release's own tag. If a newer release goes live mid-download, the
  package therefore still matches its manifest.
- **The version compare** is `X.Y.Z` with an optional `-suffix`. A suffix sorts before the plain
  version, as in semver. The game updates only to a version strictly newer than its own
  `OPENBA_VERSION`. It never moves to an older one, so pulling a release cannot downgrade anyone.
- The first line names the format version. A manifest of a format the game does not know is skipped.

## 5. Integrity

Two checks, both before anything is unpacked:

1. **SHA-256 and size** of the downloaded package against the manifest. This catches truncated or
   corrupt downloads. The download stops as soon as it passes `size`.
2. **An Ed25519 signature** over the manifest, checked against a public key compiled into the exe.
   HTTPS already proves the file came from GitHub. The signature proves it came from you. Someone
   holding a stolen GitHub token could upload a package and a manifest that agree, but cannot sign
   them. The private key lives on the publishing machines (`~/.config/openba/update_signing.key`,
   never in the repo) and in your password manager. Losing it means players install one build by
   hand again, the one carrying a new key. Ed25519 comes from Monocypher: one vendored `.c`/`.h`
   pair, CC0 or BSD-2-Clause, so it adds a `THIRD_PARTY_NOTICES.md` row. Neither Windows' CNG nor
   macOS's C APIs offer Ed25519. SHA-256 is a small portable implementation in
   `src/platform/common/`, unit-tested against the NIST vectors.

The archive is unpacked with the OS's own tool: `tar.exe` on Windows (built in since Windows 10
1803), `/usr/bin/ditto -x -k` on the Mac, `tar` on Linux. Both checks above run before that step.
On the Mac the unpacked bundle must also pass `codesign --verify --deep --strict`.

## 6. Installing at the next launch

The background check stages the build:
`<staging>/<version>/` holds the unpacked package, and `<staging>/ready` names it. The launch after
that installs it, before the logs reopen and before any engine code runs.

| | Windows (and Linux) | macOS |
|---|---|---|
| Staging | `<exe folder>\OpenBA-update\`: the same volume as the exe, so every move is a rename | `~/Library/Application Support/OpenBA/Updates/` |
| Install | Rename the running `OpenBA.exe` to `OpenBA-update\previous\OpenBA.exe`. Windows allows renaming a running exe; it forbids overwriting or deleting one. Then move the new files into the folder. | Rename `Updates/staging/<ver>` to `Updates/<ver>`, then write `Updates/current` = `<ver>`. Nothing is written into any `.app` the player installed, and nothing into a bundle that is running. |
| Start it | Start the new `OpenBA.exe` with the same command line and folder. The old process stays as a **supervisor**: it waits for the new one's "started" or its exit (§8). | Point the `Run/OpenBA` link at the new bundle's engine, set `BANISHED_AIRCONV` to that bundle's converter (what the launcher does), and `execv` the link. Same process, same Dock icon. |
| Every later launch | The new exe is simply `OpenBA.exe`. | The player's own bundle starts its engine. That engine's AtLaunch sees `current` is newer than itself and hands over to it with the same `execv`: two extra execs, under 0.1 s. |

The player's own launcher (`packaging/macos/launcher.mm`) is unchanged and keeps running at every
start: it finds the game data and makes `Run/`. Only what comes after it is updated. A future change
that needs a new launcher is the one kind that still needs a manual install; everything the
launcher hands the engine can be redone engine-side.

**Why the Mac stages outside the player's `.app`.** No admin rights are needed, even when the app
sits in `/Applications`. App Translocation (a quarantined app run from Downloads runs from a
read-only random path) cannot get in the way. Rollback is choosing a folder. And a bundle that is
running is never touched. The cost: Finder's "Get Info" on the installed app shows the version first
installed. The fixed contract that every future build must honour is small: `Updates/current` names
a folder, and that folder holds `OpenBA.app`.

**The Windows folder must be writable.** A GOG folder (`C:\GOG Games\Banished`) is. Steam grants its
own library folder to users. If the rename fails (for example a game under `Program Files` without
those rights), nothing is installed. The menu then says why: "OpenBA can't update itself in <folder>:
your account can't write there."

## 7. Telling the player, and Restart now

- **When a build is ready:** a line in the main menu beside the build line ("OpenBA 1.2.1 is ready:
  it installs when you restart"). In game, a row in the event log, the same surface the dev loop's
  banner uses. In the Multiplayer lobby, the line sits next to the version warning.
- **Restart now** (a button in the main menu and the lobby, shown only while a build is ready). In
  game it runs the dev loop harness's sequence, moved into the updater: the retail autosave
  (`AutoSaveDialog::Autosave`, written to `Auto.sav`), a wait until the asynchronous write is over
  (`Save::SaveLoad::GetOperation`), then `<staging>/resume` = `Auto` and the game's own PID, then
  `Application::Quit`. Quitting writes `settings.cfg` as on any quit. In the front end there is
  nothing to save, so it quits at once.
- **The restart** happens after the clean shutdown, as the last thing before `main` returns
  (`AutoUpdate::AtExit`). On the Mac and Linux it `execv`s the program again: the same process and
  Dock icon. On Windows it starts `OpenBA.exe` again, and the new process waits for the old PID
  (written to `restart`) before it reads anything. AtLaunch then installs the build. At the new
  build's first main-menu frame it reads `resume`, deletes it and loads that save as a player would
  (the menu's Load, the save, Load). This is *not* `BANISHED_AUTOLOAD`, which marks the boot
  unattended and answers the player's modals for them. In neighbour towns the loaded save rejoins
  the room as any reload does.
- **A plain restart works too.** Quitting and starting the game again installs the ready build.
  Restart now only spares the player the reload.

## 8. Rollback when a new build will not start

**"Started"** means the new build drew its first main-menu frame, or its first game frame after a
resume. It records `started <version>` in the staging folder.

- **Windows.** The supervisor (the old exe, §6) waits on the child process. If the child writes
  "started", the supervisor exits. If the child exits non-zero first (a crash is exit 3, a hang
  killed by the watchdog is exit 4, a failure before `main` is anything else), the supervisor moves
  `previous\OpenBA.exe` back, adds the version to `<staging>/bad` and starts the old exe again. That
  fresh start keeps the failed run's crash logs (`KeepLastCrash`) and sends them if crash reports
  are on. An exit 0 before "started" (the player closed the window mid-boot) is no verdict: nothing
  is rolled back.
- **Mac.** `execv` leaves no parent to watch, so the build that hands over counts instead. Before it
  `execv`s version V, it writes `Updates/pending` = V. V's "started" deletes it. If a launch finds
  `pending` = V still there, V's last start never reached the menu. That launch adds V to `bad`, sets
  `current` back to the previous version, and runs that one instead. A Mac player therefore sees one
  failed start, and the next launch is back on the old build.
- **Either way**, the menu says "OpenBA 1.2.1 did not start, so this is 1.2.0 again", and a `bad`
  version is never installed again. A newer release still is. One previous build is kept, and older
  ones are deleted once the new one has started.
- A save written by the newer build may not load in the older one. Retail's format is unchanged
  between our builds, but extension data could differ. Rollback is for a build that cannot start,
  not a way back after playing on it.

## 9. Quarantine, Mark of the Web, SmartScreen

- **The first install meets them all,** because a browser downloads the zip. On the Mac:
  right-click > Open, or System Settings > Privacy & Security > Open Anyway. On Windows: SmartScreen's
  "More info > Run anyway". `docs/fork/TWO_PLAYER_TESTING.md` gets a short player section on this.
- **Updates meet none of them.** Windows adds the Mark of the Web (the `Zone.Identifier` stream) only
  when a browser, Explorer or a mail client saves the file. A file that WinHTTP fetches and the game
  writes has none, and `tar.exe` has no mark on the zip to carry over. On the Mac only a file with the
  `com.apple.quarantine` attribute is checked by Gatekeeper. A browser sets it, as does a sandboxed
  app or one that declares `LSFileQuarantineEnabled`. OpenBA is none of those, so neither the zip it
  writes nor the bundle `ditto` unpacks from it gets the attribute. An ad-hoc signature is all Apple
  Silicon needs to run a non-quarantined binary. The end-to-end test (§12) checks both claims with
  `xattr -l` and a real launch, from a first install that was quarantined and approved by hand.
- **Caveats.** Windows 11's Smart App Control, when on (only possible on a fresh install), blocks
  unsigned exes whatever their origin. Signing (SignPath, `docs/reference/code_signing.md`) would
  cure that and Defender's suspicion of self-updating exes; it is not needed otherwise. On the Mac,
  macOS 13+ may tag files with `com.apple.provenance`. That attribute does not trigger a Gatekeeper
  check, and the test confirms it.

## 10. Publishing, so there is always something to update to

**One script, `tools/release/publish_update.sh`.** On the Mac it builds both required platforms:
macOS natively and Windows with mingw-w64 (`tools/build.sh build release --cross`), the
maintainer's choice of 2026-11-25. On a PC or a Linux machine it builds that machine's own platform,
and can add it to the same draft. CI can run the same steps once Actions is on:

1. Refuse unless the tree is clean, `HEAD` is a commit on `origin/main` and the tag `v<VERSION>` is
   not already published.
2. Build the release flavour and package it with the existing scripts, `OPENBA_UPDATE_FEED` set.
3. Write `openba-update-<platform>.txt` and sign it with the local key.
4. Create the release `v<VERSION>` as a **draft** on that commit if it does not exist yet (notes from
   `CHANGELOG.md`, `--repo dreadylocks/OpenBA`). Upload the package, its `.sha256` and the manifest.
   A second machine must be on the same commit, or the script stops.
5. Once the draft holds a manifest for every required platform (`windows-x64` and `macos-arm64`),
   publish it. Only then does `latest/download` point at it, so neither player is offered a build
   the other cannot have yet.

A release is therefore: a PR bumping `VERSION` plus a `CHANGELOG.md` section, merged; then
`tools/release/publish_update.sh` on the Mac. `--dry-run` does everything but the GitHub steps.
The reference Windows toolchain is still MSYS2 on the PC, where the Windows gate runs: the
cross-built exe of the first release made this way should be run once on the PC before players get
it.

`release.yml` has the same feed and a signing step for when Actions is on. The key is then the
repository secret `OPENBA_UPDATE_SIGNING_KEY`; without it a CI release has no manifests, and no game
updates to it.

## 11. Settings, code layout, rules

- **`ext.auto_update`** (an Extensions dialog row, class A, default **on**, like `ext.crash_reports`):
  `ext.auto_update=0` in `settings.cfg` stops checks, downloads and installs. The game then stays
  on the build it is on; it does not fall back to the one first installed. AtLaunch runs before the
  settings store exists, so it reads that one key from `settings.cfg` itself.
- **`update.feed=<url>`** overrides the feed address, for tests (a local server) or a self-hosted
  feed, as `crashreport.dsn` does for crash reports.
- **No new environment flag.** The restart handover passes its PID and save name in files in the
  staging folder.
- **Code.** `src/ext/auto_update/`: the pure parts (`manifest.cpp`: versions and the manifest;
  `sha256.cpp`; `launch_plan.cpp`: the launch decisions as a function of the staging folder's
  state), `staging.cpp` (the state files), `auto_update.cpp` (AtLaunch, the supervisor, the check
  thread, AtExit) and `auto_update_ui.cpp` (the button, the build line, the event-log row, Restart
  now's save, the resume). The hooks: AtLaunch and AtExit in both entry points of
  `src/app/main.cpp`; EXT SEAMs in `MainMenuDialog::OnOpened` / `OnCommand`, the menu loop's and the
  game loop's ticks; `CheckSoon` from ext.neighbour's `compat` handler. The OS work sits behind
  `src/platform/common/platform_update.h` (`update_win.cpp`, `update_darwin.cpp`, `update_posix.cpp`;
  child processes in `src/platform/posix/spawn_posix.cpp`) and `Platform::HttpGet` beside
  `HttpPost` in `platform_http.h`. No `#if _WIN32` in app or ext code. Monocypher is under
  `third_party/monocypher/`; the release tool is `tools/release/update_sign.cpp` (CMake target
  `update_sign`).
- **The GET.** WinHTTP does all of it on Windows, redirects included. On the Mac and Linux the twin
  only connects (Network.framework; sockets plus OpenSSL) and moves bytes. The request, the answer's
  framing (a length, chunked, or until the close) and the redirects are one shared, unit-tested
  reader, `src/platform/common/http_reader.cpp`. That made Linux cheap, so it is included.

## 12. How it is tested

- **Unit (`tests/unit/`, every platform):** the version compare; manifest parsing (missing, extra,
  malformed and duplicate keys; wrong platform; future format); the signature check (a test key
  pair, a flipped byte); SHA-256 against the NIST vectors; the AtLaunch decision table (ready, newer,
  bad, pending, off, not writable); the staging state machine.
- **Local feed on the Mac:** build 1.2.0 and 1.2.1. Install 1.2.0 the way a player does (quarantine
  set as a browser would, approved by hand). Serve 1.2.1's manifest and zip from a local server
  (`update.feed=http://127.0.0.1:<port>/`). Then check: download, no quarantine attribute, installed
  at restart, Restart now resumes the town, a 1.2.2 built to fail at boot is rolled back and marked
  bad, `ext.auto_update=0` stops it all.
- **Windows:** cross-built on the Mac for every change. Running it needs the PC: the same local-feed
  test, with the supervisor's rollback.
- **End to end, one release to the next:** publish v1.2.0 (the first update-capable build, Windows
  and Mac), install it from the releases page on both machines, publish v1.2.1, restart, and both
  are on 1.2.1. **This publishes two real releases, so it waits for your OK.**

## 13. Considered and not taken

- **Sparkle (Mac) and WinSparkle (Windows).** Mature, but they are two systems with two feeds and
  native dialogs rather than the game's own. Sparkle wants an Objective-C framework in the bundle.
  Neither can resume into a save or keep two players on one version.
- **A launcher stub on Windows** (a small `OpenBA.exe` that runs a versioned engine). It would catch
  even a pre-`main` crash without a supervisor, but it changes the package layout and how the game
  is started. The supervisor gets the same result inside today's layout.
- **Swapping the Mac `.app` in place.** It needs write access to `/Applications`, fails under App
  Translocation, and touches a bundle that is in use.

# macOS: saves, settings, audio, mods (historical observations, 2026-10-19)

> Session log. These notes describe the builds of 2026-10-19; current facts are in [../../reference/macos_data_audio.md](../../reference/macos_data_audio.md). The numbers are the recorded results of those runs; the one-off harness scripts are not reproducible from this checkout.

Files: `src/platform/posix/{settings,file}_posix.cpp`, `src/platform/sdl/audio_sdl.cpp` (unchanged, verified),
`tests/unit/platform_mac_data_test.cpp`. (`settings_posix.cpp` and `file_posix.cpp` are now `settings_darwin.cpp` and `file_darwin.cpp` in `src/platform/darwin/`.)

## 1. Where things live (decision)

| What | Path on macOS | Why |
|---|---|---|
| Saves | `~/Documents/Banished/Save/*.sav` | The original game's folder (`SHGetFolderPath(CSIDL_PERSONAL)\Banished\Save`) and the one the Unbanished port uses, so saves are interchangeable. |
| Settings | `~/Documents/Banished/settings.cfg` | OpenBA's existing design (`IO::SettingsStore::DefaultPath` = `<Documents>\Banished\settings.cfg`), unchanged on every platform. Retail kept these in the registry; there is no legacy store on macOS (`ReadLegacySettings` is false). |
| Retail data and mods | `<install>/WinData` (`*.pkg`, `*.pkm`) | Read-only, never written. |

`Platform::DocumentsDir` (`settings_posix.cpp`) returns `<home>/Documents` on Apple, `<home>/.local/share` on other POSIX.
`<home>` comes from `getpwuid_r`, never from `$HOME` (project rule: no getenv outside `options.cpp`). The one redirect is
`Opt().userProfile` (`USERPROFILE`, read once at boot, the same switch the Windows build and `tools/resave_gate` use).
**Every test run must set `USERPROFILE` to a throwaway folder** (setting `HOME` alone changes nothing: the password
database wins). The previous macOS spike kept data in `<exe dir>/UserData`; that is gone (old `run/UserData` can be deleted).

Notes: macOS may show a one-time "wants to access your Documents folder" prompt for an unsigned app; a signed/notarised
bundle would still need the same consent. APFS is case-insensitive by default; `FindFirst` already matches patterns
case-insensitively and sorts like NTFS, and the READ primitives (`FileOpenRead`, `FileOpenReadShared`, `PathAttributes`)
now retry with a per-component case-insensitive directory match on ENOENT, so a case-sensitive volume works too.
Spaces and non-ASCII (UTF-8, NFC or NFD as stored) in the home path work (tested with "my home + accent"). Temp files
that become saves are 0644 (mkstemp's 0600 was leaking into `Auto.sav`).

## 2. Evidence: save -> load -> resave (engine's own serializer)

`tools/resave_gate` needs a Windows exe and a golden; the equivalent on a generated world is
a one-off round-trip script (three launches per home, silent, `BANISHED_BACKGROUND=1`, everything in `/tmp/mac_data_rt`):

1. `BANISHED_AUTOSTART=1 BANISHED_SAVE_TEST=240`: a new game, the retail autosave handler writes `Documents/Banished/Save/Auto.sav` (3.87 MB for the default town).
2. `BANISHED_AUTOLOAD=auto BANISHED_SAVE_TEST=roundtrip` (lower-case name on purpose): the save loads (case-insensitive lookup), the world is serialised
   again in-process; run twice with `BANISHED_MENU_SCENE=0`, the two resaved images are **byte-identical** (10,173,052 bytes), crash log 0 bytes.
3. The same in a home named `my home <e-acute>` (space + accent): identical outcome.

Loaded-vs-resaved is DIFF by design (README of `tools/resave_gate`: the reference table depends on resident resources; here +263 bytes), exactly as on Windows; there is no Windows golden for a Mac-generated world, so the determinism of the resave is the gate.
A save written after 240 simulated frames from a loaded world is of course not a fixed point (the world moved on).
Unit tests (`tests/unit/platform_mac_data_test.cpp`): DocumentsDir; settings write/read in a path with spaces and accents;
the engine's exact save sequence (DirCreate chain, FileCreateTemp, write 3 MB, FileFlush, FileMove, FindFirst `*.SAV`, FileOpenRead)
byte-identical; read primitives with another-case spelling. Passed on a normal volume and with `TMPDIR` on a case-sensitive APFS image
(`hdiutil create -fs "Case-sensitive APFS"`), where the fallback is what makes it pass.

## 3. Audio

Backend: `audio_sdl.cpp` (SDL 3.4.18, CoreAudio) as designed; no change was needed. On this Mac the default playback device is
`MacBook Pro Speakers`, 48000 Hz stereo S16, 1024-frame period (`/tmp/mac_data_audio_check --devices`, opens nothing). SDL opens the
default device at its own format, converts and resamples each voice, and follows default-device changes itself. A missing
or failing audio driver gives a silent game, no crash (`SDL_AUDIODRIVER=bogus`: `[Audio] SDL_InitSubSystem ... failed`, game runs).

Nothing was played through speakers. Verification is by recording with SDL's `disk` driver (S16LE stereo 44100) and analysing the raw
file (a one-off analysis script):

A one-off check program linked the real `audio_sdl.cpp`, `audio_feed.cpp` and the xWMA decoder
(138 checks, ~25 s, silent). Results:
* formats: S16/U8/F32/S32, mono and stereo, 8000-48000 Hz, all play 440 Hz for 0.5 s (22049 of 22050 frames, peak within the format's quantisation), mono upmixed to identical L/R, events Start/End/StreamEnd once each, `BuffersQueued` 0 after.
* gain: `SetVolume(0.25)` gives exactly 0.25x peak, 0 gives digital silence; `SetFrequencyRatio(2)` gives 878 Hz over 11024 frames (half length).
* loops: loopCount 2 = three passes, OnLoopEnd twice, OnBufferEnd once.
* underrun: a started voice with an empty queue emits exact zeros (silence, no noise), plays a later buffer, starves again, plays again; two bursts, both buffers end.
* decode-to-device: the retail xWMA `Sheep1.wav` decoded by `XwmaDecoder` (65536 frames), submitted as PCM and played through the backend: output equals the decode **sample for sample (0 mismatches of 65536)**.
* threads: 6 threads creating, submitting, retuning, stop/starting and destroying voices on one live device for 4 s (4300+ calls): no crash, no deadlock.

The game itself, `SDL_AUDIODRIVER=disk`, main menu: music appears about 5 s in at -30 dBFS RMS with `AudioMusicVolume=50`; with the setting at 100 the peak is 0.375,
at 50 0.187, at 25 0.094 (exact 2x steps: linear gain), at 0 digital silence: the settings.cfg value reaches `AudioVoiceSetVolume`.
In a new game (music 100) the streamed music is continuous for 40 s (about -24 dBFS, no dropouts); with music 0 the in-game effects/ambience are
audible for about 12 s and then silent for the rest of a 40 s capture (not investigated: could be emitters out of range, a quiet town, or a bug).
**Real audible output was not verified** (the owner was watching a video): CoreAudio device open, latency and glitches on real hardware remain untested.

## 4. Mods (.pkm)

The install's `WinData` holds `ColonialCharterJ.pkm` and `debugactive.pkm` (+ `.sub`). With mods enabled (no `-nomods`) the menu's Mods button opens
`ModDialog` with `mods=2`; the scripted route of `docs/runtime_port/phase_d/mods_route.sh` (details dialog, enable, apply) runs to the end, exit 0, crash log 0 bytes, and
writes `ModEnabled_*`/`ModIndex_*` to `Documents/Banished/settings.cfg`. A new game with both enabled in settings.cfg ran 50 s without a crash (3600 frames).
The pack directory is the game's `WinData` (read-only); only settings.cfg is written. Whether a given mod's content changes the gameplay was not examined.

## 5. Not done / open

* No audible check; no real CoreAudio playback of the game.
* Settings stay in `~/Documents/Banished` (not `~/Library/Application Support/OpenBA`); moving them needs `settings_store.cpp` (not implemented in that investigation).
* Old portable `run/UserData` data of earlier spikes is not migrated.
* `tools/resave_gate/resave_gate.sh` itself is Windows-oriented (`stat -c`, `md5sum`, `.exe`): not adapted; the round-trip script replaced it for macOS.

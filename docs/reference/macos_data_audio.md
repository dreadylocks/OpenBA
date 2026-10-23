# macOS: data, saves, settings, audio and mods

Current facts. Dated measurements are in [../history/macos/DATA_AUDIO.md](../history/macos/DATA_AUDIO.md).
Code: `src/platform/darwin/{settings,file}_darwin.cpp`, `src/platform/sdl/audio_sdl.cpp`,
tests in `tests/unit/platform_mac_data_test.cpp`.

## Where the engine looks

| What | Path on macOS | Notes |
|---|---|---|
| Saves | `~/Documents/Banished/Save/*.sav` | Same folder as the original game (`<Documents>\Banished\Save`), so saves are interchangeable. |
| Settings | `~/Documents/Banished/settings.cfg` | `IO::SettingsStore::DefaultPath`, the same on every platform. There is no legacy registry store on macOS (`ReadLegacySettings` is false). |
| Retail data and mods | `<install>/WinData` (`*.pkg`, `*.pkm`) | Read-only, never written. |
| Logs, remembered game folder | `~/Library/Application Support/OpenBA/` | See `README_MAC.md`. |

`Platform::DocumentsDir` returns `<home>/Documents` on Apple. `<home>` comes from the password database
(`getpwuid_r`), never from `$HOME`. The only redirect is `Opt().userProfile` (the `USERPROFILE` switch,
read once at boot). Every test run must point it at a throwaway folder: setting `HOME` changes nothing.

macOS may show a one-time "wants to access your Documents folder" prompt. APFS is case-insensitive by
default; `FindFirst` matches patterns case-insensitively, and the read primitives (`FileOpenRead`,
`FileOpenReadShared`, `PathAttributes`) retry with a per-component case-insensitive directory match on
`ENOENT`, so a case-sensitive volume also works. Spaces and non-ASCII characters in the home path work.
Temporary files that become saves are created 0644.

Save round trip: loading a save and re-saving it in the same process is deterministic (two re-saves are
byte-identical). A save written later from a loaded world is not a fixed point, because the world moved on.

## Audio

The backend is `audio_sdl.cpp` (SDL 3 on CoreAudio). SDL opens the default playback device at its own
format, converts and resamples each voice, and follows default-device changes. A missing or failing audio
driver gives a silent game without a crash (the log shows `[Audio] SDL_InitSubSystem ... failed`).
`SDL_AUDIODRIVER=dummy` (or `disk`) runs without a sound device. Volume settings reach the voices as linear
gain (`AudioMusicVolume` 100 / 50 / 25 give exact 2x steps; 0 is digital silence). An empty voice queue
plays exact zeros. Playback on real hardware (latency, glitches) has not been verified.

## Mods (.pkm)

Mods are read from `WinData` (`*.pkm`, `*.sub`). With mods enabled (no `-nomods`) the menu's Mods button
opens the mod dialog; enabling a mod writes `ModEnabled_*` / `ModIndex_*` to `settings.cfg`. Only
`settings.cfg` is written.

## Open

* Settings stay in `~/Documents/Banished`, not in `~/Library/Application Support/OpenBA`.
* Data of earlier portable builds (`run/UserData`) is not migrated.

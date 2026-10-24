# Third-party notices

OpenBA is GPL-3.0 (`LICENSE`). It contains or builds the components below. Everything here was
read from the tree; where the tree does not say, the entry says "verify".

| Component | Where | Version | Licence | How it is used |
|---|---|---|---|---|
| FFmpeg WMA v2 decoder (extract) | `third_party/wmadec` | FFmpeg `n8.1.3`, commit `1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7` | LGPL-2.1-or-later (`third_party/wmadec/COPYING.LGPLv2.1`; each FFmpeg file carries its notice) | Static library `wmadec`, linked **statically** into `OpenBA` (`target_link_libraries(OpenBA PRIVATE wmadec)`) and into the `unit` tests. See below. |
| SDL3 | `third_party/SDL` (git submodule) | tag `release-3.4.18`, commit `829a65d769d935c4852f8159e964312c0957260a` | zlib (`third_party/SDL/LICENSE.txt`) | Built from source, linked **statically** into the Windows and macOS executables; on Linux built shared (dxvk-native loads it). |
| FreeType | `third_party/freetype` (git submodule), build in `third_party/freetype_build` | `VER-2-13-3`, commit `42608f77f20749dd6ddc9e0536788eaad70ea4b5` | FreeType License (FTL), taken under the FTL per `third_party/freetype_build/README.banished.md` (upstream is dual-licensed FTL / GPL-2.0+) | Hand-picked sources built as static library `freetype_min`, linked **statically** into `OpenBA` and `unit`. Credit: "Portions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved." |
| Dear ImGui | `third_party/imgui` | `v1.92.9b`, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab` | MIT (`third_party/imgui/LICENSE.txt`) | Core + SDL3 and DX11 backends, unmodified. Compiled into the **debug flavour on Windows only** (`BANISHED_TWEAK_OVERLAY`); og / perf / release and Linux builds contain none of it. |
| Intel ASSAO | `src/render/dx11/ao/assao` (code) and `src/render/common/ao/shaders` (compiled shader headers); not under `third_party/` | GameTechDev/ASSAO commit `c21d57a95` | MIT (per `src/render/dx11/ao/assao/README.md`; Intel's header stays on every file) | Compiled into the exe for `ext.ambient_occlusion`. |
| dxvk-native | `third_party/dxvk-native` (fetched by `tools/fetch_dxvk_native.sh`, not committed) | 3.1.1 (sha256 pinned in its README) | Not recorded in the tree: verify (upstream doitsujin/dxvk) | Linux build only. Its headers are used at compile time (`-isystem`, by `src/render/dx11` only); its libraries are `dlopen`ed at run time and nothing is linked. Not part of the Windows exe, and not redistributed by this repository. |
| DXMT airconv | External `third_party/dxmt_src`, built by `tools/macos/setup_deps.sh` | `50102f994b406f58f56d5d09f2ad20f9b90390a2` | LGPL-2.1-or-later (`third_party/dxmt_src/LICENSE`, full text in `third_party/dxmt_src/COPYING.LIB`; `LICENSE.OLD` documents the superseded MIT releases) | macOS only: separate, replaceable `libairconv.dylib`, loaded with `dlopen`; no DXMT code linked into the executable. `src/render/metal/airconv_abi.h` declares the dylib's C ABI locally (it does not include DXMT headers). |
| LLVM | External `third_party/llvm15_arm64/source` | 15.0.7, `8dfdcc7b7bf66834a761bd8de445840ef68e4d1a` | Apache-2.0 with LLVM Exceptions (`llvm/LICENSE.TXT`) | macOS: static bitwriter/passes components inside `libairconv.dylib`. |

## macOS converter source and replacement

DXMT corresponding source is https://github.com/3Shain/dxmt at the commit above;
LLVM source is https://github.com/llvm/llvm-project at the commit above. The unmodified
sources and the build recipe in `third_party/airconv_build/` and `tools/macos/setup_deps.sh`
allow rebuilding the converter. Replace `Contents/Frameworks/libairconv.dylib`, or set
`BANISHED_AIRCONV=/path/to/libairconv.dylib` to load a compatible modified library.
The app includes these notices and the full licence texts in `Contents/Resources/licenses/`.
SDL is static in the macOS engine too. ImGui is absent from macOS builds.
The macOS Metal ambient-occlusion implementation derives from Intel ASSAO (MIT);
its licence text is `src/render/dx11/ao/assao/LICENSE`. FreeType is used under the FTL, not its alternative GPL licence.
Developer ID signing with hardened runtime requires the entitlement
`com.apple.security.cs.disable-library-validation` to permit user-replaced converters.
The statically linked wmadec decoder remains subject to the relinking terms below.

## FFmpeg / wmadec and LGPL-2.1 section 6

`OpenBA.exe` uses the FFmpeg WMA v2 decoder (libavcodec `wmadec.c` and the files it needs,
verbatim from tag `n8.1.3`, plus glue and shim files written for this project and offered under
the same licence; see `third_party/wmadec/EXTRACT.md`). That library is covered by the GNU
Lesser General Public License, version 2.1 or (at your option) any later version. The licence
text is `third_party/wmadec/COPYING.LGPLv2.1`.

The library is linked statically. The complete corresponding source of OpenBA, including the
library and the build system, is public at https://github.com/fran-cap/OpenBA . To use a
modified version of the library, change or replace the files under `third_party/wmadec/` and
rebuild (`tools/build.sh build`, or the CMake target `OpenBA`): this rebuilds `libwmadec.a`
and relinks the exe.

## Not third-party code in the tree

The FrancophilSans typeface (Manfred Klein) is kept unchanged under `tools/release/fonts/` with his
notice (`FrancophilSans_README.txt`) and ships beside the exe in the release packages. It is not
part of OpenBA's code or of its GPL-3.0 licence; its terms are in `README.md`.

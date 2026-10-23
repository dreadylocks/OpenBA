# AOSPIKE -- the ambient-occlusion spike: the go / no-go facts (hand-in, 2026-10-14)

Block AOSPIKE of `docs/extensions/AO.md` §4. Branch `worktree-agent-ae3a2569366fb35dd` (from main
`8af6953e2`), commits `50f6b77f6..7c1af63e9`, all prefixed `spike:`. **Nothing here merges** except this
file, AO.md §6 and the blob tool (`tools/ao_shaders.py`). The experiment switches are compile-time
constants in the branch (`src/render/dx11/ao/ao_spike.h` `kMode` / `kDebugLayer`,
`src/ext/ao_spike_ui.h` `kTabMode`); no env flag, no registry row, no `getenv`.

**Verdict: GO on all eight.** One finding changes the plan (S7: the main-menu scene passes the
"main entry" test), one the menu design (S5: a loose file never overrides a packaged one).

Machine / runs: Windows 10, feature level 11_0 granted, window 1024x640, seed 12345 for the S3/S4
runs. Mods were already parked (no `*.pkm` in `WinData/`). `settings.cfg` was backed up and restored
byte-identical (diff empty) after the S3/S4/S6 runs; the stress pair ran at the user's own
`VideoMultisample=3` (4x). The coordinator's game dir now holds a debug build of this branch with
every spike switch at 0 (behaves as main + a null seam); the spike's loose `Dialog/Options_*`,
`OptionsExt_*` and `AoOptions_*` `.crs` files were removed from `WinData/Dialog/`.

---

## S1 -- does ASSAO.hlsl compile offline with fxc for everything ASSAODX11.cpp asks? **YES**

**Answer.** All 22 entry points compile with fxc 10.1 (`10.0.19041.0`), the exact defines and flags
`ASSAODX11.cpp` `InitializeDX` passes to `D3DCompile`, with `/WX` (warnings are errors, as upstream):
0 failures, 0 warnings, 99,500 bytes of DXBC.

**The matrix** (one define set for every entry; ASSAO compiles each entry once):

| defines (from `ASSAODX11.cpp` `shaderMacros`) | flags (from `shaderFlags`, release) |
|---|---|
| `SSAO_MAX_TAPS=32` `SSAO_MAX_REF_TAPS=512` `SSAO_ADAPTIVE_TAP_BASE_COUNT=5` `SSAO_ADAPTIVE_TAP_FLEXIBLE_COUNT=(32-5)` `SSAO_DEPTH_MIP_LEVELS=4` `SSAO_ENABLE_NORMAL_WORLD_TO_VIEW_CONVERSION=0` | `/Ges` (ENABLE_STRICTNESS) `/O3` `/WX` |

| entry | profile | bytes | | entry | profile | bytes |
|---|---|---|---|---|---|---|
| VSMain | vs_5_0 | 608 | | PSGenerateQ3 * | ps_5_0 | 9552 |
| PSPrepareDepths | ps_5_0 | 2589 | | PSGenerateQ3Base * | ps_5_0 | 5599 |
| PSPrepareDepthsAndNormals | ps_5_0 | 10333 | | PSSmartBlur | ps_5_0 | 2985 |
| PSPrepareDepthsHalf | ps_5_0 | 2287 | | PSSmartBlurWide | ps_5_0 | 3687 |
| PSPrepareDepthsAndNormalsHalf | ps_5_0 | 6696 | | PSNonSmartBlur | ps_5_0 | 2527 |
| PSPrepareDepthMip1 / 2 / 3 | ps_5_0 | 5311 each | | PSApply | ps_5_0 | 3737 |
| PSGenerateQ0 | ps_5_0 | 5389 | | PSNonSmartApply | ps_5_0 | 1152 |
| PSGenerateQ1 | ps_5_0 | 7911 | | PSNonSmartHalfApply | ps_5_0 | 912 |
| PSGenerateQ2 | ps_5_0 | 8666 | | PSGenerateImportanceMap * | ps_5_0 | 3773 |
| | | | | PSPostprocessImportanceMapA / B * | ps_5_0 | 2419 / 2745 |

`*` = only under `INTEL_SSAO_ENABLE_ADAPTIVE_QUALITY` (ASSAO.h:29 defines it; quality level 3,
"Adaptive"). The plan's tiers (AO.md §3: Low / Medium / High = ASSAO `QualityLevel` 0 / 1 / 2,
Lowest = -1) never need them; the
next block may undefine it and drop 5 blobs and the load-counter UAV (ASSAO.h's own comment: "makes
it easier to port").

**Evidence.** `python -I tools/ao_shaders.py` (output above; `--check` compiles into a temp dir and
writes nothing). It also compiles our own `src/render/dx11/ao/ao_depth_resolve.hlsl` (4 entries,
3,298 bytes). Headers land in `src/render/common/ao/shaders/<entry>.h` (`fxc /Fh`, array
`g_ao_<entry>`).

**Changed.** New `tools/ao_shaders.py` (the one place the matrix lives). Python with `subprocess`
rather than a `.sh`: the fxc path has spaces and parentheses.

**Next block.** Keep `tools/ao_shaders.py`; commit the generated headers (they are inputs to the
build, the exe has no compiler). Note `ps_5_0` needs feature level 11_0; the engine accepts 10_0
(`devicedata.cpp` CreateDevice asks {11_0, 10_1, 10_0}). Either gate the extension on FL >= 11_0
(simplest; log and keep the row off) or add a `ps_4_1` set (MSAA depth SRV reads need 10.1 anyway).

## S2 -- does ASSAODX11.cpp + ASSAO.h compile under GCC with the blobs? **YES, small diff**

**Answer.** Yes, with the project's flags (`-std=c++17 -fexceptions -fno-strict-aliasing
-fno-lifetime-dse -Wall -Wextra -Wno-cast-function-type -Wno-invalid-offsetof`) at `-O0 -g -D_DEBUG`
and `-O2 -DNDEBUG`, standalone and inside the real CMake build (the `src/render/dx11/*.cpp` glob
picks the files up: the debug and perf exes of this branch link with it). The object needs nothing
from d3d11.lib or d3dcompiler (it only calls through the device's vtable); unresolved symbols after
the port = CRT (`sinf`, `cosf`) and our own compat file.

**Every edit against upstream** (`git diff 50f6b77f6 HEAD -- src/render/dx11/ao/assao/`: 2 files
edited +31/-26, 2 files new; every edit marked `GCC PORT (AOSPIKE)`):

1. `ASSAODX11.cpp`: `#include <d3dcompiler.h>` and both `#pragma comment(lib, ...)` removed (d3d11 is
   resolved at run time by the backend, `platform_gfx.h`); `<string.h>` and `assao_gcc_compat.h`
   included.
2. `ASSAODX11.cpp` `CompileShader`: `D3DCompile` + `MessageBoxA` replaced by a lookup of the entry
   name in the blob table (`assao_gcc::FindBlob`); `ID3DBlob*` became a two-field `AssaoBlob`
   (static data, never released); the four `GetBufferPointer/GetBufferSize` uses and the
   `SAFE_RELEASE(shaderBlob)` calls follow. The `CreateVertexShaderAndIL` / `CreatePixelShader`
   signatures are unchanged, so `InitializeDX` is untouched except:
3. `ASSAODX11.cpp` `InitializeDX`: `shaderFlags` = 0 with the `D3DCOMPILE_*` lines under `#if 0`
   (the flags are baked by the tool).
4. `ASSAODX11.cpp`: an **upstream bug**: `memset( m_RTVs, sizeof( m_RTVs ), 0 )` (size and value
   swapped, so it zeroes nothing; harmless because `OMGetRenderTargets` fills the array). Fixed;
   GCC's `-Wmemset-transposed-args` found it.
5. `ASSAO.h`: `#include <stddef.h>` (MSVC brings `size_t` implicitly).
6. New `assao_gcc_compat.h`: MinGW-w64's `d3d11.h` lacks `CD3D11_VIEWPORT`, `CD3D11_RECT`,
   `CD3D11_TEXTURE1D_DESC`, `CD3D11_UNORDERED_ACCESS_VIEW_DESC` (only the constructors ASSAO calls are
   written, MSVC semantics).
7. New `assao_gcc_compat.cpp`: MinGW **declares but never defines**
   `CD3D11_RENDER_TARGET_VIEW_DESC(ID3D11Texture2D*, ...)` and the two
   `CD3D11_SHADER_RESOURCE_VIEW_DESC(ID3D11Texture1D*/2D*, ...)` constructors (link error otherwise);
   bodies written after the Windows SDK's. Plus the 22-entry blob table.

Warnings left (not fixed, all upstream style): 12 `-Wsign-compare`, 2 unused locals
(`lastHalfDepthMipX/Y`), 1 unused function under `NDEBUG` (`GetTextureDimsFromSRV`, assert-only).
`#pragma pack(push, 8)` in ASSAO.h is honoured by GCC (no change needed). No other MSVC-only
construct was hit.

**Not checked.** The Linux build (dxvk-native headers, `-isystem`): the compat structs are guarded
`#ifndef _MSC_VER`, but dxvk-native's `d3d11.h` may already define some of them; the next block
builds `OpenBA` under WSL once.

**Next block.** Take the four files as they are; `CreateInstance` is never called yet -- the first
call is AOCORE's. MIT header kept on all three Intel files, LICENSE copied beside them.

## S3 -- can the main depth be shader-readable under MSAA without touching the faithful path? **YES**

**Answer.** Yes. Under the switch, `CreateDepthBuffer` asks `CreateAttachment` for
`R24G8_TYPELESS` with `DEPTH_STENCIL | SHADER_RESOURCE` instead of typed `D24_UNORM_S8_UINT` /
`DEPTH_STENCIL`; `ImageBufferData::AttachDepthBuffer` then creates the DSV with an explicit
`D24_UNORM_S8_UINT` / `TEXTURE2DMS` (or `TEXTURE2D` at 1x) desc instead of the null desc (a null
desc on a typeless resource is illegal), plus an SRV `R24_UNORM_X8_TYPELESS` / `TEXTURE2DMS`. With
the switch off both functions run their retail bodies unchanged. Works at 1x, 2x and 4x. The D3D11
debug layer reports **zero** messages from the depth-view creation and **zero** from the AO pass
over 2,400 frames at 2x and at 1x; everything it reports is the faithful frame's own, pre-existing
warnings (below).

**Evidence** (`standalone_error.log`, debug exe, `kMode 2`, `kDebugLayer` on):
```
[AO] spike mode=2 device flags=0x2 featureLevel=0xb000
[AO] D3D11 debug layer ON (info queue 000002748e3ed0b0)
[AO] main depth 1024x640 fmt=44 samples=2 bind=0x48: DSV(fmt 45, dim 5)=0x00000000 SRV(fmt 46, dim 6)=0x00000000
[AO] main depth 1024x640 fmt=44 samples=1 bind=0x48: DSV(fmt 45, dim 3)=0x00000000 SRV(fmt 46, dim 4)=0x00000000   (1x run)
[AO] main depth 1024x640 fmt=44 samples=4 bind=0x48: DSV(fmt 45, dim 5)=0x00000000 SRV(fmt 46, dim 6)=0x00000000   (perf exe, 4x, S8 run)
[AO] debug layer summary frame 2400: 336047 message(s) total, 0 from the AO pass; id351(sev2)x333643 id353(sev2)x2399 id391(sev2)x5
```
(fmt 44 = R24G8_TYPELESS, 45 = D24_UNORM_S8_UINT, 46 = R24_UNORM_X8_TYPELESS; dim 5/6 = 2DMS.)
The attribution: the info queue is drained immediately before the pass and again right after it;
the counter only counts what arrived in between.

**The faithful frame's own debug-layer output** (a by-product, not AO's; all severity 2 = WARNING,
no ERROR / CORRUPTION in either run):
- id 351, ~140 per frame: `DrawIndexed[Instanced]: The size of the Constant Buffer at slot 3 of the
  Vertex Shader unit is too small (64..1216 bytes provided, 3328 bytes, at least, expected)` -- the
  bone / instance buffer is bound smaller than the shader's declared array; out-of-bounds reads
  return 0, retail's own behaviour.
- id 353 (0x300161), once per frame: `DrawIndexed: The Pixel Shader expects a Render Target View
  bound to slot 0, but none is bound` -- a depth-only pass (shadow map).
- id 391, 5 at load: `CreateInputLayout: ... 'TEXCOORD'/2,3,5 ... int32 ... R8G8B8A8_UINT` (the
  skinning layout's bone indices).

**Changed.** `devicedata.cpp` (`CreateDepthBuffer`, and `D3D11_CREATE_DEVICE_DEBUG` +
`AoSpike::OnDeviceCreated` in `CreateDevice`), `imagebufferdata.cpp` (`AttachDepthBuffer`), new
`src/render/dx11/ao/ao_spike.{h,cpp}`.

**Next block.** Use the readable main depth (no copy). The depth is created at device creation and
on resize / `settingsChanged`, so a LIVE toggle (class A) needs either (a) the depth always created
readable while the row is on at boot and re-created through the existing rebuild path when the row
flips, or (b) the format chosen at every rebuild from the ext's state (one bool read per rebuild, not
per frame). The faithful arm must stay the literal retail call. Under RenderDoc, the info-queue QI
failed in one of the two captures (RenderDoc wraps the device): read the debug layer from plain
runs.

## S4 -- does the Texture2DMS resolve give correct linear depth at 2x and 1x? Which projection? **YES**

**Answer.** Yes, at both. `RtVideoDeviceUsesZeroToOneDepth()` is **true** on DX11 (DeviceData slot 7
= retail `mov al,1; ret`; logged `zeroToOne=1`). The main camera has `_zUp=1`, perspective, and its
`_projection` (row-vector convention, `v * M`) is

```
[1.60033 0       0        0]      row 0: x        -> clip x
[0       0       1.00392  1]      row 1: y (FORWARD, Z-up camera) -> clip z and clip w
[0       2.74748 0        0]      row 2: z (UP)   -> clip y
[0       0      -2.00784  0]      row 3: translation
```
i.e. retail's right-handed 0..1 projection (`Frustum::Projection`, m10 = -f/(f-n),
m14 = -nf/(f-n)) post-multiplied by `kZUpToYUp`: in the camera's own Z-up space (x right, **y
forward**, z up) it behaves as a **left-handed** projection, `w = +forward distance`, depth 0 at
near, 1 at far. n = 2, f = 512 (computed back from the matrix: near=2 far=511.992). 2.74748 =
1/tan(20 deg), so fovY = 40 as AO.md says. (Aside: m00 = 1.60033 gives a projection aspect of 1.717
on a 1024x640 = 1.6 client; the frame is consistent with it, so the projection's aspect does not
come from the window client size. Nothing for AO to do; noted.)

**The linearisation that produced a correct image** (with `wRow` = the row whose column 3 is
non-zero, here row 1):
```
viewDepth = mul / (add - z),   mul = -M[3][2] = n*f/(f-n) = 2.00784,
                               add =  M[wRow][2] / M[wRow][3] = f/(f-n) = 1.00392
z == 1.0 (cleared, never written) -> a far sentinel
```
This is ASSAO's own `ScreenSpaceToViewSpaceDepth` (`DepthUnpackConsts`) -- BUT ASSAO computes
`mul`/`add` from `proj.m[3][2]` / `proj.m[2][2]` and the FOV from `m[0][0]` / `m[1][1]`. With our
Z-up matrix `m[2][2] = 0` and `m[1][1] = 0`, so **ASSAO must be handed the projection with rows 1
and 2 swapped** (`[1.6 0 0 0 | 0 2.747 0 0 | 0 0 1.00392 1 | 0 0 -2.00784 0]`, a standard LH
D3D perspective) with `MatricesRowMajorOrder = true`. Then its own code yields mul = 2.00784,
add = 1.00392 (same signs: no handedness flip), tanHalfFOV = (1/1.6, 1/2.747).

**Evidence.** Two RenderDoc captures (`captures/aospike_ms2.rdc`, `captures/aospike_ms1.rdc`; not
committed, `captures/` is ignored), frame ~574 in game, our draw at event 1116 (2x, PS = the MS
resolve) / 1113 (1x, PS = the 1x resolve), output the R32F target (ResourceId::1852, 1024x640). Saved
as `captures/aospike_ms{2,1}_lineardepth.dds` and normalised PNGs (`..._lineardepth.png`), beside the
colour target (`captures/aospike_ms2_colour.png`). The depth image shows the town correctly (near
roofs and trees dark, the ground grading to the far tree line), and the two sample counts agree:

| | min | max | (512,320) centre | (512,80) top | (512,560) bottom |
|---|---|---|---|---|---|
| 2x | 9.3267 | 51.1238 | 24.7015 | 36.9164 | 17.1382 |
| 1x | 9.3244 | 51.1656 | 24.6805 | 36.8992 | 17.1339 |

(world units; no NaN, no negative, no inf; the default camera sees no sky, so the sentinel never
appears in this frame).

**Changed.** `src/render/dx11/ao/ao_depth_resolve.hlsl` (`PSResolveDepthMS` reads sample 0,
`PSResolveDepth1x`, `VSFullscreen` via `SV_VertexID`), the hook in `ao_spike.cpp`: it saves RTVs /
DSV / viewports / shaders / IL / topology / RS / DSS / blend / PS SRV0 / PS CB0, unbinds the DSV,
draws, and restores everything. No engine dirty bit had to be raised: restoring the D3D state exactly
was enough (the engine's next draws, the Blended list, rendered normally in both captures).

**Next block.** ASSAO's `PSPrepareDepths*` already linearise from a raw `Texture2D<float>` depth, so
our resolve only has to turn the MSAA depth into a **1x raw-depth** R32F (sample 0, or min) and let
ASSAO linearise with the swapped matrix -- or keep our linear target and hand ASSAO `mul = 1,
add = 0`-equivalent constants. The first is less code. At 1x ASSAO can read the main depth's SRV
directly (`R24_UNORM_X8`, which `ASSAO_InputsDX11::DepthSRV` explicitly supports; it rejects MSAA).

## S5 -- does a loose `WinData/Dialog/Options_*.crs` override the packaged object? **NO**

**Answer.** No, by construction. `IO::FileSystem::OpenFile` (retail 0x1800ac880,
`src/engine/io/filesystem_openfile.cpp:337`) looks the name up in the **packfile index first** and
opens the loose file only when no package has it (`if (!emitted) { ... PlatformOpenFile }`).
`Dialog/Options_*.crs` are all in `data*.pkg`, so a loose copy is never read. (DESIGN.md §1's
"load order: loose files, .pkg, then .pkm" is therefore wrong for names the packages hold: loose
wins only for NEW names. A `.pkm` mod can override, through the pack stack order, but that is a mod,
not our mechanism.)

**Evidence.** `src/ext/ui/Dialog/Options.rsc` (a copy of the kit's with `labelTitle._text` changed
from `"Options"` to `"Graphics"`, which the game's table renders "Video") compiled cleanly through
`tools/ext_resources.sh` (108 objects, 0 problems) and 89 `Dialog/Options_*.crs` were deployed loose;
the dialog opened by the menu driver (`BANISHED_AUTOMENU_BUTTON=4`) still reads **"Options"**:
`captures/aospike_s5_override.png`.

**Next block.** Never ship a retail-named object loose; give every new object a new name.

## S6 -- can the extension add a fifth Options tab? **YES, two ways; runtime AddTab is cheapest**

**Answer.** Both work, verified on screen, with the retail `OptionsDialog` class unchanged:

- **(a) runtime `UI::TabElement::AddTab`** (cheapest): in `OptionsDialog::OnOpened`, after the
  dialog finds `tabOptions`, load two NEW objects `Dialog/AoOptions.rsc:buttonEnhanced` and
  `:enhancedOptions` through `ExternalFactory::FindOrLoadAs<UI::ElementDescription>` and call
  `tabOptions->AddTab(button, content)`. `AddTab` (retail 0x1800da130) is ported and retail itself
  uses it at run time (`toolui.cpp:155`, `UIDescription::AddControllers`, the town-hall tabs). One
  small `.rsc` with only new names (a string table, a button, a label, the page); no copy of
  retail's 685-line `Options.rsc`. Log: `AddTab: button=... content=... tabCount before=4` ->
  `page=... tabCount after=5`; screenshot `captures/aospike_s6a_addtab.png` (tab "Enhanced" with
  the Video icon, page text shown, `SetActiveTab(4) -> active=4`).
- **(b) a renamed five-tab copy** `Dialog/OptionsExt.rsc` (the kit's Options.rsc + a fifth `Tab`
  pointing at `AoOptions.rsc:buttonEnhanced` / `:enhancedOptions`), loaded by
  `MainMenuDialog::OpenOptions` instead of `Dialog/Options.rsc`. Works
  (`[LoadDialogResource] 'Dialog/OptionsExt.rsc' ... IsDerivedFrom=1`, `tabs=5`,
  `captures/aospike_s6b_optionsext.png`), but it forks the whole dialog resource, and every OTHER
  opener (the in-game pause menu's Options) must be switched too.
- override: impossible (S5). Own dialog: not needed.

**`lastOption` / `SetActiveTab`.** `SetActiveTab(4)` works. `lastOption` is written from
`tabOptions->_activeTab` on OK / Apply (`optionsdialog.cpp:789`, `:1027`), so 4 round-trips through
`settings.cfg` (`GameLastOption`). On reopen it is **clamped** by
`if (lastTab < 0 || lastTab > 3) lastTab = 0` (`optionsdialog.cpp:710`, comment "options dialog has
4 tabs"): with `GameLastOption=4` the log reads `tabs=5 lastOption read=4 -> SetActiveTab(0)`. So the
fifth tab is not restored. Not checked whether that clamp is retail's or the port's (it sits under
`s_enableTabRestore`); AOMENU must look it up in Ghidra before touching it. Note the order: retail's
`SetActiveTab(lastTab)` runs BEFORE our AddTab in (a); with the ext the tab must be added first,
then the restore run against `_tabCount - 1`.

**Changed.** `src/ext/ui/Dialog/AoOptions.rsc`, `src/ext/ui/Dialog/OptionsExt.rsc`,
`tools/ext_resources.sh` (3 TARGETS, the deploy glob), `optionsdialog.cpp` (the AddTab arm + log),
`mainmenudialog.cpp` (the (b) arm), `src/ext/ao_spike_ui.h`.

**Next block (AOMENU).** Take (a). The AddTab arm lives at the end of `OnOpened` (one `if` on the
extension row, a call into `src/ext/ambient_occlusion/`), then the restore. The page's combo and
slider are found by name inside the added page (`FindElement` on the page `InternalPtr` AddTab
returns), and Apply / OK must route through the ext's own apply (retail's `ApplyVideoSettings` does
not know them). The tab button's sprite: the spike borrowed `OptionsGraphics` from
`SpriteSheet.rsc`; a new icon goes in `ExtensionsSprites`.

## S7 -- where does Sky sort, what depth does it leave, is list 2 used? And the hook position

**Answer.**
- **Sky is item 0 of list 0** in every logged frame (`sortKey 0`), material
  `Material\Sky\SkyMaterial.rsc`. Its render state `rs` (kit
  `ToolKit/resource/Material/Sky/SkyMaterial.rsc`) has `Depth { _enable = false; _write = false; }`
  and `MaterialType _type = Opaque`; its `depth` pass is empty. So it draws first, writes no depth,
  and sky pixels keep the per-entry `Clear(6, ..., 1.0f, 0)` value: **depth = 1.0 exactly** (the far
  plane). With the linearisation above that is 2.00784 / 0.00392 = 512 = far; ASSAO skips it if
  `FadeOutTo` < 512 (no special case needed), our resolve maps `z >= 1` to a sentinel.
- **List 2 (Effect) is always empty**: `[AO] lists: 0=1585 1=64 2=0` (main menu scene),
  `0=740 1=96 2=0` (in game, frame 1), `0=762 1=24 2=0` (frames 600..2400), at 2x and at 1x.
- Item order samples: list 0 = Sky, then Terrain chunks..., last a `BillboardEvergreenMaterial`;
  list 1 starts with `TerrainDecalMaterial`.

**FINDING -- the main-menu scene passes the main-entry test.** The spike's test
(`entry._renderBuffer == context._defaultRenderBuffer`; it also has `_shader == 0`) fired for the
**main menu's background town** from the first menu frame (stderr lines 91-123 of the 2x run come
before `[AutoStart] menu settled`). The reflection entry is distinguishable (`shader=2`, its own
render buffer, group 2). So AO.md §2's rule "target == defaultRenderBuffer, shader 0 -- and nowhere
else (not the main menu)" cannot be implemented with that test alone. Options for AOCORE: arm the
seam per frame from `GameUpdateLoop::Draw` (and disarm after), or accept AO on the menu backdrop
(arguably nice; the user's call). The minimap paint was not seen at the seam in the first 12 entries
logged.

**Evidence.** The `[AO] lists`, `[AO] sky in list 0`, `[AO] entry` lines in the runs' stderr (kept
in the spike's scratch, quoted above).

## S8 -- what does an EMPTY hook cost on the stress town? **Nothing measurable**

**Answer.** The seam + readable depth + a pass that binds our R32F RTV and the depth SRV, unbinds
them and restores the frame's targets (no draw), against the same branch with the seam null:

| metric | faithful (`kMode 0`) | empty hook (`kMode 1`) | delta |
|---|---|---|---|
| sim ms / iteration | 0.15 | 0.15 | +1.3% |
| update ms / frame | 1.49 | 1.51 | +1.4% |
| draw ms / frame | 0.89 | 0.90 | +0.9% |
| frame ms / fps | 7.16 / 140 | 7.16 / 140 | 0 |
| private MB (end) | 537 | 540 | +0.6% |

`tools/stress_report.py <empty>.csv --baseline <faithful>.csv`: 0 regressions over threshold, PLAN 5%
gate PASS. The sim moved +1.3% with a change that cannot touch the sim, so the noise floor of one
pair is ~1-2%: the draw delta is inside it. Results: `perf/results/20261014_151329_aospike_faithful/`,
`perf/results/20261014_151546_aospike_emptyhook/` (worktree; exe md5 05da5995... / a44289de...,
verified before and after each run by the harness).

**Machine load: CLEAN** for both legs (the harness's sampler: compiler 0, make 0, other game 0 in all
20 samples; no `cc1plus` alive before or after). **Caveat:** fps sat at exactly 140 in both legs:
the RTSS limiter (memory `reference_rtss_caps_fps_at_140`) was evidently running; I did not stop the
user's tools. So the GPU cost of the pass is invisible here; the CPU-side `draw ms` is the number.
MSAA was the user's 4x.

**Next block.** Repeat the pair with the real pass (AOCORE) and with RTSS stopped if the GPU cost is
wanted.

---

## Summary for AO.md §6

- 6.1 depth input: **readable main depth** (R24G8_TYPELESS + DS|SRV, explicit D24S8 DSV, R24_UNORM_X8
  SRV; MSAA 2DMS). No copy. Live toggle needs the rebuild path (S3).
- 6.2 linearisation: `viewDepth = (n f/(f-n)) / (f/(f-n) - z)`, n=2 f=512 (from the matrix: mul =
  -M[3][2], add = M[wRow][2]/M[wRow][3]); zeroToOne = true; the projection is RH-0..1 x Z-up swap =
  LH in the Z-up camera; ASSAO gets `_projection` with rows 1 and 2 swapped, row-major.
- 6.3 menu shape: **fifth tab via runtime `TabElement::AddTab`** from `OptionsDialog::OnOpened`,
  resources under new names only; fix the lastOption clamp (after checking retail).
- 6.4 blob tool: `tools/ao_shaders.py`, 1 VS + 21 PS, one define set (above); drop the 5 adaptive
  entries if Adaptive is not offered.
- New for AOCORE: the main-menu scene passes the main-entry test (S7).

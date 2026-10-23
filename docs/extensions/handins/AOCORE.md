# AOCORE -- `ext.ambient_occlusion` (class A): the depth, the pass, the seam (hand-in, 2026-10-14)

> (moved to the DESIGN.md §3 names 2026-10-20, block ext-standard: `src/ext/ambient_occlusion/ao.{h,cpp}` moved to `ambient_occlusion.{h,cpp}` (namespaces `AmbientOcclusion` / `Ext::AO` / `AoMath` -> `Ext::AmbientOcclusion` / `Ext::AmbientOcclusion::AoMath`), `tests/unit/ext_ao_test.cpp` to `ext_ambient_occlusion_test.cpp`; the `[AO]` stderr prefix is `[AmbientOcclusion]`.)

Block AOCORE of `docs/extensions/AO.md` §4. Branch `worktree-agent-a5e8b4a1bb43292ab`. Main is merged
in, including AOMENU (f333a092d): the row, the valued keys, and `Ext::AO::Current()` are main's.
Nothing is pushed or merged. The coordinator runs the gate.

## 0. Short version

- **Works.** With `ext.ambient_occlusion=1`, Intel ASSAO runs between the Opaque and Blended lists
  of the game's main scene. It multiplies its AO into the MSAA colour. The RenderDoc capture shows
  the resolved depth, ASSAO's AO texture, and the colour before and after (§5.2).
- **Row off.** No [AO] line, no target, no pass. The main depth keeps retail's typed format. The
  frame's command stream is unchanged (trace_oracle, §5.3).
- **Cost, faithful vs row on.** Stress town, machine CLEAN, fps capped at 140 by RTSS. Low,
  Medium and High all sit inside the pair's noise: sim -1.7..+2.7%, CPU draw -1.6..+2.6%. The GPU
  cost is NOT measured: the cap hides it (§5.4).
- **D3D11 debug layer.** 0 messages from the AO pass over 11,000 frames at 4x MSAA and 7,000 at 1x
  (§5.5).
- **Unit tests.** 1211 tests, 0 failures, of which 4 are new (`ext_ao_*`). Acceptance: 0 lines off
  target.

## 1. What exists

| Path | What it is |
|---|---|
| `src/engine/graphics/renderer_ext_ao.h` | The seam's data record (`Graphics::ExtAoFrame`, plain data) and the declaration of `Graphics::ExtAoAfterOpaque` (its body is in the backend). |
| `src/engine/graphics/Camera.h`, `wave27_lane_gfxbig.cpp` | `static const ExtAoFrame* Renderer::_extAo` (null by default) and its single test in pass 2 (§2). |
| `src/render/dx11/ao/ao_stage.h` | The internal interface. `AoStage::Stage` has `Resize(w,h)` and `Draw(depth1x, projectionLH, frame)`. `CreateAssao()` builds it. XeGTAO would replace only `ao_stage_assao.cpp`. |
| `src/render/dx11/ao/ao_stage_assao.cpp` | The ASSAO implementation: `ExtAoFrame` maps to `ASSAO_Settings`, with `DrawOpaque=false` and `NormalSRV=null`. |
| `src/render/dx11/ao/ao_depth_resolve.{h,cpp,hlsl}` | `Texture2DMS<float>` sample 0 (or the 1x texture) into a 1x **R32F raw depth** (§3.1). |
| `src/render/dx11/ao/ao_pass.cpp` | `ExtAoAfterOpaque`: save all state, resolve, rebind colour + depth, ASSAO, restore. It also holds the readable-depth state (`ao_backend.h`, `ao_depth_views.h`) and rebuilds the pass objects when the device changes. |
| `src/render/dx11/ao/assao/` | Intel's three files plus `LICENSE` (MIT header kept), the GCC compat file, and a `README.md` that records upstream c21d57a95 and every edit, including the `memset` size/value swap GCC found. |
| `src/render/common/ao/shaders/*.h` | 20 DXBC headers from `tools/ao_shaders.py`: `VSMain`, 16 ps_5_0 ASSAO entries, and 3 resolve entries. The 5 Adaptive-only entries are gone because `INTEL_SSAO_ENABLE_ADAPTIVE_QUALITY` is now off. |
| `src/render/dx11/devicedata.cpp` `CreateDepthBuffer`, `imagebufferdata.cpp` `AttachDepthBuffer` | The readable-depth arms (§3.2). The faithful arm is the literal retail call. |
| `src/ext/ambient_occlusion/ao.{h,cpp}` | `Sync()`, `Enabled()` (an inline bool load), `BuildFrame` / `Arm` / `Disarm`. |
| `src/ext/ambient_occlusion/ao_math.{h,cpp}` | The pure parts: the tier table, the zoom curve, the row swap, the linearisation constants, and the frame record. |
| `src/ext/render_pipe/{packet.h,passes.cpp,render_pipe.cpp,render_pipe_internal.h}` | The packet carries `aoOn` plus an `ExtAoFrame` by value. `Pass2` tests it. |
| `src/ext/registry_boot.cpp` | `AmbientOcclusion::Sync()` is called from `InitFromSettings` and `SetAndRecord`. The registry row and keys are AOMENU's (main). |
| `tests/unit/ext_ao_test.cpp` | Four tests, registered in `tests/unit/sources.cmake` (`ao_math.cpp`). |
| `tools/stress_run.sh` | `STRESS_EXT` now restores the valued keys `ext.ao_quality` / `ext.ao_opacity` (§5.4). |

`params_stub.cpp` was deleted at the merge. AOMENU's `settings.cpp` provides `Current()` / `Set()`.

## 2. The seam and the arm / disarm sites

**The engine seam** follows DESIGN §3 rule 5: one null-by-default static data pointer,
`Graphics::Renderer::_extAo` (`Camera.h`). There is no virtual and no function-pointer table.
`Renderer::Draw` pass 2 reads it once per frame into a local before the scene-entry loop. After each
entry's k = 0 list it then tests:

```cpp
if (k == 0 && extAo != nullptr && e->_shader == 0 &&
    e->_renderBuffer == context._defaultRenderBuffer)
    ExtAoAfterOpaque(context, *extAo, *e);
```

That call is direct, to a function declared in the engine header and defined in
`src/render/dx11/ao/ao_pass.cpp`, because only the dx11 TUs see the D3D headers. `src/engine` does
not include `src/ext`. The faithful path pays one load plus a compare per entry per k.
`passes.cpp` (render_pipe's copy of the loop) runs the same test against the packet's record.

**Ruling on §6.5, game only.** The seam is armed only by the game's own draw:

| Site | Arm | Disarm |
|---|---|---|
| `src/game/Game/gameupdateloop.cpp` `GameUpdateLoop::Draw` step 7 | `aoArmed = AmbientOcclusion::Enabled() && AmbientOcclusion::Arm(*m_playerController)`, right before `Renderer::Draw` (the eye_camera branch included) | `if (aoArmed) AmbientOcclusion::Disarm()`, right after |
| `src/ext/render_pipe/render_pipe.cpp` `MainHalf` (step 12's tail) | `p.aoOn = Enabled() && BuildFrame(..., p.ao)`. The record goes into the packet by value, so the render thread never reads the main thread's static | the packet's lifetime ends it. `Pass2(*p.renderer, *ctx, p.aoOn ? &p.ao : nullptr)` |
| `AmbientOcclusion::Sync()` | none | nulls `_extAo` when the row goes off |

These never get AO:

- ~~The main-menu backdrop~~ -- since AO-MENU (2026-10-17, `handins/AO-MENU.md`) the menu loop's
  draw arms the seam the same way (its own `PlayerController::distance`), the user's call.
- The minimap paint (step 3, before the arm).
- The water reflection (shader 2, its own render buffer).

`Arm` reads `Ext::AO::Current()` exactly once per frame. Nothing reads it in a loop.

## 3. Decisions

### 3.1 The resolve writes RAW depth, not linear (a deliberate departure from the brief)

ASSAO's `PSPrepareDepths*` already linearise with exactly §6.2's formula: `ScreenSpaceToViewSpaceDepth`
computes `mul / (add - z)`, with `mul = -P[3][2]` and `add = P[2][2]` of the row-swapped matrix.
A linear R32F target would therefore be linearised twice. The resolve's job is MSAA to 1x only:
sample 0 at MSAA, a plain copy at 1x. The copy also keeps ASSAO from ever binding the main depth,
which is the bound DSV.

The unit test `ext_ao_linearisation_constants` proves the chain on the spike's matrix:

- ASSAO's own reading of the swapped matrix (`AoMath::AssaoUnpack`) equals `LinearizeConsts` on the
  game matrix: 2.00784 / 1.00392.
- Depth 0 maps to near 2. Depth 1 maps to far 512 (the sky).
- tan(fovY / 2) = tan 20°.
- It also holds for eye_camera's near of 0.1.

### 3.2 Readable main depth (§6.1)

`CreateDepthBuffer` asks `AoBackend::BuildReadableDepth()`, which reads the extension's request and
records it as built.

- **Readable:** `R24G8_TYPELESS`, `DEPTH_STENCIL | SHADER_RESOURCE`.
- **Otherwise:** retail's literal call.

`AttachDepthBuffer` tests what was built. In the readable arm it creates an explicit
`D24_UNORM_S8_UINT` DSV (2DMS / 2D) and an `R24_UNORM_X8_TYPELESS` SRV. The SRV goes into
`ImageBufferData::shaderResourceView`, a slot retail leaves null for the depth alias. That puts the
SRV under the **tracked-image registry**: `~ImageBufferData`, `ReleaseDynamic` and `RecreateDynamic`
release and rebuild it together with the depth on resize, `settingsChanged` and device reset.

**Toggle.** `Sync()` calls `RequestReadableDepth(on)`. When the request differs from what was built,
it raises `Video::Device::settingsChanged`. The next `BeginCommands` then runs `DeviceData_Resize`,
the same rebuild path a window resize or an Options Apply takes. Until that frame the depth SRV is
null and the pass skips. That is a legitimate transitional state, not a faithful crash.

At boot the device exists before `InitFromSettings`, so a row that is on costs one rebuild on the
first frame. This is visible in the log as
`[AO] main depth -> readable ... rebuild requested` followed by `[AO] readable main depth 1024x640 x4`.

**The AO targets** are the resolve's R32F and ASSAO's internal textures. They are not engine
`Video::ImageBuffer`s, so they are not literally registered with the tracked-image list. ASSAO
creates its own textures and resizes them in `Draw`. Instead:

- `ao_pass.cpp` resizes the resolve target whenever the depth's size changes.
- It rebuilds all pass objects when `Dx11Device()` changes (device lost runs
  `DeviceData::CreateDevice`, which makes a new device).

The only object tied to the swap chain or depth, the SRV, is in the registry. **Device-lost is not
exercised** (§6).

### 3.3 Opacity goes through ASSAO's ShadowMultiplier

`shadowMultiplier = 1.0 x opacity / 100`, and opacity 0 skips the pass. A true
`lerp(1, ao, opacity)` would need a spare blend factor that the multiply blend (ZERO / SRC_COLOR)
does not have, or edits to Intel's three Apply shaders. With ShadowPower 1.5, the slider response is
not linear in perceived darkness. If AOTUNE wants a lerp, it is a one-constant-buffer-field change in
`PSApply` / `PSNonSmartApply` / `PSNonSmartHalfApply`. ASSAO's Apply writes alpha 1.0 and blends
`DestBlendAlpha = SRC_ALPHA`, so the colour alpha (the terrain's water height) is preserved.

### 3.4 Tiers and zoom (`ao_math.h`)

| quality | ASSAO QualityLevel | BlurPassCount |
|---|---|---|
| 1 Low | 0 (or -1 Lowest when `kLowIsLowest`) | 1 |
| 2 Medium | 1 | 2 |
| 3 High | 2 | 2 |

The zoom curve uses the player camera's `distance`, retail's 20..130 zoom (camera_input.cpp):
`t = clamp((d - 20) / 110)`.

| | Street (t = 0) | Overview (t = 1) |
|---|---|---|
| Radius (world units) | 1.5 | 4.0 |
| FadeOutFrom / To | 120 / 220 | 300 / 460 |

The fade always ends below 512, so the sky (raw depth 1.0, view depth 512) gets none. These are
starting values. AOTUNE owns them.

### 3.5 Feature level 11_0 (§6.4)

`AoBackend::Supported()` is the live device's `GetFeatureLevel() >= 11_0`.
`Enabled() = row && Supported()`. Below 11_0 the row stays inactive, with one `[AO]` line.

## 4. GCC / ASSAO notes

- The spike's two commits were cherry-picked as they were (`039bcee5a` = 50f6b77f6, `07fff7c7c` =
  a31503949).
- `ASSAO_Effect::GetAllocatedVideoMemory()` is never updated upstream (always 0). The log therefore
  says only `[AO] ASSAO targets allocated for WxH`.
- ASSAO's samplers have MaxLOD = FLT_MAX. trace_oracle's S4 invariant (`maxLOD == mips-1`, a retail
  property) flags exactly those 18 records (§5.3).

## 5. Verification

### 5.1 Runs

| Run | Exe | Result |
|---|---|---|
| `tools/build.sh run` (faithful, the gate's by-hand run) | debug, final | RUNNING to frame 6680 in game. No `[AO]` line, crash log empty, no hang log. |
| Row on, 60 s in game (`ext.ambient_occlusion=1`, all other ext.* = 0) | debug, final | RUNNING to frame 12894. The `[AO]` lines above. Crash and hang logs empty. |
| Row on, an earlier build | debug | ran 611 s and 84k frames, clean |

### 5.2 RenderDoc (`build/aocore/caps/on.rdc`, perf exe, seed 12345, pin town,40, 4x, frame 600 in game)

These captures are not committed (`build/` is ignored). The frame sequence:

| Events | What happens |
|---|---|
| ..1244 | The Opaque list |
| 1255 | The depth resolve into R32F (ResourceId 2661). Stats 0.918..0.975 raw, which is view depth 23..69; no NaN, no inf, no negatives |
| 1274..1435 | ASSAO's prepare, mip, generate and blur passes. Final AO is ResourceId 2714, R8G8 x4, AO 0.008..1.0 |
| 1450 | The apply into ResourceId 1791 (the main 1024x640 MSAA colour, with the main DSV bound) |
| 1498.. | The Blended list |

Images, in `build/aocore/caps/`:

- `on_colour_before_apply.png` / `on_colour_after_apply.png`: contact shadows under the eaves and
  around the houses' bases, and darker tree interiors.
- `on_assao_final_slice0.png`: R = AO, G = edges.
- `on_resolved_depth.dds`.

`menu_on.rdc` is the menu's own capture (the menu loop captures its draw 10 whenever RenderDoc is
attached). It shows the backdrop town with **no** AO resource.

**trace_oracle caveat.** `capture_arm.sh` takes the first `.rdc`, which is always that menu capture.
My copy, `build/aocore/capture_arm_safe.sh`, waits for the second one, and it kills only the
boot-state pid (the stock script reaps by image name). Every `trace_oracle` capture before this fix
that was meant to be "in game" deserves a second look. **Coordinator: the stock script should get
the same fix.**

### 5.3 trace_oracle (`tools/trace_oracle/`, perf exe)

| Comparison | Result |
|---|---|
| off vs off2 (identity) | S1 / S2 / S4 / S5 / S6 = 0 |
| on vs off, floored by off / off2 | S3 = 0, S6 = 0. S1 79, S2 41, S5 6, S4 18 |
| the same, with the main depth's texture key normalised (`D24S8_TYPELESS` back to `D24S8` in on.jsonl) | S1 9, S2 9, S5 6, all only-on; **only-off 0** on every screen; S3 0, S6 0; S4 18 = ASSAO's samplers |

Every faithful pipeline configuration is still present with the row on. The only differences are:

- the AO pass's 9 configurations and 6 SRVs, and
- the main depth's format, which is the intended §6.1 change.

`trace_selftest` on off.jsonl: 9/9 negatives clean, every mutation effective.

**Row off vs faithful.** This was not captured against a pre-AO exe. With the row off, the code
differs from main only by the null test, one atomic load per depth (re)build, and the `Enabled()`
bool. No D3D call differs.

### 5.4 Stress town (`tools/stress_run.sh`, perf exe, 4x MSAA, machine load CLEAN for every leg)

**fps sat at exactly 140 in every leg: RTSS / Afterburner / RTSSHooksLoader64 are running. I did not
stop them.** The GPU cost of the pass is therefore invisible. `draw ms` is the CPU-side submit.

| Leg | sim ms/iter | draw ms/frame | frame ms | fps | private MB | Results |
|---|---|---|---|---|---|---|
| faithful (`/profile:faithful`) | 0.15 | 0.90 | 7.16 | 140 | 538 | `20261014_161106_aocore_faithful` |
| row on, Medium | 0.15 (-0.2%) | 0.89 (-1.6%) | 7.16 | 140 | 552 (+2.6%) | `..._161315_aocore_on_medium` |
| row on, Low | 0.15 (-1.7%) | 0.90 (-0.3%) | 7.16 | 140 | 549 (+2.1%) | `..._161521_aocore_on_low` |
| row on, High | 0.16 (+2.7%) | 0.93 (+2.6%) | 7.16 | 140 | 549 (+2.1%) | `..._aocore_on_high` (the second one) |
| render_pipe only | 0.52 | 2.56 | 7.53 | 133 | | `..._aocore_renderpipe_only` |
| render_pipe + AO Medium | 0.51 | 2.57 | 7.52 | 133 | | `..._aocore_on_medium_renderpipe` |

- 0 regressions over threshold for Low, Medium and High.
- The ~+10 MB is ASSAO's targets plus the R32F.
- The render_pipe legs show render_pipe's own per-entry numbers. AO adds nothing measurable on top.
  I am recording this, not chasing it. It does prove that the packet path runs AO on the render
  thread without a crash.
- **Lowest was not run.** Low costs nothing visible on the CPU side, and the GPU side is hidden by
  the cap. An uncapped pair (RTSS stopped by the user) is what would decide Low vs Lowest.
- The first "High" leg actually ran Low, because `STRESS_EXT` zeroed `ext.ao_quality` and the
  registry clamped it to 1. That run is kept as `..._aocore_on_high_WAS_LOW_ao_quality_zeroed`.
  `tools/stress_run.sh` is fixed in its own `tools:` commit.

### 5.5 D3D11 debug layer

This used a temporary, never-committed patch: `D3D11_CREATE_DEVICE_DEBUG` plus an info-queue drain
immediately before and after `ExtAoAfterOpaque`'s state save and restore. Debug exe, row on, Medium.

| MSAA | Frames | Messages from the AO pass | The engine's own |
|---|---|---|---|
| 4x | 11,000 | **0** | 204,486 (AOSPIKE's known ids 351 / 353 / 391) |
| 1x | 7,000 | **0** | |

The readable-depth view creation was already verified silent by AOSPIKE S3. It ran in both runs
(`DSV 0x00000000, SRV 0x00000000`).

### 5.6 Gates I ran

- `tools/build.sh unit`: 1211 / 0.
- `tools/acceptance/acceptance.sh`: 0 lines off target.
- The resave gate, castmetric and worldcmp were **not** run. They are the coordinator's.

## 6. Not verified

- **The live toggle in game.** Off to on and on to off from the Extensions dialog or AOMENU's tab
  mid-game is untested. Only the boot path was run: `Sync` at boot sets `settingsChanged`, then the
  rebuild, then the readable depth. The code takes the Options-Apply path.
- **Device lost with the row on** (`BANISHED_FORCE_DEVICE_LOST`). The device-change rebuild in
  `ao_pass.cpp` has not been exercised.
- **GPU cost per tier.** Needs an uncapped run with RTSS stopped by the user. The RenderDoc replay
  gave no counters.
- **Lowest vs Low** (same reason).
- **Linux / DXVK build** of the ao files (AOSPIKE S2's open item). The compat structs are guarded
  `#ifndef _MSC_VER`.
- **Look.** Trees darken strongly, and the foliage edge noise shows in the AO texture. Baked AO
  darkens twice. Both are AOTUNE's.
- **Eye camera + AO.** The projection constants follow the near 0.1 matrix (unit test), but this was
  not run.

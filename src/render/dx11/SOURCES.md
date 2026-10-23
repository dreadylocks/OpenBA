# src/render/dx11/src — clean C++ reimplementations

One file per class or per closely-related group, matching the `*Data` classes in
`../types/VideoDX11Data.h`. Since ENDGAME B5 (the dx11 seam, 2026-10-05) each is a
real `final` subclass of the runtime's abstract `Video::*Data` base
(`src/engine/video/CompanionData.h`) and every slot is an `override`: the
compiler builds the vtables, `DeviceData`'s eleven factories are `new XData(owner)`
(the base's `operator new` is retail's `MemAlloc(size, 4)`) and the runtime
destroys a companion with `delete`. The hand-built tables, `DX11_REGISTER_SLOT`
and `src/render/dx11/funcs/dx11_seam.{h,cpp}` are gone. `backend.cmake` (included by the top-level
`CMakeLists.txt` for `BANISHED_RENDER_BACKEND=dx11`) globs `src/render/dx11/*.cpp`; the top level
adds `src/render/common/*.cpp`.

## Landed — all twelve classes, all 84 slots, unconditional

Slot counts are the `override` declarations per class (plus the destructor); the
total is 84, pinned by retail number in `tests/unit/b39_vid_data_test.cpp`.
Nothing here is opt-in any more: the `BANISHED_DX11_SHADERS` gate this table used
to cite **no longer exists** — it is not in `src/app/options.h` and not in any
`src/render/dx11/` source — and every slot is registered on every boot. That is DIRECTION §2's
rule ("a port registers unconditionally"), and the last four rows of the table are
verified per-draw against the shipped DLL in RenderDoc — see `src/render/dx11/README.md`
§"Per-draw pipeline-state equality".

| File | Covers | Slots |
|------|--------|-------|
| `devicedata.cpp` | `DeviceData` — dtor, `EnumerateInterfaces`, `CreateDevice`, `Shutdown`, `SetImageBuffer` (cursor) and the eleven companion factories | 16 |
| `devicedata_stubs.cpp` | `DeviceData` slots 4–7 — the four one-liner stubs | 4 |
| `contextdata.cpp` | `ContextData` — the whole 20-slot command/draw surface, including `Finish`'s **device-lost recovery** (`Present` → `DXGI_ERROR_DEVICE_REMOVED`/`_RESET` → shutdown + recreate → `RecreateDynamic`), testable with `BANISHED_FORCE_DEVICE_LOST` | 20 |
| `buffers.cpp` | `VertexBufferData` (7) + `IndexBufferData` (7) + `ConstantBufferData` (4). One `Lock`/`Unlock` body serves the first two, which are byte-identical in layout; also the `CbRegistry` mutex (see below) | 18 |
| `imagebufferdata.cpp` | `ImageBufferData` — dtor, attach colour/depth, `Create`, `Bind`, `Update`, `Read` | 7 |
| `renderstatedata.cpp` | `RenderStateData` — dtor, `Create`, `Bind` | 3 |
| `shaderprograms.cpp` | `VertexProgramData` (3) + `PixelProgramData` (3) — dtor, `PlatformCreate` (`Create{Vertex,Pixel}Shader` per permutation), `Bind` (`{VS,PS}SetShader` with the index-0 fallback) | 6 |
| `vertexlayoutdata.cpp` | `VertexLayoutData` — dtor, `Bind` (`IASetInputLayout`), `PlatformCreate` (`CreateInputLayout`; the DLL's only "TEXCOORD" user) | 3 |
| `smallclasses.cpp` | `VideoProgramData` (3, inert) + `RenderBufferData` (4 — `OMSetRenderTargets` / `ResolveSubresource`) | 7 |
| `dx9_placeholder.cpp` | The cutover-only DX9 placeholder, a real subclass of `Video::DeviceData` that lists the same adapters under the "DirectX 9" name so the interface array keeps one entry per slot | — |
| `dx11_backend_access.{h,cpp}` | Shared plumbing: the `Video::Device -> DeviceData -> ID3D11Device/Context` walk (the one downcast dx11 makes: it built the object), `Dx11SwapChain()`, the constant-buffer registry | — |
| `exports.cpp` | The two plugin entry points (`../types/VideoDX11Plugin.h`): `Dx11_CreateDeviceData` is `new DeviceData(device)` | — |
| `render_backend_dx11.cpp` | NOT RETAIL. The backend-neutral `Render::` entry points (`../common/render_backend.h`) as forwarders to the plugin's four (`VideoDX11Plugin.h`); what `src/app/video_interface.cpp` writes into the Device's interface slots | — |
| `backend.cmake` | NOT RETAIL. This backend's CMake fragment: `RENDER_SRCS`, include directory, per-file properties, the Linux dxvk-native check (`../common/README.md`) | — |

## The seam that is gone (ENDGAME B5, 2026-10-05)

B.3.5's RULE B5-8 left the twelve concrete classes as C structs behind an 84-slot
hand-built table because `ContextData_Finish` dispatched the device-lost
`Shutdown` / `CreateDevice` by INDEX and the seam's completeness check was the
only proof the slots were non-null. B5 made the by-index call a by-name one first
(W0b), then flipped the twelve bases to `virtual ~XData()` and the twelve dx11
structs to real subclasses, one block per hierarchy, each gated with the
resave gate and a bounded forced device loss (`docs/history/endgame_b/b5_session/`).
A destructor no longer re-seats a vptr, calls its base by qualified name or
frees `this`: the compiler's D0 and the base's class-scope `operator delete` do
exactly those three retail instructions.

## The Runtime headers this directory includes

There is **no** `DX11_FLAGS` in the Makefile. `src/render/dx11/*.cpp` and
`src/render/dx11/funcs/*.cpp` are folded into `SA_SRCS` and compile with the single shared
include set, which already carries `-Iruntime/types -Iruntime/funcs`. So dx11
includes real Runtime types rather than keeping private views of them, and
B.3.2/B.3.5 have been collapsing those views one owner class at a time:

| included header | replaced this directory's view of |
|---|---|
| `collection/Collection.h` | `RtArray` (B.3.2 step 2) |
| `system/{String,WideString,Error}.h` | — (B.3.4b) |
| `video/CompanionData.h` | the twelve base `*Data` vptr/owner pairs |
| `video/Buffers.h` | `GpuBufferOwner`, a two-field prefix of two classes at once |
| `video/ImageBuffer.h` | `ImageBufferOwner` |
| `video/Device.h` | `RenderBufferOwner`, and `devicedata.cpp`'s four mirrors of `Device::{DisplayMode,RefreshRate,Adapter,GraphicsInterface}` |
| `video/ShaderProgram.h` | `ShaderProgramOwner` |
| `video/VertexLayout.h` | `VertexLayoutOwner` + `VertexLayoutElement` — the one layout this directory was the ONLY home of, until block A authored the header |
| `video/ConstantBuffer.h` | `ConstantBufferOwner` (block VC) |
| `video/Context.h` | `contextdata.cpp`'s local `struct Viewport` (block VC) |
| `io/Serializer.h` | `SerializerStreamMode`'s hand-read stream-direction offsets (block VC, B35_RULES §5.5) |

**Every duplicate view B35_RULES §1.3 tabulated is now collapsed**, and the
`static_assert`s that used to repeat the Runtime-side offsets now check dx11's own
independently-recovered constants AGAINST the shared definition — so a future
divergence breaks this directory's build instead of reading the wrong field. The
one false witness in the old set, `static_assert(sizeof(RenderBufferOwner) ==
0x10)` on a prefix view of a 0x30-byte class, is gone.

## The one non-retail lock in the port

`buffers.cpp`'s `CbRegistryLock()` / `CbRegistry()` and
`Dx11TrackConstantBuffer` / `Dx11Untrack…` / `Dx11InvalidateConstantBufferBindings`
fix a **port-introduced** dangling-binding bug after `ClearState()` on a device
reset. They are touched on create, on destroy and once per device reset — never
per draw. RULE B5-8 keeps them and their comment as they are.

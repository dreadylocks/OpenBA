# src/render/dx11/ — VideoDX11-x64.dll reimplementation

Mirrors the `engine/` and `runtime/` layout for the third binary: the DX11
renderer plugin. Goal is the same as everywhere else in this project — replace
the shipped DLL's code with clean C++ that links into `OpenBA.exe`, one
function at a time, with the original still available as a fallback until each
piece is proven.

Reference decompilation status: [../decomp/videodx11.md](../decomp/videodx11.md)
(scope validated 2026-07-24).

```
src/render/dx11/
  README.md      — this file: scope, the (historical) seam, workflow, status board
  types/         — the 12 VideoDX11::*Data classes (real subclasses of Video::*Data) + the plugin header
  src/           — clean C++ reimplementations, one file per class
```

**ENDGAME B5 (2026-10-05): the seam is gone.** Everything below that describes
`src/render/dx11/funcs/dx11_seam.{h,cpp}`, `DX11_REGISTER_SLOT`, `Dx11SeamInstall()`, hand-built
vtables, "AB mode" and slot verification is HISTORY: the twelve `VideoDX11::*Data`
types are real `final` subclasses of the runtime's abstract `Video::*Data` bases
(`src/engine/video/CompanionData.h`, `virtual ~XData()`), every slot is an
`override`, the compiler builds the tables, `new` / `delete` go through the base's
class-scope operators (retail's `MemAlloc(size, 4)` / `MemFree`). The status board's
addresses and slot roles are still the reference; `src/README.md` has the current
file map. Rule sheet: `docs/history/endgame_b/B5_RULES.md`.

**The backend seam (2026-10-20).** This directory is ONE renderer backend, picked by CMake's
`BANISHED_RENDER_BACKEND=dx11` (the default): `backend.cmake` here sets `RENDER_SRCS`, the include
directory, the ASSAO per-file warning properties and, off Windows, the dxvk-native check and its
`-isystem`. The code outside the backend reaches it only through the backend-neutral headers in
`../common/` (`render_backend.h`, `ao/ao_backend.h`, `better_trees/foliage_aa_backend.h`,
`better_trees/billboard_shadow_backend.h`); `render_backend_dx11.cpp` forwards
`Render::DeviceName` / `CreateDeviceData` / the placeholder pair to `VideoDX11Plugin.h`'s four. The
pure DXBC tooling (`dxbc`, `cutout_rewrite`, `receiver_rewrite`, `edge_dither`) moved to
`../common/better_trees/` with them; the D3D stages that use it (`better_trees/*_stage.*`,
`ao/*`) stay here. `../common/README.md` has the contract a second backend fills. The one
`VideoDX11::` name still used outside: `Dx11SwapChain()` in `src/app/main.cpp`'s Windows-only
swap-chain probes.

---

## Why this is a tractable target

VideoDX11-x64.dll has 576 functions but only **85** are renderer code
(`0x180001000`–`0x180005b7f`, ~19 KB); the other 491 are the MSVC CRT, the C++
exception machinery, and locale tables. We never port those — the exe already
has its own CRT.

85 functions, 12 classes, 2 exports, one vtable that reaches everything. This is
by far the smallest of the three binaries and it is a clean *leaf*: Runtime
drives it, it never drives Runtime.

---

## The replacement seam

Unlike exe functions (which `ResolveAddr(Module::Exe, …)` redirects at the call
site), this DLL is loaded by Runtime at startup and reached through exactly two
function pointers. `Video::Device::InitializeInterfaces` (Runtime `0x1800f79f0`)
`LoadLibrary`s `VideoDX11-x64.dll` and stores the two exports on the
`Video::Device` singleton (Runtime `0x1801fe7a8`):

```
Device+0xc8  HMODULE          VideoDX11-x64.dll
Device+0xd0  GetDeviceName    -> "DirectX 11"
Device+0xd8  CreateDeviceData -> Video::DeviceData* (Video::Device*)
```

**Those two slots are not usable as the seam.** The scaffold originally proposed
overwriting `+0xd0`/`+0xd8` between `InitializeInterfaces` and
`CreateVideoInterface`, with a hybrid "delegating `DeviceData`" behind them.
Implementing Phase 0 killed that plan on three counts:

1. **There is no window.** `InitializeInterfaces` *and* `CreateVideoInterface`
   both run inside a single `Engine::Startup` call. The exe never gets control
   between them, so there is no point at which the slots can be swapped.
2. **A delegate splits the object.** Every resource `Bind` in the DLL reads
   `g_Device->[0x100]->[0x28]` to find the D3D11 context — i.e. un-ported
   original code reads fields out of whichever `DeviceData` Runtime was handed.
   With two objects, ported and un-ported slots would read different state.
3. **`DeviceData+0x60` is a live registry** of `ImageBuffer*`s, appended by
   `0x180002300` and swap-removed by `~ImageBufferData`. Split across two
   objects it stops being a registry.

### The seam that works: own the vtables

Each ported slot registers its reimplementation, and `Dx11SeamInstall()`
assembles the registrations into twelve arrays in this exe that our factories
store as vptrs.

```
Dx11RegisterSlot(Vt::IndexBufferData, 5, &IndexBufferData_Bind,
                 "IndexBufferData::Bind");
...
Dx11SeamInstall();   // refuse on any hole, then publish the twelve vtables
```

This is strictly better than the delegate:

- **One object, the real one.** Our slot receives the genuine `DeviceData` with
  the genuine fields, so every slot shares state by construction.
- **No ordering constraint.** Install any time before `Engine::Startup`; we do it
  in `src/app/main.cpp`, Init() step 4b.
- **Works for all twelve classes** without touching any factory.
- **One slot = one unit of work**, exactly like an entry in
  `standalone/address_registry.cpp`.

**Install-time check.** With the DLL gone there is no live map to verify against,
so the check is completeness: every slot of every vtable must have a registered
implementation. A hole is a null function pointer that Runtime will eventually
dispatch through, so it is refused rather than zero-filled — the whole install
aborts. First run: `[Dx11Seam] 84/84 slots ours, in 12 exe-owned vtables`.

> **This is not how the port was built.** Through all six porting lanes the seam
> patched the shipped DLL's `.rdata` in place, verified all 84 live slot pointers
> against an address map on every boot, and kept the original reachable so one
> slot could be flipped between ours and the shipped code. See "AB mode, and why
> it is gone" below.

**No per-slot flags.** Every slot is unconditionally ours. While the DLL was
mapped each registration carried a `BANISHED_DX11_*` name and a `defaultOn` bool,
following the project's kill-switch convention (`the default-on table` in
`src/game/Component/component_factory_base.cpp`): a new slot started opt-in and
graduated to default-on once verified in-game, after which the var was a `=0`
kill switch. That convention needs somewhere to fall back *to*. With the original
gone a declined registration would just be a null vtable entry, so the flags were
retired along with the lane they belonged to — the registration takes a label and
nothing else.

---

## One driver: the port replaces the DLL outright

`VideoDX11-x64.dll` is not loaded, not patched, and does not need to be present.
The twelve vtables are twelve arrays in `OpenBA.exe`, both exports are
written straight onto `Video::Device`, and the install-time check is
completeness: every slot of every vtable must have a registered implementation,
because an unregistered slot is a null pointer Runtime will dispatch through.

### AB mode, and why it is gone (2026-07-24)

Until the per-draw pass below, there was a second mode (`BANISHED_DX11_AB=1`)
that loaded the shipped DLL, verified all 84 live slot pointers against an
address map, patched its `.rdata` in place, and detoured the two exports. Its
whole point was `Dx11SetSlotEnabled()`: flip one slot between ours and the
shipped implementation so two RenderDoc captures differ by exactly one function.
That is how all six porting lanes were verified.

It was removed once per-draw pipeline-state equality was proven end to end. With
the comparison done there was nothing left to compare against, and AB mode was
carrying real cost: a second seam mechanism, a second install path, the 84-slot
address map, `Dx11IsDetourStub`, and the DEP tripwire that existed only to watch
the shipped `.text` for code the vtables could not reach.

Removed with it, because each only made sense with the DLL mapped:

| gone | was |
|---|---|
| `BANISHED_DX11_AB`, `Mode`, `Dx11ActiveMode` | mode switch |
| the `.rdata` patching path, `WriteSlot`, `Dx11SetSlotEnabled` | per-slot A/B |
| `Dx11RegisterDetour` / `DX11_REGISTER_DETOUR`, the export prologue byte tables, `Dx11IsDetourStub` | export detours |
| `VtDesc::target[]` — the 84-slot address map | install-time verification against the live DLL |
| `src/render/dx11/funcs/dx11_tripwire.{h,cpp}`, `BANISHED_DX11_TRIPWIRE` | DEP tripwire over the shipped `.text` |

**To A/B a future change against the original again, restore AB mode from git.**
It was deleted in the commit following the one that added the per-draw section
below. Two things make that less often necessary than it sounds: the tooling in
[`tools/dx11_state_diff/`](../tools/dx11_state_diff/) is current, and
`docs/history/notes/dx11_state/shipped1.jsonl` / `shipped2.jsonl` are recorded per-draw dumps
of the shipped renderer that can be diffed against a new capture of ours without
re-running the DLL at all.

Kept deliberately: `Dx11OriginalSlot()` and its fallback counter. It can only
return null now, but it is still the single choke point for the 27 "a Runtime
import failed to resolve" branches, and `fallbacks to shipped code: 0` printed
at exit stays positive evidence rather than an absence of it.

Retired with it: the 84 per-slot `BANISHED_DX11_*` flags and the `defaultOn`
bool. They were the graduation mechanism, and graduating needs somewhere to fall
back to. `Dx11RegisterSlot` and `DX11_REGISTER_SLOT` now take a label and nothing
else; the per-lane `DX11_*_FLAG` defines are gone.

### How the port stops Runtime loading the DLL

`Video::Device::InitializeInterfaces` (Runtime `0x1800f79f0`) walks three
interface slots and, for each, **skips it if the HMODULE word is already
non-zero**. So we do not reimplement it, hook `LoadLibrary`, or ship a stub DLL
— we get there first. `src/app/video_interface.cpp` detours it, fills
`Device+0xc8/+0xd0/+0xd8` with a sentinel handle and our two exports, then
chains to the original, which now skips DX11 and probes DX9/GL exactly as
before. `CreateVideoInterface` (`0x1800f7930`) then requires only that all three
words be non-zero; the HMODULE is never called through.

Two things had to be untangled before that worked, both invisible while the DLL
was mapped:

- **`ResolveAddr` returned the Ghidra address unchanged for an unloaded module**
  (delta 0), i.e. a plausible non-null pointer into nothing rather than null.
  Fixed to return 0, guarded on the handle rather than the delta.
- **`CreateDXGIFactory1` and `D3D11CreateDevice` were read out of VideoDX11's
  own IAT**, with `GetProcAddress` as a fallback the comment called unreachable.
  Standalone made it the only path, so the order is inverted and it uses
  `LoadLibrary` — with VideoDX11 absent, nothing else has pulled `d3d11.dll` in
  yet, so `GetModuleHandle` alone returns null and the renderer never comes up.

Verified 2026-07-24 with `VideoDX11-x64.dll` **renamed off disk**: autostart
reaches the in-game world and renders terrain, foliage, buildings, alpha-blended
icons, UI and smoke, with an empty crash log, zero fallbacks, and

```
[Resolve-Standalone] VideoDX11-x64.dll: not loaded
[Dx11Seam] mode: Standalone (our own vtables)
[Dx11Seam] Standalone: 84/84 slots ours, in 12 exe-owned vtables.
[VideoInterface] DX11 slot pre-filled — Runtime will not LoadLibrary VideoDX11-x64.dll
[VideoProbe]   +0x04 interface = 1 (VideoDX11)
[VideoProbe]   active vtable lives in: OpenBA.exe
```

`DumpVideoState` shouts on stderr whenever the active `DeviceData` vtable is not
in the module the current mode expects — `OpenBA.exe` in Standalone,
`VideoDX11-x64.dll` in AB. That guard exists because `Video::Device` answers a
failed interface bring-up by silently falling back to interface 0 and rendering
the whole session on the original `VideoDX9-x64.dll`, which looks like success.
**If you see it, any A/B measured from that run is meaningless.**

---

## Is the shipped DLL actually unreachable? (verification pass, 2026-07-24)

`[Dx11Seam] Installed: 84 slot(s) ours` is **not** the claim "no shipped code
runs". It is a claim about vtable dispatch only, and the ways into the DLL that
are not vtable dispatch are exactly the ones that stay invisible. Three of them
turned out to be live:

1. **The two exports.** Reached through `Video::Device+0xd8`/`+0xd0`, which the
   seam never patches, so `CreateDeviceData` ran the shipped body on *every*
   boot — it is what allocated the `DeviceData` whose vtable we then patched.
   Closed by porting both and installing them as detours (see Exports below).
2. **`ContextData::BeginCommands` -> `0x180004340`.** A near `CALL`, not a
   vtable dispatch, so the seam had no way to redirect it. L5 resolved it with
   `ResolveAddr(Module::VideoDX11, 0x180004340)` on the reasoning that L6 owned
   the function and both callers would converge once L6 landed. They did not:
   L6's `WM_ACTIVATE` handler called L6's port while this call kept entering the
   shipped body — which then called the shipped `CreateColorBuffer`
   (`0x1800052b0`) beneath it. Closed by binding L5 to `Dx11_DeviceData_Resize`,
   the one function in the port with external linkage.
3. **The retired `Dx11FuncsInit`'s sanity check** *called* the shipped `GetDeviceName` to
   prove the ASLR delta. Closed by reading the `.rdata` literal it would have
   returned (`0x1800185c0`) and comparing it to `"DirectX 11"` — same proof,
   nothing executed.

### How it is measured, not argued

Two instruments, because reading the code is what produced the wrong answer
above:

- **Fallback counter.** Every `Dx11OriginalSlot()` call site is a "a Runtime
  import failed to resolve, run the shipped slot instead" branch. That function
  is the single choke point for all 27 of them, so it counts calls and names the
  first per slot on stderr. `[Dx11Seam] fallbacks to shipped code: 0` is printed
  at exit unconditionally — a check nobody has to remember to switch on.
- **DEP tripwire** (`BANISHED_DX11_TRIPWIRE=1`, off by default,
  `src/render/dx11/funcs/dx11_tripwire.h`). Withdraws execute permission from the shipped
  `.text`; a vectored handler catches the DEP fault, records the address and the
  apparent caller, reopens the 4 KiB page that faulted, and
  `ContextData::BeginCommands` re-closes every reopened page each frame.
  Findings are classified against `decomp/videodx11.md`'s partition of the DLL:
  below `0x180005b7f` is renderer code and a finding, `0x180005c24`+ is MSVC
  CRT/EH/locale and expected.

The tripwire is the instrument that found #2 — it is not a formality. Its
positive control is equally real: with the export detours off it reports
`0x1800011a0` (`CreateDeviceData`) with a caller inside Runtime, which is the
residual the notes predicted.

### Result

A full autostart session to the in-game world build, with everything enabled:

```
[Dx11Seam] Installed: 84 slot(s) ours, 0 dormant, 0 failed, 0 still original
[Dx11Seam] detour OURS: CreateDeviceData (0x1800011a0)
[Dx11Seam] detour OURS: GetDeviceName (0x180001230)
[Dx11Seam] fallbacks to shipped code: 0 (expected 0)
[Dx11Tripwire] CLEAN: no shipped VideoDX11 RENDERER code executed while armed
```

The seven "expected" entries are all CRT: `_DllMainCRTStartup` on every thread
attach/detach, `_freefls` on every thread exit, and the `_calloc_impl` /
`__removelocaleref` chain beneath them.

### What is irreducible, and what is still untested

- **The DLL is loaded, and its CRT initialises.** Runtime is what
  `LoadLibrary`s it, and the seam needs it mapped to read the twelve vtables out
  of `.rdata`. `DllMain` at load and CRT teardown at detach are outside any
  armed window and cannot be removed while that is true.

- **`0x180001000`** (`MemFree` import thunk) and **`0x180001520`** (free-and-null
  helper, reached only from EH unwind funclets) remain un-ported. Both are now
  provably dead rather than assumed dead: they are inside the armed range and
  never reported.
- **Coverage limits.** A hit reopens its page until the next frame, so counts
  are lower bounds — zero findings still means zero entries, but a non-zero
  count is not a total. A second thread executing DLL code during that window
  would be missed; nothing in VideoDX11 creates threads, but that is an
  argument, not a measurement.
- ~~**Still open from before this pass:** per-draw pipeline-state equality~~ —
  **closed 2026-07-24**, see "Per-draw pipeline-state equality" below.

### Superseded — this whole section is history

Everything above describes the arrangement where the shipped DLL was mapped and
its `.rdata` patched. That is gone: the DLL is never loaded, so there is no
shipped renderer code in the process to reach, nothing for the tripwire to
watch, and no `DllMain`/CRT residual either. The section is kept because it
records *how* three non-obvious live paths were found — reading the code is what
produced the wrong answer, twice — and that lesson outlives the mechanism.

---

## Per-draw pipeline-state equality (2026-07-24) — the claim AB mode existed for

The frame-level A/B above compares totals: draw count, texture count, clear
count. It rules out gross structural differences and little else — the counts it
compares move on their own between two runs of the *same* renderer. This pass
compares **every draw's full pipeline state**, and is what retired AB mode.

Tooling is in [`tools/dx11_state_diff/`](../tools/dx11_state_diff/); raw output
is `docs/history/notes/dx11_state/RESULTS.txt`, per-draw dumps are the `.jsonl` beside it.

### Making two captures comparable at all

RenderDoc `ResourceId`s are allocation-order handles, so `ResourceId::136` in one
process is unrelated to `ResourceId::136` in another. Every resource reference in
a record is therefore replaced by a **content-derived key**: shaders by sha1 of
their DXBC, textures by creation desc plus sha1 of mip 0, buffers by byte length.
Render targets are keyed by desc and role only — their contents *are* the frame
being rendered, so hashing them would just re-measure the simulation.

### The method: let a control decide what counts as noise

Nothing here is judged against intuition about which fields "should" match. Both
comparisons are run twice — once between two runs of the **same** renderer, once
between ours and the shipped one — and only fields the control proves stable are
treated as evidence.

Four captures, `BANISHED_SEED=12345`, in-game frame 150, all with
`--opt-ref-all-resources`: `shipped1`/`shipped2` (AB mode, every lane flag `0`,
so the shipped DLL's own slots run) and `sa1`/`sa2` (Standalone).

> **`sa1` was discarded**, and the reason matters: it rendered at 2560×1377 while
> every other run got 1280×720 — the intermittent window-size bug below. Its
> capture is 219 MB against ~88 MB for the rest. Comparing it would have compared
> two resolutions. `capture.sh` verifies the active renderer per run; it does
> **not** verify resolution, so check the viewport before trusting a capture.

### Result 1 — per-field diff over aligned draws

The two draw sequences are aligned on pipeline signature (difflib), then every
field of every matched pair is compared. 439 aligned pairs in the control, 368
and 472 in the two test comparisons:

| | fields at 0.0% in control | same fields, ours vs shipped |
|---|---|---|
| `blend` `cbBindings` `depthState` `ds` `gs` `hs` `ibStride` `layout` `om` `ps` `raster` `sampler` `scissors` `srv` `topology` `uav` `viewports` `vs` | **18 fields, 0/439** | **18 fields, 0/368 and 0/472** |

**Every field a same-renderer control proves stable is identical between our
renderer and the shipped one.** That covers both shader stages, the input
layout, all four state objects field-by-field (not by handle), viewport and
scissor rects, render-target and depth bindings, sampler state, and SRV bindings
down to the content hash of the texture.

The six fields the control shows are *not* stable — `cbContents` (100%),
`draw` (82%), `vbStrides` (17%), `cbBufferSizes` (29%), `flags`/`name` (6%) —
are no worse in the test than in the control, except `flags`/`name` at ~12%.
That one was chased: the draw-type mix swings further between the two *shipped*
runs (314 indexed / 259 instanced vs 397 / 202) than between ours and either,
and the non-indexed count is exactly 6 in all three. It is content-driven
batching, not a renderer difference.

**`cbContents` is 100% noise and this pass cannot say anything about it.** The
world state at render-frame 150 genuinely differs between runs, so per-object
transforms differ. Constant-buffer *bindings* are verified (0%); the bytes
inside them are not. Closing that would need a capture pinned to a sim tick
rather than a frame.

### Result 2 — pipeline signatures, with no alignment step

Alignment is the weak point of Result 1, so this comparison avoids it entirely.
Each draw is reduced to a signature: the whole pipeline setup with everything
derived from *which* resource is bound removed. Three quantities had to come out
or the comparison would have been circular — D3D11 derives them from the bound
texture, so they smuggle its identity back in:

```
sampler.maxLOD    clamped to the bound texture's mip count
srv.numMips       the view covers the texture's mips
srv.numSlices     the view covers its array slices
```

They are not discarded, they are promoted to invariants and checked against the
description of the texture actually bound at that slot — a stronger statement
than set equality: not "both used 7" but "both derived it from the texture by
the same rule, every time".

| | control (shipped1 vs shipped2) | ours (sa2) vs shipped1 / shipped2 |
|---|---|---|
| distinct signatures | 35 vs 35 — **identical set** | 31 vs 35, **all 31 of ours occur in shipped** |
| gap, classified | — | 4 signatures, **all 4** shader-pair-never-drawn (content), **0 unexplained** |
| `maxLOD == mips-1` | 1475/1475, 1633/1633 | 1405/1405 |
| `srv.numMips == mips` | 1475/1475, 1633/1633 | 1405/1405 |
| `srv.numSlices == arraySize` | 1475/1475, 1633/1633 | 1405/1405 |

Zero exceptions to any invariant on either side. The four signatures ours lacks
are materials that had nothing to draw in that run — its shader pairs never
appear in our capture at all.

Getting here took two wrong signature definitions, both of which manufactured
findings: the first left `sampler.maxLOD` in and reported 7 "suspects", the
second left `srv.numMips` in and reported 7 again *plus* one in the control. A
signature that flags the control is measuring content, not correctness.

### Result 3 — resource sets, with `--opt-ref-all-resources`

This is what the old buffer-count mystery needed. RenderDoc lists only
frame-*referenced* resources by default, which is why the count wandered 94–99
across nine captures on both sides. With all live resources referenced:

| | sa2 | shipped2 |
|---|---|---|
| resources / textures / buffers | 1028 / 207 / 341 | 1028 / 207 / 341 |
| texture bytes | 124,769,459 | 124,769,459 |
| buffer bytes | 26,675,612 | 26,675,612 |
| resource-type histogram | \-\- identical \-\- | 9/9 keys |
| textures by desc **and content hash** | \-\- identical \-\- | 189/189 keys |
| buffers by byte length | \-\- identical \-\- | 247/247 keys |

**Identical to the byte**, including the constant-buffer size ladder that the
old count was a noisy proxy for. The shipped-vs-shipped control does *not* match
itself this closely (1016 vs 1028 resources, 200 vs 207 textures) — shipped1 was
captured slightly earlier in asset loading. Our renderer creates the same D3D11
objects, holding textures with the same contents, as the DLL it replaces.

### What this does and does not establish

Established: identical pipeline configuration per draw, identical state-object
field values, identical resource bindings, identical live resource sets, and
identical rules for deriving view/sampler parameters from a texture.

Not established: constant-buffer *contents* (irreducibly noisy here), and
anything about frames other than one in-game frame at one camera position.
Ordering *within* a frame is only checked to the extent alignment succeeds.

---

## Verification is unusually good here

This is the one subsystem where we can diff our output against the original
frame-by-frame instead of reasoning about it:

- **RenderDoc MCP** is already wired into this repo (`src/diag/renderdoc_integration.cpp`,
  the `mcp__renderdoc__*` tools, captures in `captures/`). Capture a frame with
  the original DLL, capture the same frame with a slot ported, and diff draw
  calls / pipeline state / render targets.
- `src/app/main.cpp` already has `DumpVideoState()` and `DumpSwapChainState()`
  probes that read the Device singleton and the swap chain.
- D3D11 debug layer: `D3D11CreateDevice` takes a flags word we control once
  slot 2 is ours, so `D3D11_CREATE_DEVICE_DEBUG` becomes available.

---

## Suggested porting order

Ordered by "unblocks the most, risks the least". Nothing here is committed to yet.

| Phase | What | Why first / why later |
|-------|------|-----------------------|
| **0 ✅** | **Seam installer — DONE.** `src/render/dx11/funcs/dx11_seam.{h,cpp}`, installed from `src/app/main.cpp`. Verifies 84/84 slots, patches none. | Pure infrastructure. Verified in-game 2026-07-24: `Verified 84/84`, `0 slot(s) ours`, autostart reached the world build, empty crash log. |
| 1 | `GetDeviceName`, dtor (slot 0), the 4 small stubs (slots 4–7) | Trivial, low blast radius, exercises the seam. |
| 2 | The 11 resource factories (slots 9–19) | Each is a ~15-line `MemAlloc` + base ctor + vptr store. Porting these transfers *ownership* of the 11 resource classes to us without yet porting their vtables. |
| 3 | Per-resource vtables, cheapest first: `RenderBufferData` and `VideoProgramData` (0 private bytes), then `VertexLayoutData` (0x08) | Small structs, few D3D11 objects each. |
| 4 | `VertexBufferData` / `IndexBufferData` / `ConstantBufferData` (Lock/Unlock/Bind) | The buffer-upload path — high call volume, so RenderDoc diffs are informative. |
| 5 | `ImageBufferData` (0x38) | Texture creation, formats, compressed/mip/render-target/depth variants. Biggest of the resource classes. |
| 6 | `VertexProgramData` / `PixelProgramData` / `RenderStateData` | Shader objects, input-layout matching (`"TEXCOORD"`), rasterizer/blend/depth state objects. |
| 7 | `ContextData` (slot 9 users: `SetViewport`, `SetRenderBuffer`, `BindResources`, `BindTextures`) | Depends on nearly every resource class above. |
| 8 | Slot 8 `SetImageBuffer` (`HCURSOR` from a 32×32 ImageBuffer) | Self-contained, no D3D11 — could be done any time as a warm-up. |
| 9 | Slot 1 adapter/mode enumeration (`0x180004690`, 323 lines) | Largest single function; touches `Device::GraphicsInterface` array marshalling. |
| 10 | Slot 2 device + swap-chain creation (`0x180004e10`), slot 3 shutdown | Last, because getting it wrong means no window. Also where the known fullscreen-on-focus-loss bug lives — see the `DumpSwapChainState` comment in `src/app/main.cpp`. |

### Known issue to keep in view

`src/app/main.cpp` documents a fullscreen bug: standalone enters DXGI
exclusive fullscreen, focus loss makes DXGI silently drop FSE, the window snaps
to 1280×720, and `Present` succeeds against a stale surface. That lives in slots
2/3 and the `WM_ACTIVATE` handler pair (`0x1800042d0` → `0x180004340`). Owning
those slots is the fix path, which argues for pulling phase 10 forward if the
bug becomes a priority.

---

## Porting lanes

The port runs as six dependency-ordered lanes. L1–L4 are mutually independent —
each owns its own `src/render/dx11/*.cpp` and its own struct block in
`src/render/dx11/VideoDX11Data.h`, and registers its slots from a static initializer
via `DX11_REGISTER_SLOT`, so no lane edits a file another lane owns.

Each lane had its own `BANISHED_DX11_*` opt-in flag (`_BUFFERS`, `_SHADERS`,
`_IMAGES`, `_STATE`, `_CONTEXT`, `_DEVICE`) so it could be enabled or killed as a
unit. Those are gone with AB mode; the lane structure below is the file layout it
left behind.

| Lane | Classes | Slots | Depends on |
|------|---------|-------|------------|
| L1 Buffers | `VertexBufferData`, `IndexBufferData`, `ConstantBufferData` | 18 | — |
| L2 Shaders | `VertexProgramData`, `PixelProgramData`, `VertexLayoutData`, `VideoProgramData`, `RenderBufferData` | 16 | — |
| L3 Images | `ImageBufferData` | 7 | — |
| L4 State | `RenderStateData` | 3 | — |
| L5 Context | `ContextData` | 20 | L1–L4 |
| L6 Device | `DeviceData` slots 1/2/3 + 8 helpers | 4 + helpers | L1–L5 |

`VertexBufferData` and `IndexBufferData` **must** share a lane: their vtable
slots 3 and 4 are the same two addresses (`0x180003290`, `0x180003340`), so
splitting them would produce two competing ports of identical code.

### Integration protocol (historical — it depended on AB mode)

Lanes implemented and compiled; they did **not** run the game.
`C:/GOG Games/Banished/` is a single shared install, so in-game verification was
serialized:

1. Merge the lane branch, confirm `mingw32-make standalone-debug` is clean.
2. Baseline run with the lane's env flag **unset** — expect
   `Installed: N dormant`, and the game unchanged.
3. Run with the flag `=1`. Check `[Dx11Seam] Installed: N slot(s) ours, 0 failed`,
   then that autostart reaches the world build with an empty
   `standalone_crash.log`.
4. RenderDoc-diff the frame against the baseline capture. Draw calls, pipeline
   state and render targets must match.
5. Only then flip the registration to `defaultOn=true` — the env var becomes a
   `=0` kill switch, per `the default-on table` policy.

Steps 2–4 required the shipped implementation reachable and are not available
now. To verify a *change* to an already-ported slot, the current equivalent is to
capture a frame and diff it against the recorded per-draw dumps of the shipped
renderer in `docs/history/notes/dx11_state/shipped*.jsonl` — see
[`tools/dx11_state_diff/`](../tools/dx11_state_diff/). To bisect a *new* port
slot-by-slot again, restore AB mode from git.

## Status board

Status key matches `decomp/README.md`: `-` not started, `~` partial,
`x` decompiled & documented, `R` reimplemented & live.

### Exports

Reimplemented in `src/render/dx11/exports.cpp`. They are not vtable slots and are not
registered with the seam: Runtime reaches both through
`Video::Device+0xd8`/`+0xd0`, and `src/app/video_interface.cpp` writes our
function pointers straight into those fields from inside a detour on
`InitializeInterfaces` — which skips any interface slot already populated, so
Runtime never `LoadLibrary`s the DLL to fill them.

They used to be installed as **inline detours** over the shipped DLL's own entry
points (`MOV RAX,imm64; JMP RAX` over the first 12 bytes, written only if the
existing bytes matched the decompiled prologue). That check earned its keep
immediately: `PUSH RDI` at `0x1800011a0` is encoded `40 57` — a REX prefix with
no bits set — not `57`, and the byte check refused rather than clipping the
function. The detour mechanism went with AB mode.

| Status | Address | Name |
|--------|---------|------|
| R | `0x1800011a0` | `CreateDeviceData` |
| R | `0x180001230` | `GetDeviceName` |

### `DeviceData` vtable (20 slots @ `0x180018bb0`)

All 20 slots are ported, verified in-game and graduated default-on (L6).

| Status | Slot | Address | Role |
|--------|------|---------|------|
| **R** | 0 | `0x180004270` | scalar deleting dtor |
| **R** | 1 | `0x180004690` | enumerate adapters / outputs / display modes |
| **R** | 2 | `0x180004e10` | create D3D11 device + swap chain + buffers |
| **R** | 3 | `0x1800051c0` | shutdown / release |
| **R** | 4 | `0x180001190` | `ret` — no-op (shared with `VideoProgramData` slot 2) |
| **R** | 5 | `0x180005430` | `xorps xmm0,xmm0; ret` — returns `0.0f` |
| **R** | 6 | `0x180005440` | `xor al,al; ret` — returns `false` |
| **R** | 7 | `0x180001060` | `mov al,1; ret` — returns `true` (shared with `RenderBufferData`/`VideoProgramData` slot 1) |
| **R** | 8 | `0x180005450` | `SetImageBuffer` → `HCURSOR` |
| **R** | 9 | `0x180005650` | create `ContextData` (0x20, vt `…86f0`, ctor IAT `…13478`) |
| **R** | 10 | `0x1800056c0` | create `ImageBufferData` (0x38, vt `…8790`, ctor IAT `…133e8`) |
| **R** | 11 | `0x180005730` | create `ConstantBufferData` (0x28, vt `…85d0`, ctor IAT `…13490`) |
| **R** | 12 | `0x1800057a0` | create `IndexBufferData` (0x20, vt `…87c8`, ctor IAT `…13390`) |
| **R** | 13 | `0x180005810` | create `VertexBufferData` (0x20, vt `…8830`, ctor IAT `…13340`) |
| **R** | 14 | `0x180005880` | create `VertexLayoutData` (0x18, vt `…8878`, ctor IAT `…13330`) |
| **R** | 15 | `0x1800058f0` | create `VertexProgramData` (0x30, vt `…8890`, ctor IAT `…13320`) |
| **R** | 16 | `0x180005960` | create `PixelProgramData` (0x30, vt `…8800`, ctor IAT `…13360`) |
| **R** | 17 | `0x1800059d0` | create `RenderStateData` (0x28, vt `…8818`, ctor IAT `…13350`) |
| **R** | 18 | `0x180005a40` | create `VideoProgramData` (0x10, vt `…85a8`, ctor IAT `…134a0`) |
| **R** | 19 | `0x180005aa0` | create `RenderBufferData` (0x10, vt `…8588`, ctor IAT `…134b0`) |

`P` = ported and compiling, **not yet run**. The factory ↔ class pairing was
verified two independent ways: Ghidra's recovered return type per factory, and
the base-ctor CALL displacement resolved through the VideoDX11 Runtime-import
IAT (base `0x180013218`) to a mangled `??0<Class>Data@Video@@` name.

### Runtime-import IAT base — `0x180013218`

VideoDX11's Runtime import thunks are one contiguous run; index *N* is at
`0x180013218 + N*8`, in `objdump -p` order. The base was **derived**, not
assumed: it is the unique value consistent with all six IAT addresses earlier
lanes had pinned independently (`?Lock@Device` `…13408`,
`?_singleton@Configuration` `…13480`, `??1ContextData` `…13470`,
`?_singleton@Device` `…134b8`, `?MemAlloc` `…134c0`, `?MemFree` `…134d0`) and it
additionally predicts all eleven base-ctor slots above, each matching its
factory's CALL displacement. 17 independent confirmations of one constant.

### Resource class vtables

Walked 2026-07-24 by reading `.rdata` `0x180018580`–`0x180018c60` directly. Each
vtable's extent is the run of code pointers before the next vtable or the next
string/GUID literal — the extents are **not** contiguous, string data is
interleaved (`"DirectX 11"` sits between `VideoProgramData` and
`ConstantBufferData`; `"TEXCOORD"` between `VertexBufferData` and
`VertexLayoutData`). `RenderBufferData`'s base at `0x180018588` is confirmed
independently by the `lea rax,[rip+0x17554]` vptr store in its dtor.

| Status | Class | Vtable | Slots | Extent | Lane | Notes |
|--------|-------|--------|-------|--------|------|-------|
| **R** | `RenderBufferData` | `0x180018588` | **4** | `…588`–`…5a7` | L2 | 0 private bytes, but slots 2–3 reach real views via the owner |
| **R** | `VideoProgramData` | `0x1800185a8` | **3** | `…5a8`–`…5bf` | L2 | 0 private bytes; slots 1–2 are shared stubs |
| **R** | `ConstantBufferData` | `0x1800185d0` | **4** | `…5d0`–`…5ef` | L1 | buffer ladder, `activeIndex` rung cache |
| **R** | `ContextData` | `0x1800186f0` | **20** | `…6f0`–`…78f` | L5 | biggest vtable; all 20 slot *names* recovered from Runtime, not inferred |
| **R** | `ImageBufferData` | `0x180018790` | **7** | `…790`–`…7c7` | L3 | largest resource *struct* (0x38); 8-entry format table |
| **R** | `IndexBufferData` | `0x1800187c8` | **7** | `…7c8`–`…7ff` | L1 | slots 3–4 shared with `VertexBufferData` |
| **R** | `PixelProgramData` | `0x180018800` | **3** | `…800`–`…817` | L2 | |
| **R** | `RenderStateData` | `0x180018818` | **3** | `…818`–`…82f` | L4 | full state-desc mapping recovered |
| **R** | `VertexBufferData` | `0x180018830` | **7** | `…830`–`…867` | L1 | slots 3–4 shared with `IndexBufferData` |
| **R** | `VertexLayoutData` | `0x180018878` | **3** | `…878`–`…88f` | L2 | the only `"TEXCOORD"` user |
| **R** | `VertexProgramData` | `0x180018890` | **3** | `…890`–`…8a7` | L2 | |
| **R** | `DeviceData` | `0x180018bb0` | **20** | `…bb0`–`…c4f` | L6 | slots 4–7 live; other 16 ported, awaiting first run |

**48 of 84 slots (57%) are ported and verified running in-game** as of 2026-07-24:
the 4 `DeviceData` stubs plus all of L1 (18), L2 (16), L3 (7) and L4 (3). Each
lane was run alone and all four were run together — `Installed: 48 slot(s) ours,
0 dormant, 0 failed`, autostart reached the world build, empty crash log, and the
rendered frame was correct (terrain, BC-compressed textures, alpha foliage,
water, shadows, smoke, selection outline, UI).

**GRADUATED default-on 2026-07-24** after the RenderDoc A/B pass below. (The
per-slot flags were retired entirely when AB mode went — see above.)

### RenderDoc A/B result

Captured in-game frame 150 with `BANISHED_SEED=12345` pinning the world, via
`renderdoccmd capture`. **Two baseline runs and two ported runs**, because the
first baseline-vs-ported diff showed a delta and the noise floor had to be
measured before it could be interpreted:

| metric | base #1 | base #2 | ours #1 | ours #2 |
|--------|---------|---------|---------|---------|
| total actions | 176 | 173 | 175 | 175 |
| draw calls | 170 | 167 | 169 | 169 |
| clears | 3 | 3 | 3 | 3 |
| textures | 54 / 62.12 MB | 54 / 62.12 MB | 54 / 62.12 MB | 54 / 62.12 MB |
| buffer memory | 20.84 MB | 20.84 MB | 20.84 MB | 20.84 MB |
| buffer count | 96 | 96 | 95 | 95 |
| RT `::132` 1280×720 | 76 draws | 73 draws | 75 draws | 75 draws |
| RT `::278` 640×360 | 46 draws | 46 draws | 46 draws | 46 draws |

**The simulation is not frame-deterministic** even with the seed pinned —
baseline-to-baseline varies by 3 draw calls, and the ported runs land *inside*
that range. Everything content-independent is identical across all four runs:
resolution, both render targets (same `ResourceId`s, same `B8G8R8A8_UNORM`
formats), clear count, texture count and texture memory to the byte, buffer
memory, and the 640×360 target's draw count.

The buffer count (96/96 vs 95/95) looked like a real signal and was chased. It
is not a missing constant-buffer ladder rung: comparing the actual sets, ours
has four rungs baseline lacks (960, 1792, 1920, 2688) and lacks five baseline
has (1664, 1856, 1984, 2816, 3072) — a two-directional difference netting −1,
i.e. a different mix of rungs *referenced by the captured frame*, not an
omission. RenderDoc lists only frame-referenced resources, so this metric is
content-dependent like the draw counts.

**What this pass did and did not establish.** It rules out a systematic
difference in render-target setup, texture creation, clears and the shadow/
half-res pass. It does *not* prove per-draw pipeline-state equality — that would
need a per-event state comparison, and a resource-count comparison would need
`--opt-ref-all-resources` to remove the frame-reference dependence. Both are
open follow-ups.

### L5 A/B (2026-07-24) — 68/84 slots

Same method, isolating `ContextData` (L1–L4 default-on in both arms):

| metric | L5 off | L5 on |
|--------|--------|-------|
| total actions | 175 | 174 |
| draw calls | 169 | 168 |
| clears | 3 | 3 |
| textures | 54 / 62.12 MB | 54 / 62.12 MB |
| buffer memory | 20.84 MB | 20.84 MB |
| buffer count | 99 | 94 |
| RT `::132` | 76 draws | 74 draws |
| RT `::278` | 46 draws | 46 draws |

Every invariant matches. Note the buffer count has now been observed at **94,
95, 96 and 99 across nine captures on both sides** — that settles it as a noisy,
content-dependent metric, and retroactively confirms the earlier 96-vs-95 was
never evidence of a defect.

L5 graduated default-on. Caveat: n=1 per arm here, versus n=2 for L1–L4. The
invariants have been stable across all nine captures, which is why that was
judged sufficient.

**84 vtable slots** across 12 classes, resolving to **79 distinct addresses**
(five targets are shared between vtables).

### Function roster — all 85 attributed

Ghidra defines 85 functions in `0x180001000`–`0x180005b7f`: one import thunk
(`MemFree`) plus 84 real functions. **67** of them are vtable targets; the other
**18** are the two exports, four shared utilities, and twelve class-private
helpers. Every one is attributed below.

A further **12 vtable targets are entry points Ghidra never split into their own
functions** — they are 3-to-30-byte tails living inside a preceding function's
body. They must be treated as real functions when porting. All twelve are
decoded here:

| Address | Bytes | Meaning | Used by |
|---------|-------|---------|---------|
| `0x180001060` | `b0 01 c3` | `return true` | `RenderBufferData`[1], `VideoProgramData`[1], `DeviceData`[7] |
| `0x180001190` | `c2 00 00` | `return` (void) | `VideoProgramData`[2], `DeviceData`[4] |
| `0x180001450` | `48 8b 41 08 / 8b 40 14 / ff c0 / c3` | `return owner->[0x14] + 1` | `ConstantBufferData`[3] |
| `0x180001d10` | tail-jmp `[ctx+0x98]` | `IASetIndexBuffer(NULL, R16_UINT, 0)` | `ContextData`[10] — re-verified by L5 |
| `0x180001ef0` | `b8 34 00 00 00 / c3` | `return 52` | `ContextData`[13] — re-verified by L5 |
| `0x180002020` | `e9 2b 01 00 00` | `jmp 0x180002150` | `ContextData`[17] (alias of slot 19) — re-verified by L5 |

`ContextData`'s twenty slots are named, not guessed: Runtime's `Video::Context`
holds its `Video::ContextData*` at `Context+0x2160` and every public method is a
two-load tail-jump through it (`Video::Context::BindTransforms` `0x1800f22a0` →
`jmp [platform+0x40]`), so each slot's name and full signature come straight out
of the Runtime export table. The mapping is tabulated in
[`src/render/dx11/contextdata.cpp`](src/contextdata.cpp).
| `0x180003380` | tail-jmp `[ctx+0x98]` | `IASetIndexBuffer(this->[0x10], R16_UINT, 0)` | `IndexBufferData`[5] |
| `0x1800036a0` | tail-jmp `[ctx+0x48]` | `PSSetShader(this->variants[i ?: 0], NULL, 0)` | `PixelProgramData`[2] |
| `0x180003fc0` | tail-jmp `[ctx+0x88]` | `IASetInputLayout(this->[0x10])` | `VertexLayoutData`[1] |
| `0x180004230` | tail-jmp `[ctx+0x58]` | `VSSetShader(this->variants[i ?: 0], NULL, 0)` | `VertexProgramData`[2] |
| `0x180005430` | `0f 57 c0 / c3` | `return 0.0f` | `DeviceData`[5] |
| `0x180005440` | `32 c0 / c3` | `return false` | `DeviceData`[6] |

So the real entry-point count for the port is **96** (84 functions + 12 stubs),
not 85.

#### Shared / infrastructure (4)

| Address | Role |
|---------|------|
| `0x180001000` | `MemFree` import thunk (not ported — Runtime's allocator) |
| `0x180001520` | free-and-null an array `{void* data; int count;}` — reached **only** from EH unwind funclets |
| `0x180001550` | generic `Array<T*>::SetLength(int)` — grow ×3, `MemAlloc`/copy/`MemFree` |
| `0x180003060` | tail-thunk to `Video::Device::Unlock(*g_Device)` — EH unwind path |

#### Exports (2)

`0x1800011a0` `CreateDeviceData`, `0x180001230` `GetDeviceName`.

#### Per-class attribution

| Class | Vtable slot targets (in slot order) | Private helpers |
|-------|-------------------------------------|-----------------|
| `RenderBufferData` | `1010`, `1060`*, `1070`, `10e0` | — |
| `VideoProgramData` | `1140`, `1060`*, `1190`* | — |
| `ConstantBufferData` | `1240`, `1300`, `1460`, `1450`* | `1270` (`~ConstantBufferData`) |
| `ContextData` | `1600`, `1650`, `1720`, `17c0`, `1920`, `1ad0`, `1b60`, `1be0`, `1c40`, `1cb0`, `1d10`*, `1d30`, `1d80`, `1ef0`*, `1e60`, `1f00`, `2000`, `2020`*, `2030`, `2150` | — |
| `ImageBufferData` | `2400`, `2580`, `2650`, `2870`, `2da0`, `2e10`, `2ef0` | `2430` (`~ImageBufferData`), `2720` |
| `IndexBufferData` | `3080`, `30f0`, `31c0`, `3290`†, `3340`†, `3380`*, `33b0` | — |
| `PixelProgramData` | `3580`, `3610`, `36a0`* | — |
| `RenderStateData` | `36e0`, `3760`, `3a90` | — |
| `VertexBufferData` | `3b20`, `3b90`, `3c60`, `3290`†, `3340`†, `3d30`, `3d90` | — |
| `VertexLayoutData` | `3f60`, `3fc0`*, `3ff0` | — |
| `VertexProgramData` | `4110`, `41a0`, `4230`* | — |
| `DeviceData` | `4270`, `4690`, `4e10`, `51c0`, `1190`*, `5430`*, `5440`*, `1060`*, `5450`, `5650`, `56c0`, `5730`, `57a0`, `5810`, `5880`, `58f0`, `5960`, `59d0`, `5a40`, `5aa0` | `42d0`, `4340`, `4510`, `4590`, `52b0`, `5350`, `53f0`, `5b00`, and `2300` (shared with `ImageBufferData`) |

`*` = one of the 12 sub-function stubs above. `†` = shared between
`IndexBufferData` and `VertexBufferData` (identical Lock/Unlock semantics).
All addresses are `0x180000000`-relative, `0x1800…` prefix elided.

#### `DeviceData` helper roles (recovered)

| Address | Role |
|---------|------|
| `0x1800042d0` | **WM_ACTIVATE** callback (`RegisterMessageCallback` id **6**), installed by slot 2. Saves/restores exclusive fullscreen across focus loss: on `WA_INACTIVE` sets `DeviceData+0x40=1`, `Configuration+0x40=0`, resizes; on activate does the reverse. **No-ops entirely when both flags are already 0** |
| `0x180004340` | swap-chain resize / buffer recreate; also called directly by `ContextData` slot 1. **First statement sets `Device+0x9c=1`, second is `if (!IsWindowVisible(Device+0x90)) return 0` — the early-out leaves `+0x9c` set** |
| `0x180004510` | `ResolveMultisampleMode()` — clamps **`Video::Configuration+0x24`** (not `Device+0x24`; `0x18000451f` loads IAT `0x180013480`) against `GetMultisampleModeCount`, walks down to a supported mode, writes the lowered index back, returns `sampleCount` (index+1) |
| `0x180004590` | `FillSwapChainDesc(DXGI_SWAP_CHAIN_DESC*)` — see field notes in `../decomp/videodx11.md` |
| `0x1800052b0` | `CreateColorBuffer()` → `CreateTexture2D(B8G8R8A8_UNORM, BIND_RENDER_TARGET)` into `+0x38` |
| `0x180005350` | `CreateDepthBuffer()` → `CreateTexture2D(D24_UNORM_S8_UINT, BIND_DEPTH_STENCIL)` into `+0x30` |
| `0x1800053f0` | **WM_SETCURSOR** callback (`RegisterMessageCallback` id **0x20**), installed by slot 2 — `SetCursor(DeviceData+0x48)` then `ShowCursor(TRUE)` |
| `0x180005b00` | adapter/output enumeration helper for slot 1 |
| `0x180002300` | `TrackImageBuffer(ImageBuffer*)` — add-if-absent into the `DeviceData+0x60` list; the matching remove lives in `~ImageBufferData` |

---

## Workflow (per function)

Same as the project-wide loop in `../CLAUDE.md`, with the DX11 specifics:

1. **Decompile** in Ghidra (`switch_program("VideoDX11-x64.dll")` first — the
   MCP server defaults to Runtime), rename to `VideoDX11_<Class>_<Method>`.
2. **Type header** in `src/render/dx11/` with `static_assert` on every offset.
   Sizes are already pinned by the `MemAlloc` in each factory.
3. **Resolve deps** — Runtime-side calls go in `engine/funcs/engine_funcs.h`
   (existing convention, already has `Video::Device::Lock/Unlock` etc.);
   VideoDX11-internal calls have no home any more: the shipped DLL is never loaded.
4. **Write clean C++** in `src/render/dx11/`.
5. **Wire the slot** with `DX11_REGISTER_SLOT` in the lane's own `.cpp`. Note
   there is no longer an opt-in flag to hide it behind: a registered slot is
   live on the next run, so it needs to be right before it is registered.
6. **Test** — `mingw32-make sa-run`, then read
   `C:/GOG Games/Banished/standalone_debug.log`, and RenderDoc-diff the frame
   against a capture taken with the slot forwarding to the original.

### Ghidra note

`ResolveAddr(Module::VideoDX11, addr)` already exists
(`standalone/resolve.h`, `standalone/resolve_standalone.cpp` — the DLL is
registered with preferred base `0x180000000` and resolved optionally). Both
Runtime-x64.dll and VideoDX11-x64.dll share preferred base `0x180000000`, so
**always pass the right `Module::` enum** — the addresses are otherwise
indistinguishable.

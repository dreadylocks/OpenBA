# Intel ASSAO, vendored

`ASSAO.h`, `ASSAODX11.cpp`, `ASSAO.hlsl` and `LICENSE` come from GameTechDev/ASSAO (MIT),
commit `c21d57a95`, copied unmodified in this repo's commit 50f6b77f6 (AOSPIKE). Intel's MIT
header stays on every file. `ext.ambient_occlusion` uses them through `../ao_stage_assao.cpp`
(docs/extensions/AO.md, docs/extensions/handins/AOCORE.md).

## Edits against upstream

Each edit is marked in the source with `GCC PORT (AOSPIKE)` or `AOCORE`.

- `ASSAODX11.cpp`: no `<d3dcompiler.h>`, no `#pragma comment(lib)` (d3d11 is resolved at run time
  by the backend). `CompileShader` looks the entry point up in the precompiled DXBC table
  (`assao_gcc_compat.cpp`) where upstream called `D3DCompile` plus a `MessageBoxA` on error. The
  exe has no runtime shader compiler. `ID3DBlob` becomes a two-field static `AssaoBlob`, and
  `shaderFlags` is 0 because `/Ges /O3 /WX` are applied by `tools/ao_shaders.py`.
- `ASSAODX11.cpp`: **an upstream bug.** `memset( m_RTVs, sizeof( m_RTVs ), 0 )` had its size and
  value swapped, so it zeroed nothing. That was harmless because `OMGetRenderTargets` fills the
  array anyway. It is now `memset( m_RTVs, 0, sizeof( m_RTVs ) )`. GCC's
  `-Wmemset-transposed-args` found it.
- `ASSAODX11.cpp`: **an upstream leak** (marked `AOFIX`). `D3D11SSAOStateBackupRAII` takes the
  DSV with `OMGetRenderTargets`, which AddRefs it, and its destructor released the RTVs but never
  `m_DSV`. That is one reference per AO frame on the main depth's DSV, which keeps the texture alive
  after it is released. Every rebuild of a readable depth (turning AO off, a resize, an Options Apply
  while AO is on) leaked the whole MSAA depth: about 69 MB at 2560x1377 4x. The destructor now
  releases `m_DSV`.
- `ASSAO.h`: `#include <stddef.h>` (MSVC brings in `size_t` implicitly).
  `INTEL_SSAO_ENABLE_ADAPTIVE_QUALITY` is commented out (AOCORE). Adaptive (QualityLevel 3) is not
  offered, so its five shaders and the load-counter UAV go with it.
- New `assao_gcc_compat.h` / `.cpp`: MinGW-w64's `d3d11.h` lacks some `CD3D11_*` helpers, and
  declares three constructors it never defines. The new files supply those, plus the DXBC table.

## The shaders

`tools/ao_shaders.py` (fxc 10.0.19041, `python -I`) compiles `ASSAO.hlsl` into
`src/render/common/ao/shaders/<entry>.h` with exactly the defines and flags `ASSAODX11.cpp` passes to D3DCompile:
`VSMain` as vs_5_0 and 16 ps_5_0 entries. ps_5_0 needs feature level 11_0, so the extension row is
gated on it. Rerun the tool after any `.hlsl` change. It also removes headers that no entry writes
any more.

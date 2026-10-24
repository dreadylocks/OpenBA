#pragma once
// src/ext/compress_ao/compress_ao.h -- ext.compress_ao (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/COMPRESSAO.md, issue #13). Retail loads the baked
// per-material ambient-occlusion maps as R8_UNORM textures: 7,505 of them, 2.08 GB, on a MegaMod
// 7-part boot. With the row on, the renderer backend creates them as BC4_UNORM, the same single
// channel in half the GPU and process memory (-1.1 GB measured); the top level's PSNR is 37.9 dB at
// the worst percentile, 50.8 dB at the median, and the user saw no difference in an A/B.
//
// WHERE IT RUNS. In the backend (src/render/common/compress_ao/compress_ao_backend.h; the DX11 body
// is src/render/dx11/compress_ao/, the encode src/render/common/compress_ao/bc4_chain.h). Boot opens
// the shared derived-data cache (src/ext/derived_cache/) and passes it with the row's value to
// CompressAoBackend::Request, once: the first start after a mod change encodes (~5 s on the loader
// with 8 workers), later starts read the encoded chains back. Never under the faithful profile (the
// row is off there and the cache is never opened).

namespace Ext { namespace CompressAo {

// Boot only (registry_boot.cpp, before the first load): the dialog's toggle is recorded for the
// next start.
void Boot();

}}  // namespace Ext::CompressAo

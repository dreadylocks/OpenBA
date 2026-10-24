#pragma once
// ============================================================================
// src/render/metal/compress_ao/bc4_create.h -- ext.compress_ao, the Metal side
// (docs/extensions/handins/COMPRESSAO.md, issue #13)
// ============================================================================
// NOT RETAIL. The Metal twin of src/render/dx11/compress_ao/bc4_create.h. ImageBufferData::Create
// (backend.mm, its EXT SEAM) asks Requested() and, for a 2D R8 texture seeded from a resource file
// (kind 2, every level of the chain present, the top level a multiple of 4 each way), has
// CompressAo::Encode (render/common/compress_ao/bc4_chain.h) turn its levels into BC4: the descriptor's
// pixel format becomes MTLPixelFormatBC4_RUnorm and each level is uploaded from its encoded bytes
// with the block-row pitch (8 bytes per 4x4 block). Video::ImageBuffer::_format stays 4 (R8): the
// engine never reads these textures back, and Update / Read refuse a converted texture (D3D11 refuses
// its IMMUTABLE one).
//
// DEVICE. BC4 needs a device with BC texture compression (every Intel / AMD Mac; Apple silicon from
// macOS 11, reported by -[MTLDevice supportsBCTextureCompression]). Supported() asks the live device
// once; without it the answer is "not supported" (one line) and the AO maps stay R8.
//
// The backend has no create worker (ext.async_gpu_create is "not supported" on Metal), so the
// conversion runs where the create runs: the loader thread, or the cache lookup alone on a warm start.
// ============================================================================

namespace VideoMetal {
namespace CompressAo {

// The row's boot value (CompressAoBackend::Request), false when the device lacks BC4. One relaxed load.
bool Requested();

}  // namespace CompressAo
}  // namespace VideoMetal

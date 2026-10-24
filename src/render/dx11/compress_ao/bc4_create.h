#pragma once
// ============================================================================
// src/render/dx11/compress_ao/bc4_create.h -- ext.compress_ao, the DX11 side
// (docs/extensions/handins/COMPRESSAO.md, issue #13)
// ============================================================================
// NOT RETAIL. ImageBufferData::Create (imagebufferdata.cpp, its EXT SEAM) asks Requested() and, for
// a mipmapped-or-not 2D R8_UNORM texture seeded from a resource file (usage 2, every level present,
// the top level a multiple of 4 each way), has CompressAo::Encode (render/common/compress_ao/
// bc4_chain.h) turn its levels into BC4: the descriptor's format becomes DXGI_FORMAT_BC4_UNORM, each
// subresource points at its encoded level with the block-row pitch, and the default SRV follows the
// texture's format. Video::ImageBuffer::_format stays 4 (R8): the engine never reads these textures
// back (Lock / Read / Update callers are generated BGRA images). Inside ext.async_gpu_create's loader
// bracket the deferred create copies the R8 levels and its worker converts them (ConvertForCreate):
// the cache lookup and a cold start's encode run off the loader.
// ============================================================================

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <vector>

namespace VideoDX11 {
namespace CompressAo {

// The row's boot value (CompressAoBackend::Request). One relaxed load.
bool Requested();

// The deferred create's conversion (a worker): `count` R8 subresources of a 2D `desc` texture,
// level i `sizes[i]` bytes. True: `*desc` is BC4_UNORM, `subres[i]` points into `*bc4` with the
// block-row pitch and `sizes[i]` is the BC4 level's size. False (a level of another size): nothing
// changed, the texture is created R8 as the loader-side path does.
bool ConvertForCreate(D3D11_TEXTURE2D_DESC* desc, D3D11_SUBRESOURCE_DATA* subres, int count,
                      std::vector<uint64_t>* sizes, uint32_t sourceFormat, std::vector<uint8_t>* bc4);

}  // namespace CompressAo
}  // namespace VideoDX11

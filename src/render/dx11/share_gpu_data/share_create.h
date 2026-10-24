#pragma once
// ============================================================================
// src/render/dx11/share_gpu_data/share_create.h -- ext.share_gpu_data, the DX11 side
// (docs/extensions/handins/SHAREGPUDATA.md, issue #13)
// ============================================================================
// NOT RETAIL. Retail's VideoDX11 makes one D3D11 object per engine object. With the row on, the
// creates of IMMUTABLE, data-seeded objects go through here:
//   - a vertex / index buffer (GpuBuffer_Create, and ext.async_gpu_create's worker), and
//   - a file-seeded texture (ImageBufferData::Create, kind 2: shader resource only, after
//     ext.compress_ao's conversion, so the key is the bytes actually created) with its SRV;
// each is keyed by its full descriptor (the SRV's too), the device, and the XXH64 of every seed byte
// (chained over the subresources with each one's pitch and size), and an equal key hands out the
// object made first (AddRef'd, written where the create would have written it). Samplers stay per
// object. Anything not IMMUTABLE, or whose subresources do not cover what D3D11 reads, is created
// as retail creates it: an immutable object can never be mapped or updated, so no engine object
// can change what another sees (Lock / SetData / Update on one fail exactly as on retail's own
// immutable object).
//
// The table and its reference rules: src/render/common/share_gpu_data/share_table.h. Every user drops
// itself (DropBuffer / DropTexture) before it releases its handle; DeviceData::Shutdown (exit, a
// device-lost recovery, a device rebuild) clears the table after ext.async_gpu_create's drain.
// Hashing runs where the create runs: on the async workers inside the loader bracket, else on the
// creating thread.
// ============================================================================

#include <windows.h>
#include <d3d11.h>

#include <cstdint>

namespace VideoDX11 {
namespace ShareGpuData {

// The row's boot value (ShareGpuDataBackend::Request). One relaxed load.
bool Requested();

// ID3D11Device::CreateBuffer, sharing an IMMUTABLE seeded buffer. Same result and out-value contract.
HRESULT CreateBuffer(ID3D11Device* dev, const D3D11_BUFFER_DESC& desc, const D3D11_SUBRESOURCE_DATA* init,
                     ID3D11Buffer** out);

// ImageBufferData::Create's CreateTexture2D, then (only on success) CreateShaderResourceView with
// `srvDesc` (null = the default view), sharing an IMMUTABLE shader-resource texture. `levelSizes[i]` is
// the byte count at subres[i].pSysMem. Returns the texture's HRESULT when it failed, else the SRV's;
// the out-values are what the two creates write.
HRESULT CreateTexture(ID3D11Device* dev, const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* subres,
                      int subresCount, const uint64_t* levelSizes, const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc,
                      ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv);

// Before a user releases its handle (the buffer destructors, ImageBufferData::ReleaseInterfaces). When
// it was the object's last user, the table's references are released here. One load when idle.
void DropBuffer(ID3D11Buffer* buffer);
void DropTexture(ID3D11Texture2D* texture);

// DeviceData::Shutdown, after the async drain: the table forgets every object and releases its
// references (users keep theirs).
void Clear();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything was shared
// since the last.
void LoadEnd();

}  // namespace ShareGpuData
}  // namespace VideoDX11

// GCC PORT (AOSPIKE): the DXBC table that replaces ASSAODX11.cpp's D3DCompile. The headers are
// written by tools/ao_shaders.py (fxc /Fh, one `const uint8_t g_ao_<entry>[]` each).
#include "assao_gcc_compat.h"

#include "ao/shaders/VSMain.h"
#include "ao/shaders/PSPrepareDepths.h"
#include "ao/shaders/PSPrepareDepthsAndNormals.h"
#include "ao/shaders/PSPrepareDepthsHalf.h"
#include "ao/shaders/PSPrepareDepthsAndNormalsHalf.h"
#include "ao/shaders/PSPrepareDepthMip1.h"
#include "ao/shaders/PSPrepareDepthMip2.h"
#include "ao/shaders/PSPrepareDepthMip3.h"
#include "ao/shaders/PSGenerateQ0.h"
#include "ao/shaders/PSGenerateQ1.h"
#include "ao/shaders/PSGenerateQ2.h"
#include "ao/shaders/PSSmartBlur.h"
#include "ao/shaders/PSSmartBlurWide.h"
#include "ao/shaders/PSNonSmartBlur.h"
#include "ao/shaders/PSApply.h"
#include "ao/shaders/PSNonSmartApply.h"
#include "ao/shaders/PSNonSmartHalfApply.h"

#ifndef _MSC_VER
// MinGW-w64's d3d11.h DECLARES these texture constructors but never defines them (link error
// otherwise). Bodies follow the Windows SDK's d3d11.h: UNKNOWN format / -1 counts are filled
// from the texture's own desc.
CD3D11_RENDER_TARGET_VIEW_DESC::CD3D11_RENDER_TARGET_VIEW_DESC(
    ID3D11Texture2D* tex, D3D11_RTV_DIMENSION dim, DXGI_FORMAT format, UINT mipSlice,
    UINT firstSlice, UINT arraySize) {
    ViewDimension = dim;
    if (format == DXGI_FORMAT_UNKNOWN ||
        (arraySize == (UINT)-1 && (dim == D3D11_RTV_DIMENSION_TEXTURE2DARRAY ||
                                   dim == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY))) {
        D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
        if (format == DXGI_FORMAT_UNKNOWN) format = d.Format;
        if (arraySize == (UINT)-1) arraySize = d.ArraySize - firstSlice;
    }
    Format = format;
    switch (dim) {
    case D3D11_RTV_DIMENSION_TEXTURE2D: Texture2D.MipSlice = mipSlice; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        Texture2DArray.MipSlice = mipSlice; Texture2DArray.FirstArraySlice = firstSlice;
        Texture2DArray.ArraySize = arraySize; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS: break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        Texture2DMSArray.FirstArraySlice = firstSlice; Texture2DMSArray.ArraySize = arraySize; break;
    default: break;
    }
}

CD3D11_SHADER_RESOURCE_VIEW_DESC::CD3D11_SHADER_RESOURCE_VIEW_DESC(
    ID3D11Texture1D* tex, D3D11_SRV_DIMENSION dim, DXGI_FORMAT format, UINT mostDetailedMip,
    UINT mipLevels, UINT firstSlice, UINT arraySize) {
    ViewDimension = dim;
    if (format == DXGI_FORMAT_UNKNOWN || mipLevels == (UINT)-1 ||
        (arraySize == (UINT)-1 && dim == D3D11_SRV_DIMENSION_TEXTURE1DARRAY)) {
        D3D11_TEXTURE1D_DESC d; tex->GetDesc(&d);
        if (format == DXGI_FORMAT_UNKNOWN) format = d.Format;
        if (mipLevels == (UINT)-1) mipLevels = d.MipLevels - mostDetailedMip;
        if (arraySize == (UINT)-1) arraySize = d.ArraySize - firstSlice;
    }
    Format = format;
    switch (dim) {
    case D3D11_SRV_DIMENSION_TEXTURE1D:
        Texture1D.MostDetailedMip = mostDetailedMip; Texture1D.MipLevels = mipLevels; break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        Texture1DArray.MostDetailedMip = mostDetailedMip; Texture1DArray.MipLevels = mipLevels;
        Texture1DArray.FirstArraySlice = firstSlice; Texture1DArray.ArraySize = arraySize; break;
    default: break;
    }
}

CD3D11_SHADER_RESOURCE_VIEW_DESC::CD3D11_SHADER_RESOURCE_VIEW_DESC(
    ID3D11Texture2D* tex, D3D11_SRV_DIMENSION dim, DXGI_FORMAT format, UINT mostDetailedMip,
    UINT mipLevels, UINT firstSlice, UINT arraySize) {
    ViewDimension = dim;
    if (format == DXGI_FORMAT_UNKNOWN ||
        (mipLevels == (UINT)-1 && dim != D3D11_SRV_DIMENSION_TEXTURE2DMS &&
         dim != D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY) ||
        (arraySize == (UINT)-1 && (dim == D3D11_SRV_DIMENSION_TEXTURE2DARRAY ||
                                   dim == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY ||
                                   dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY))) {
        D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
        if (format == DXGI_FORMAT_UNKNOWN) format = d.Format;
        if (mipLevels == (UINT)-1) mipLevels = d.MipLevels - mostDetailedMip;
        if (arraySize == (UINT)-1) {
            arraySize = d.ArraySize - firstSlice;
            if (dim == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY) arraySize /= 6;
        }
    }
    Format = format;
    switch (dim) {
    case D3D11_SRV_DIMENSION_TEXTURE2D:
        Texture2D.MostDetailedMip = mostDetailedMip; Texture2D.MipLevels = mipLevels; break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        Texture2DArray.MostDetailedMip = mostDetailedMip; Texture2DArray.MipLevels = mipLevels;
        Texture2DArray.FirstArraySlice = firstSlice; Texture2DArray.ArraySize = arraySize; break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS: break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
        Texture2DMSArray.FirstArraySlice = firstSlice; Texture2DMSArray.ArraySize = arraySize; break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
        TextureCube.MostDetailedMip = mostDetailedMip; TextureCube.MipLevels = mipLevels; break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        TextureCubeArray.MostDetailedMip = mostDetailedMip; TextureCubeArray.MipLevels = mipLevels;
        TextureCubeArray.First2DArrayFace = firstSlice; TextureCubeArray.NumCubes = arraySize; break;
    default: break;
    }
}
#endif

namespace assao_gcc {
namespace {
struct Entry { const char* name; const void* data; size_t size; };
#define AO_BLOB(n) { #n, g_ao_##n, sizeof(g_ao_##n) }
const Entry kBlobs[] = {   // AOCORE: the 5 Adaptive-only entries are gone (ASSAO.h)
    AO_BLOB(VSMain), AO_BLOB(PSPrepareDepths), AO_BLOB(PSPrepareDepthsAndNormals),
    AO_BLOB(PSPrepareDepthsHalf), AO_BLOB(PSPrepareDepthsAndNormalsHalf),
    AO_BLOB(PSPrepareDepthMip1), AO_BLOB(PSPrepareDepthMip2), AO_BLOB(PSPrepareDepthMip3),
    AO_BLOB(PSGenerateQ0), AO_BLOB(PSGenerateQ1), AO_BLOB(PSGenerateQ2),
    AO_BLOB(PSSmartBlur), AO_BLOB(PSSmartBlurWide),
    AO_BLOB(PSNonSmartBlur), AO_BLOB(PSApply), AO_BLOB(PSNonSmartApply),
    AO_BLOB(PSNonSmartHalfApply),
};
#undef AO_BLOB
}  // namespace

bool FindBlob(const char* entry, const void** data, size_t* size) {
    for (const Entry& e : kBlobs) {
        if (strcmp(e.name, entry) == 0) { *data = e.data; *size = e.size; return true; }
    }
    return false;
}
}  // namespace assao_gcc

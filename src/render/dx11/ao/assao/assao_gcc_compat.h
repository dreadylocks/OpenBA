// GCC PORT (AOSPIKE) -- the pieces MSVC's Windows SDK gives ASSAODX11.cpp that MinGW-w64's
// d3d11.h does not: the CD3D11_VIEWPORT / CD3D11_RECT / CD3D11_TEXTURE1D_DESC /
// CD3D11_UNORDERED_ACCESS_VIEW_DESC helpers (only the constructors ASSAODX11.cpp calls), and the
// precompiled-blob table that replaces D3DCompile. Not Intel code; ASSAODX11.cpp keeps its MIT
// header.
#pragma once

#include <d3d11.h>
#include <stddef.h>
#include <string.h>

// MinGW-w64's stdlib.h defines _countof; glibc (the Linux build over dxvk-native) does not.
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef _MSC_VER
struct CD3D11_VIEWPORT : public D3D11_VIEWPORT {
    CD3D11_VIEWPORT(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) {
        TopLeftX = x; TopLeftY = y; Width = w; Height = h; MinDepth = minDepth; MaxDepth = maxDepth;
    }
};
struct CD3D11_RECT : public D3D11_RECT {
    CD3D11_RECT(LONG l, LONG t, LONG r, LONG b) { left = l; top = t; right = r; bottom = b; }
};
struct CD3D11_TEXTURE1D_DESC : public D3D11_TEXTURE1D_DESC {
    CD3D11_TEXTURE1D_DESC(DXGI_FORMAT format, UINT width, UINT arraySize = 1, UINT mipLevels = 0,
                          UINT bindFlags = D3D11_BIND_SHADER_RESOURCE,
                          D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpuAccess = 0, UINT misc = 0) {
        Width = width; MipLevels = mipLevels; ArraySize = arraySize; Format = format;
        Usage = usage; BindFlags = bindFlags; CPUAccessFlags = cpuAccess; MiscFlags = misc;
    }
};
struct CD3D11_UNORDERED_ACCESS_VIEW_DESC : public D3D11_UNORDERED_ACCESS_VIEW_DESC {
    // the two forms ASSAODX11.cpp uses: (Texture1D, TEXTURE1D) and (Texture2D, TEXTURE2D), the
    // format, mip slice 0, first slice 0 and array size taken from the texture (MSVC's semantics)
    CD3D11_UNORDERED_ACCESS_VIEW_DESC(ID3D11Texture1D* tex, D3D11_UAV_DIMENSION dim) {
        D3D11_TEXTURE1D_DESC d; tex->GetDesc(&d);
        memset(static_cast<D3D11_UNORDERED_ACCESS_VIEW_DESC*>(this), 0, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
        Format = d.Format; ViewDimension = dim;
        if (dim == D3D11_UAV_DIMENSION_TEXTURE1DARRAY) Texture1DArray.ArraySize = d.ArraySize;
    }
    CD3D11_UNORDERED_ACCESS_VIEW_DESC(ID3D11Texture2D* tex, D3D11_UAV_DIMENSION dim) {
        D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
        memset(static_cast<D3D11_UNORDERED_ACCESS_VIEW_DESC*>(this), 0, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
        Format = d.Format; ViewDimension = dim;
        if (dim == D3D11_UAV_DIMENSION_TEXTURE2DARRAY) Texture2DArray.ArraySize = d.ArraySize;
    }
};
#endif

namespace assao_gcc {
bool FindBlob(const char* entry, const void** data, size_t* size);
}

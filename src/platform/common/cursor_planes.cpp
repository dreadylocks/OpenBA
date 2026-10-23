// ============================================================================
// src/platform/common/cursor_planes.cpp -- a Win32 AND / XOR cursor as ARGB pixels
// ============================================================================
// Phase D.4 SDL step W5. The table is in cursor_planes.h.
// ============================================================================

#include "cursor_planes.h"

namespace Platform {
namespace Cursor {

int32_t PlaneStride(int32_t width) {
    return ((width + 15) / 16) * 2;
}

void PlanesToArgb(const uint8_t* andBytes, const uint8_t* xorBytes, int32_t width,
                  int32_t height, uint32_t inverted, uint32_t* argb, int32_t pitchPixels) {
    const int32_t stride = PlaneStride(width);

    for (int32_t y = 0; y < height; ++y) {
        uint32_t* const out = argb + static_cast<int64_t>(y) * pitchPixels;
        for (int32_t x = 0; x < width; ++x) {
            const int32_t  byte = y * stride + x / 8;
            const uint8_t  bit  = static_cast<uint8_t>(0x80u >> (x % 8));
            const bool     a    = (andBytes[byte] & bit) != 0;
            const bool     o    = (xorBytes[byte] & bit) != 0;
            out[x] = a ? (o ? inverted : kArgbTransparent)
                       : (o ? kArgbWhite : kArgbBlack);
        }
    }
}

void TexelsToPlanes(const uint32_t* texels, int32_t width, int32_t height,
                    int32_t pitchPixels, uint8_t* andPlane, uint8_t* xorPlane) {
    const int32_t stride = PlaneStride(width);
    for (int32_t i = 0; i < stride * height; ++i) {
        andPlane[i] = 0;
        xorPlane[i] = 0;
    }
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const uint32_t t    = texels[static_cast<int64_t>(y) * pitchPixels + x];
            const int32_t  byte = y * stride + x / 8;
            const uint8_t  bit  = static_cast<uint8_t>(0x80u >> (x % 8));
            if ((t & 0xff000000u) == 0)   andPlane[byte] |= bit;
            else if ((t & 0xffu) != 0)    xorPlane[byte] |= bit;
        }
    }
}

void UpscaleNearest(const uint32_t* src, int32_t width, int32_t height, int32_t factor,
                    uint32_t* dst, int32_t dstPitchPixels) {
    for (int32_t y = 0; y < height * factor; ++y)
        for (int32_t x = 0; x < width * factor; ++x)
            dst[static_cast<int64_t>(y) * dstPitchPixels + x] = src[(y / factor) * width + x / factor];
}

}  // namespace Cursor
}  // namespace Platform

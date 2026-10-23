#pragma once
// ============================================================================
// src/platform/common/cursor_planes.h -- a Win32 AND / XOR cursor as ARGB pixels
// ============================================================================
// Phase D.4 SDL step W5 (plan §A.1, RENDER-3 hand-in). VideoDX11::DeviceData slot 8
// (src/render/dx11/devicedata.cpp, retail 0x180005450) builds the game cursor as the two
// monochrome planes Win32's CreateCursor takes. SDL creates a cursor from an ARGB8888
// surface (SDL_CreateColorCursor), so the SDL backend (src/platform/sdl/cursor_sdl.cpp)
// converts the planes here. Pure: no OS header, no SDL header
// (tests/unit/cursor_planes_test.cpp).
//
// Win32's meaning of the planes, per pixel (the leftmost pixel is the MSB of a row's
// first byte; each row is padded to a WORD, as every Win32 monochrome bitmap row is):
//
//   AND  XOR   screen
//    0    0    black
//    0    1    white
//    1    0    transparent (the screen shows through)
//    1    1    inverted (the screen pixel's colour is inverted)
//
// Retail's builder never produces AND = 1 with XOR = 1 (its classification is
// exclusive: alpha 0 -> AND, else blue != 0 -> XOR, else neither). An ARGB pixel cannot
// say "invert", so the caller passes the value it wants for that case: SDL's own
// SDL_CreateCursor uses 0x00FFFFFF on Windows (its Windows backend turns that value back
// into an inverting pixel) and opaque black elsewhere; cursor_sdl.cpp does the same.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Cursor {

constexpr uint32_t kArgbTransparent = 0x00000000u;
constexpr uint32_t kArgbBlack       = 0xFF000000u;
constexpr uint32_t kArgbWhite       = 0xFFFFFFFFu;

// Bytes per row of a Win32 monochrome plane `width` pixels wide (WORD-aligned).
int32_t PlaneStride(int32_t width);

// Convert the two planes (each PlaneStride(width) * height bytes) into width * height
// ARGB8888 pixels, row by row, `pitchPixels` pixels apart (>= width). `inverted` is the
// value written for AND = 1 with XOR = 1.
void PlanesToArgb(const uint8_t* andPlane, const uint8_t* xorPlane, int32_t width,
                  int32_t height, uint32_t inverted, uint32_t* argb, int32_t pitchPixels);

// Retail's classification of a 32-bit texel (VideoDX11 slot 8, 0x180005520): alpha 0 ->
// AND bit (transparent); else blue byte != 0 -> XOR bit (white); else neither (black).
// `texels` is width * height BGRA8 pixels (alpha in the top byte, blue in the low byte),
// `pitchPixels` apart; the planes are PlaneStride(width) * height bytes each and are
// overwritten (zeroed first). Same result as devicedata.cpp's bit loop + byte reversal.
void TexelsToPlanes(const uint32_t* texels, int32_t width, int32_t height,
                    int32_t pitchPixels, uint8_t* andPlane, uint8_t* xorPlane);

// Nearest-neighbour upscale by an integer `factor` (>= 1): `dst` is (width * factor) x
// (height * factor) pixels, `dstPitchPixels` apart. A 1-bit cursor stays crisp.
void UpscaleNearest(const uint32_t* src, int32_t width, int32_t height, int32_t factor,
                    uint32_t* dst, int32_t dstPitchPixels);

}  // namespace Cursor
}  // namespace Platform

// ============================================================================
// tests/unit/cursor_planes_test.cpp -- a Win32 AND / XOR cursor as ARGB pixels
// ============================================================================
// Phase D.4 SDL step W5. Platform::Cursor::PlanesToArgb
// (src/platform/common/cursor_planes.cpp) is what the SDL cursor backend hands
// SDL_CreateColorCursor. Checked: the four AND / XOR combinations, the bit order (the
// leftmost pixel is the MSB of a row's first byte), the WORD-aligned row stride, the
// output pitch, and the planes VideoDX11::DeviceData slot 8 builds from a texture
// (src/render/dx11/devicedata.cpp, retail 0x180005450), replayed here byte for byte:
// alpha 0 -> transparent, blue != 0 -> white, otherwise black. No SDL (D4_RULES C7).
// ============================================================================
#include "unit.h"

#include <cstdint>
#include <cstring>

#include "cursor_planes.h"

using Platform::Cursor::PlanesToArgb;
using Platform::Cursor::PlaneStride;
using Platform::Cursor::kArgbBlack;
using Platform::Cursor::kArgbTransparent;
using Platform::Cursor::kArgbWhite;

namespace {

constexpr uint32_t kInverted = 0x00FFFFFFu;

// Slot 8's plane builder (devicedata.cpp SetImageBuffer), the same loop: bit 31 is the
// row's first pixel, then each row's dword is byte-reversed, so in memory the first byte
// of a row holds pixels 0..7 with pixel 0 in its MSB.
void BuildLikeSlot8(const uint32_t* pixels, uint32_t andPlane[32], uint32_t xorPlane[32]) {
    std::memset(andPlane, 0, 32 * sizeof(uint32_t));
    std::memset(xorPlane, 0, 32 * sizeof(uint32_t));
    int flat = 0;
    for (int row = 0; row < 32; ++row) {
        for (int bit = 31; bit >= 0; --bit, ++flat) {
            const uint32_t texel = pixels[flat];
            const uint32_t mask  = 1u << bit;
            if ((texel & 0xff000000u) == 0)   andPlane[row] |= mask;
            else if ((texel & 0xffu) != 0)    xorPlane[row] |= mask;
        }
        const uint32_t a = andPlane[row];
        andPlane[row] = (a >> 24) | ((a >> 8) & 0xff00u) | ((a << 8) & 0xff0000u) | (a << 24);
        const uint32_t x = xorPlane[row];
        xorPlane[row] = (x >> 24) | ((x >> 8) & 0xff00u) | ((x << 8) & 0xff0000u) | (x << 24);
    }
}

}  // namespace

UNIT_TEST(cursor_planes_stride_is_word_aligned) {
    CHECK_EQ(PlaneStride(32), 4);
    CHECK_EQ(PlaneStride(16), 2);
    CHECK_EQ(PlaneStride(8), 2);
    CHECK_EQ(PlaneStride(1), 2);
    CHECK_EQ(PlaneStride(24), 4);
}

UNIT_TEST(cursor_planes_four_combinations_msb_first) {
    // One 16-pixel row (stride 2). Pixels 0..3: AND/XOR = 00, 01, 10, 11; the rest
    // AND = 1, XOR = 0 (transparent).
    const uint8_t andPlane[2] = {0x3F, 0xFF};   // 0011 1111
    const uint8_t xorPlane[2] = {0x50, 0x00};   // 0101 0000
    uint32_t out[16];
    PlanesToArgb(andPlane, xorPlane, 16, 1, kInverted, out, 16);
    CHECK_EQ(out[0], kArgbBlack);         // AND 0, XOR 0
    CHECK_EQ(out[1], kArgbWhite);         // AND 0, XOR 1
    CHECK_EQ(out[2], kArgbTransparent);   // AND 1, XOR 0
    CHECK_EQ(out[3], kInverted);          // AND 1, XOR 1
    for (int x = 4; x < 16; ++x) CHECK_EQ(out[x], kArgbTransparent);
}

UNIT_TEST(cursor_planes_rows_and_pitch) {
    // 8 x 2: row 0 all black, row 1 all white; the output pitch leaves a gap column.
    const uint8_t andPlane[4] = {0x00, 0x00, 0x00, 0x00};
    const uint8_t xorPlane[4] = {0x00, 0x00, 0xFF, 0x00};
    uint32_t out[2 * 9];
    for (uint32_t& p : out) p = 0x12345678u;
    PlanesToArgb(andPlane, xorPlane, 8, 2, kInverted, out, 9);
    for (int x = 0; x < 8; ++x) {
        CHECK_EQ(out[x], kArgbBlack);
        CHECK_EQ(out[9 + x], kArgbWhite);
    }
    CHECK_EQ(out[8], 0x12345678u);         // the pitch gap is not written
    CHECK_EQ(out[17], 0x12345678u);
}

UNIT_TEST(cursor_planes_slot8_texture_round_trip) {
    // A 32 x 32 BGRA texture with every class of texel in a pattern that differs per row
    // and column, built into planes exactly as slot 8 builds them, then converted.
    uint32_t pixels[32 * 32];
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int k = (x * 7 + y * 3) % 5;
            uint32_t t;
            switch (k) {
                case 0:  t = 0x00FFFFFFu; break;   // alpha 0: transparent
                case 1:  t = 0xFF0000FFu; break;   // blue != 0: white
                case 2:  t = 0xFFFF0000u; break;   // pure red: blue 0 -> black (retail)
                case 3:  t = 0x80000001u; break;   // partial alpha, blue 1: white
                default: t = 0xFF00FF00u; break;   // green: black
            }
            pixels[y * 32 + x] = t;
        }
    }
    uint32_t andPlane[32];
    uint32_t xorPlane[32];
    BuildLikeSlot8(pixels, andPlane, xorPlane);

    // The bytes CreateCursor reads: the dword rows as they lie in memory.
    uint8_t andBytes[32 * 4];
    uint8_t xorBytes[32 * 4];
    std::memcpy(andBytes, andPlane, sizeof(andBytes));
    std::memcpy(xorBytes, xorPlane, sizeof(xorBytes));

    uint32_t out[32 * 32];
    PlanesToArgb(andBytes, xorBytes, 32, 32, kInverted, out, 32);

    int transparent = 0, white = 0, black = 0;
    for (int i = 0; i < 32 * 32; ++i) {
        const uint32_t t = pixels[i];
        const uint32_t want = ((t & 0xff000000u) == 0) ? kArgbTransparent
                            : ((t & 0xffu) != 0)       ? kArgbWhite
                                                       : kArgbBlack;
        CHECK_EQ(out[i], want);
        if (want == kArgbTransparent) ++transparent;
        else if (want == kArgbWhite)  ++white;
        else                          ++black;
    }
    // Effectiveness: every class occurred, and slot 8 never makes an inverting pixel.
    CHECK(transparent > 0);
    CHECK(white > 0);
    CHECK(black > 0);
    for (int i = 0; i < 32 * 32; ++i) CHECK(out[i] != kInverted);
}

// TexelsToPlanes (what the Metal backend feeds the SDL cursor) must equal slot 8's loop
// byte for byte, on a texel image that exercises all three classes, and on a non-32 width.
UNIT_TEST(cursor_planes_texels_to_planes_matches_slot8) {
    uint32_t pixels[32 * 32];
    for (int i = 0; i < 32 * 32; ++i) {
        const int k = (i * 7 + i / 32) % 5;
        pixels[i] = (k == 0) ? 0x00000000u : (k < 3) ? 0xFFFFFFFFu : 0xFF000000u;
    }
    uint32_t a32[32], x32[32];
    BuildLikeSlot8(pixels, a32, x32);
    uint8_t a[128], x[128];
    Platform::Cursor::TexelsToPlanes(pixels, 32, 32, 32, a, x);
    CHECK(std::memcmp(a, a32, 128) == 0);
    CHECK(std::memcmp(x, x32, 128) == 0);

    // width 10: stride 2, pixel 9 is the second byte's bit 6
    uint32_t t[10 * 2] = {};
    for (auto& p : t) p = 0xFF000000u;
    t[9] = 0x00000000u;           // row 0: transparent -> AND
    t[10 + 8] = 0xFF0000FFu;      // row 1: white -> XOR
    uint8_t a2[4], x2[4];
    Platform::Cursor::TexelsToPlanes(t, 10, 2, 10, a2, x2);
    CHECK_EQ(a2[0], 0);
    CHECK_EQ(a2[1], 0x40);
    CHECK_EQ(a2[2], 0);
    CHECK_EQ(a2[3], 0);
    CHECK_EQ(x2[0], 0);
    CHECK_EQ(x2[1], 0);
    CHECK_EQ(x2[2], 0);
    CHECK_EQ(x2[3], 0x80);
}

UNIT_TEST(cursor_planes_upscale_nearest_2x) {
    const uint32_t src[2 * 2] = {1, 2, 3, 4};
    uint32_t dst[4 * 4];
    Platform::Cursor::UpscaleNearest(src, 2, 2, 2, dst, 4);
    const uint32_t want[16] = {1,1,2,2, 1,1,2,2, 3,3,4,4, 3,3,4,4};
    CHECK(std::memcmp(dst, want, sizeof(want)) == 0);
}

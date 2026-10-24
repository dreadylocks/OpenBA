#pragma once
// ============================================================================
// src/render/common/bc4/bc4.h -- a BC4_UNORM (single-channel block) encoder and decoder
// ============================================================================
// NOT RETAIL. Pure: no D3D, no allocation. Issue #13 (MegaMod memory): the baked per-material
// ambient-occlusion maps load as R8_UNORM; BC4 holds the same channel in half the bytes.
//
// THE FORMAT. A 4x4 block is 8 bytes: two endpoints e0, e1, then sixteen 3-bit indices, pixel
// (x, y) at bit 16 + 3 * (4y + x) of the little-endian 64-bit word. e0 > e1 selects the eight-value
// palette {e0, e1, six interpolants 6:1 .. 1:6 / 7}; e0 <= e1 the six-value one {e0, e1, four
// interpolants 4:1 .. 1:4 / 5, 0, 255}. The hardware interpolates at more than 8 bits; the decoder
// here rounds each palette entry to the nearest byte, within half a step of what a GPU returns.
//
// THE SURFACE. A level of w x h pixels is max(1, ceil(w / 4)) x max(1, ceil(h / 4)) blocks, rows of
// blocks packed tight (row pitch = blocks across * 8): the D3D layout of a BC level, including the
// 2x2 / 1x1 mips that still occupy one whole block. Texels of an edge block that fall outside the
// level are not part of the fit; their indices are 0.
//
// THE SEARCH, per block and per palette mode, keeping the lower squared error of the two:
//   eight-value: start at (min, max); least-squares refit of the endpoints to the chosen indices
//     while that lowers the error (at most four times); then one walk of each endpoint by +-1
//     around the best pair (the eight neighbours).
//   six-value (only when the block reaches 0 or 255, so the fixed entries can serve exactly):
//     start at the min / max of the pixels that are not 0 / 255; the same refit and walk.
// A flat block, or one whose range is under eight steps, is exact at once. Throughput (-O2, one
// thread, 1024 x 1024 AO-like content): ~75 MB/s on smooth shading, ~35 MB/s with heavy grain;
// the search gains 1..2.5 dB PSNR over plain min / max endpoints.
// ============================================================================
#include <cstddef>
#include <cstdint>

namespace Bc4 {

// Bytes of one encoded level of w x h pixels (w, h >= 1).
size_t EncodedSize(int w, int h);
// Bytes of one row of blocks.
int BlockRowPitch(int w);

// Encodes w x h pixels (row pitch `pitch` bytes) into EncodedSize(w, h) bytes at dst.
void EncodeBC4(const uint8_t* src, int w, int h, int pitch, uint8_t* dst);
// Decodes EncodedSize(w, h) bytes into w x h pixels at dst (row pitch `dstPitch` bytes).
void DecodeBC4(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch);

// One block: `pixels` holds 16 values in row order; `valid` is a 16-bit mask of the texels that
// belong to the level (bit 4y + x). Out: 8 bytes.
void EncodeBlock(const uint8_t pixels[16], unsigned valid, uint8_t out[8]);
void DecodeBlock(const uint8_t in[8], uint8_t pixels[16]);

}  // namespace Bc4

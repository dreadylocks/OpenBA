// ============================================================================
// src/render/common/bc4/bc4.cpp -- BC4_UNORM encoder and decoder (bc4.h has the format and search)
// ============================================================================
#include "bc4/bc4.h"

#include <cmath>
#include <cstring>

namespace Bc4 {
namespace {

// The palette both modes decode to; e0 > e1 picks the eight-value one.
void Palette(int e0, int e1, int pal[8]) {
    pal[0] = e0;
    pal[1] = e1;
    if (e0 > e1) {
        for (int k = 1; k <= 6; ++k)
            pal[k + 1] = ((7 - k) * e0 + k * e1 + 3) / 7;
    } else {
        for (int k = 1; k <= 4; ++k)
            pal[k + 1] = ((5 - k) * e0 + k * e1 + 2) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
}

// Nearest palette entry for each of the n pixels; returns the summed squared error. Nearest by
// absolute distance is nearest by squared distance; the distance and the entry share one 16-bit key
// (distance << 3 | entry) so the minimum is branch-free, vectorises on plain SSE2 (pminsw) and the
// lowest entry wins a tie.
int Assign(const int* pix, int n, const int pal[8], uint8_t* idx) {
    int16_t px[16];
    int16_t keys[16];
    for (int i = 0; i < 16; ++i)
        px[i] = static_cast<int16_t>(pix[i]);
    for (int i = 0; i < 16; ++i)
        keys[i] = 0x7fff;
    for (int k = 0; k < 8; ++k) {
        const int16_t entry = static_cast<int16_t>(pal[k]);
        for (int i = 0; i < 16; ++i) {
            const int16_t d = static_cast<int16_t>(px[i] - entry);
            const int16_t a = d < 0 ? static_cast<int16_t>(-d) : d;
            const int16_t key = static_cast<int16_t>(a << 3 | k);
            keys[i] = key < keys[i] ? key : keys[i];
        }
    }
    int total = 0;
    for (int i = 0; i < n; ++i) {
        const int a = keys[i] >> 3;
        idx[i] = static_cast<uint8_t>(keys[i] & 7);
        total += a * a;
    }
    return total;
}

struct Fit {
    int e0;
    int e1;
    int err;
    uint8_t idx[16];
};

int Clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// Endpoints in mode order: the eight-value mode stores (hi, lo) with hi > lo, the six-value mode
// (lo, hi) with lo <= hi. Returns false when the pair cannot be expressed in that mode.
bool ModePair(bool eight, int lo, int hi, int* e0, int* e1) {
    lo = Clamp255(lo);
    hi = Clamp255(hi);
    if (lo > hi) {
        const int t = lo;
        lo = hi;
        hi = t;
    }
    if (eight) {
        if (hi == lo) return false;
        *e0 = hi;
        *e1 = lo;
    } else {
        *e0 = lo;
        *e1 = hi;
    }
    return true;
}

bool Try(bool eight, int lo, int hi, const int* pix, int n, Fit* best) {
    int e0;
    int e1;
    if (!ModePair(eight, lo, hi, &e0, &e1)) return false;
    int pal[8];
    Palette(e0, e1, pal);
    uint8_t idx[16];
    const int err = Assign(pix, n, pal, idx);
    if (err >= best->err) return false;
    best->e0 = e0;
    best->e1 = e1;
    best->err = err;
    std::memcpy(best->idx, idx, static_cast<size_t>(n));
    return true;
}

// The weight of the high endpoint each index carries, in 35ths (the common multiple of the two
// modes' 7ths and 5ths), or -1 for the six-value mode's fixed 0 / 255 entries.
const int kHiWeight35[2][8] = {
    { 0, 35, 7, 14, 21, 28, -1, -1 },    // six-value: e0 = lo, e1 = hi, then 1:4 .. 4:1 / 5
    { 35, 0, 30, 25, 20, 15, 10, 5 },    // eight-value: e0 = hi, e1 = lo, then 6:1 .. 1:6 / 7
};

// Least-squares endpoints for the fit's current indices.
bool Refit(bool eight, const Fit& fit, const int* pix, int n, int* lo, int* hi) {
    const int* const weights = kHiWeight35[eight ? 1 : 0];
    int64_t a = 0;
    int64_t b = 0;
    int64_t c = 0;
    int64_t x = 0;
    int64_t y = 0;
    for (int i = 0; i < n; ++i) {
        const int f = weights[fit.idx[i]];
        if (f < 0) continue;
        const int g = 35 - f;
        a += g * g;
        b += f * g;
        c += f * f;
        x += g * pix[i];
        y += f * pix[i];
    }
    // The 35ths cancel: lo = (c x - b y) / det * 35, with det in 35^4 and x, y in 35.
    const int64_t det = a * c - b * b;
    if (det == 0) return false;
    const double scale = 35.0 / static_cast<double>(det);
    *lo = static_cast<int>(std::lround(static_cast<double>(c * x - b * y) * scale));
    *hi = static_cast<int>(std::lround(static_cast<double>(a * y - b * x) * scale));
    return true;
}

void Search(bool eight, int lo, int hi, const int* pix, int n, Fit* best) {
    Fit mode;
    mode.err = 1 << 30;
    Try(eight, lo, hi, pix, n, &mode);
    if (mode.err == 1 << 30) return;

    for (int iter = 0; iter < 4 && mode.err > 0; ++iter) {
        int rlo;
        int rhi;
        if (!Refit(eight, mode, pix, n, &rlo, &rhi)) break;
        if (!Try(eight, rlo, rhi, pix, n, &mode)) break;
    }

    // One walk of each endpoint by one around the refit pair (a second round buys < 0.1 dB on
    // AO-like content for a third more time).
    for (int round = 0; round < 1 && mode.err > 0; ++round) {
        const int curLo = eight ? mode.e1 : mode.e0;
        const int curHi = eight ? mode.e0 : mode.e1;
        bool improved = false;
        for (int dl = -1; dl <= 1; ++dl) {
            for (int dh = -1; dh <= 1; ++dh) {
                if (dl == 0 && dh == 0) continue;
                if (Try(eight, curLo + dl, curHi + dh, pix, n, &mode)) improved = true;
            }
        }
        if (!improved) break;
    }

    if (mode.err < best->err) *best = mode;
}

}  // namespace

size_t EncodedSize(int w, int h) {
    const int bw = w > 0 ? (w + 3) / 4 : 1;
    const int bh = h > 0 ? (h + 3) / 4 : 1;
    return static_cast<size_t>(bw) * static_cast<size_t>(bh) * 8;
}

int BlockRowPitch(int w) {
    const int bw = w > 0 ? (w + 3) / 4 : 1;
    return bw * 8;
}

void EncodeBlock(const uint8_t pixels[16], unsigned valid, uint8_t out[8]) {
    int pix[16] = {};   // Assign reads all 16; the unused tail stays 0
    int slot[16];
    int n = 0;
    int mn = 255;
    int mx = 0;
    for (int i = 0; i < 16; ++i) {
        if (((valid >> i) & 1u) == 0) continue;
        const int p = pixels[i];
        slot[n] = i;
        pix[n] = p;
        ++n;
        if (p < mn) mn = p;
        if (p > mx) mx = p;
    }

    Fit best;
    best.err = 1 << 30;
    if (n == 0 || mn == mx) {
        // Flat (or empty): the six-value mode with e0 == e1 is exact at index 0.
        best.e0 = n == 0 ? 0 : mn;
        best.e1 = best.e0;
        best.err = 0;
        std::memset(best.idx, 0, sizeof best.idx);
    } else {
        Search(true, mn, mx, pix, n, &best);
        if (best.err > 0 && (mn == 0 || mx == 255)) {
            int inLo = 255;
            int inHi = 0;
            for (int i = 0; i < n; ++i) {
                if (pix[i] == 0 || pix[i] == 255) continue;
                if (pix[i] < inLo) inLo = pix[i];
                if (pix[i] > inHi) inHi = pix[i];
            }
            if (inLo > inHi) {   // only 0s and 255s: both are fixed entries
                inLo = 0;
                inHi = 0;
            }
            Search(false, inLo, inHi, pix, n, &best);
        }
    }

    uint64_t bits = 0;
    for (int i = 0; i < n; ++i)
        bits |= static_cast<uint64_t>(best.idx[i] & 7) << (3 * slot[i]);
    out[0] = static_cast<uint8_t>(best.e0);
    out[1] = static_cast<uint8_t>(best.e1);
    for (int b = 0; b < 6; ++b)
        out[2 + b] = static_cast<uint8_t>(bits >> (8 * b));
}

void DecodeBlock(const uint8_t in[8], uint8_t pixels[16]) {
    int pal[8];
    Palette(in[0], in[1], pal);
    uint64_t bits = 0;
    for (int b = 0; b < 6; ++b)
        bits |= static_cast<uint64_t>(in[2 + b]) << (8 * b);
    for (int i = 0; i < 16; ++i)
        pixels[i] = static_cast<uint8_t>(pal[(bits >> (3 * i)) & 7]);
}

void EncodeBC4(const uint8_t* src, int w, int h, int pitch, uint8_t* dst) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            uint8_t block[16];
            unsigned valid = 0;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x;
                    const int py = by * 4 + y;
                    const bool in = px < w && py < h;
                    block[y * 4 + x] = in ? src[static_cast<size_t>(py) * pitch + px] : 0;
                    if (in) valid |= 1u << (y * 4 + x);
                }
            }
            EncodeBlock(block, valid, dst + (static_cast<size_t>(by) * bw + bx) * 8);
        }
    }
}

void DecodeBC4(const uint8_t* src, int w, int h, uint8_t* dst, int dstPitch) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            uint8_t block[16];
            DecodeBlock(src + (static_cast<size_t>(by) * bw + bx) * 8, block);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const int px = bx * 4 + x;
                    const int py = by * 4 + y;
                    if (px < w && py < h) dst[static_cast<size_t>(py) * dstPitch + px] = block[y * 4 + x];
                }
            }
        }
    }
}

}  // namespace Bc4

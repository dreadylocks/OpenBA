// ============================================================================
// tests/unit/bc4_test.cpp -- src/render/common/bc4: the BC4_UNORM encoder and decoder
// ============================================================================
//   * flat blocks and two-value blocks are exact; a 0 / 128 / 255 block is exact (six-value mode);
//   * on random blocks the search never ends worse than plain min / max endpoints;
//   * edge sizes (1x1 .. 7x9): the encoded size is the D3D one, nothing is written past it, texels
//     outside the level do not affect the fit;
//   * a smooth gradient keeps a high PSNR; a hard edge between two flats stays exact;
//   * the single-threaded throughput on a 1024 x 1024 smooth image is printed (not checked).
// ============================================================================
#include "unit.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "bc4/bc4.h"

namespace {

struct Rng {
    uint64_t s;
    uint64_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    int Below(int n) { return static_cast<int>((Next() >> 32) % static_cast<uint64_t>(n)); }
};

uint64_t SquaredError(const uint8_t* a, const uint8_t* b, size_t n) {
    uint64_t sum = 0;
    for (size_t i = 0; i < n; ++i) {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        sum += static_cast<uint64_t>(d * d);
    }
    return sum;
}

double Psnr(uint64_t sse, size_t n) {
    if (sse == 0) return 99.0;
    const double mse = static_cast<double>(sse) / static_cast<double>(n);
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Plain min / max endpoints with nearest indices: the floor the search must not fall below.
uint64_t MinMaxError(const uint8_t px[16]) {
    int mn = 255;
    int mx = 0;
    for (int i = 0; i < 16; ++i) {
        if (px[i] < mn) mn = px[i];
        if (px[i] > mx) mx = px[i];
    }
    if (mn == mx) return 0;
    int pal[8] = { mx, mn };
    for (int k = 1; k <= 6; ++k)
        pal[k + 1] = ((7 - k) * mx + k * mn + 3) / 7;
    uint64_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 1 << 30;
        for (int k = 0; k < 8; ++k) {
            const int d = px[i] - pal[k];
            if (d * d < best) best = d * d;
        }
        sum += static_cast<uint64_t>(best);
    }
    return sum;
}

uint64_t BlockRoundTrip(const uint8_t px[16], uint8_t out[16]) {
    uint8_t enc[8];
    Bc4::EncodeBlock(px, 0xffffu, enc);
    Bc4::DecodeBlock(enc, out);
    return SquaredError(px, out, 16);
}

}  // namespace

UNIT_TEST(bc4_exact_blocks) {
    uint8_t px[16];
    uint8_t out[16];
    for (int v : { 0, 1, 77, 254, 255 }) {
        for (int i = 0; i < 16; ++i)
            px[i] = static_cast<uint8_t>(v);
        CHECK_EQ(BlockRoundTrip(px, out), 0u);
    }
    // Two values: the eight-value endpoints carry both exactly.
    for (int i = 0; i < 16; ++i)
        px[i] = static_cast<uint8_t>((i & 1) ? 10 : 200);
    CHECK_EQ(BlockRoundTrip(px, out), 0u);
    // 0, 128 and 255: exact only in the six-value mode (e0 == e1 == 128, fixed 0 and 255).
    for (int i = 0; i < 16; ++i)
        px[i] = static_cast<uint8_t>(i % 3 == 0 ? 0 : (i % 3 == 1 ? 128 : 255));
    CHECK_EQ(BlockRoundTrip(px, out), 0u);
    // Eight evenly spaced values span the eight-value palette exactly.
    for (int i = 0; i < 16; ++i)
        px[i] = static_cast<uint8_t>(14 + 7 * (i % 8));
    CHECK_EQ(BlockRoundTrip(px, out), 0u);
}

UNIT_TEST(bc4_search_beats_minmax) {
    Rng rng{ 0x9e3779b97f4a7c15ull };
    uint8_t px[16];
    uint8_t out[16];
    uint64_t searched = 0;
    uint64_t plain = 0;
    int worse = 0;
    for (int trial = 0; trial < 20000; ++trial) {
        // Mix wide random blocks, narrow ones (AO-like) and blocks touching 0 / 255.
        const int kind = trial % 3;
        const int base = rng.Below(256);
        const int span = kind == 0 ? 256 : (kind == 1 ? 1 + rng.Below(24) : 64);
        for (int i = 0; i < 16; ++i) {
            int v = kind == 0 ? rng.Below(256) : base + rng.Below(span) - span / 2;
            if (kind == 2 && rng.Below(4) == 0) v = rng.Below(2) ? 0 : 255;
            px[i] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        const uint64_t e = BlockRoundTrip(px, out);
        const uint64_t m = MinMaxError(px);
        if (e > m) ++worse;
        searched += e;
        plain += m;
    }
    CHECK_EQ(worse, 0);
    CHECK(searched < plain);
    std::printf("  bc4 random blocks: searched SSE %llu vs min/max %llu (%.1f%%)\n",
                static_cast<unsigned long long>(searched), static_cast<unsigned long long>(plain),
                100.0 * static_cast<double>(searched) / static_cast<double>(plain));
}

UNIT_TEST(bc4_edge_sizes) {
    const int sizes[][2] = { { 1, 1 }, { 2, 2 }, { 3, 5 }, { 5, 3 }, { 4, 4 }, { 7, 9 }, { 1, 13 }, { 16, 1 } };
    Rng rng{ 12345 };
    for (const auto& sz : sizes) {
        const int w = sz[0];
        const int h = sz[1];
        const size_t blocks = static_cast<size_t>((w + 3) / 4) * static_cast<size_t>((h + 3) / 4);
        CHECK_EQ(Bc4::EncodedSize(w, h), blocks * 8);
        CHECK_EQ(Bc4::BlockRowPitch(w), ((w + 3) / 4) * 8);

        // A pitch wider than the level, with junk past the row end that must not be read.
        const int pitch = w + 3;
        std::vector<uint8_t> src(static_cast<size_t>(pitch) * h, 0xee);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                src[static_cast<size_t>(y) * pitch + x] = static_cast<uint8_t>(100 + rng.Below(40));

        std::vector<uint8_t> enc(Bc4::EncodedSize(w, h) + 16, 0xcd);
        Bc4::EncodeBC4(src.data(), w, h, pitch, enc.data());
        for (size_t i = Bc4::EncodedSize(w, h); i < enc.size(); ++i)
            CHECK_EQ(enc[i], 0xcd);

        std::vector<uint8_t> dec(static_cast<size_t>(w) * h, 0);
        Bc4::DecodeBC4(enc.data(), w, h, dec.data(), w);
        int maxErr = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int d = static_cast<int>(src[static_cast<size_t>(y) * pitch + x]) - dec[static_cast<size_t>(y) * w + x];
                if (std::abs(d) > maxErr) maxErr = std::abs(d);
            }
        }
        // A 40-wide range over eight entries: within a step and a half of the palette.
        CHECK(maxErr <= 5);
    }

    // A 1x1 flat level is exact, and its out-of-level texels do not leak into the endpoints.
    const uint8_t one = 42;
    uint8_t enc[8];
    Bc4::EncodeBC4(&one, 1, 1, 1, enc);
    uint8_t back = 0;
    Bc4::DecodeBC4(enc, 1, 1, &back, 1);
    CHECK_EQ(back, 42);
}

UNIT_TEST(bc4_gradient_psnr) {
    const int w = 256;
    const int h = 256;
    std::vector<uint8_t> src(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>((x + y) / 2);
    std::vector<uint8_t> enc(Bc4::EncodedSize(w, h));
    Bc4::EncodeBC4(src.data(), w, h, w, enc.data());
    std::vector<uint8_t> dec(src.size());
    Bc4::DecodeBC4(enc.data(), w, h, dec.data(), w);
    const double psnr = Psnr(SquaredError(src.data(), dec.data(), src.size()), src.size());
    std::printf("  bc4 gradient PSNR %.2f dB\n", psnr);
    CHECK(psnr > 45.0);
}

UNIT_TEST(bc4_hard_edge) {
    // A vertical edge between two flats through the middle of each block: two values per block.
    const int w = 64;
    const int h = 64;
    std::vector<uint8_t> src(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(((x + 2) / 4 + y / 4) % 2 ? 230 : 15);
    std::vector<uint8_t> enc(Bc4::EncodedSize(w, h));
    Bc4::EncodeBC4(src.data(), w, h, w, enc.data());
    std::vector<uint8_t> dec(src.size());
    Bc4::DecodeBC4(enc.data(), w, h, dec.data(), w);
    CHECK_EQ(SquaredError(src.data(), dec.data(), src.size()), 0u);

    // Black / white with a one-texel grey seam: 0 and 255 come from the six-value mode's fixed
    // entries and the grey from its endpoints, so this too is exact.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(x % 4 == 0 ? 0 : (x % 4 == 1 ? 97 : 255));
    Bc4::EncodeBC4(src.data(), w, h, w, enc.data());
    Bc4::DecodeBC4(enc.data(), w, h, dec.data(), w);
    CHECK_EQ(SquaredError(src.data(), dec.data(), src.size()), 0u);
}

UNIT_TEST(bc4_throughput) {
    // An AO-like 1024 x 1024: smooth low-frequency shading plus a little grain.
    const int w = 1024;
    const int h = 1024;
    std::vector<uint8_t> src(static_cast<size_t>(w) * h);
    Rng rng{ 777 };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double v = 170.0 + 50.0 * std::sin(x * 0.013) * std::cos(y * 0.021) + 20.0 * std::sin((x + y) * 0.07);
            const int g = static_cast<int>(v) + rng.Below(5) - 2;
            src[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
        }
    }
    std::vector<uint8_t> enc(Bc4::EncodedSize(w, h));
    const auto t0 = std::chrono::steady_clock::now();
    const int reps = 3;
    for (int r = 0; r < reps; ++r)
        Bc4::EncodeBC4(src.data(), w, h, w, enc.data());
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count() / reps;
    std::vector<uint8_t> dec(src.size());
    Bc4::DecodeBC4(enc.data(), w, h, dec.data(), w);
    const double psnr = Psnr(SquaredError(src.data(), dec.data(), src.size()), src.size());
    std::printf("  bc4 1024x1024: %.1f ms, %.1f MB/s source, PSNR %.2f dB\n", sec * 1000.0,
                static_cast<double>(src.size()) / (1024.0 * 1024.0) / sec, psnr);
    CHECK(psnr > 35.0);
}

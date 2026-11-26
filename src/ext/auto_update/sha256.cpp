// src/ext/auto_update/sha256.cpp -- NOT RETAIL. SHA-256 (FIPS 180-4); sha256.h.
#include "ext/auto_update/sha256.h"

#include <cstring>

namespace AutoUpdate {

namespace {

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t Rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

}  // namespace

Sha256::Sha256() {
    Reset();
}

void Sha256::Reset() {
    static constexpr uint32_t kInit[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    std::memcpy(h_, kInit, sizeof(h_));
    buffered_ = 0;
    length_ = 0;
}

void Sha256::Block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) | (uint32_t(p[4 * i + 2]) << 8) |
               uint32_t(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
        const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::Add(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    length_ += size;
    if (buffered_ > 0) {
        const size_t take = size < 64 - buffered_ ? size : 64 - buffered_;
        std::memcpy(buffer_ + buffered_, p, take);
        buffered_ += take;
        p += take;
        size -= take;
        if (buffered_ < 64) return;
        Block(buffer_);
        buffered_ = 0;
    }
    while (size >= 64) {
        Block(p);
        p += 64;
        size -= 64;
    }
    std::memcpy(buffer_, p, size);
    buffered_ = size;
}

void Sha256::Finish(uint8_t digest[32]) {
    const uint64_t bits = length_ * 8;
    const uint8_t  one = 0x80;
    Add(&one, 1);
    const uint8_t zero = 0;
    while (buffered_ != 56) Add(&zero, 1);
    uint8_t tail[8];
    for (int i = 0; i < 8; ++i) tail[i] = uint8_t(bits >> (56 - 8 * i));
    Add(tail, 8);
    for (int i = 0; i < 8; ++i) {
        digest[4 * i] = uint8_t(h_[i] >> 24);
        digest[4 * i + 1] = uint8_t(h_[i] >> 16);
        digest[4 * i + 2] = uint8_t(h_[i] >> 8);
        digest[4 * i + 3] = uint8_t(h_[i]);
    }
}

std::string Sha256::Hex(const uint8_t digest[32]) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i) {
        out += kDigits[digest[i] >> 4];
        out += kDigits[digest[i] & 15];
    }
    return out;
}

std::string Sha256::HexOf(const void* data, size_t size) {
    Sha256  s;
    uint8_t d[32];
    s.Add(data, size);
    s.Finish(d);
    return Hex(d);
}

}  // namespace AutoUpdate

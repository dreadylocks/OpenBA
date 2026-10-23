// ============================================================================
// src/ext/neighbour/handshake.cpp -- NOT RETAIL. ext.neighbour's layer-1 fields (handshake.h)
// ============================================================================

#include "ext/neighbour/handshake.h"

#include <cstring>

namespace Neighbour {

namespace {

constexpr uint64_t kP1 = 0x9E3779B185EBCA87ull;
constexpr uint64_t kP2 = 0xC2B2AE3D27D4EB4Full;
constexpr uint64_t kP3 = 0x165667B19E3779F9ull;
constexpr uint64_t kP4 = 0x85EBCA77C2B2AE63ull;
constexpr uint64_t kP5 = 0x27D4EB2F165667C5ull;

inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

inline uint64_t Read64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

inline uint32_t Read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t Round(uint64_t acc, uint64_t input) {
    acc += input * kP2;
    acc = Rotl(acc, 31);
    return acc * kP1;
}

inline uint64_t MergeRound(uint64_t acc, uint64_t val) {
    acc ^= Round(0, val);
    return acc * kP1 + kP4;
}

// "%25" etc. for the three characters the mod list uses as separators.
std::string EscapeName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '%')      out += "%25";
        else if (c == '|') out += "%7C";
        else if (c == ';') out += "%3B";
        else               out += c;
    }
    return out;
}

bool UnescapeName(const std::string& s, std::string& out) {
    out.clear();
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') { out += s[i]; continue; }
        if (i + 2 >= s.size()) return false;
        const std::string code = s.substr(i + 1, 2);
        if (code == "25")      out += '%';
        else if (code == "7C") out += '|';
        else if (code == "3B") out += ';';
        else return false;
        i += 2;
    }
    return true;
}

bool ParseU64(const std::string& s, int base, uint64_t& out) {
    if (s.empty() || s.size() > (base == 16 ? 16u : 20u)) return false;
    uint64_t v = 0;
    for (char c : s) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        if (d < 0 || d >= base) return false;
        const uint64_t b = static_cast<uint64_t>(base);
        if (v > (UINT64_MAX - static_cast<uint64_t>(d)) / b) return false;   // would overflow
        v = v * b + static_cast<uint64_t>(d);
    }
    out = v;
    return true;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(sep, start);
        parts.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return parts;
}

}  // namespace

Xxh64::Xxh64(uint64_t seed) : seed_(seed) {
    v_[0] = seed + kP1 + kP2;
    v_[1] = seed + kP2;
    v_[2] = seed;
    v_[3] = seed - kP1;
}

void Xxh64::Update(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total_ += size;
    if (bufLen_ + size < 32) {
        if (size > 0) std::memcpy(buf_ + bufLen_, p, size);
        bufLen_ += size;
        return;
    }
    if (bufLen_ > 0) {
        const size_t fill = 32 - bufLen_;
        std::memcpy(buf_ + bufLen_, p, fill);
        for (int i = 0; i < 4; ++i) v_[i] = Round(v_[i], Read64(buf_ + 8 * i));
        p += fill;
        size -= fill;
        bufLen_ = 0;
    }
    while (size >= 32) {
        for (int i = 0; i < 4; ++i) v_[i] = Round(v_[i], Read64(p + 8 * i));
        p += 32;
        size -= 32;
    }
    if (size > 0) std::memcpy(buf_, p, size);
    bufLen_ = size;
}

uint64_t Xxh64::Digest() const {
    uint64_t h;
    if (total_ >= 32) {
        h = Rotl(v_[0], 1) + Rotl(v_[1], 7) + Rotl(v_[2], 12) + Rotl(v_[3], 18);
        for (int i = 0; i < 4; ++i) h = MergeRound(h, v_[i]);
    } else {
        h = seed_ + kP5;
    }
    h += total_;
    const uint8_t* p = buf_;
    size_t left = bufLen_;
    while (left >= 8) {
        h ^= Round(0, Read64(p));
        h = Rotl(h, 27) * kP1 + kP4;
        p += 8;
        left -= 8;
    }
    if (left >= 4) {
        h ^= static_cast<uint64_t>(Read32(p)) * kP1;
        h = Rotl(h, 23) * kP2 + kP3;
        p += 4;
        left -= 4;
    }
    while (left > 0) {
        h ^= static_cast<uint64_t>(*p) * kP5;
        h = Rotl(h, 11) * kP1;
        ++p;
        --left;
    }
    h ^= h >> 33;
    h *= kP2;
    h ^= h >> 29;
    h *= kP3;
    h ^= h >> 32;
    return h;
}

uint64_t Xxh64Of(const void* data, size_t size, uint64_t seed) {
    Xxh64 x(seed);
    x.Update(data, size);
    return x.Digest();
}

std::string Hex64(uint64_t v) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[v & 0xF];
        v >>= 4;
    }
    return out;
}

std::string EncodeMods(const std::vector<ModEntry>& mods) {
    std::string out;
    const size_t n = mods.size() < kMaxMods ? mods.size() : kMaxMods;
    for (size_t i = 0; i < n; ++i) {
        const ModEntry& m = mods[i];
        if (i > 0) out += ';';
        out += EscapeName(m.name);
        out += '|';
        out += std::to_string(m.size);
        out += '|';
        if (m.hasHash) out += Hex64(m.hash);
    }
    return out;
}

bool DecodeMods(const std::string& text, std::vector<ModEntry>& out) {
    out.clear();
    if (text.empty()) return true;
    const std::vector<std::string> entries = Split(text, ';');
    if (entries.size() > kMaxMods) return false;
    std::vector<ModEntry> mods;
    for (const std::string& e : entries) {
        const std::vector<std::string> f = Split(e, '|');
        if (f.size() != 3) return false;
        ModEntry m;
        if (!UnescapeName(f[0], m.name) || m.name.empty() || m.name.size() > 260) return false;
        if (!ParseU64(f[1], 10, m.size)) return false;
        if (!f[2].empty()) {
            if (f[2].size() != 16 || !ParseU64(f[2], 16, m.hash)) return false;
            m.hasHash = true;
        }
        mods.push_back(m);
    }
    out = std::move(mods);
    return true;
}

std::string EncodeExts(const std::vector<ExtEntry>& exts) {
    std::string out;
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i > 0) out += ',';
        out += exts[i].id + ":" + std::to_string(exts[i].formatVersion);
    }
    return out;
}

void AddHandshake(Message& m, const LocalInfo& info) {
    m.Set("engine", kEngineId)
     .Set("engineVer", info.engineVersion)
     .Set("simBuild", info.simBuild)
     .Set("dataHash", info.dataHash)
     .Set("mods", EncodeMods(info.mods))
     .Set("exts", EncodeExts(info.exts))
     .SetInt("maxPlayers", kMaxPlayers);
}

}  // namespace Neighbour

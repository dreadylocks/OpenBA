#pragma once
// Pure logic of the Metal shader cache and of locating the converter (no Foundation, no Metal),
// so tests/unit/metal_shader_cache_test.cpp covers it on any host.
//
// A converted shader depends on three things: the DXBC bytes, the converter that produced the
// library, and the Metal / OS it was built for. The cache key names all three, so swapping
// libairconv.dylib (the LGPL replaceability story) or updating macOS never serves a stale entry.
#include <cstdint>
#include <string>
#include <vector>

namespace VideoMetal {
namespace ShaderCache {

// Lowest macOS the backend runs on. Keep equal to LSMinimumSystemVersion in
// packaging/macos/Info.plist (DXMT's shaders target macOS 14).
constexpr int kMinimumMacOSMajor = 14;
constexpr int kMinimumMacOSMinor = 0;

struct ConverterIdentity {
    std::string path;       // the dylib that was actually loaded
    uint64_t size = 0;      // bytes
    int64_t mtimeNs = 0;    // modification time, nanoseconds since the epoch
};

inline uint64_t Fnv1a64(const std::string& text, uint64_t h = 1469598103934665603ull) {
    for (unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

inline std::string Hex16(uint64_t v) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[i] = digits[v & 15];
        v >>= 4;
    }
    return out;
}

// Everything but the DXBC hash, folded to 16 hex digits. Changes when the dylib's path, size or
// mtime changes, and when the OS or Metal language version string changes.
inline std::string Fingerprint(const ConverterIdentity& converter, const std::string& osVersion,
                               const std::string& metalVersion) {
    std::string text = converter.path;
    text += '\n';
    text += std::to_string(converter.size);
    text += '\n';
    text += std::to_string(converter.mtimeNs);
    text += '\n';
    text += osVersion;
    text += '\n';
    text += metalVersion;
    return Hex16(Fnv1a64(text));
}

// <caches>/OpenBA/shaders-<fingerprint>: one folder per fingerprint, so a changed converter
// starts a fresh folder instead of colliding with the old files.
inline std::string EntryDirectory(const std::string& home, const std::string& fingerprint) {
    return home + "/Library/Caches/OpenBA/shaders-" + fingerprint;
}

// The folder the caches live under: the redirected profile (Opt().userProfile, set by the launcher's
// --run-dir) when there is one, else the account's home. Both the shader and the pipeline cache use it.
inline std::string ResolveHome(const std::string& userProfile, const std::string& accountHome) {
    std::string home = userProfile.empty() ? accountHome : userProfile;
    while (home.size() > 1 && home.back() == '/')
        home.pop_back();
    return home;
}

inline std::string EntryFileName(const std::string& dxbcHashHex) {
    return dxbcHashHex + ".metallib";
}

// Where libairconv.dylib is looked for, first match wins: the explicit override (Opt().airconv /
// BANISHED_AIRCONV, empty when unset), then the .app's Frameworks folder, then beside the exe.
inline std::vector<std::string> ConverterSearchOrder(const std::string& overridePath, const std::string& exeDir) {
    std::vector<std::string> order;
    if (!overridePath.empty())
        order.push_back(overridePath);
    order.push_back(exeDir + "/../Frameworks/libairconv.dylib");
    order.push_back(exeDir + "/libairconv.dylib");
    return order;
}

// "14.5.1" -> {14,5,1}; missing parts are 0. False when nothing numeric leads the text.
inline bool ParseVersion(const std::string& text, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = 0;
    for (int part = 0; part < 3; ++part) {
        if (i >= text.size() || text[i] < '0' || text[i] > '9')
            return part > 0;
        int value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            value = value * 10 + (text[i++] - '0');
        out[part] = value;
        if (i < text.size() && text[i] == '.')
            ++i;
        else
            break;
    }
    return true;
}

inline bool OsAtLeast(int major, int minor, int minMajor, int minMinor) {
    return major > minMajor || (major == minMajor && minor >= minMinor);
}

}  // namespace ShaderCache
}  // namespace VideoMetal

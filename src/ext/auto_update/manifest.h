#pragma once
// ============================================================================
// src/ext/auto_update/manifest.h -- NOT RETAIL. ext.auto_update: versions and the update manifest
// ============================================================================
// docs/fork/AUTO_UPDATE.md §4-§5. Pure: no file, no network, no OS header
// (tests/unit/auto_update_test.cpp).
//
// THE VERSION. "X.Y.Z" or "X.Y.Z-suffix" (OPENBA_VERSION, the CMake project VERSION). Compared as
// semver: numbers first, then a version with a suffix sorts BEFORE the same version without one,
// and two suffixes compare by their dot-separated parts (numbers numerically and before words).
//
// THE MANIFEST. One per platform, published with each release as openba-update-<platform>.txt:
//
//   openba-update 1
//   version=1.2.1
//   commit=77a86958f3c1
//   platform=macos-arm64
//   url=https://github.com/dreadylocks/OpenBA/releases/download/v1.2.1/OpenBA-v1.2.1-mac-arm64.zip
//   size=12812809
//   sha256=<64 lower-case hex>
//   signature=<128 lower-case hex>
//
// The first line names the format; a format this game does not know is refused ("unknown format").
// Every other line is key=value. Unknown keys are allowed (a later format may add some), a key given
// twice is not. `signature` is the last line: Ed25519 (RFC 8032) over every byte before it, checked
// against the public key the game was built with (update_key.h). Line ends are "\n" or "\r\n"; the
// signature covers the bytes exactly as they arrived.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace AutoUpdate {

struct Version {
    uint32_t    major = 0;
    uint32_t    minor = 0;
    uint32_t    patch = 0;
    std::string suffix;   // after the '-', "" for a release
};

// False for anything but "X.Y.Z" or "X.Y.Z-suffix" (digits only in X, Y, Z, at most 6 each; the
// suffix 1-32 characters of [0-9A-Za-z.-]).
bool ParseVersion(const std::string& text, Version& out);
// <0, 0 or >0, as a semver comparison.
int CompareVersions(const Version& a, const Version& b);
// True when both parse and `candidate` is strictly newer than `current`.
bool IsNewer(const std::string& candidate, const std::string& current);

constexpr int      kManifestFormat   = 1;
constexpr uint64_t kMaxPackageBytes  = 512ull * 1024 * 1024;   // a package larger than this is refused
constexpr size_t   kMaxManifestBytes = 16 * 1024;

struct Manifest {
    int         format = 0;
    std::string version;
    std::string commit;
    std::string platform;
    std::string url;
    uint64_t    size = 0;
    std::string sha256;     // 64 lower-case hex
    std::string signature;  // 128 lower-case hex
    size_t      signedBytes = 0;   // the length of the text the signature covers
};

// Parse and check every field's shape (not the signature). False with a short reason in `error`.
bool ParseManifest(const std::string& text, Manifest& out, std::string& error);
// The signature over text[0, m.signedBytes) against `publicKey`. False with a reason.
bool VerifyManifest(const std::string& text, const Manifest& m, const uint8_t publicKey[32], std::string& error);

// The manifest's text without its signature line (what the signature covers), from its fields.
std::string ManifestBody(const Manifest& m);
// `body` + its signature line, signed with the 64-byte Ed25519 secret key (update_sign, the tests).
std::string SignManifest(const std::string& body, const uint8_t secretKey[64]);

// Hex helpers: false when `hex` is not 2 * n lower- or upper-case hex digits.
bool FromHex(const std::string& hex, uint8_t* out, size_t n);
std::string ToHex(const uint8_t* p, size_t n);

}  // namespace AutoUpdate

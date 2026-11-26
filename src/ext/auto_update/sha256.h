#pragma once
// ============================================================================
// src/ext/auto_update/sha256.h -- NOT RETAIL. SHA-256 (FIPS 180-4), streaming
// ============================================================================
// ext.auto_update (docs/fork/AUTO_UPDATE.md §5): a downloaded package is hashed as it arrives and
// compared with its manifest's `sha256`. Portable C++, no OS call, so every platform hashes the same
// way (tests/unit/auto_update_test.cpp checks the NIST vectors).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace AutoUpdate {

class Sha256 {
public:
    Sha256();
    void Add(const void* data, size_t size);
    // The digest; the object is spent afterwards (call Reset to hash again).
    void Finish(uint8_t digest[32]);
    void Reset();

    // Lower-case hex of `digest`.
    static std::string Hex(const uint8_t digest[32]);
    static std::string HexOf(const void* data, size_t size);

private:
    void Block(const uint8_t* p);

    uint32_t h_[8];
    uint8_t  buffer_[64];
    size_t   buffered_ = 0;
    uint64_t length_   = 0;   // bytes added
};

}  // namespace AutoUpdate

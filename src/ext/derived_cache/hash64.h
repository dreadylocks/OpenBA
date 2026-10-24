#pragma once
// ============================================================================
// src/ext/derived_cache/hash64.h -- XXH64, the derived-data cache's content hash
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/COMPRESSAO.md). Written here from the published
// algorithm (Yann Collet's xxHash specification, XXH64: four 64-bit lanes over 32-byte stripes,
// multiply-rotate rounds, the avalanche at the end); no xxHash source is linked or copied.
//
// WHY THIS HASH. The cache keys an entry by the hash of its SOURCE bytes (2 GB of AO maps on a
// MegaMod boot), hashed on the loader thread at every texture create, so it must run near memory
// speed: XXH64 does ~10 GB/s at -O2 (this file is always built -O2, CMakeLists.txt), the byte-wise
// FNV-1a the tree already has (src/diag/load_time.h) ~1 GB/s, the table CRC-32 less. A 64-bit hash
// over ~10^4 entries collides with probability ~n^2 / 2^65 ~ 3e-12; and a key is the hash PLUS the
// producer, its version, the shape (width, height, levels, format) and the source size, all
// compared on a lookup, so a collision would also need the same shape and size. Not cryptographic:
// the cache is the user's own, on the user's own machine.
//
// Test vectors (tests/unit/ext_derived_cache_test.cpp): "" -> 0xEF46DB3751D8E999,
// "abc" -> 0x44BC2CF5AD770999 (seed 0).
// ============================================================================
#include <cstddef>
#include <cstdint>

namespace Ext {
namespace DerivedCache {

uint64_t Hash64(const void* data, size_t size, uint64_t seed = 0);

}  // namespace DerivedCache
}  // namespace Ext

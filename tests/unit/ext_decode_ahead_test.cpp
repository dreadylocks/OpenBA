// ============================================================================
// tests/unit/ext_decode_ahead_test.cpp -- ext.decode_ahead's block cache (DECODEAHEAD)
// ============================================================================
// Ext::DecodeAhead::BlockCache (src/ext/decode_ahead/block_cache.*) with a stand-in decoder:
//   * a submitted block comes back from Take with the decoder's bytes, once;
//   * a block decoding to a size other than usize is "no block" (the caller's retail path),
//     and so is a Take whose identity (offset, src, csize) differs from the job's;
//   * Submit refuses a duplicate and a full pool; Drop frees an owner's blocks, queued or done,
//     leaves other owners' blocks alone, and the slots are reusable afterwards;
//   * the real System::Compression::Decompress round-trips through the cache from several
//     threads' worth of blocks (Compress -> Submit -> Take == the input).
// ============================================================================
#include "unit.h"

#include "ext/decode_ahead/block_cache.h"
#include "system/Compression.h"
#include "system/string_internal.h"   // RtSystem::RtMemFree: the table Compress allocated

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using Ext::DecodeAhead::BlockCache;

std::atomic<int> g_calls{0};

// out[i] = src[i % srcLen] ^ 0x5A for srcLen * 4 bytes; srcLen 1 decodes to the wrong size (3).
int FakeDecode(const void* src, uint32_t srcLen, void* dst, uint32_t dstCap) {
    ++g_calls;
    const uint8_t* s = static_cast<const uint8_t*>(src);
    uint8_t* d = static_cast<uint8_t*>(dst);
    const uint32_t n = srcLen == 1 ? 3 : srcLen * 4;
    if (n > dstCap) return 0;
    for (uint32_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(s[i % srcLen] ^ 0x5A);
    return static_cast<int>(n);
}

std::vector<uint8_t> Bytes(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}

}  // namespace

UNIT_TEST(ext_decode_ahead_take_returns_the_decoded_block_once) {
    BlockCache cache(2, 4, &FakeDecode);
    const int owner = 0;
    const std::vector<uint8_t> src = Bytes(100, 3);
    CHECK(cache.Submit(&owner, 5, 1234, src.data(), 100, 400));
    CHECK(!cache.Submit(&owner, 5, 1234, src.data(), 100, 400));   // already held
    std::vector<uint8_t> dst(BlockCache::kBlock, 0xEE);
    CHECK_EQ(cache.Take(&owner, 5, 1234, src.data(), 100, dst.data()), 400);
    for (int i = 0; i < 400; ++i) CHECK_EQ(dst[i], static_cast<uint8_t>(src[i % 100] ^ 0x5A));
    CHECK_EQ(dst[400], 0xEE);                                       // nothing past usize written
    CHECK_EQ(cache.Take(&owner, 5, 1234, src.data(), 100, dst.data()), -1);   // taken once
    CHECK_EQ(cache.Take(&owner, 6, 1234, src.data(), 100, dst.data()), -1);   // never submitted
}

UNIT_TEST(ext_decode_ahead_wrong_size_or_identity_is_no_block) {
    BlockCache cache(1, 4, &FakeDecode);
    const int owner = 0;
    const std::vector<uint8_t> src = Bytes(64, 9);
    std::vector<uint8_t> dst(BlockCache::kBlock, 0xEE);
    CHECK(cache.Submit(&owner, 0, 0, src.data(), 1, 4));            // decodes to 3, not 4
    CHECK_EQ(cache.Take(&owner, 0, 0, src.data(), 1, dst.data()), -1);
    CHECK_EQ(dst[0], 0xEE);
    CHECK(cache.Submit(&owner, 1, 10, src.data(), 16, 64));
    CHECK_EQ(cache.Take(&owner, 1, 11, src.data(), 16, dst.data()), -1);     // other offset
    CHECK(cache.Submit(&owner, 2, 10, src.data(), 16, 64));
    CHECK_EQ(cache.Take(&owner, 2, 10, src.data() + 1, 16, dst.data()), -1); // other bytes
    CHECK(cache.Submit(&owner, 3, 10, src.data(), 16, 64));
    CHECK_EQ(cache.Take(&owner, 3, 10, src.data(), 15, dst.data()), -1);     // other size
    CHECK_EQ(cache.GetStats().rejected, 4);
}

UNIT_TEST(ext_decode_ahead_full_pool_and_drop) {
    BlockCache cache(1, 3, &FakeDecode);
    const int a = 0, b = 0;
    const std::vector<uint8_t> src = Bytes(32, 1);
    CHECK(cache.Submit(&a, 0, 0, src.data(), 32, 128));
    CHECK(cache.Submit(&a, 1, 32, src.data(), 32, 128));
    CHECK(cache.Submit(&b, 0, 0, src.data(), 32, 128));
    CHECK(!cache.Submit(&b, 1, 32, src.data(), 32, 128));          // three slots
    CHECK_EQ(cache.GetStats().full, 1);
    cache.Drop(&a);
    std::vector<uint8_t> dst(BlockCache::kBlock);
    CHECK_EQ(cache.Take(&a, 0, 0, src.data(), 32, dst.data()), -1);
    CHECK_EQ(cache.Take(&a, 1, 32, src.data(), 32, dst.data()), -1);
    CHECK(cache.Submit(&b, 1, 32, src.data(), 32, 128));           // a's slots are free again
    CHECK_EQ(cache.Take(&b, 0, 0, src.data(), 32, dst.data()), 128);   // b untouched by Drop(a)
    CHECK_EQ(cache.Take(&b, 1, 32, src.data(), 32, dst.data()), 128);
    cache.Drop(&b);                                                  // nothing left: a no-op
}

UNIT_TEST(ext_decode_ahead_real_decompress_round_trips) {
    // Compressible 32 KB blocks, compressed by the real encoder, decoded by the cache's workers.
    const int kBlocks = 24;
    System::Compression comp;
    std::vector<std::vector<uint8_t>> plain(kBlocks), packed(kBlocks);
    for (int k = 0; k < kBlocks; ++k) {
        plain[k].resize(BlockCache::kBlock);
        for (uint32_t i = 0; i < BlockCache::kBlock; ++i)
            plain[k][i] = static_cast<uint8_t>((i / (k + 3)) ^ (i % 17) ^ k);
        packed[k].resize(BlockCache::kBlock + 64);
        const int n = comp.Compress(plain[k].data(), BlockCache::kBlock, packed[k].data(), BlockCache::kBlock);
        CHECK(n > 0 && n < static_cast<int>(BlockCache::kBlock));
        packed[k].resize(static_cast<size_t>(n));
    }
    if (comp._table != nullptr) ::RtSystem::RtMemFree(comp._table);
    comp._table = nullptr;

    BlockCache cache(3, 8, &::System::Compression::Decompress);
    const int owner = 0;
    std::vector<uint8_t> dst(BlockCache::kBlock);
    for (int k = 0; k < kBlocks; ++k) {
        for (int ahead = k; ahead < kBlocks && ahead < k + 8; ++ahead)
            cache.Submit(&owner, ahead, static_cast<uint32_t>(ahead) << 15, packed[ahead].data(),
                         static_cast<uint16_t>(packed[ahead].size()), static_cast<uint16_t>(BlockCache::kBlock));
        CHECK_EQ(cache.Take(&owner, k, static_cast<uint32_t>(k) << 15, packed[k].data(),
                            static_cast<uint16_t>(packed[k].size()), dst.data()),
                 static_cast<int>(BlockCache::kBlock));
        CHECK(std::memcmp(dst.data(), plain[k].data(), BlockCache::kBlock) == 0);
    }
}

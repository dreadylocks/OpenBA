#pragma once
// ============================================================================
// src/render/common/compress_ao/bc4_chain.h -- ext.compress_ao: one texture's R8 levels to BC4
// ============================================================================
// NOT RETAIL (docs/extensions/handins/COMPRESSAO.md, issue #13). Backend-neutral: a backend's
// texture create calls Encode with the levels a resource file seeded, and creates the texture in
// its BC4 format from `out` when it returns true.
//
// THE CACHE. With a derived-data cache set (SetCache, from the backend's Request), the chain is first
// looked up under its content key: producer 'AOB4', kEncoderVersion, shape {width, height, levels,
// source format}, the levels' total size and their chained Hash64 (each level hashed with the
// previous level's hash as its seed). A hit is the stored chain (verified against its record); a miss
// is encoded and queued for storing. Bump kEncoderVersion whenever the encoder's output changes
// (src/render/common/bc4/bc4.cpp): every old entry then misses and ages out through the cap.
//
// THE ENCODE. Bands of 16 block rows (64 pixel rows) of every level on a pool of up to 8 worker
// threads (half the hardware threads), the caller waiting for its own bands. ~57 MB/s per core;
// a MegaMod boot's 2.08 GB is ~36 s of CPU, ~5.4 s on the loader with 8 workers -- once: the next
// start finds every chain in the cache.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace DerivedCache {
class Store;
}
}  // namespace Ext

namespace CompressAo {

constexpr uint32_t kEncoderVersion = 1;

struct Level {
    const uint8_t* data;
    int64_t        size;
};

// The cache Encode uses (null: none). The backend's Request sets it once, at boot.
void SetCache(Ext::DerivedCache::Store* cache);

// `count` levels of a width x height R8 texture, level i being max(1, width >> i) x
// max(1, height >> i) bytes, tight rows. Returns false (nothing done) when a level has another size.
// Else `out` holds the BC4 levels back to back (the D3D layout of each: rows of 8-byte blocks) and
// `offsets` count + 1 entries, level i at [offsets[i], offsets[i + 1]).
bool Encode(const Level* levels, int count, int width, int height, uint32_t sourceFormat,
            std::vector<uint8_t>* out, std::vector<size_t>* offsets);

// The load's end (the backend's loader bracket): when anything was converted since the last, one
// summary line, the cache asked to persist its keep list (without waiting) and its mapping released.
void LoadEnd();

}  // namespace CompressAo

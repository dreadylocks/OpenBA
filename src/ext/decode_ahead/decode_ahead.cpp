// ============================================================================
// src/ext/decode_ahead/decode_ahead.cpp -- NOT RETAIL. ext.decode_ahead
// ============================================================================
// Class A (docs/extensions/handins/DECODEAHEAD.md); decode_ahead.h has the contract.
//
// The seam's two points:
//   kExtDecodeBlock(b)  DecompressFromSource(b) of a stream with at least two blocks over a
//                       packed FileStream: queue the compressed blocks b+1 .. b+kAhead whose bytes a
//                       ReadFile would return in full from the package mapping (PackedMapSpan), then
//                       hand block b back if a worker decoded it. When it does, the source is left
//                       where retail's SetCurrentPos(off) + Read(csize) leaves it (cursor off + csize)
//                       and the mapping's emulated file pointer where that ReadFile leaves it. Any
//                       other case (block 0, a stored block, a block not queued, a damaged block, a
//                       loose or unmapped source) returns false: retail's body decodes it.
//   kExtDecodeRelease   the stream's blocks are dropped.
// Workers run System::Compression::Decompress, which is pure (no state, no allocation): the decode
// of a block on a worker is the decode the loader thread would have run.
// ============================================================================

#include "ext/decode_ahead/decode_ahead.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "ext/decode_ahead/block_cache.h"
#include "ext/registry.h"
#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "io/Stream.h"
#include "system/Compression.h"

namespace Ext { namespace DecodeAhead {

namespace {

constexpr int32_t kAhead   = 8;    // blocks queued past the one being read
constexpr int     kMaxJobs = 48;   // decoded blocks held at once (48 x 32 KB), across nested streams

BlockCache* g_cache = nullptr;     // made by the first Sync with the row on; never destroyed

int Workers() {
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    return std::max(1, std::min(3, hw - 2));   // the loader and main threads keep their cores
}

bool Hook(IO::CompressedStream& cs, int32_t point, int32_t blk, uint8_t* dst) {
    if (point == IO::CompressedStream::kExtDecodeRelease) {
        g_cache->Drop(&cs);
        return false;
    }
    if (cs._headerSize < 2) return false;
    IO::FileStream* const fs = dynamic_cast<IO::FileStream*>(cs._source);
    if (fs == nullptr) return false;
    IO::FileSystem* const sys = IO::FileSystem::_singleton;

    const int32_t last = std::min(cs._headerSize - 1, blk + kAhead);
    for (int32_t b = blk + 1; b <= last; ++b) {
        const RtIo::CompressedStreamHeader& h = cs._headerData[b];
        if (h.compressedSize == 0) continue;   // stored: nothing to decode
        const uint8_t* const src =
            sys->PackedMapSpan(fs->_file, static_cast<int32_t>(h.sourceOffset), h.compressedSize);
        if (src == nullptr) break;
        g_cache->Submit(&cs, b, h.sourceOffset, src, h.compressedSize, h.uncompressedSize);
    }

    const RtIo::CompressedStreamHeader& h = cs._headerData[blk];
    if (h.compressedSize == 0) return false;
    const int32_t off = static_cast<int32_t>(h.sourceOffset);
    const uint8_t* const src = sys->PackedMapSpan(fs->_file, off, h.compressedSize);
    if (src == nullptr) return false;
    if (g_cache->Take(&cs, blk, h.sourceOffset, src, h.compressedSize, dst) < 0) return false;

    // What retail's SetCurrentPos(off) + Read(csize) leave behind (DecompressFromSource 1800a9715 /
    // 1800a9730): the packed FileStream's cursor at off + csize, and the package's file pointer.
    cs._source->SetCurrentPos(off + h.compressedSize);
    sys->PackedMapNoteRead(fs->_file, off, h.compressedSize);
    return true;
}

}  // namespace

void Sync() {
    if (Ext().decode_ahead && Ext().create_on_demand) {
        // With the payloads skipped (docs/extensions/handins/STREAMCACHE.md section 2) a resource
        // stream is read in its header block and in the block each skipped payload ends in: the
        // kAhead blocks queued at the header are mostly never wanted, and the one that is waits in
        // the queue behind them. Sampled on a MegaMod boot: 9% of the loader thread waiting for its
        // own block, 19% of the blocks served. Nothing to run ahead of: the seam stays null.
        ::IO::CompressedStream::_extDecodeAhead = nullptr;
        std::fprintf(stderr, "[DecodeAhead] off while ext.create_on_demand is on: the loader reads only the "
                     "blocks around the payloads it skips\n");
        return;
    }
    if (Ext().decode_ahead) {
        if (g_cache == nullptr) g_cache = new BlockCache(Workers(), kMaxJobs, &::System::Compression::Decompress);
        ::IO::CompressedStream::_extDecodeAhead = &Hook;
    } else {
        ::IO::CompressedStream::_extDecodeAhead = nullptr;
    }
}

}}  // namespace Ext::DecodeAhead

#pragma once
// ============================================================================
// src/ext/stream_cache/stream_record.h -- ext.stream_cache: the record of what a load consumed
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). Standard C++ only (no engine
// header): tests/unit/ext_stream_cache_test.cpp drives it with synthetic streams.
//
// A compiled resource in a package is one compressed stream (payload.h has the layout). Loading it
// consumes a few ranges of the DECOMPRESSED stream: all of it for a small description, the header
// and the inter-level size fields for a texture or a mesh whose payloads ext.create_on_demand skips.
// A Record is those ranges with their bytes: the loader can read the same resource again from it
// instead of opening the compressed stream (the 32 KB window, the block directory, the LZF decode of
// the header block and of every block a skipped payload ends in). The bytes are a function of the
// package alone; only WHICH ranges a load reads depends on the build, so a record is never wrong,
// at most too narrow -- and a load that reads outside it continues on the compressed stream
// (segment_stream.h) and the record is widened (Recorder::Seed).
//
// RECORD LAYOUT (little-endian, 8-byte aligned, as a Bundle chunk holds it):
//   +0x00  u64  entry offset in the package      } the entry's identity (EntryId): the record is
//   +0x08  u32  entry size                       } used only when all four match the entry about
//   +0x0c  u32  block count                      } to be read
//   +0x10  u64  Hash64 of the block directory    }
//   +0x18  u32  decompressed total (what CompressedStream::GetSize answers for the whole stream)
//   +0x1c  u32  segment count
//   +0x20  segment count x { u32 pos, u32 len }   in increasing pos, disjoint, not adjacent
//   then   the segments' bytes back to back, padded with zeros to a multiple of 8
//
// A CHUNK is a 16-byte header { 'STRC', version, record count, 0 } and that many records. A
// package's bundle is one or more chunks (one per load bracket that recorded something); a later
// chunk's record for an entry replaces an earlier one.
//
// GENERATIONS. A widened record leaves the record it replaced as dead weight in its chunk, and a
// chunk whose records are all replaced is read back for nothing. When a bundle's dead bytes reach a
// quarter of its bytes (or it spans more than kConsolidateChunks chunks), the boot's end writes ONE
// chunk holding exactly the live records (Bundle::Consolidate, each byte for byte) at the next chunk
// ordinal, then a MARKER naming that ordinal as the package's first chunk: 16 bytes { 'STRG',
// kMarkerVersion, start ordinal, ~start }. Markers have their own ordinals 0, 1, ...; ReadBack
// takes the newest marker whose start chunk is stored (a marker can outlive a chunk the cap refused
// or a crash lost), else reads from ordinal 0 as before. Chunks below the start are never read
// again: they drop out of the derived cache's keep list and age out through its cap.
//
// WHAT IS RECORDED. Every successful read is noted; overlapping and adjacent ranges merge (the bytes
// of an overlap must agree, or the record is dropped). A stream is stored only when every read
// succeeded and the bytes consumed are at most kRecordMax: the per-type load table (STREAMCACHE.md
// section 1) puts the description-class streams at 0.2..2 KB and the consumed part of a texture or
// mesh stream under 2 KB, while CPU geometry (picking and edge meshes, 30..50 KB, fully read) and
// anything else large stays on the compressed path. The limit is a constant, not a setting: it is a
// property of the content.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace StreamCache {

constexpr uint32_t kRecordMax     = 8192;        // bytes a record may hold (see above)
constexpr uint32_t kChunkMagic    = 0x43525453;  // 'STRC'
constexpr uint32_t kChunkVersion  = 2;           // 2: records may be widened (segments merge)
constexpr uint32_t kChunkHeader   = 16;
constexpr uint32_t kRecordHeader  = 32;
constexpr uint32_t kMarkerMagic   = 0x47525453;  // 'STRG'
constexpr uint32_t kMarkerVersion = 1;
constexpr uint32_t kMarkerBytes   = 16;
constexpr uint32_t kConsolidateChunks = 16;      // more chunks than this are merged even when all live

struct Segment {
    uint32_t pos;
    uint32_t len;
};

struct EntryId {
    uint64_t offset     = 0;
    uint32_t size       = 0;
    uint32_t blockCount = 0;
    uint64_t dirHash    = 0;
};

bool SameEntry(const EntryId& a, const EntryId& b);

// A record inside a chunk's bytes (the chunk outlives the view).
struct RecordView {
    EntryId        id;
    uint32_t       total    = 0;
    uint32_t       segCount = 0;
    const Segment* segs     = nullptr;   // segCount entries
    const uint8_t* bytes    = nullptr;   // the segments' bytes, back to back
    uint32_t       byteCount = 0;        // sum of the segments' len

    // Copy [pos, pos + n) of the decompressed stream out of the record; false when the range is
    // not inside one recorded segment (the consumer asked for bytes the recording never read).
    bool Read(uint32_t pos, uint32_t n, uint8_t* dst) const;
};

// One load's reads, merged into sorted disjoint segments.
class Recorder {
public:
    void Begin(const EntryId& id, uint32_t total);
    // Begin with a record's segments already in: the load that read outside `view` goes on, and
    // the result replaces `view`.
    void Seed(const RecordView& view);
    // A successful read of n bytes at pos (n == 0 is ignored). Bytes over an already recorded
    // range must agree with it.
    void Note(uint32_t pos, uint32_t n, const uint8_t* bytes);
    // A failed read, or a load that did not produce its object: the record is dropped.
    void Fail();
    bool Active() const { return _active; }
    bool Storable() const { return _active && !_bad && !_overflow; }
    uint32_t Consumed() const { return _consumed; }
    bool Overflowed() const { return _overflow; }
    bool Bad() const { return _bad; }
    // Appends the record to `chunk` (a chunk body under construction) when it is storable. False
    // otherwise (nothing appended). Ends the recording either way.
    bool Finish(std::vector<uint8_t>& chunk);

private:
    struct Seg {
        uint32_t pos;
        std::vector<uint8_t> bytes;
    };
    EntryId  _id;
    uint32_t _total    = 0;
    uint32_t _consumed = 0;   // bytes held
    bool     _active   = false;
    bool     _bad      = false;
    bool     _overflow = false;
    std::vector<Seg> _segs;   // sorted by pos, disjoint, not adjacent
};

// Chunk framing.
void     BeginChunk(std::vector<uint8_t>& chunk);              // writes the 16-byte header (count 0)
void     EndChunk(std::vector<uint8_t>& chunk, uint32_t count); // stamps the record count
uint32_t ChunkRecordCount(const std::vector<uint8_t>& chunk);  // 0 for a malformed header

// A package's records: the chunks' bytes and an index by entry offset.
class Bundle {
public:
    // Parses `chunk` (taken by move) and indexes its records (a record for an entry already
    // indexed replaces it); false (nothing kept) when the chunk is malformed: a bad header or
    // version, a record past the end, segments out of order.
    bool AddChunk(std::vector<uint8_t> chunk);
    // The record for `id`, or null.
    const RecordView* Find(const EntryId& id) const;
    // Frees every chunk.
    void Clear();
    size_t Records() const { return _index.size(); }
    size_t Chunks() const { return _chunks.size(); }
    size_t Bytes() const { return _bytes; }
    // The bytes the indexed (live) records take in their chunks, chunk headers not counted.
    size_t LiveBytes() const;
    // One chunk holding exactly the indexed records, each byte for byte as its chunk holds it, in
    // entry-offset order. Empty when there is no record.
    std::vector<uint8_t> Consolidate() const;

private:
    std::vector<std::vector<uint8_t>>             _chunks;
    std::unordered_map<uint64_t, RecordView>      _index;   // by entry offset
    size_t                                        _bytes = 0;
};

// True when `b` is worth rewriting as one chunk: at least two chunks, and the records later chunks
// replaced take at least a quarter of its bytes, or it spans more than kConsolidateChunks chunks.
bool ShouldConsolidate(const Bundle& b);

// The generation marker's payload, and its parse (false: not a marker of this version).
std::vector<uint8_t> EncodeMarker(uint32_t start);
bool DecodeMarker(const std::vector<uint8_t>& marker, uint32_t& start);

// Reads a package's chunks back. `findChunk(ordinal, out)` / `findMarker(ordinal, out)` are the
// derived cache's lookups (false = a miss). Markers are read from ordinal 0 until a miss; the newest
// one whose start chunk is found names the first chunk read, otherwise chunks are read from ordinal
// 0; either way on to the first missing ordinal. Every chunk found goes into `bundle`.
using FindFn = std::function<bool(uint32_t ordinal, std::vector<uint8_t>& out)>;
struct ReadBackResult {
    uint32_t start   = 0;   // the first chunk ordinal read
    uint32_t next    = 0;   // the first missing chunk ordinal: where the next chunk goes
    uint32_t read    = 0;   // chunks found (malformed ones included)
    uint32_t markers = 0;   // markers found: where the next marker goes
    bool     startMissing = false;    // the newest marker named a chunk that is not stored
    std::vector<uint32_t> malformed;  // ordinals of the chunks AddChunk refused
};
ReadBackResult ReadBack(Bundle& bundle, const FindFn& findChunk, const FindFn& findMarker);

}  // namespace StreamCache
}  // namespace Ext

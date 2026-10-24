#pragma once
// ============================================================================
// src/ext/derived_cache/derived_cache.h -- the derived-data cache (shared infrastructure)
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/COMPRESSAO.md). Data an extension DERIVES from the
// game's content at load (today: ext.compress_ao's BC4 encode of the baked AO maps), kept on the
// user's machine so the next start finds it instead of recomputing it -- the shape of UE5's derived
// data cache. Ruled allowed by the user 2026-10-22: it lives only in the user's documents folder,
// is built from the user's own install, and is never shipped (DESIGN.md section 3 rule 7).
// Standard C++ over the Platform file seam (platform_file.h): the unit tests drive it over a temp
// directory (tests/unit/ext_derived_cache_test.cpp). The exe's one shared store and its folder are
// derived_cache_shared.cpp.
//
// CONTENT-ADDRESSED. A Key is what the payload was derived FROM and HOW: the producer (a FourCC),
// the producer's encoder version, four shape words (for a texture: width, height, levels, source
// format), the source's byte count and Hash64 (hash64.h, XXH64) of the source bytes. Nothing names a
// package or a path: a changed mod is a different hash and simply misses; a new encoder version
// misses every old entry (they age out through the cap, below). Every field is compared on a
// lookup, so a hash collision would also need the same producer, version, shape and size.
//
// THE FILES, in <dir> (the exe: <Documents>\Banished\Cache\, beside settings.cfg):
//   derived.lock   held exclusively while a store is open (Platform::FileOpenExclusive): a second
//                  running instance gets OpenStatus::InUse and runs without the cache
//   derived.dat    32-byte header, then the payloads back to back, append-only
//   derived.idx    32-byte header, then one 80-byte IndexRecord per payload, in payload order
//   derived.used   the key ids the last session read or wrote (the cap's keep list), rewritten
//                  at every Flush / RequestFlush (the exe: each load's end) and at Close
//   derived.clear  the Options dialog's "Clear cache": the next Open deletes the cache first
// Little-endian on disk (x86-64 and arm64 both are). The header's format word changing resets both
// files.
//
// CRASH SAFETY. A record is written after its payload, and carries Hash64 of its own 72 bytes and of
// the payload. Open walks the index and keeps the longest valid prefix (magic, own hash, contiguous
// offset, inside the data file), then truncates both files to it: a torn tail from a crash or a kill
// is dropped. A payload whose bytes do not match its record's hash on a read (the OS wrote the
// record but not the payload before a power loss) is a miss, and its key is stored again: a second
// record for the key. Open keeps only a key's last record and rewrites the files without the earlier
// ones, so the bad payload is read and rejected once, not at every start.
//
// THE CAP. Open with `capBytes`: a payload that would take the data file past it is not stored (one
// log line). At the next Open a cache past its cap is rewritten keeping only the entries the
// previous session used, in their order, up to the cap; with no keep list it is emptied.
//
// THREADS. Find is called from any thread (the loader's texture creates); its copy out of the
// mapping (or read through the handle) holds the file lock, so ReleaseMapping is safe at any time.
// Put queues the payload and returns, and one writer thread appends it (at most 256 MB queued, then
// Put waits). A write that fails (the disk is full) turns storing off for the run with one log line;
// reads go on.
// ============================================================================
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "platform_file.h"

namespace Ext {
namespace DerivedCache {

struct Key {
    uint32_t producer   = 0;    // FourCC of the producer (MakeProducer('A','O','B','4'))
    uint32_t version    = 0;    // the producer's encoder version
    uint32_t shape[4]   = {};   // the producer's parameters
    uint64_t sourceSize = 0;    // bytes hashed
    uint64_t sourceHash = 0;    // Hash64 of those bytes
};

bool SameKey(const Key& a, const Key& b);

constexpr uint32_t MakeProducer(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

// One record of derived.idx (80 bytes).
struct IndexRecord {
    uint32_t magic;         // +0x00 kRecordMagic
    uint32_t producer;      // +0x04
    uint32_t version;       // +0x08
    uint32_t reserved;      // +0x0C 0
    uint32_t shape[4];      // +0x10
    uint64_t sourceSize;    // +0x20
    uint64_t sourceHash;    // +0x28
    uint64_t offset;        // +0x30 the payload's offset in derived.dat
    uint64_t size;          // +0x38 the payload's bytes
    uint64_t payloadHash;   // +0x40 Hash64 of the payload
    uint64_t check;         // +0x48 Hash64 of +0x00..+0x47
};
static_assert(sizeof(IndexRecord) == 80, "derived.idx records are 80 bytes");

constexpr uint32_t kFormatVersion = 1;   // both files' header; a change resets the cache

struct Stats {
    uint64_t entries     = 0;   // records in the index (this run's included)
    uint64_t dataBytes   = 0;   // derived.dat's size
    uint64_t hits        = 0;
    uint64_t misses      = 0;
    uint64_t stored      = 0;   // payloads this run appended
    uint64_t refusedFull = 0;   // payloads not stored: the cap
    uint64_t badPayloads = 0;   // reads whose bytes did not match their record
    bool     writeFailed = false;
};

class Store {
public:
    enum class OpenStatus { Opened, InUse, Failed };

    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // `dir` is UTF-8, created when absent (its parent must exist). On InUse / Failed nothing is held
    // and every call below is a no-op miss.
    OpenStatus Open(const std::string& dir, uint64_t capBytes);
    // Writes what is queued, stops the writer, closes the files and releases the lock.
    void Close();
    bool IsOpen() const { return _open; }

    // The payload stored under `key`, verified against its record. false = a miss (`out` cleared).
    bool Find(const Key& key, std::vector<uint8_t>& out);
    // Queues `payload` under `key` (taken by move). Dropped when disabled, after a failed write, or
    // when the key is already stored.
    void Put(const Key& key, std::vector<uint8_t> payload);
    // Waits until every queued payload is written and the keep list is current.
    void Flush();
    // The same without waiting: the writer rewrites the keep list once its queue is empty.
    void RequestFlush();
    // Unmaps derived.dat: later reads go through the file handle. The mapping's pages count in the
    // process's working set while it is held (file-backed, ~1 GB after a MegaMod boot), so the exe
    // drops it once the load that read the cache is over.
    void ReleaseMapping();
    // Stops every read and write for the rest of the run (the clear request: the files go at the
    // next Open).
    void Disable();

    Stats GetStats() const;
    uint64_t BytesOnDisk() const;   // derived.dat + derived.idx

private:
    struct Pending {
        Key                  key;
        std::vector<uint8_t> payload;
    };

    bool LoadIndex(uint64_t capBytes);
    bool Compact(uint64_t capBytes, std::vector<IndexRecord>& records, bool usedOnly);
    void ResetFiles();
    void WriterMain();
    void WriteOne(Pending& p);
    void WriteUsedList();
    bool ReadAt(Platform::FileHandle h, uint64_t offset, void* dst, uint64_t size);
    bool WriteAt(Platform::FileHandle h, uint64_t offset, const void* src, uint64_t size);
    const IndexRecord* Lookup(const Key& key) const;   // under _mutex

    std::string _dir;
    uint64_t    _cap  = 0;
    bool        _open = false;

    Platform::FileHandle  _lock = Platform::kInvalidFileHandle;
    Platform::FileHandle  _dat  = Platform::kInvalidFileHandle;
    Platform::FileHandle  _idx  = Platform::kInvalidFileHandle;
    Platform::FileMapping _map;     // derived.dat as it was at Open (until ReleaseMapping / Close)
    std::mutex            _fileMutex;   // the two handles' file positions, and the mapping

    mutable std::mutex                             _mutex;   // everything below
    std::condition_variable                        _cv;
    std::unordered_multimap<uint64_t, IndexRecord> _records;   // by sourceHash
    std::unordered_set<uint64_t>                   _used;      // KeyId()s read or written this run
    std::deque<Pending>                            _queue;
    uint64_t _queuedBytes = 0;
    uint64_t _dataEnd     = 0;
    uint64_t _indexEnd    = 0;
    bool     _usedDirty   = false;
    bool     _writing     = false;   // the writer is writing a payload or the keep list
    bool     _flushRequested = false; // a Flush waits: the writer rewrites the keep list
    bool     _stop        = false;
    bool     _loggedFull  = false;
    std::atomic<bool> _disabled{false};
    Stats    _stats;
    std::thread _writer;
};

// A key's identity in the keep list.
uint64_t KeyId(const Key& key);

// The Options dialog's "Clear cache": writes `dir`\derived.clear (the next Open deletes the cache).
bool RequestClear(const std::string& dir);
bool ClearRequested(const std::string& dir);
// derived.dat + derived.idx in `dir`, from the files themselves (0 when absent or held open by a
// store: ask that store's BytesOnDisk instead).
uint64_t BytesInDir(const std::string& dir);

// ---- the exe's shared store (derived_cache_shared.cpp; not in the unit build) -------------------
// <Documents>\Banished\Cache: the folder settings.cfg is in (IO::SettingsStore::DefaultPath), plus
// "Cache". false when the documents folder is unknown.
bool CacheDir(std::string& out);
// Opens the shared store once (later calls return it, or null): null under the faithful profile,
// when the folder is unknown, or when another instance holds the cache. The cap is
// Val().derived_cache_mb.
Store* OpenShared();
Store* Shared();   // the store OpenShared opened, or null
// The button: the clear request for the shared folder, and the shared store disabled at once.
void ClearShared();
bool SharedClearPending();
uint64_t SharedBytes();   // the cache's size on disk (0 once a clear is pending)

}  // namespace DerivedCache
}  // namespace Ext

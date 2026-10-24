// ============================================================================
// src/ext/derived_cache/derived_cache.cpp -- the derived-data cache (NOT RETAIL)
// ============================================================================
// derived_cache.h has the contract and the file layout. Issue #13, docs/extensions/handins/COMPRESSAO.md.
// ============================================================================
#include "ext/derived_cache/derived_cache.h"

#include "ext/derived_cache/hash64.h"

#include <cstdio>
#include <cstring>

namespace Ext {
namespace DerivedCache {

namespace {

constexpr uint32_t kRecordMagic = 0x31454344u;            // "DCE1"
constexpr uint64_t kHeaderSize  = 32;
constexpr uint64_t kMaxQueued   = 256ull * 1024 * 1024;   // Put waits past this
constexpr uint32_t kChunk       = 1u << 20;               // one FileRead / FileWrite at most

// Both files start with this.
struct FileHeader {
    char     magic[8];     // "OBADCDAT" / "OBADCIDX" / "OBADCUSE"
    uint32_t format;       // kFormatVersion
    uint32_t recordSize;   // 80 for the index, 8 for the keep list, 0 for the data
    uint64_t reserved[2];
};
static_assert(sizeof(FileHeader) == kHeaderSize, "a cache file header is 32 bytes");

FileHeader MakeHeader(const char* magic, uint32_t recordSize) {
    FileHeader h = {};
    std::memcpy(h.magic, magic, 8);
    h.format     = kFormatVersion;
    h.recordSize = recordSize;
    return h;
}

bool HeaderIs(const FileHeader& h, const char* magic, uint32_t recordSize) {
    return std::memcmp(h.magic, magic, 8) == 0 && h.format == kFormatVersion && h.recordSize == recordSize;
}

// UTF-8 -> the Platform path spelling (UTF-16). A malformed byte becomes U+FFFD.
std::u16string ToPath(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t c = 0xFFFD;
        size_t extra = 0;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            extra = 3;
        }
        bool ok = (b < 0x80) || extra != 0;
        for (size_t k = 1; ok && k <= extra; ++k) {
            if (i + k >= s.size() || (static_cast<uint8_t>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
        }
        if (!ok) {
            out.push_back(u'\xFFFD');
            ++i;
            continue;
        }
        i += extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

std::u16string FilePath(const std::string& dir, const char* name) {
    return ToPath(dir + "\\" + name);
}

bool Valid(Platform::FileHandle h) {
    return h != Platform::kInvalidFileHandle && h != nullptr;
}

void CloseHandle(Platform::FileHandle& h) {
    if (Valid(h)) Platform::FileClose(h);
    h = Platform::kInvalidFileHandle;
}

uint64_t RecordCheck(const IndexRecord& r) {
    return Hash64(&r, offsetof(IndexRecord, check));
}

IndexRecord MakeRecord(const Key& k, uint64_t offset, uint64_t size, uint64_t payloadHash) {
    IndexRecord r = {};
    r.magic       = kRecordMagic;
    r.producer    = k.producer;
    r.version     = k.version;
    std::memcpy(r.shape, k.shape, sizeof(r.shape));
    r.sourceSize  = k.sourceSize;
    r.sourceHash  = k.sourceHash;
    r.offset      = offset;
    r.size        = size;
    r.payloadHash = payloadHash;
    r.check       = RecordCheck(r);
    return r;
}

Key RecordKey(const IndexRecord& r) {
    Key k;
    k.producer   = r.producer;
    k.version    = r.version;
    std::memcpy(k.shape, r.shape, sizeof(k.shape));
    k.sourceSize = r.sourceSize;
    k.sourceHash = r.sourceHash;
    return k;
}

// Whole-file helpers over the Platform seam (used for the small files: the keep list, the marker).
bool ReadWhole(const std::u16string& path, std::vector<uint8_t>& out) {
    Platform::FileHandle h = Platform::FileOpenReadShared(path.c_str());
    if (!Valid(h)) return false;
    const int64_t size = Platform::FileSize64(h);
    bool ok = size >= 0 && size < (int64_t{1} << 31);
    if (ok) {
        out.resize(static_cast<size_t>(size));
        uint32_t got = 0;
        ok = size == 0 || (Platform::FileRead(h, out.data(), static_cast<uint32_t>(size), &got) != 0 &&
                           got == static_cast<uint32_t>(size));
    }
    Platform::FileClose(h);
    return ok;
}

bool WriteWhole(const std::u16string& path, const void* data, size_t size) {
    const std::u16string tmp = path + u".tmp";
    Platform::FileHandle h = Platform::FileCreateWrite(tmp.c_str());
    if (!Valid(h)) return false;
    uint32_t wrote = 0;
    bool ok = size == 0 || (Platform::FileWrite(h, data, static_cast<uint32_t>(size), &wrote) != 0 &&
                            wrote == static_cast<uint32_t>(size));
    Platform::FileClose(h);
    ok = ok && Platform::FileReplace(tmp.c_str(), path.c_str()) != 0;
    if (!ok) Platform::FileDelete(tmp.c_str());
    return ok;
}

uint64_t FileBytes(const std::u16string& path) {
    Platform::FileHandle h = Platform::FileOpenReadShared(path.c_str());
    if (!Valid(h)) return 0;
    const int64_t size = Platform::FileSize64(h);
    Platform::FileClose(h);
    return size > 0 ? static_cast<uint64_t>(size) : 0;
}

// Keeps the last record of each key, in file order; returns how many earlier ones it dropped.
size_t DropSuperseded(std::vector<IndexRecord>& records) {
    std::unordered_multimap<uint64_t, size_t> last;   // sourceHash -> the index of the key's latest record
    std::vector<bool> dropped(records.size(), false);
    size_t count = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const Key key = RecordKey(records[i]);
        bool seen = false;
        auto range = last.equal_range(key.sourceHash);
        for (auto it = range.first; it != range.second; ++it) {
            if (!SameKey(RecordKey(records[it->second]), key)) continue;
            dropped[it->second] = true;
            it->second = i;
            seen = true;
            ++count;
            break;
        }
        if (!seen) last.emplace(key.sourceHash, i);
    }
    if (count == 0) return 0;
    size_t kept = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        if (!dropped[i]) records[kept++] = records[i];
    }
    records.resize(kept);
    return count;
}

}  // namespace

bool SameKey(const Key& a, const Key& b) {
    return a.producer == b.producer && a.version == b.version && a.shape[0] == b.shape[0] &&
           a.shape[1] == b.shape[1] && a.shape[2] == b.shape[2] && a.shape[3] == b.shape[3] &&
           a.sourceSize == b.sourceSize && a.sourceHash == b.sourceHash;
}

uint64_t KeyId(const Key& key) {
    uint8_t bytes[40];
    std::memcpy(bytes, &key.producer, 4);
    std::memcpy(bytes + 4, &key.version, 4);
    std::memcpy(bytes + 8, key.shape, 16);
    std::memcpy(bytes + 24, &key.sourceSize, 8);
    std::memcpy(bytes + 32, &key.sourceHash, 8);
    return Hash64(bytes, sizeof(bytes));
}

bool RequestClear(const std::string& dir) {
    const char mark[] = "clear\n";
    return WriteWhole(FilePath(dir, "derived.clear"), mark, sizeof(mark) - 1);
}

bool ClearRequested(const std::string& dir) {
    return Platform::PathAttributes(FilePath(dir, "derived.clear").c_str()) != Platform::kInvalidPathAttributes;
}

uint64_t BytesInDir(const std::string& dir) {
    return FileBytes(FilePath(dir, "derived.dat")) + FileBytes(FilePath(dir, "derived.idx"));
}

Store::~Store() {
    Close();
}

Store::OpenStatus Store::Open(const std::string& dir, uint64_t capBytes) {
    Close();
    _dir = dir;
    _cap = capBytes;
    Platform::DirCreate(ToPath(dir).c_str());
    const uint32_t attrs = Platform::PathAttributes(ToPath(dir).c_str());
    if (attrs == Platform::kInvalidPathAttributes || (attrs & Platform::kFileAttributeDirectory) == 0) {
        std::fprintf(stderr, "[DerivedCache] cannot create %s: no cache this run\n", dir.c_str());
        return OpenStatus::Failed;
    }
    _lock = Platform::FileOpenExclusive(FilePath(dir, "derived.lock").c_str());
    if (!Valid(_lock)) {
        _lock = Platform::kInvalidFileHandle;
        std::fprintf(stderr, "[DerivedCache] %s is in use by another instance: no cache this run\n", dir.c_str());
        return OpenStatus::InUse;
    }
    if (ClearRequested(dir)) {
        Platform::FileDelete(FilePath(dir, "derived.dat").c_str());
        Platform::FileDelete(FilePath(dir, "derived.idx").c_str());
        Platform::FileDelete(FilePath(dir, "derived.used").c_str());
        Platform::FileDelete(FilePath(dir, "derived.clear").c_str());
        std::fprintf(stderr, "[DerivedCache] cleared (requested from the Options dialog)\n");
    }
    _dat = Platform::FileOpenExclusive(FilePath(dir, "derived.dat").c_str());
    _idx = Platform::FileOpenExclusive(FilePath(dir, "derived.idx").c_str());
    if (!Valid(_dat) || !Valid(_idx) || !LoadIndex(capBytes)) {
        CloseHandle(_dat);
        CloseHandle(_idx);
        CloseHandle(_lock);
        std::fprintf(stderr, "[DerivedCache] cannot open the cache files in %s: no cache this run\n", dir.c_str());
        return OpenStatus::Failed;
    }
    if (_dataEnd > kHeaderSize) Platform::FileMapOpen(_dat, &_map);
    _open     = true;
    _stop     = false;
    _disabled = false;
    _writer   = std::thread([this] { WriterMain(); });
    std::fprintf(stderr, "[DerivedCache] %s: %llu entries, %.1f MB (cap %llu MB)\n", dir.c_str(),
                 static_cast<unsigned long long>(_records.size()),
                 static_cast<double>(_dataEnd + _indexEnd) / 1048576.0,
                 static_cast<unsigned long long>(capBytes >> 20));
    return OpenStatus::Opened;
}

void Store::ResetFiles() {
    Platform::FileTruncate(_dat, 0);
    Platform::FileTruncate(_idx, 0);
    const FileHeader dh = MakeHeader("OBADCDAT", 0);
    const FileHeader ih = MakeHeader("OBADCIDX", sizeof(IndexRecord));
    WriteAt(_dat, 0, &dh, sizeof(dh));
    WriteAt(_idx, 0, &ih, sizeof(ih));
    _dataEnd  = kHeaderSize;
    _indexEnd = kHeaderSize;
}

// The longest valid prefix of the index; both files truncated to it; compacted when over the cap.
bool Store::LoadIndex(uint64_t capBytes) {
    _records.clear();
    const int64_t datSize = Platform::FileSize64(_dat);
    const int64_t idxSize = Platform::FileSize64(_idx);
    if (datSize < 0 || idxSize < 0) return false;
    FileHeader dh = {};
    FileHeader ih = {};
    const bool headersOk = datSize >= static_cast<int64_t>(kHeaderSize) &&
                           idxSize >= static_cast<int64_t>(kHeaderSize) && ReadAt(_dat, 0, &dh, sizeof(dh)) &&
                           ReadAt(_idx, 0, &ih, sizeof(ih)) && HeaderIs(dh, "OBADCDAT", 0) &&
                           HeaderIs(ih, "OBADCIDX", sizeof(IndexRecord));
    if (!headersOk) {
        if (datSize != 0 || idxSize != 0)
            std::fprintf(stderr, "[DerivedCache] unknown or damaged cache format: starting empty\n");
        ResetFiles();
        return true;
    }
    const uint64_t count = (static_cast<uint64_t>(idxSize) - kHeaderSize) / sizeof(IndexRecord);
    std::vector<IndexRecord> all(static_cast<size_t>(count));
    if (count != 0 && !ReadAt(_idx, kHeaderSize, all.data(), count * sizeof(IndexRecord))) return false;
    std::vector<IndexRecord> records;
    records.reserve(all.size());
    uint64_t end = kHeaderSize;
    for (const IndexRecord& r : all) {
        if (r.magic != kRecordMagic || r.check != RecordCheck(r) || r.offset != end || r.size == 0 ||
            r.offset + r.size > static_cast<uint64_t>(datSize))
            break;
        records.push_back(r);
        end += r.size;
    }
    const uint64_t indexEnd = kHeaderSize + records.size() * sizeof(IndexRecord);
    if (records.size() != all.size() || static_cast<uint64_t>(datSize) != end ||
        static_cast<uint64_t>(idxSize) != indexEnd) {
        std::fprintf(stderr, "[DerivedCache] dropped a damaged tail: kept %llu of %llu entries\n",
                     static_cast<unsigned long long>(records.size()), static_cast<unsigned long long>(count));
        Platform::FileTruncate(_dat, static_cast<int64_t>(end));
        Platform::FileTruncate(_idx, static_cast<int64_t>(indexEnd));
    }
    _dataEnd  = end;
    _indexEnd = indexEnd;
    // A key stored twice (its first payload failed its hash on a read, and the caller stored it
    // again): the later record is the live one. The earlier ones are dropped here, and the rewrite
    // below takes their bytes out of the files, so the next Open does not meet them again.
    const size_t superseded = DropSuperseded(records);
    if (superseded != 0)
        std::fprintf(stderr, "[DerivedCache] %llu entries were stored again since: dropping the old copies\n",
                     static_cast<unsigned long long>(superseded));
    const bool overCap = _dataEnd > capBytes;
    if ((overCap || superseded != 0) && !Compact(capBytes, records, overCap)) return false;
    for (const IndexRecord& r : records)
        _records.emplace(r.sourceHash, r);
    _stats.entries   = _records.size();
    _stats.dataBytes = _dataEnd;
    return true;
}

// Rewrites the files with `records`, in their order, up to the cap. Over the cap only the entries
// the last session used are kept (`usedOnly`); after DropSuperseded alone, every record left is.
bool Store::Compact(uint64_t capBytes, std::vector<IndexRecord>& records, bool usedOnly) {
    std::unordered_set<uint64_t> keep;
    std::vector<uint8_t> used;
    if (usedOnly && ReadWhole(FilePath(_dir, "derived.used"), used) && used.size() >= kHeaderSize) {
        FileHeader h = {};
        std::memcpy(&h, used.data(), sizeof(h));
        if (HeaderIs(h, "OBADCUSE", 8)) {
            for (size_t at = kHeaderSize; at + 8 <= used.size(); at += 8) {
                uint64_t id = 0;
                std::memcpy(&id, used.data() + at, 8);
                keep.insert(id);
            }
        }
    }
    const std::u16string datNew = FilePath(_dir, "derived.dat.new");
    const std::u16string idxNew = FilePath(_dir, "derived.idx.new");
    Platform::FileHandle nd = Platform::FileOpenExclusive(datNew.c_str());
    Platform::FileHandle ni = Platform::FileOpenExclusive(idxNew.c_str());
    if (!Valid(nd) || !Valid(ni)) {
        CloseHandle(nd);
        CloseHandle(ni);
        return false;
    }
    Platform::FileTruncate(nd, 0);
    Platform::FileTruncate(ni, 0);
    const FileHeader dh = MakeHeader("OBADCDAT", 0);
    const FileHeader ih = MakeHeader("OBADCIDX", sizeof(IndexRecord));
    bool ok = WriteAt(nd, 0, &dh, sizeof(dh)) && WriteAt(ni, 0, &ih, sizeof(ih));
    std::vector<IndexRecord> kept;
    uint64_t end = kHeaderSize;
    std::vector<uint8_t> payload;
    for (const IndexRecord& r : records) {
        if (!ok) break;
        if (usedOnly && keep.find(KeyId(RecordKey(r))) == keep.end()) continue;
        if (end + r.size > capBytes) continue;
        payload.resize(static_cast<size_t>(r.size));
        if (!ReadAt(_dat, r.offset, payload.data(), r.size)) continue;
        IndexRecord n = r;
        n.offset = end;
        n.check  = RecordCheck(n);
        ok = WriteAt(nd, end, payload.data(), r.size) &&
             WriteAt(ni, kHeaderSize + kept.size() * sizeof(IndexRecord), &n, sizeof(n));
        kept.push_back(n);
        end += r.size;
    }
    CloseHandle(nd);
    CloseHandle(ni);
    CloseHandle(_dat);
    CloseHandle(_idx);
    const std::u16string dat = FilePath(_dir, "derived.dat");
    const std::u16string idx = FilePath(_dir, "derived.idx");
    ok = ok && Platform::FileReplace(datNew.c_str(), dat.c_str()) != 0 &&
         Platform::FileReplace(idxNew.c_str(), idx.c_str()) != 0;
    _dat = Platform::FileOpenExclusive(dat.c_str());
    _idx = Platform::FileOpenExclusive(idx.c_str());
    if (!Valid(_dat) || !Valid(_idx)) return false;
    if (!ok) {
        // A half-made rewrite: start empty rather than trust either file.
        std::fprintf(stderr, "[DerivedCache] the over-cap rewrite failed: starting empty\n");
        Platform::FileDelete(datNew.c_str());
        Platform::FileDelete(idxNew.c_str());
        ResetFiles();
        records.clear();
        return true;
    }
    if (usedOnly)
        std::fprintf(stderr, "[DerivedCache] over the cap (%llu MB): kept %llu of %llu entries the last run used\n",
                     static_cast<unsigned long long>(capBytes >> 20), static_cast<unsigned long long>(kept.size()),
                     static_cast<unsigned long long>(records.size()));
    else
        std::fprintf(stderr, "[DerivedCache] rewritten without the old copies: %llu entries, %.1f MB\n",
                     static_cast<unsigned long long>(kept.size()), static_cast<double>(end) / 1048576.0);
    records.swap(kept);
    _dataEnd  = end;
    _indexEnd = kHeaderSize + records.size() * sizeof(IndexRecord);
    return true;
}

void Store::Close() {
    if (_writer.joinable()) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;
        }
        _cv.notify_all();
        _writer.join();
    }
    Platform::FileMapClose(&_map);
    CloseHandle(_dat);
    CloseHandle(_idx);
    CloseHandle(_lock);
    std::lock_guard<std::mutex> lock(_mutex);
    _records.clear();
    _used.clear();
    _queue.clear();
    _queuedBytes = 0;
    _usedDirty   = false;
    _loggedFull  = false;
    _open        = false;
    _stats       = Stats();
}

// The newest record of `key` (the highest offset: the files are append-only). Open leaves one per
// key; preferring the newest keeps a re-stored entry ahead of its bad copy even if both were held.
const IndexRecord* Store::Lookup(const Key& key) const {
    const IndexRecord* best = nullptr;
    auto range = _records.equal_range(key.sourceHash);
    for (auto it = range.first; it != range.second; ++it) {
        if (!SameKey(RecordKey(it->second), key)) continue;
        if (best == nullptr || it->second.offset > best->offset) best = &it->second;
    }
    return best;
}

bool Store::Find(const Key& key, std::vector<uint8_t>& out) {
    out.clear();
    if (!_open || _disabled.load(std::memory_order_acquire)) return false;
    IndexRecord rec;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const IndexRecord* r = Lookup(key);
        if (r == nullptr) {
            ++_stats.misses;
            return false;
        }
        rec = *r;
    }
    out.resize(static_cast<size_t>(rec.size));
    bool ok = true;
    {
        std::lock_guard<std::mutex> files(_fileMutex);   // also keeps the mapping until the copy is done
        if (_map.data != nullptr && rec.offset + rec.size <= _map.size)
            std::memcpy(out.data(), _map.data + rec.offset, static_cast<size_t>(rec.size));
        else
            ok = ReadAt(_dat, rec.offset, out.data(), rec.size);
    }
    ok = ok && Hash64(out.data(), out.size()) == rec.payloadHash;
    std::lock_guard<std::mutex> lock(_mutex);
    if (!ok) {
        // A payload the OS never wrote (a power loss after its record): forget it; the caller
        // derives it again and Put stores it anew.
        if (_stats.badPayloads++ == 0)
            std::fprintf(stderr, "[DerivedCache] an entry's bytes do not match its record: re-deriving it\n");
        auto range = _records.equal_range(key.sourceHash);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second.offset == rec.offset) {
                _records.erase(it);
                break;
            }
        }
        ++_stats.misses;
        out.clear();
        return false;
    }
    ++_stats.hits;
    if (_used.insert(KeyId(key)).second) _usedDirty = true;
    return true;
}

void Store::Put(const Key& key, std::vector<uint8_t> payload) {
    if (!_open || _disabled.load(std::memory_order_acquire) || payload.empty()) return;
    std::unique_lock<std::mutex> lock(_mutex);
    if (_stats.writeFailed) return;
    _cv.wait(lock, [&] { return _queuedBytes == 0 || _queuedBytes + payload.size() <= kMaxQueued; });
    _queuedBytes += payload.size();
    _queue.push_back(Pending{ key, std::move(payload) });
    lock.unlock();
    _cv.notify_all();
}

void Store::Flush() {
    if (!_open) return;
    std::unique_lock<std::mutex> lock(_mutex);
    _flushRequested = true;
    _cv.notify_all();
    _cv.wait(lock, [&] { return (_queue.empty() && !_writing && !_usedDirty) || _stop; });
}

void Store::RequestFlush() {
    if (!_open) return;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _flushRequested = true;
    }
    _cv.notify_all();
}

void Store::ReleaseMapping() {
    std::lock_guard<std::mutex> files(_fileMutex);
    Platform::FileMapClose(&_map);
}

void Store::Disable() {
    _disabled.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Pending& p : _queue)
        _queuedBytes -= p.payload.size();   // a payload the writer holds is subtracted by the writer
    _queue.clear();
    _usedDirty = false;
    _cv.notify_all();
}

Stats Store::GetStats() const {
    std::lock_guard<std::mutex> lock(_mutex);
    Stats s = _stats;
    s.entries   = _records.size();
    s.dataBytes = _dataEnd;
    return s;
}

uint64_t Store::BytesOnDisk() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _open ? _dataEnd + _indexEnd : 0;
}

// The keep list is rewritten only on a Flush (the load's end) or at Close, never per payload: a
// first launch stores thousands of entries, and a list rewritten after each would be quadratic.
void Store::WriterMain() {
    std::unique_lock<std::mutex> lock(_mutex);
    for (;;) {
        _cv.wait(lock, [&] { return _stop || !_queue.empty() || (_usedDirty && _flushRequested); });
        if (!_queue.empty()) {
            Pending p = std::move(_queue.front());
            _queue.pop_front();
            _writing = true;
            lock.unlock();
            WriteOne(p);
            lock.lock();
            _queuedBytes -= p.payload.size();
            _writing = false;
            _cv.notify_all();
            continue;
        }
        if (_usedDirty) {
            _writing = true;
            lock.unlock();
            if (!_disabled.load(std::memory_order_acquire)) WriteUsedList();
            lock.lock();
            _usedDirty      = false;
            _flushRequested = false;
            _writing        = false;
            _cv.notify_all();
            continue;
        }
        _cv.notify_all();
        if (_stop) return;
    }
}

void Store::WriteOne(Pending& p) {
    if (_disabled.load(std::memory_order_acquire)) return;
    const uint64_t size = p.payload.size();
    uint64_t at = 0;
    uint64_t indexAt = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_stats.writeFailed || Lookup(p.key) != nullptr) return;
        if (_dataEnd + size > _cap) {
            ++_stats.refusedFull;
            if (!_loggedFull) {
                _loggedFull = true;
                std::fprintf(stderr, "[DerivedCache] the cache is at its cap (%llu MB): new entries are not stored "
                                     "this run\n",
                             static_cast<unsigned long long>(_cap >> 20));
            }
            return;
        }
        at      = _dataEnd;
        indexAt = _indexEnd;
    }
    const uint64_t payloadHash = Hash64(p.payload.data(), p.payload.size());
    const IndexRecord rec = MakeRecord(p.key, at, size, payloadHash);
    bool ok;
    {
        std::lock_guard<std::mutex> files(_fileMutex);
        ok = WriteAt(_dat, at, p.payload.data(), size) && WriteAt(_idx, indexAt, &rec, sizeof(rec));
        if (!ok) {
            // Best effort: the next Open drops whatever tail is left anyway.
            Platform::FileTruncate(_dat, static_cast<int64_t>(at));
            Platform::FileTruncate(_idx, static_cast<int64_t>(indexAt));
        }
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (!ok) {
        _stats.writeFailed = true;
        std::fprintf(stderr, "[DerivedCache] a write failed (is the disk full?): the cache stores nothing more "
                             "this run\n");
        return;
    }
    _dataEnd  = at + size;
    _indexEnd = indexAt + sizeof(rec);
    _records.emplace(rec.sourceHash, rec);
    ++_stats.stored;
    if (_used.insert(KeyId(p.key)).second) _usedDirty = true;
}

void Store::WriteUsedList() {
    std::vector<uint8_t> bytes;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _usedDirty = false;
        bytes.resize(kHeaderSize + _used.size() * 8);
        const FileHeader h = MakeHeader("OBADCUSE", 8);
        std::memcpy(bytes.data(), &h, sizeof(h));
        size_t at = kHeaderSize;
        for (uint64_t id : _used) {
            std::memcpy(bytes.data() + at, &id, 8);
            at += 8;
        }
    }
    WriteWhole(FilePath(_dir, "derived.used"), bytes.data(), bytes.size());
}

bool Store::ReadAt(Platform::FileHandle h, uint64_t offset, void* dst, uint64_t size) {
    if (Platform::FileSeek64(h, static_cast<int64_t>(offset)) == 0) return false;
    uint8_t* p = static_cast<uint8_t*>(dst);
    while (size != 0) {
        const uint32_t n = size < kChunk ? static_cast<uint32_t>(size) : kChunk;
        uint32_t got = 0;
        if (Platform::FileRead(h, p, n, &got) == 0 || got != n) return false;
        p += n;
        size -= n;
    }
    return true;
}

bool Store::WriteAt(Platform::FileHandle h, uint64_t offset, const void* src, uint64_t size) {
    if (Platform::FileSeek64(h, static_cast<int64_t>(offset)) == 0) return false;
    const uint8_t* p = static_cast<const uint8_t*>(src);
    while (size != 0) {
        const uint32_t n = size < kChunk ? static_cast<uint32_t>(size) : kChunk;
        uint32_t wrote = 0;
        if (Platform::FileWrite(h, p, n, &wrote) == 0 || wrote != n) return false;
        p += n;
        size -= n;
    }
    return true;
}

}  // namespace DerivedCache
}  // namespace Ext

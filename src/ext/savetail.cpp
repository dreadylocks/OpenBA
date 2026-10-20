// ============================================================================
// src/ext/savetail.cpp -- the class B / C manifest chunk (savetail.h)
// ============================================================================
// NOT RETAIL. Standard C++ only (the unit tests link it with registry.cpp).
// ============================================================================

#include "ext/savetail.h"

#include "ext/registry.h"

#include <utility>

namespace Ext {
namespace SaveTail {

namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// A bounds-checked cursor over the body.
struct Reader {
    const uint8_t* p;
    size_t         left;
    bool U32(uint32_t& v) {
        if (left < 4) return false;
        v = GetU32(p); p += 4; left -= 4;
        return true;
    }
    bool Bytes(size_t n, const uint8_t*& at) {
        if (left < n) return false;
        at = p; p += n; left -= n;
        return true;
    }
};

// The payload hooks (RegisterPayload). A handful of class C extensions at most:
// a linear scan, filled at boot, read when a save is written or read.
struct PayloadHooks {
    std::string   id;
    PayloadWriter writer = nullptr;
    PayloadReader reader = nullptr;
};
std::vector<PayloadHooks>& Hooks() {
    static std::vector<PayloadHooks> hooks;
    return hooks;
}
const PayloadHooks* FindHooks(const std::string& id) {
    for (const PayloadHooks& h : Hooks())
        if (h.id == id) return &h;
    return nullptr;
}

}  // namespace

void RegisterPayload(const char* id, PayloadWriter writer, PayloadReader reader) {
    if (id == nullptr) return;
    for (PayloadHooks& h : Hooks()) {
        if (h.id == id) { h.writer = writer; h.reader = reader; return; }
    }
    Hooks().push_back(PayloadHooks{id, writer, reader});
}

void DeliverPayloads(const std::vector<Record>& records) {
    for (const Record& r : records) {
        const PayloadHooks* h = FindHooks(r.id);
        if (h != nullptr && h->reader != nullptr) h->reader(r.formatVersion, r.payload);
    }
}

const char* StatusName(Status s) {
    switch (s) {
    case Status::NoManifest:         return "no manifest";
    case Status::Ok:                 return "ok";
    case Status::Truncated:          return "truncated";
    case Status::BadChecksum:        return "bad checksum";
    case Status::NewerChunkVersion:  return "chunk version newer than this build";
    case Status::Malformed:          return "malformed";
    case Status::UnknownId:          return "unknown extension";
    case Status::NotLockedClass:     return "not a class B / C extension";
    case Status::NewerFormatVersion: return "format version newer than this build";
    case Status::DuplicateId:        return "extension named twice";
    }
    return "?";
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> Encode(const std::vector<Record>& records) {
    std::vector<uint8_t> out;
    if (records.empty()) return out;   // zero bytes: the faithful profile's tail
    PutU32(out, kMagic);
    PutU32(out, kVersion);
    PutU32(out, static_cast<uint32_t>(records.size()));
    for (const Record& r : records) {
        PutU32(out, static_cast<uint32_t>(r.id.size()));
        out.insert(out.end(), r.id.begin(), r.id.end());
        PutU32(out, static_cast<uint32_t>(r.formatVersion));
        PutU32(out, static_cast<uint32_t>(r.payload.size()));
        out.insert(out.end(), r.payload.begin(), r.payload.end());
    }
    const uint32_t bodyLength = static_cast<uint32_t>(out.size());
    const uint32_t crc = Crc32(out.data(), out.size());
    PutU32(out, bodyLength);
    PutU32(out, crc);
    return out;
}

Status Decode(const uint8_t* data, size_t size, std::vector<Record>& out) {
    out.clear();
    if (data == nullptr || size == 0) return Status::NoManifest;

    // Ours only if it starts with the magic. A cut inside the magic itself is
    // a manifest that was truncated; anything else is not ours.
    uint8_t magic[4];
    for (int i = 0; i < 4; ++i) magic[i] = static_cast<uint8_t>(kMagic >> (8 * i));
    const size_t head = size < 4 ? size : 4;
    for (size_t i = 0; i < head; ++i)
        if (data[i] != magic[i]) return Status::NoManifest;
    if (size < 12 + kTrailerBytes) return Status::Truncated;

    // The trailer, from the end.
    const uint32_t bodyLength = GetU32(data + size - kTrailerBytes);
    const uint32_t crc        = GetU32(data + size - 4);
    if (static_cast<size_t>(bodyLength) != size - kTrailerBytes) return Status::Truncated;
    if (Crc32(data, bodyLength) != crc) return Status::BadChecksum;

    Reader r{data, bodyLength};
    uint32_t m = 0, version = 0, count = 0;
    r.U32(m);
    r.U32(version);
    if (version > kVersion) return Status::NewerChunkVersion;
    if (version < 1) return Status::Malformed;
    r.U32(count);

    std::vector<Record> records;
    for (uint32_t i = 0; i < count; ++i) {
        Record rec;
        uint32_t idLength = 0;
        const uint8_t* idBytes = nullptr;
        if (!r.U32(idLength) || idLength == 0 || idLength > kMaxIdLength ||
            !r.Bytes(idLength, idBytes))
            return Status::Malformed;
        rec.id.assign(idBytes, idBytes + idLength);   // byte by byte: no pointer cast
        if (!ValidId(rec.id.c_str())) return Status::Malformed;
        uint32_t fv = 0, payloadLength = 0;
        const uint8_t* payload = nullptr;
        if (!r.U32(fv) || !r.U32(payloadLength) || !r.Bytes(payloadLength, payload))
            return Status::Malformed;
        rec.formatVersion = static_cast<int32_t>(fv);
        if (rec.formatVersion < 1) return Status::Malformed;
        rec.payload.assign(payload, payload + payloadLength);
        records.push_back(std::move(rec));
    }
    if (r.left != 0) return Status::Malformed;
    out = std::move(records);
    return Status::Ok;
}

Status Validate(const std::vector<Record>& records, std::string* offending) {
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& rec = records[i];
        Status st = Status::Ok;
        const Entry* e = FindEntry(rec.id.c_str());
        if (e == nullptr) st = Status::UnknownId;
        else if (e->tier != Tier::Extension || !IsLockedClass(e->cls)) st = Status::NotLockedClass;
        else if (rec.formatVersion > e->formatVersion) st = Status::NewerFormatVersion;
        else {
            for (size_t j = 0; j < i; ++j)
                if (records[j].id == rec.id) { st = Status::DuplicateId; break; }
        }
        if (st != Status::Ok) {
            if (offending != nullptr) *offending = rec.id;
            return st;
        }
    }
    return Status::Ok;
}

std::vector<Record> ActiveRecords() {
    std::vector<Record> out;
    for (int i = 0; i < EntryCount(); ++i) {
        const Entry& e = EntryAt(i);
        if (e.tier != Tier::Extension || !IsLockedClass(e.cls) || !Active(e)) continue;
        Record r;
        r.id = e.id;
        r.formatVersion = e.formatVersion;
        if (const PayloadHooks* h = FindHooks(r.id))
            if (h->writer != nullptr) r.payload = h->writer();
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<uint8_t> EncodeActive() { return Encode(ActiveRecords()); }

}  // namespace SaveTail
}  // namespace Ext

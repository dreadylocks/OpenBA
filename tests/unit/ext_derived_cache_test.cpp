// ============================================================================
// tests/unit/ext_derived_cache_test.cpp -- src/ext/derived_cache: Hash64 and the Store
// ============================================================================
// docs/extensions/handins/COMPRESSAO.md. Over a fresh temp directory per case:
//   * Hash64 is XXH64 (vectors checked against MSYS2's xxhsum -H1);
//   * a payload stored, flushed, closed and reopened reads back byte for byte;
//   * a torn tail (half a record, stray data bytes) is dropped at Open, the entries before it kept;
//   * a payload whose bytes were damaged is a miss, and is stored again; the next Open serves the
//     good copy and rewrites the files without the bad one (a key's newest record wins);
//   * a new encoder version, or a key equal in hash but not in shape / size, misses;
//   * the cap refuses a payload past it; an over-cap cache keeps what the last run used;
//   * a second Store on the same folder gets InUse; a clear request empties the next Open;
//   * a damaged header starts the cache empty.
// ============================================================================
#include "unit.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"

namespace fs = std::filesystem;
using namespace Ext::DerivedCache;

namespace {

struct TempDir {
    fs::path dir;
    TempDir() {
        static int serial = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("banished_dcache_unit_" + std::to_string(static_cast<long long>(stamp)) +
                                           "_" + std::to_string(++serial));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string Path() const { return dir.u8string(); }
    fs::path File(const char* name) const { return dir / name; }
};

Key MakeKey(uint64_t hash, uint32_t w = 64, uint32_t version = 1) {
    Key k;
    k.producer   = MakeProducer('T', 'E', 'S', 'T');
    k.version    = version;
    k.shape[0]   = w;
    k.shape[1]   = 64;
    k.shape[2]   = 7;
    k.shape[3]   = 4;
    k.sourceSize = 4096;
    k.sourceHash = hash;
    return k;
}

std::vector<uint8_t> Payload(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>(seed + i * 31u);
    return v;
}

void Append(const fs::path& p, size_t n) {
    std::ofstream f(p, std::ios::binary | std::ios::app);
    for (size_t i = 0; i < n; ++i)
        f.put(static_cast<char>(0x5A));
}

constexpr uint64_t kBig = 1ull << 40;

}  // namespace

UNIT_TEST(ext_derived_cache_hash64_is_xxh64) {
    CHECK_EQ(Hash64("", 0), 0xEF46DB3751D8E999ull);
    CHECK_EQ(Hash64("abc", 3), 0x44BC2CF5AD770999ull);
    const char* s = "Nobody inspects the spammish repetition";
    CHECK_EQ(Hash64(s, std::strlen(s)), 0xFBCEA83C8A378BF1ull);
    std::vector<uint8_t> v(1003);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<uint8_t>(i * 7 + 3);
    CHECK_EQ(Hash64(v.data(), v.size()), 0x778430D269B09BD7ull);   // every tail path: 31 stripes + 8 + 4 + 3
}

UNIT_TEST(ext_derived_cache_round_trip) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(1000, 1);
    const std::vector<uint8_t> b = Payload(3 * 1024 * 1024 + 7, 2);   // past one 1 MB write chunk
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(!s.Find(MakeKey(1), out));
        s.Put(MakeKey(1), a);
        s.Put(MakeKey(2), b);
        s.Put(MakeKey(1), a);   // a duplicate is not stored twice
        s.Flush();
        CHECK(s.Find(MakeKey(1), out));   // this run's entry: read through the handle
        CHECK(out == a);
        const Stats st = s.GetStats();
        CHECK_EQ(st.stored, 2ull);
        CHECK_EQ(st.entries, 2ull);
        CHECK_EQ(s.BytesOnDisk(), 32ull + a.size() + b.size() + 32ull + 2 * sizeof(IndexRecord));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK(s.Find(MakeKey(2), out));   // read through the mapping
    CHECK(out == b);
    CHECK(s.Find(MakeKey(1), out));
    CHECK(out == a);
    CHECK_EQ(s.GetStats().hits, 2ull);
    s.Close();
    CHECK_EQ(BytesInDir(t.Path()), 32ull + a.size() + b.size() + 32ull + 2 * sizeof(IndexRecord));
}

UNIT_TEST(ext_derived_cache_torn_tail_is_dropped) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(500, 3);
    uint64_t good = 0;
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(1), a);
        s.Flush();
        good = s.BytesOnDisk();
    }
    Append(t.File("derived.dat"), 300);   // a payload written, its record not
    Append(t.File("derived.idx"), 40);    // half a record
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK_EQ(s.BytesOnDisk(), good);
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(1), out));
        CHECK(out == a);
    }
    // A whole record whose own hash is wrong (a torn write of its bytes) is dropped too.
    {
        std::fstream f(t.File("derived.idx"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 0x30);
        f.put(static_cast<char>(0x77));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 0ull);
    CHECK_EQ(s.BytesOnDisk(), 64ull);
}

UNIT_TEST(ext_derived_cache_damaged_payload_misses_and_is_restored) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(800, 4);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(9), a);
    }
    {
        std::fstream f(t.File("derived.dat"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 100);
        f.put(static_cast<char>(~a[100]));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK(!s.Find(MakeKey(9), out));
    CHECK_EQ(s.GetStats().badPayloads, 1ull);
    s.Put(MakeKey(9), a);
    s.Flush();
    CHECK(s.Find(MakeKey(9), out));
    CHECK(out == a);
}

UNIT_TEST(ext_derived_cache_restored_payload_survives_reopen) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(800, 11);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(9), a);
    }
    {
        std::fstream f(t.File("derived.dat"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(32 + 100);
        f.put(static_cast<char>(~a[100]));
    }
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(!s.Find(MakeKey(9), out));
        s.Put(MakeKey(9), a);   // a second record for the key; the bad one is still in the files
        s.Flush();
        CHECK_EQ(s.BytesOnDisk(), 64ull + 2 * a.size() + 2 * sizeof(IndexRecord));
    }
    // The next start serves the good copy, and the files no longer hold the bad one.
    for (int run = 0; run < 2; ++run) {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        CHECK_EQ(s.GetStats().entries, 1ull);
        CHECK_EQ(s.BytesOnDisk(), 64ull + a.size() + sizeof(IndexRecord));
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(9), out));
        CHECK(out == a);
        CHECK_EQ(s.GetStats().badPayloads, 0ull);
        CHECK_EQ(s.GetStats().stored, 0ull);
    }
}

UNIT_TEST(ext_derived_cache_newest_record_wins_and_old_is_dropped) {
    TempDir t;
    const std::vector<uint8_t> a = Payload(600, 12);
    const std::vector<uint8_t> b = Payload(700, 13);
    const std::vector<uint8_t> c = Payload(300, 14);
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(5), a);
        s.Put(MakeKey(6), c);
    }
    // A second, valid record for key 5 appended by hand (what a re-store leaves), with other bytes.
    {
        IndexRecord r = {};
        r.magic = 0x31454344u;   // "DCE1"
        const Key k = MakeKey(5);
        r.producer = k.producer;
        r.version  = k.version;
        std::memcpy(r.shape, k.shape, sizeof(r.shape));
        r.sourceSize  = k.sourceSize;
        r.sourceHash  = k.sourceHash;
        r.offset      = 32 + a.size() + c.size();
        r.size        = b.size();
        r.payloadHash = Hash64(b.data(), b.size());
        r.check       = Hash64(&r, offsetof(IndexRecord, check));
        std::ofstream d(t.File("derived.dat"), std::ios::binary | std::ios::app);
        d.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        std::ofstream i(t.File("derived.idx"), std::ios::binary | std::ios::app);
        i.write(reinterpret_cast<const char*>(&r), sizeof(r));
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 2ull);
    // The Open's rewrite dropped the old record and its bytes: key 6, then key 5's newest.
    CHECK_EQ(s.BytesOnDisk(), 64ull + c.size() + b.size() + 2 * sizeof(IndexRecord));
    std::vector<uint8_t> out;
    CHECK(s.Find(MakeKey(5), out));
    CHECK(out == b);
    CHECK(s.Find(MakeKey(6), out));
    CHECK(out == c);
    CHECK(!fs::exists(t.File("derived.dat.new")));
    s.Close();
    CHECK_EQ(BytesInDir(t.Path()), 64ull + c.size() + b.size() + 2 * sizeof(IndexRecord));
}

UNIT_TEST(ext_derived_cache_version_and_shape_are_part_of_the_key) {
    TempDir t;
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    const std::vector<uint8_t> a = Payload(64, 5);
    const std::vector<uint8_t> b = Payload(64, 6);
    s.Put(MakeKey(7, 64, 1), a);
    s.Put(MakeKey(7, 128, 1), b);   // the same source hash, another shape: a separate entry
    s.Flush();
    std::vector<uint8_t> out;
    CHECK(!s.Find(MakeKey(7, 64, 2), out));   // a new encoder version misses
    CHECK(s.Find(MakeKey(7, 64, 1), out));
    CHECK(out == a);
    CHECK(s.Find(MakeKey(7, 128, 1), out));
    CHECK(out == b);
    Key other = MakeKey(7, 64, 1);
    other.sourceSize = 4097;   // the same hash with another size: a collision, not a hit
    CHECK(!s.Find(other, out));
    other = MakeKey(7, 64, 1);
    other.producer = MakeProducer('O', 'T', 'H', 'R');
    CHECK(!s.Find(other, out));
}

UNIT_TEST(ext_derived_cache_cap_refuses_then_keeps_the_used) {
    TempDir t;
    const std::vector<uint8_t> p = Payload(1000, 7);
    {
        Store s;
        CHECK(s.Open(t.Path(), 32 + 2500) == Store::OpenStatus::Opened);   // room for two payloads
        s.Put(MakeKey(1), p);
        s.Put(MakeKey(2), p);
        s.Put(MakeKey(3), p);
        s.Flush();
        CHECK_EQ(s.GetStats().stored, 2ull);
        CHECK_EQ(s.GetStats().refusedFull, 1ull);
    }
    {
        // A larger cap: the last run uses entry 2 and adds 3 and 4; 1 goes unused.
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        std::vector<uint8_t> out;
        CHECK(s.Find(MakeKey(2), out));
        s.Put(MakeKey(3), p);
        s.Put(MakeKey(4), p);
        s.Flush();
        CHECK_EQ(s.GetStats().entries, 4ull);
    }
    // Back under a 3-payload cap: 4000 bytes are over it, so the cache keeps what the last run used
    // (2, 3, 4), in their order, and drops 1.
    Store s;
    CHECK(s.Open(t.Path(), 32 + 3500) == Store::OpenStatus::Opened);
    std::vector<uint8_t> out;
    CHECK_EQ(s.GetStats().entries, 3ull);
    CHECK(!s.Find(MakeKey(1), out));
    CHECK(s.Find(MakeKey(2), out));
    CHECK(out == p);
    CHECK(s.Find(MakeKey(3), out));
    CHECK(s.Find(MakeKey(4), out));
    CHECK(out == p);
    CHECK(!fs::exists(t.File("derived.dat.new")));
}

UNIT_TEST(ext_derived_cache_second_instance_and_clear) {
    TempDir t;
    Store a;
    CHECK(a.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    a.Put(MakeKey(1), Payload(100, 8));
    a.Flush();
    Store b;
    CHECK(b.Open(t.Path(), kBig) == Store::OpenStatus::InUse);
    std::vector<uint8_t> out;
    CHECK(!b.Find(MakeKey(1), out));
    b.Put(MakeKey(2), Payload(100, 9));   // a no-op

    CHECK(RequestClear(t.Path()));
    CHECK(ClearRequested(t.Path()));
    a.Disable();
    CHECK(!a.Find(MakeKey(1), out));   // disabled at once
    a.Close();
    CHECK(a.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK(!ClearRequested(t.Path()));
    CHECK_EQ(a.GetStats().entries, 0ull);
    CHECK_EQ(a.BytesOnDisk(), 64ull);
}

UNIT_TEST(ext_derived_cache_unknown_format_starts_empty) {
    TempDir t;
    {
        Store s;
        CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
        s.Put(MakeKey(1), Payload(100, 10));
    }
    {
        std::fstream f(t.File("derived.idx"), std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(8);
        f.put(static_cast<char>(99));   // the format word
    }
    Store s;
    CHECK(s.Open(t.Path(), kBig) == Store::OpenStatus::Opened);
    CHECK_EQ(s.GetStats().entries, 0ull);
    CHECK_EQ(s.BytesOnDisk(), 64ull);
}

// ============================================================================
// tests/unit/ext_compress_ao_test.cpp -- ext.compress_ao (block COMPRESSAO)
// ============================================================================
// The registry row (class A, default ON, off under /profile:faithful, never a pseudo-mod), the
// derived cache's cap key, and the cache's contract with the encoder: a BC4 chain stored in the
// cache and read back after a reopen is byte-identical to a fresh encode of the same levels.
// docs/extensions/handins/COMPRESSAO.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"
#include "bc4/bc4.h"
#include "io/settings_file.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State Resolved(const char* text, const char* cmd = nullptr) {
    IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, nullptr);
}

// An AO-like chain: a shading ramp with grain, every level box-filtered from the one above.
std::vector<std::vector<uint8_t> > Chain(int w, int h) {
    std::vector<std::vector<uint8_t> > levels;
    std::vector<uint8_t> top(static_cast<size_t>(w) * h);
    uint32_t s = 12345;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            s = s * 1664525u + 1013904223u;
            top[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(96 + (x * 97 / w) + ((s >> 24) & 15));
        }
    }
    levels.push_back(top);
    while (w > 1 || h > 1) {
        const int nw = w > 1 ? w / 2 : 1;
        const int nh = h > 1 ? h / 2 : 1;
        const std::vector<uint8_t>& p = levels.back();
        std::vector<uint8_t> n(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                const int x0 = x * 2 < w ? x * 2 : w - 1;
                const int y0 = y * 2 < h ? y * 2 : h - 1;
                const int x1 = x0 + 1 < w ? x0 + 1 : x0;
                const int y1 = y0 + 1 < h ? y0 + 1 : y0;
                const int sum = p[static_cast<size_t>(y0) * w + x0] + p[static_cast<size_t>(y0) * w + x1] +
                                p[static_cast<size_t>(y1) * w + x0] + p[static_cast<size_t>(y1) * w + x1];
                n[static_cast<size_t>(y) * nw + x] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
        levels.push_back(n);
        w = nw;
        h = nh;
    }
    return levels;
}

std::vector<uint8_t> EncodeChain(const std::vector<std::vector<uint8_t> >& levels, int w, int h) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < levels.size(); ++i) {
        const int lw = (w >> i) > 0 ? (w >> i) : 1;
        const int lh = (h >> i) > 0 ? (h >> i) : 1;
        const size_t at = out.size();
        out.resize(at + Bc4::EncodedSize(lw, lh));
        Bc4::EncodeBC4(levels[i].data(), lw, lh, lw, out.data() + at);
    }
    return out;
}

}  // namespace

UNIT_TEST(ext_compress_ao_row) {
    const Ext::Entry* e = Ext::FindEntry("compress_ao");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2), like the other issue-13 rows
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.compress_ao");

    CHECK(Resolved("").ext.compress_ao);
    CHECK(!Resolved("ext.compress_ao=0\n").ext.compress_ao);
    CHECK(!Resolved("ext.compress_ao=1\nprofile=faithful\n").ext.compress_ao);
    CHECK(!Resolved("ext.compress_ao=1\n", "faithful").ext.compress_ao);   // the gate's /profile:faithful

    Ext::State on{};
    on.ext.compress_ao = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().compress_ao);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);   // class A: never in the save
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_compress_ao_cache_cap_key) {
    const Ext::ValueKey* k = Ext::FindValueKey("derived_cache_mb");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK_STREQ(k->owner, "compress_ao");
    CHECK_EQ(k->defaultValue, 4096);
    CHECK_EQ(Resolved("").val.derived_cache_mb, 4096);
    CHECK_EQ(Resolved("ext.derived_cache_mb=1024\n").val.derived_cache_mb, 1024);
    CHECK_EQ(Resolved("ext.derived_cache_mb=1\n").val.derived_cache_mb, 256);   // clamped into range
    CHECK_EQ(Resolved("ext.derived_cache_mb=1024\nprofile=faithful\n").val.derived_cache_mb, 4096);
}

UNIT_TEST(ext_compress_ao_cached_chain_equals_a_fresh_encode) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir =
        fs::temp_directory_path() / ("banished_compress_ao_unit_" + std::to_string(static_cast<long long>(stamp)));
    fs::create_directories(dir);
    const int w = 256;
    const int h = 128;
    const std::vector<std::vector<uint8_t> > levels = Chain(w, h);
    Ext::DerivedCache::Key key;
    key.producer = Ext::DerivedCache::MakeProducer('A', 'O', 'B', '4');
    key.version  = 1;
    key.shape[0] = w;
    key.shape[1] = h;
    key.shape[2] = static_cast<uint32_t>(levels.size());
    key.shape[3] = 4;
    uint64_t hash = 0;
    for (const std::vector<uint8_t>& l : levels) {
        hash = Ext::DerivedCache::Hash64(l.data(), l.size(), hash);
        key.sourceSize += l.size();
    }
    key.sourceHash = hash;
    const std::vector<uint8_t> first = EncodeChain(levels, w, h);
    {
        Ext::DerivedCache::Store s;
        CHECK(s.Open(dir.u8string(), 1ull << 30) == Ext::DerivedCache::Store::OpenStatus::Opened);
        s.Put(key, first);
    }
    std::vector<uint8_t> cached;
    {
        Ext::DerivedCache::Store s;
        CHECK(s.Open(dir.u8string(), 1ull << 30) == Ext::DerivedCache::Store::OpenStatus::Opened);
        CHECK(s.Find(key, cached));
    }
    CHECK(cached == EncodeChain(levels, w, h));   // the encoder is deterministic and the cache exact
    CHECK_EQ(cached.size(), first.size());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ============================================================================
// tests/unit/ext_share_gpu_data_test.cpp -- ext.share_gpu_data (block SHAREGPUDATA)
// ============================================================================
// The registry row (class A, default ON, off under /profile:faithful, never a pseudo-mod) and the
// backend-neutral table (src/render/common/share_gpu_data/share_table.h) driven the way the DX11
// backend drives it, over counted fake objects standing in for COM references: an equal key shares,
// any differing key field does not, the last user frees, a raced insert keeps its own object, a
// clear leaves users their objects, and no reference leaks or is dropped twice.
// docs/extensions/handins/SHAREGPUDATA.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/derived_cache/hash64.h"
#include "io/settings_file.h"
#include "share_gpu_data/share_table.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

// A counted object: what a COM reference count does, nothing more.
struct Obj {
    int refs = 1;   // the creator's reference
};

void AddRef(void* p) {
    ++static_cast<Obj*>(p)->refs;
}

void Release(void* p) {
    --static_cast<Obj*>(p)->refs;
}

GpuShare::Key MakeKey(const void* device, uint32_t kind, const std::vector<uint8_t>& bytes, uint32_t width) {
    GpuShare::Key k;
    k.device = device;
    k.kind   = kind;
    std::memcpy(k.desc, &width, sizeof(width));
    k.descSize     = sizeof(width);
    k.payloadBytes = bytes.size();
    k.payloadHash  = Ext::DerivedCache::Hash64(bytes.data(), bytes.size());
    return k;
}

// The backend's create-or-share over the fake objects (share_create.cpp's shape): a hit takes the
// caller's reference; a miss "creates" `fresh`, takes the table's reference and inserts.
void* CreateOrShare(GpuShare::Table& t, const GpuShare::Key& key, Obj* fresh) {
    void* h[GpuShare::kHandles] = {};
    if (t.Acquire(key, h)) {
        AddRef(h[0]);
        return h[0];
    }
    AddRef(fresh);
    h[0] = fresh;
    if (!t.Insert(key, h)) Release(fresh);
    return fresh;
}

// A user going away: drop from the table (releasing the table's reference when last), then its own.
void UserRelease(GpuShare::Table& t, void* obj) {
    void* h[GpuShare::kHandles] = {};
    if (t.Drop(obj, h)) Release(h[0]);
    Release(obj);
}

}  // namespace

UNIT_TEST(ext_share_gpu_data_row) {
    const Ext::Entry* e = Ext::FindEntry("share_gpu_data");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2), like the other issue-13 rows
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.share_gpu_data");

    CHECK(Resolved("").ext.share_gpu_data);
    CHECK(!Resolved("ext.share_gpu_data=0\n").ext.share_gpu_data);
    CHECK(!Resolved("ext.share_gpu_data=1\nprofile=faithful\n").ext.share_gpu_data);
    CHECK(!Resolved("ext.share_gpu_data=1\n", "faithful").ext.share_gpu_data);   // the gate's /profile:faithful

    Ext::State on{};
    on.ext.share_gpu_data = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().share_gpu_data);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);   // class A: never in the save
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_share_gpu_data_equal_key_shares_and_last_user_frees) {
    GpuShare::Table t;
    int device = 0;
    const std::vector<uint8_t> mesh(4096, 7);
    Obj a, b, c;
    void* ua = CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 64), &a);
    void* ub = CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 64), &b);
    void* uc = CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 64), &c);
    CHECK(ua == &a);
    CHECK(ub == &a);   // shared: the second and third creates hand out the first object
    CHECK(uc == &a);
    CHECK_EQ(a.refs, 4);   // three users + the table
    GpuShare::Stats s = t.GetStats();
    CHECK_EQ(s.hits[GpuShare::kVertexBuffer], 2ull);
    CHECK_EQ(s.hitBytes[GpuShare::kVertexBuffer], 8192ull);
    CHECK_EQ(s.live, 1ull);

    UserRelease(t, ua);   // the creator goes first: the others keep the object
    CHECK_EQ(a.refs, 3);
    UserRelease(t, uc);
    CHECK_EQ(a.refs, 2);
    CHECK_EQ(t.GetStats().live, 1ull);
    UserRelease(t, ub);   // the last user: the table's reference goes too
    CHECK_EQ(a.refs, 0);
    CHECK_EQ(t.GetStats().live, 0ull);
    CHECK_EQ(t.GetStats().freed, 1ull);

    // A later equal create misses (the entry is gone) and makes its own.
    Obj d;
    void* ud = CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 64), &d);
    CHECK(ud == &d);
    UserRelease(t, ud);
    CHECK_EQ(d.refs, 0);
}

UNIT_TEST(ext_share_gpu_data_any_key_field_differs_no_share) {
    GpuShare::Table t;
    int device = 0;
    int otherDevice = 0;
    const std::vector<uint8_t> mesh(512, 3);
    std::vector<uint8_t> changed = mesh;
    changed[511] = 4;
    const std::vector<uint8_t> longer(513, 3);
    Obj base, o1, o2, o3, o4, o5;
    CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 64), &base);
    CHECK(CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, changed, 64), &o1) == &o1);   // bytes
    CHECK(CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, mesh, 65), &o2) == &o2);      // descriptor
    CHECK(CreateOrShare(t, MakeKey(&device, GpuShare::kIndexBuffer, mesh, 64), &o3) == &o3);       // kind
    CHECK(CreateOrShare(t, MakeKey(&otherDevice, GpuShare::kVertexBuffer, mesh, 64), &o4) == &o4); // device
    CHECK(CreateOrShare(t, MakeKey(&device, GpuShare::kVertexBuffer, longer, 64), &o5) == &o5);    // size
    CHECK_EQ(t.GetStats().live, 6ull);
    uint64_t hits = 0;
    for (uint32_t k = 0; k < GpuShare::kKindCount; ++k) {
        hits += t.GetStats().hits[k];
    }
    CHECK_EQ(hits, 0ull);
    // Same hash, same size, a different descriptor blob: still two entries (the blob is compared).
    GpuShare::Key k1 = MakeKey(&device, GpuShare::kTexture, mesh, 64);
    GpuShare::Key k2 = k1;
    k2.desc[3] = 1;
    CHECK(!(k1 == k2));
}

UNIT_TEST(ext_share_gpu_data_raced_insert_keeps_its_own) {
    GpuShare::Table t;
    int device = 0;
    const std::vector<uint8_t> tex(256, 9);
    const GpuShare::Key key = MakeKey(&device, GpuShare::kTexture, tex, 16);
    // Two workers both missed, both created; the first insert wins.
    Obj first, second;
    void* h1[GpuShare::kHandles] = { &first, nullptr };
    void* h2[GpuShare::kHandles] = { &second, nullptr };
    AddRef(&first);
    CHECK(t.Insert(key, h1));
    AddRef(&second);
    CHECK(!t.Insert(key, h2));
    Release(&second);   // the loser drops the table's reference it took
    CHECK_EQ(second.refs, 1);
    CHECK_EQ(t.GetStats().raced, 1ull);
    void* out[GpuShare::kHandles] = {};
    CHECK(!t.Drop(&second, out));   // the loser's object is not shared: its Drop finds nothing
    Release(&second);
    CHECK_EQ(second.refs, 0);
    // A third raced create swaps to the winner's object before anyone saw its own (share_create.cpp).
    Obj third;
    void* h3[GpuShare::kHandles] = { &third, nullptr };
    AddRef(&third);
    CHECK(!t.Insert(key, h3));
    Release(&third);
    void* winner[GpuShare::kHandles] = {};
    CHECK(t.Acquire(key, winner));
    CHECK(winner[0] == &first);
    AddRef(winner[0]);
    Release(&third);   // its own object goes
    CHECK_EQ(third.refs, 0);
    CHECK_EQ(first.refs, 3);   // the creator, the swapped user, the table
    UserRelease(t, winner[0]);
    UserRelease(t, &first);
    CHECK_EQ(first.refs, 0);
}

UNIT_TEST(ext_share_gpu_data_texture_view_travels_with_its_texture) {
    GpuShare::Table t;
    int device = 0;
    const std::vector<uint8_t> tex(1024, 1);
    const GpuShare::Key key = MakeKey(&device, GpuShare::kTexture, tex, 32);
    Obj texture, view;
    AddRef(&texture);
    AddRef(&view);
    void* h[GpuShare::kHandles] = { &texture, &view };
    CHECK(t.Insert(key, h));
    void* got[GpuShare::kHandles] = {};
    CHECK(t.Acquire(key, got));
    CHECK(got[0] == &texture);
    CHECK(got[1] == &view);
    AddRef(got[0]);
    AddRef(got[1]);
    CHECK_EQ(texture.refs, 3);
    CHECK_EQ(view.refs, 3);
    void* out[GpuShare::kHandles] = {};
    CHECK(!t.Drop(&view, out));   // only the texture is the entry's handle
    CHECK(!t.Drop(&texture, out));   // first user: not the last
    Release(&view);
    Release(&texture);
    CHECK(t.Drop(&texture, out));    // second user: the last
    CHECK(out[0] == &texture);
    CHECK(out[1] == &view);
    Release(out[1]);
    Release(out[0]);
    Release(&view);
    Release(&texture);
    CHECK_EQ(texture.refs, 0);
    CHECK_EQ(view.refs, 0);
}

UNIT_TEST(ext_share_gpu_data_clear_leaves_users_their_objects) {
    GpuShare::Table t;
    int device = 0;
    const std::vector<uint8_t> mesh(64, 5);
    Obj a, b;
    void* ua = CreateOrShare(t, MakeKey(&device, GpuShare::kIndexBuffer, mesh, 8), &a);
    void* ub = CreateOrShare(t, MakeKey(&device, GpuShare::kIndexBuffer, mesh, 8), &b);
    CHECK(ub == &a);
    std::vector<void*> released;
    t.Clear(&released);   // the device is going away
    CHECK_EQ(released.size(), static_cast<size_t>(1));
    for (void* p : released) {
        Release(p);
    }
    CHECK_EQ(a.refs, 2);   // the two users still hold it
    CHECK_EQ(t.GetStats().live, 0ull);
    UserRelease(t, ua);    // their Drops find nothing: no double release
    UserRelease(t, ub);
    CHECK_EQ(a.refs, 0);
    CHECK_EQ(b.refs, 1);   // b was never used (the create hit); its creator would never have made it
}

UNIT_TEST(ext_share_gpu_data_threads_never_free_a_handed_out_object) {
    // Four threads create-or-share and release the same key over and over; every object must reach
    // exactly zero references, never below (a free under a user) and never stay above (a leak).
    GpuShare::Table t;
    int device = 0;
    const std::vector<uint8_t> mesh(128, 2);
    const GpuShare::Key key = MakeKey(&device, GpuShare::kVertexBuffer, mesh, 4);
    constexpr int kThreads = 4;
    constexpr int kRounds  = 2000;
    // Atomic counts: the fake objects are touched from several threads here.
    struct AObj {
        std::atomic<int> refs{1};
    };
    std::vector<std::unique_ptr<AObj> > made(kThreads * kRounds);
    for (auto& m : made) {
        m.reset(new AObj);
    }
    std::atomic<int> negative{0};
    auto worker = [&](int id) {
        for (int r = 0; r < kRounds; ++r) {
            AObj* fresh = made[static_cast<size_t>(id * kRounds + r)].get();
            void* h[GpuShare::kHandles] = {};
            AObj* use = fresh;
            if (t.Acquire(key, h)) {
                use = static_cast<AObj*>(h[0]);
                use->refs.fetch_add(1);
                fresh->refs.fetch_sub(1);   // never created: drop the placeholder's own count
            } else {
                fresh->refs.fetch_add(1);
                h[0] = fresh;
                if (!t.Insert(key, h)) fresh->refs.fetch_sub(1);
            }
            void* out[GpuShare::kHandles] = {};
            if (t.Drop(use, out)) {
                if (static_cast<AObj*>(out[0])->refs.fetch_sub(1) <= 0) negative.fetch_add(1);
            }
            if (use->refs.fetch_sub(1) <= 0) negative.fetch_add(1);
        }
    };
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back(worker, i);
    }
    for (auto& th : threads) {
        th.join();
    }
    CHECK_EQ(negative.load(), 0);
    CHECK_EQ(t.GetStats().live, 0ull);
    int leaked = 0;
    for (auto& m : made) {
        if (m->refs.load() != 0) ++leaked;
    }
    CHECK_EQ(leaked, 0);
}

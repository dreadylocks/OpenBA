#pragma once
// ============================================================================
// src/render/common/share_gpu_data/share_table.h -- ext.share_gpu_data's table of shared GPU objects
// (docs/extensions/handins/SHAREGPUDATA.md, issue #13)
// ============================================================================
// NOT RETAIL. Backend-neutral: the handles are opaque pointers, the backend takes and drops its own
// references on them (COM AddRef / Release on DX11). Unit-tested on its own
// (tests/unit/ext_share_gpu_data_test.cpp).
//
// A MegaMod boot creates 2.5 GB of byte-identical immutable buffers and textures (mesh variants,
// *_edge.crs meshes, AO maps shared by building variants). With the row on, the backend keys every
// immutable, data-seeded create by its full descriptor and a hash of the bytes it is seeded with;
// a second create with an equal key hands out the first one's object instead of making a copy.
//
// REFERENCES. The table holds one reference of its own on each entry's handles, taken by the
// creator before Insert, and counts the entry's USERS: the creator, plus one per Acquire. A user
// tells the table before it drops its own reference (Drop); the user that was the last one gets the
// handles back and releases the table's reference too. So:
//   - an entry exists only while at least one user holds the object, and the table's own
//     reference keeps it alive until then: Acquire never hands out a freed object, and the address
//     of a live entry can never be reused by a new object;
//   - nothing outlives its last user: the table never keeps a dead object alive.
// An Acquire's caller takes its own reference after the call: it is already counted, so no other
// user's Drop can reach zero (and free the object) before it does.
//
// Threads: every member takes the table's one mutex; no backend call is made under it. Drop's fast
// path (nothing shared) is one acquire load.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace GpuShare {

// What an entry is, for the counters only (the descriptor already tells kinds apart).
enum Kind : uint32_t { kVertexBuffer = 0, kIndexBuffer = 1, kTexture = 2, kKindCount = 3 };

constexpr size_t kMaxDesc  = 96;   // the largest backend descriptor blob a key carries
constexpr int    kHandles  = 2;    // a resource and its view (a buffer has no view: null)

struct Key {
    const void* device       = nullptr;   // the device the object was made on
    uint32_t    kind         = 0;         // Kind
    uint32_t    descSize     = 0;         // bytes of `desc` in use
    uint64_t    payloadBytes = 0;         // the seed bytes hashed
    uint64_t    payloadHash  = 0;         // their content hash (XXH64, chained per subresource)
    uint8_t     desc[kMaxDesc] = {};      // the full create descriptor, compared byte for byte

    bool operator==(const Key& o) const;
};

struct Stats {
    uint64_t hits[kKindCount]        = {};   // Acquires that found an object
    uint64_t hitBytes[kKindCount]    = {};   // their payload bytes: what the copies would have held
    uint64_t inserts[kKindCount]     = {};   // objects the table started sharing
    uint64_t insertBytes[kKindCount] = {};
    uint64_t raced                   = 0;    // Inserts refused because an equal key got there first
    uint64_t freed                   = 0;    // entries whose last user dropped them
    uint64_t live                    = 0;    // entries now
};

class Table {
public:
    // A hit: `out` receives the entry's handles and the caller counts as one more user. It takes
    // its own reference on each non-null handle after the call. False: no equal key; `out` untouched.
    bool Acquire(const Key& key, void* out[kHandles]);

    // After a miss and a successful create: the entry starts with one user (the creator). The
    // caller has already taken the table's reference on each non-null handle. False: an equal key
    // was inserted meanwhile (two creates raced) or `handles[0]` is already an entry: the caller drops
    // the table's references it took, and either keeps its object unshared or, while no user has
    // seen it yet, Acquires the winner's and drops its own (the DX11 backend does).
    bool Insert(const Key& key, void* const handles[kHandles]);

    // Before a user drops its reference on `resource` (an entry's handles[0]): one user less. When it
    // was the last, the entry is removed, `out` receives its handles and the call returns true: the
    // caller releases the table's reference on each. False for anything not shared.
    bool Drop(const void* resource, void* out[kHandles]);

    // The device is going away: every entry is removed and its handles appended to `out` for the
    // caller to release the table's references. Users that still hold an object keep it; their later
    // Drop finds nothing.
    void Clear(std::vector<void*>* out);

    Stats GetStats() const;

private:
    struct KeyHash {
        size_t operator()(const Key& k) const { return static_cast<size_t>(k.payloadHash ^ (k.payloadBytes << 1)); }
    };
    struct Entry {
        void*   handles[kHandles];
        int64_t users;
    };

    mutable std::mutex                           mutex_;
    std::unordered_map<Key, Entry, KeyHash>      byKey_;
    std::unordered_map<const void*, Key>         byResource_;
    std::atomic<size_t>                          size_{0};
    Stats                                        stats_;
};

}  // namespace GpuShare

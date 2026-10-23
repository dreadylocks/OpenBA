// Pure-logic test of the Metal backend's raw-pointer pipeline cache (src/render/metal/fast_pipeline_cache.h):
// a destroyed owner's address must never hit a stale entry once the address is recycled.
#include "unit.h"

#include <set>
#include <string>

#include "../../src/render/metal/fast_pipeline_cache.h"
#include "../../src/render/metal/retire_pool.h"

namespace {
using Cache = VideoMetal::FastPipelineCache<int>;
int a, b, c, d;   // stand-ins for function / layout / blend objects
}  // namespace

UNIT_TEST(metal_fast_cache_find_insert) {
    Cache cache;
    int v = 0;
    CHECK(!cache.Find("k1", v));
    cache.Insert("k1", 7, { &a, &b });
    CHECK(cache.Find("k1", v));
    CHECK_EQ(v, 7);
    CHECK_EQ(cache.Size(), 1u);
}

UNIT_TEST(metal_fast_cache_evict_drops_every_entry_naming_the_owner) {
    Cache cache;
    cache.Insert("k1", 1, { &a, &b });
    cache.Insert("k2", 2, { &a, &c });
    cache.Insert("k3", 3, { &c, &d });
    CHECK_EQ(cache.Evict(&a), 2u);
    int v = 0;
    CHECK(!cache.Find("k1", v));
    CHECK(!cache.Find("k2", v));
    CHECK(cache.Find("k3", v));
    CHECK_EQ(cache.Size(), 1u);
    CHECK_EQ(cache.Evict(&a), 0u);       // already gone
    CHECK_EQ(cache.Evict(&b), 0u);       // the sibling index entry went with k1
    CHECK_EQ(cache.Evict(&c), 1u);
    CHECK_EQ(cache.Size(), 0u);
}

UNIT_TEST(metal_fast_cache_recycled_address_misses) {
    Cache cache;
    cache.Insert("addr-X", 5, { &a });
    cache.Evict(&a);                     // owner destroyed
    cache.Insert("addr-X", 9, { &a });     // address reused by a new owner: fresh entry, not 5
    int v = 0;
    CHECK(cache.Find("addr-X", v));
    CHECK_EQ(v, 9);
}

UNIT_TEST(metal_fast_cache_reinsert_replaces_and_ignores_null_and_duplicate_owners) {
    Cache cache;
    cache.Insert("k", 1, { &a, nullptr, &a });
    cache.Insert("k", 2, { &b });
    int v = 0;
    CHECK(cache.Find("k", v));
    CHECK_EQ(v, 2);
    CHECK_EQ(cache.Evict(&a), 0u);       // the replaced entry no longer names a
    CHECK_EQ(cache.Evict(nullptr), 0u);
    CHECK_EQ(cache.Evict(&b), 1u);
    cache.Insert("z", 3, { &c });
    cache.Clear();
    CHECK_EQ(cache.Size(), 0u);
}

// ---- retire pool (src/render/metal/retire_pool.h) ----
namespace {
VideoMetal::RetireTicket NewTicket(bool done) {
    return std::make_shared<std::atomic<bool>>(done);
}
}  // namespace

UNIT_TEST(metal_retire_pool_take_needs_done_ticket_and_matching_length) {
    VideoMetal::RetirePool<int> pool;
    auto ticket = NewTicket(false);
    pool.Retire(1, 64, ticket, 0);
    int out = 0;
    auto idle = [](int) { return false; };
    CHECK(!pool.Take(64, idle, out));
    ticket->store(true);
    CHECK(!pool.Take(128, idle, out));
    CHECK(pool.Take(64, idle, out));
    CHECK_EQ(out, 1);
    CHECK_EQ(pool.Size(), 0u);
}

UNIT_TEST(metal_retire_pool_refuses_buffer_used_after_retire) {
    // The race: Draw snapshots buffer B, a rename retires B with the OLD ticket, then Draw records B's use on the
    // CURRENT command buffer. The old ticket completing must not release B while the new use is unfinished.
    VideoMetal::RetirePool<int> pool;
    auto oldTicket = NewTicket(false);
    auto currentTicket = NewTicket(false);
    pool.Retire(7, 256, oldTicket, 0);
    auto busy = [&](int buffer) { return buffer == 7 && !currentTicket->load(); };
    oldTicket->store(true);
    int out = 0;
    CHECK(!pool.Take(256, busy, out));
    currentTicket->store(true);
    CHECK(pool.Take(256, busy, out));
    CHECK_EQ(out, 7);
}

UNIT_TEST(metal_retire_pool_is_bounded_but_keeps_unfinished) {
    VideoMetal::RetirePool<int> pool(4);
    auto pending = NewTicket(false);
    pool.Retire(0, 8, pending, 0);
    for (int i = 1; i < 20; ++i) {
        pool.Retire(i, 8, NewTicket(true), i);
    }
    CHECK_EQ(pool.Size(), 4u);
    CHECK_EQ(pool.Entries().front().buffer, 0);
    for (int i = 20; i < 30; ++i) {
        pool.Retire(i, 8, NewTicket(false), i);
    }
    CHECK_EQ(pool.Size(), 11u);   // the pending one plus ten new: nothing unfinished is dropped
}

UNIT_TEST(metal_retire_pool_trim_drops_by_age) {
    VideoMetal::RetirePool<int> pool;
    pool.Retire(1, 8, NewTicket(true), 0);
    pool.Retire(2, 8, NewTicket(true), 100);
    pool.Trim(200, 180);
    CHECK_EQ(pool.Size(), 1u);
}

UNIT_TEST(metal_pipeline_record_cap_evicts_stale_then_adds) {
    std::set<std::string> keys = { "a", "b", "c" };
    std::vector<std::string> order = { "a", "b", "c" };
    auto staleB = [](const std::string& k) { return k == "b"; };
    CHECK(VideoMetal::MakeRoomForRecord(keys, order, 3, staleB));
    CHECK(keys.count("b") == 0);
    keys.insert("d");
    CHECK(!VideoMetal::MakeRoomForRecord(keys, order, 3, [](const std::string&) { return false; }));
    CHECK_EQ(keys.size(), 3u);
    CHECK(VideoMetal::MakeRoomForRecord(keys, order, 4, staleB));
}

// ============================================================================
// tests/unit/ext_neighbour_sync_test.cpp -- ext.neighbour's nature sync (NOT RETAIL)
// ============================================================================
// src/ext/neighbour/nature_sync.cpp: the per-block digest, the differing blocks, the owner's block
// lists cut into messages, and the plan that makes a block here match the owner's.
// ============================================================================
#include "unit.h"

#include "ext/neighbour/nature_sync.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace NS = Neighbour::NatureSync;

namespace {

NS::Plant P(int cell, const char* tmpl, int grp = 0, int var = 1, int prog = 500) {
    NS::Plant p;
    p.cell = cell;
    p.tmpl = tmpl;
    p.grp = grp;
    p.var = var;
    p.prog = prog;
    return p;
}

}  // namespace

UNIT_TEST(ext_neighbour_sync_blocks_and_halves) {
    // A Medium map: 384 tiles a side, 24 blocks a row, the east half from x = 192.
    CHECK_EQ(NS::BlockOf(0, 384), 0);
    CHECK_EQ(NS::BlockOf(15, 384), 0);
    CHECK_EQ(NS::BlockOf(16, 384), 1);
    CHECK_EQ(NS::BlockOf(16 * 384, 384), 24);
    CHECK_EQ(NS::BlockOf(383 * 384 + 383, 384), 24 * 24 - 1);
    CHECK_EQ(NS::HalfOf(191, 384), 0);
    CHECK_EQ(NS::HalfOf(192, 384), 1);
    CHECK_EQ(NS::HalfOf(5 * 384 + 191, 384), 0);
    // A size that is not a multiple of the block: the last block of a row is narrower.
    CHECK_EQ(NS::BlockOf(20 * 20 - 1, 20), 3);
}

UNIT_TEST(ext_neighbour_sync_digest_round_trip_and_order_free) {
    std::map<int, NS::BlockSum> a, b;
    NS::AddTo(a, 384, 100, "Template\\Tree.rsc:resource");
    NS::AddTo(a, 384, 101, "Template\\Onion.rsc:resource");
    NS::AddTo(a, 384, 9000, "Template\\Tree.rsc:resource");
    NS::AddTo(b, 384, 9000, "Template\\Tree.rsc:resource");
    NS::AddTo(b, 384, 101, "Template\\Onion.rsc:resource");
    NS::AddTo(b, 384, 100, "Template\\Tree.rsc:resource");
    CHECK(a == b);
    CHECK_EQ(a.size(), static_cast<size_t>(2));
    const std::vector<std::string> lists = NS::EncodeDigest(a, 10000);
    CHECK_EQ(lists.size(), static_cast<size_t>(1));
    std::map<int, NS::BlockSum> back;
    CHECK(NS::DecodeDigest(lists[0], back));
    CHECK(back == a);
    // Cut small: one block a list, and still the same digest put together.
    const std::vector<std::string> small = NS::EncodeDigest(a, 10);
    CHECK_EQ(small.size(), static_cast<size_t>(2));
    std::map<int, NS::BlockSum> joined;
    for (const std::string& l : small) CHECK(NS::DecodeDigest(l, joined));
    CHECK(joined == a);
    // An empty digest is one empty list, which decodes to nothing.
    const std::vector<std::string> none = NS::EncodeDigest({}, 100);
    CHECK_EQ(none.size(), static_cast<size_t>(1));
    std::map<int, NS::BlockSum> empty;
    CHECK(NS::DecodeDigest(none[0], empty));
    CHECK(empty.empty());
    // Malformed lists add nothing.
    std::map<int, NS::BlockSum> bad;
    CHECK(!NS::DecodeDigest("1,2", bad));
    CHECK(!NS::DecodeDigest("1,2,zz", bad));
    CHECK(!NS::DecodeDigest("1,2,3;x,1,1", bad));
    CHECK(bad.empty());
}

UNIT_TEST(ext_neighbour_sync_differing_blocks) {
    std::map<int, NS::BlockSum> owner, here;
    NS::AddTo(owner, 384, 100, "T");     // block 6
    NS::AddTo(here, 384, 100, "T");
    NS::AddTo(owner, 384, 200, "T");     // block 12: the owner's only
    NS::AddTo(here, 384, 300, "T");      // block 18: here only
    NS::AddTo(owner, 384, 16 * 384 + 4, "T");   // block 24: a different template here
    NS::AddTo(here, 384, 16 * 384 + 4, "U");
    int skipped = 0;
    std::vector<int> d = NS::Differing(owner, here, {}, &skipped);
    CHECK_EQ(d.size(), static_cast<size_t>(3));
    CHECK_EQ(d[0], 12);
    CHECK_EQ(d[1], 18);
    CHECK_EQ(d[2], 24);
    CHECK_EQ(skipped, 0);
    d = NS::Differing(owner, here, {18, 6}, &skipped);   // 6 is equal anyway: not counted
    CHECK_EQ(d.size(), static_cast<size_t>(2));
    CHECK_EQ(skipped, 1);
}

UNIT_TEST(ext_neighbour_sync_blocks_pack_and_unpack) {
    std::map<int, std::vector<NS::Plant>> plants;
    plants[0].push_back(P(1, "Template\\NaturalResourceTree.rsc:resource", 2, 3, 1000));
    plants[0].push_back(P(2, "Template\\NaturalResourceOnion.rsc:resource", 0, 0, 250));
    plants[1].push_back(P(20, "Template\\NaturalResourceTree.rsc:resource", 1, 4, 10));
    // Block 2 is empty at the owner (asked because this copy has something there).
    const std::vector<int> asked = {0, 1, 2};
    for (size_t limit : {static_cast<size_t>(100000), static_cast<size_t>(60), static_cast<size_t>(1)}) {
        const std::vector<NS::BlockMessage> msgs = NS::PackBlocks(asked, plants, limit);
        CHECK(!msgs.empty());
        std::map<int, std::vector<NS::Plant>> into;
        std::vector<int> done;
        for (const NS::BlockMessage& m : msgs) CHECK(NS::UnpackBlocks(m, 384, into, done));
        CHECK(done == asked);
        CHECK_EQ(into[0].size(), static_cast<size_t>(2));
        CHECK_EQ(into[1].size(), static_cast<size_t>(1));
        CHECK(into.find(2) == into.end() || into[2].empty());
        CHECK_STREQ(into[0][1].tmpl.c_str(), "Template\\NaturalResourceOnion.rsc:resource");
        CHECK_EQ(into[0][0].grp, 2);
        CHECK_EQ(into[0][0].var, 3);
        CHECK_EQ(into[0][0].prog, 1000);
        CHECK_EQ(into[1][0].cell, 20);
        // A block is never listed done before the message with its last entry.
        std::set<int> seen;
        for (const NS::BlockMessage& m : msgs) {
            std::vector<int> d;
            CHECK(NS::ParseInts(m.done, d));
            std::map<int, std::vector<NS::Plant>> one;
            std::vector<int> ignored;
            CHECK(NS::UnpackBlocks(m, 384, one, ignored));
            for (const auto& kv : one) CHECK(seen.count(kv.first) == 0);
            seen.insert(d.begin(), d.end());
        }
    }
    // Malformed messages add nothing.
    std::map<int, std::vector<NS::Plant>> into;
    std::vector<int> done;
    NS::BlockMessage bad;
    bad.tmpls = "T";
    bad.list = "1,1,0,0,0";   // template 1 of a table of one
    CHECK(!NS::UnpackBlocks(bad, 384, into, done));
    bad.list = "999999,0,0,0,0";   // off the map
    CHECK(!NS::UnpackBlocks(bad, 384, into, done));
    bad.list = "1,0,0,0,0";
    bad.done = "x";
    CHECK(!NS::UnpackBlocks(bad, 384, into, done));
    CHECK(into.empty());
    CHECK(done.empty());
}

UNIT_TEST(ext_neighbour_sync_plan_mend) {
    const std::vector<NS::Plant> here = {P(1, "T"), P(2, "T"), P(2, "T"), P(3, "Onion"), P(4, "T")};
    const std::vector<NS::Plant> owner = {P(1, "T"), P(2, "T"), P(3, "Mushroom"), P(4, "T"),
                                          P(5, "T", 7, 2, 900)};
    const NS::Mend m = NS::PlanMend(here, owner);
    // Goes: one of the two trees on tile 2, the onion on 3. Comes: the mushroom on 3, the tree on 5.
    CHECK_EQ(m.remove.size(), static_cast<size_t>(2));
    std::set<size_t> gone(m.remove.begin(), m.remove.end());
    CHECK(gone.count(3) == 1);
    CHECK(gone.count(1) == 1 || gone.count(2) == 1);
    CHECK_EQ(m.add.size(), static_cast<size_t>(2));
    std::set<std::string> come;
    for (const NS::Plant& p : m.add) come.insert(std::to_string(p.cell) + p.tmpl);
    CHECK(come.count("3Mushroom") == 1);
    CHECK(come.count("5T") == 1);
    for (const NS::Plant& p : m.add)
        if (p.cell == 5) {
            CHECK_EQ(p.grp, 7);
            CHECK_EQ(p.var, 2);
            CHECK_EQ(p.prog, 900);
        }
    // Equal blocks need nothing.
    const NS::Mend same = NS::PlanMend(owner, owner);
    CHECK(same.remove.empty());
    CHECK(same.add.empty());
}

UNIT_TEST(ext_neighbour_sync_ints) {
    std::vector<int> v;
    CHECK(NS::ParseInts("", v));
    CHECK(v.empty());
    CHECK(NS::ParseInts("3,-1,40", v));
    CHECK_EQ(v.size(), static_cast<size_t>(3));
    CHECK_EQ(v[1], -1);
    CHECK_STREQ(NS::JoinInts(v).c_str(), "3,-1,40");
    std::vector<int> w;
    CHECK(!NS::ParseInts("1,,2", w));
    CHECK(!NS::ParseInts("1,a", w));
    CHECK(w.empty());
}

UNIT_TEST(ext_neighbour_sync_settled_blocks) {
    // Block 7 could not be made to match (a lost rock): settled with the owner's sum at the time.
    std::map<int, NS::BlockSum> owner, here;
    NS::AddTo(owner, 384, 7 * 16, "Rock");
    NS::AddTo(owner, 384, 7 * 16 + 1, "Tree");
    NS::AddTo(here, 384, 7 * 16 + 1, "Tree");
    std::map<int, NS::Settled> settled;
    settled[7].owner = owner[7];
    // The next digest: the same owner sum; this copy's sum is learnt and the block stays out.
    std::set<int> still = NS::StillSettled(settled, owner, here);
    CHECK(still.count(7) == 1);
    CHECK(settled[7].hereKnown);
    CHECK(settled[7].here == here[7]);
    // Neither copy changed: still settled.
    still = NS::StillSettled(settled, owner, here);
    CHECK(still.count(7) == 1);
    // This copy changed (a plant spread there): asked for again.
    std::map<int, NS::BlockSum> here2 = here;
    NS::AddTo(here2, 384, 7 * 16 + 2, "Onion");
    still = NS::StillSettled(settled, owner, here2);
    CHECK(still.empty());
    CHECK(settled.empty());
    // The owner's copy changed: asked for again too.
    settled[7].owner = owner[7];
    std::map<int, NS::BlockSum> owner2 = owner;
    NS::AddTo(owner2, 384, 7 * 16 + 3, "Tree");
    still = NS::StillSettled(settled, owner2, here);
    CHECK(still.empty());
    CHECK(settled.empty());
}

UNIT_TEST(ext_neighbour_sync_settled_block_emptied_at_the_owner) {
    // A settled block whose plants are all gone at the owner (its digest no longer lists it): asked again.
    std::map<int, NS::BlockSum> owner, here;
    NS::AddTo(owner, 384, 3, "Rock");
    std::map<int, NS::Settled> settled;
    settled[0].owner = owner[0];
    CHECK(NS::StillSettled(settled, owner, here).count(0) == 1);
    const std::map<int, NS::BlockSum> emptied;
    CHECK(NS::StillSettled(settled, emptied, here).empty());
    CHECK(settled.empty());
}

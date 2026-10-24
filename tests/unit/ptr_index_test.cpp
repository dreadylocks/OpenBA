// ============================================================================
// tests/unit/ptr_index_test.cpp -- block VISITPASS: the pointer-keyed index
//   (src/engine/collection/ptr_index.h) names the one node holding an object
// ============================================================================
// The oracle is a std::map<object, node> filled the way the resource tree is
// filled (one insert per node, the object written once at insert), under
// random inserts, erases and probes; every answer is compared by address, the
// per-row flag follows its row, and the decline rules (a second tree, a count
// that differs, a null object, a duplicate object, an erase that went round
// the hooks) each decline rather than answer. The game's own tree is checked
// live by the debug exe (`[VisitPass] MISMATCH`).
// ============================================================================
#include "unit.h"
#include "collection/ptr_index.h"

#include <cstdint>
#include <map>
#include <random>
#include <vector>

using Collection::PtrIndex;

namespace {

struct FakeNode {
    const void* external;
    int         pad;
};

const int kTree = 1;   // any distinct address does as a tree identity
const int kOtherTree = 2;

bool Answer(const PtrIndex& ix, const void* tree, int64_t count, const void* key, const void*& node, bool& flag) {
    node = nullptr;
    flag = false;
    return ix.Find(tree, count, key, &node, &flag);
}

}  // namespace

UNIT_TEST(ptr_index_empty_and_unbound) {
    PtrIndex ix;
    const void* node = nullptr;
    bool flag = true;
    int obj = 0;
    // an empty tree it was never bound to: no row holds anything
    CHECK(Answer(ix, &kTree, 0, &obj, node, flag));
    CHECK(node == nullptr);
    CHECK(!flag);
    // a non-empty tree it was never bound to: decline
    CHECK(!Answer(ix, &kTree, 3, &obj, node, flag));
    // a null object: decline
    CHECK(!Answer(ix, &kTree, 0, nullptr, node, flag));
    CHECK(!ix.SetFlag(&kTree, &obj, true));
}

UNIT_TEST(ptr_index_random_inserts_erases_probes_match_a_map) {
    PtrIndex ix;
    std::mt19937 rng(20261023);
    std::vector<int> objects(5000);
    std::vector<FakeNode> nodes;
    nodes.reserve(60000);   // one push per step at most: the addresses stay put
    std::map<const void*, const FakeNode*> live;   // object -> its node
    std::map<const void*, bool> flags;
    int64_t count = 0;

    auto probe = [&](const void* key) {
        const void* node = nullptr;
        bool flag = false;
        CHECK(Answer(ix, &kTree, count, key, node, flag));
        auto it = live.find(key);
        if (it == live.end()) {
            CHECK(node == nullptr);
            CHECK(!flag);
        } else {
            CHECK(node == it->second);
            CHECK(flag == flags[key]);
        }
    };

    for (int step = 0; step < 60000; ++step) {
        const int pick = static_cast<int>(rng() % objects.size());
        const void* key = &objects[pick];
        const int op = static_cast<int>(rng() % 10);
        auto it = live.find(key);
        if (op < 5) {
            if (it == live.end()) {
                nodes.push_back(FakeNode{key, 0});
                const FakeNode* n = &nodes.back();
                live[key] = n;
                flags[key] = false;
                ++count;
                ix.Insert(&kTree, n, key);
            }
        } else if (op < 8) {
            if (it != live.end()) {
                ix.Erase(&kTree, it->second, key);
                --count;
                live.erase(it);
                flags.erase(key);
            }
        } else if (op == 8) {
            if (it != live.end()) {
                CHECK(ix.SetFlag(&kTree, key, true));
                flags[key] = true;
            } else {
                CHECK(!ix.SetFlag(&kTree, key, true));
            }
        } else {
            probe(key);
        }
        if ((step & 1023) == 0) {
            for (int k = 0; k < 50; ++k)
                probe(&objects[rng() % objects.size()]);
        }
    }
    CHECK(!ix.Broken());
    CHECK_EQ(ix.Size(), live.size());
    for (const auto& kv : live)
        probe(kv.first);
}

UNIT_TEST(ptr_index_flag_follows_its_row) {
    PtrIndex ix;
    int a = 0;
    int b = 0;
    FakeNode na{&a, 0};
    FakeNode nb{&b, 0};
    ix.Insert(&kTree, &na, &a);
    ix.Insert(&kTree, &nb, &b);
    CHECK(ix.SetFlag(&kTree, &a, true));
    const void* node = nullptr;
    bool flag = false;
    CHECK(Answer(ix, &kTree, 2, &a, node, flag));
    CHECK(node == &na);
    CHECK(flag);
    CHECK(Answer(ix, &kTree, 2, &b, node, flag));
    CHECK(node == &nb);
    CHECK(!flag);
    // the row goes, the flag goes with it; a new row for the same object starts clear
    ix.Erase(&kTree, &na, &a);
    CHECK(Answer(ix, &kTree, 1, &a, node, flag));
    CHECK(node == nullptr);
    CHECK(!flag);
    FakeNode na2{&a, 1};
    ix.Insert(&kTree, &na2, &a);
    CHECK(Answer(ix, &kTree, 2, &a, node, flag));
    CHECK(node == &na2);
    CHECK(!flag);
}

UNIT_TEST(ptr_index_declines) {
    const void* node = nullptr;
    bool flag = false;
    int a = 0;
    int b = 0;
    FakeNode na{&a, 0};
    FakeNode nb{&b, 0};
    {
        // a count that differs from the tree's: a mutation went round the hooks
        PtrIndex ix;
        ix.Insert(&kTree, &na, &a);
        CHECK(Answer(ix, &kTree, 1, &a, node, flag));
        CHECK(!Answer(ix, &kTree, 2, &a, node, flag));
        // another tree
        CHECK(!Answer(ix, &kOtherTree, 1, &a, node, flag));
        CHECK(!Answer(ix, &kOtherTree, 0, &a, node, flag));
    }
    {
        // a row with a null object counts toward the tree but is not indexed
        PtrIndex ix;
        FakeNode nn{nullptr, 0};
        ix.Insert(&kTree, &nn, nullptr);
        ix.Insert(&kTree, &na, &a);
        CHECK(Answer(ix, &kTree, 2, &a, node, flag));
        CHECK(node == &na);
        ix.Erase(&kTree, &nn, nullptr);
        CHECK(Answer(ix, &kTree, 1, &a, node, flag));
        CHECK(node == &na);
    }
    {
        // two rows holding one object: ambiguous, declines for good
        PtrIndex ix;
        FakeNode na2{&a, 1};
        ix.Insert(&kTree, &na, &a);
        ix.Insert(&kTree, &na2, &a);
        CHECK(ix.Broken());
        CHECK_EQ(ix.Duplicates(), static_cast<uint64_t>(1));
        CHECK(!Answer(ix, &kTree, 2, &a, node, flag));
        CHECK(!ix.SetFlag(&kTree, &a, true));
    }
    {
        // a second tree: declines for both
        PtrIndex ix;
        ix.Insert(&kTree, &na, &a);
        ix.Insert(&kOtherTree, &nb, &b);
        CHECK(ix.Broken());
        CHECK(!Answer(ix, &kTree, 1, &a, node, flag));
    }
    {
        // an erase of a row the index never saw
        PtrIndex ix;
        ix.Insert(&kTree, &na, &a);
        ix.Erase(&kTree, &nb, &b);
        CHECK(ix.Broken());
        CHECK(!Answer(ix, &kTree, 1, &a, node, flag));
    }
    {
        // Clear unbinds: the next bind may be another tree
        PtrIndex ix;
        ix.Insert(&kTree, &na, &a);
        ix.SetFlag(&kTree, &a, true);
        ix.Clear(&kTree);
        CHECK_EQ(ix.Size(), static_cast<size_t>(0));
        ix.Insert(&kOtherTree, &nb, &b);
        CHECK(Answer(ix, &kOtherTree, 1, &b, node, flag));
        CHECK(node == &nb);
        CHECK(!flag);
        CHECK(!ix.Broken());
    }
}

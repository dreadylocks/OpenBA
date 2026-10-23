// ============================================================================
// tests/unit/ext_draw_gather_test.cpp -- DRAWGATHER: ext.draw_gather (class A)
// ============================================================================
// src/ext/draw_gather/gather_side.{h,cpp} on plain structs, the engine's add log
// (src/engine/graphics/scene_ext.h) on its own, and the registry row:
//   * the side array: store / valid / invalidate / growth / clear, hints apart
//     from the box;
//   * NodeVisible gives retail's `live && box` in either order, and skips the
//     Geometry read when the box test fails first;
//   * Clean's upkeep rule, and a scripted scene that models Geometry::Clean's
//     two-phase box machine, AddObject (with its own Clean and id reuse at the
//     same address) and RemoveObject, checking after every step that every
//     record the gather would trust holds the live box (the "dirty epoch");
//   * the gather order: a walk over records gives the same visible sequence as
//     a walk over the live objects, over random worlds;
//   * the scene table's LRU, the add log's drain and overflow.
// docs/extensions/handins/DRAWGATHER.md
// ============================================================================
#include "unit.h"
#include "ext/draw_gather/gather_side.h"
#include "ext/registry.h"
#include "graphics/scene_ext.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Ext::DrawGather::SideArray;
using Ext::DrawGather::SideRecord;

// A model geometry: the fields the gather and Clean read, with Geometry::Clean's
// box half (scene.cpp 0x1800a4420): bit 1 = local box changed -> recompute the
// world box, set bit 0x10, report a change; bit 0x10 alone -> clear, report.
struct ModelGeo {
    float    local[6];
    float    tx = 0.0f;                 // the transform (a translation is enough here)
    float    world[6];
    uint32_t dirty = 0;
    uint32_t flags = 1;
    int32_t  detail = 0;

    void SetBounds(const float b[6]) {
        if (std::memcmp(local, b, sizeof(local)) != 0) { std::memcpy(local, b, sizeof(local)); dirty |= 2u; }
    }
    void SetTransform(float x) {
        if (x != tx) { tx = x; dirty |= 1u | 2u; }   // a move recomputes the box at the next Clean
    }
    bool Clean() {
        bool changed = false;
        if (dirty & 1u) { dirty = (dirty & ~1u) | 8u; changed = true; }
        else if (dirty & 8u) { dirty &= ~8u; changed = true; }
        if (dirty & 2u) {
            dirty = (dirty & ~2u) | 0x10u;
            for (int k = 0; k < 6; ++k) world[k] = local[k] + ((k == 0 || k == 3) ? tx : 0.0f);
            changed = true;
        } else if (dirty & 0x10u) {
            dirty &= ~0x10u;
            changed = true;
        }
        return changed;
    }
};

// A model scene: node id -> geometry, a LIFO free list (retail's), the add log.
struct ModelScene {
    std::vector<ModelGeo*> nodes;
    std::vector<uint32_t>  freeIds;
    Graphics::SceneAddLog* log = nullptr;

    uint32_t AddObject(ModelGeo* g) {
        if (g->dirty != 0) g->Clean();                  // Scene::AddObject's own Clean
        uint32_t id;
        if (!freeIds.empty()) { id = freeIds.back(); freeIds.pop_back(); nodes[id] = g; }
        else { id = static_cast<uint32_t>(nodes.size()); nodes.push_back(g); }
        if (log) log->Note(nullptr, id);
        return id;
    }
    void RemoveObject(uint32_t id) { nodes[id] = nullptr; freeIds.push_back(id); }
};

void Drain(Graphics::SceneAddLog& log, SideArray& side) {
    bool overflow = false;
    log.Drain([&](const Graphics::SceneAddLog::Entry& e) { side.Invalidate(static_cast<int32_t>(e.id)); },
              [&](bool o) { overflow = o; });
    if (overflow) side.Clear();
}

// The extension's Clean over the model.
void ExtClean(ModelScene& sc, SideArray& side, Graphics::SceneAddLog& log) {
    Drain(log, side);
    for (size_t i = 0; i < sc.nodes.size(); ++i) {
        ModelGeo* g = sc.nodes[i];
        if (g == nullptr) continue;
        const bool changed = g->dirty != 0 && g->Clean();
        Ext::DrawGather::UpkeepAfterClean(side, static_cast<int32_t>(i), g, changed,
                                     [g](float* b) { std::memcpy(b, g->world, sizeof(g->world)); },
                                     g->flags, g->detail);
    }
}

// Every record the gather would trust (Valid) holds the live box.
bool TrustedRecordsAreLive(const ModelScene& sc, Graphics::SceneAddLog& log, SideArray& side) {
    Drain(log, side);   // the gather drains first too
    for (size_t i = 0; i < sc.nodes.size(); ++i) {
        const ModelGeo* g = sc.nodes[i];
        if (g == nullptr || !side.Valid(static_cast<int32_t>(i), g)) continue;
        if (std::memcmp(side.At(static_cast<int32_t>(i)).box, g->world, sizeof(g->world)) != 0) return false;
    }
    return true;
}

const float kBox0[6] = { 0, 0, 0, 1, 1, 1 };

}  // namespace

UNIT_TEST(ext_draw_gather_side_array_store_valid_invalidate) {
    SideArray s;
    int a = 0, b = 0;
    CHECK(!s.Valid(0, &a));
    const float box[6] = { 1, 2, 3, 4, 5, 6 };
    s.Store(5000, &a, box, 7u, 2);
    CHECK(s.Size() >= 5001);
    CHECK(s.Valid(5000, &a));
    CHECK(!s.Valid(5000, &b));          // another owner at the same id
    CHECK(!s.Valid(4999, &a));          // grown slots start invalid
    CHECK(!s.Valid(5000, nullptr));
    CHECK(!s.Valid(-1, &a));
    CHECK(std::memcmp(s.At(5000).box, box, sizeof(box)) == 0);
    s.StoreHints(5000, 9u, -1);
    CHECK(s.At(5000).flagsHint == 9u && s.At(5000).detailHint == -1);
    CHECK(std::memcmp(s.At(5000).box, box, sizeof(box)) == 0);   // hints never touch the box
    s.Invalidate(5000);
    CHECK(!s.Valid(5000, &a));
    s.Invalidate(1 << 30);              // out of range: ignored
    s.Store(3, &a, box, 1u, 0);
    s.Clear();
    CHECK(s.Size() == 0 && !s.Valid(3, &a));
}

UNIT_TEST(ext_draw_gather_node_visible_is_live_and_box_in_either_order) {
    for (int hint = 0; hint < 2; ++hint)
        for (int live = 0; live < 2; ++live)
            for (int box = 0; box < 2; ++box) {
                int liveCalls = 0, boxCalls = 0;
                const bool r = Ext::DrawGather::NodeVisible(
                    hint != 0,
                    [&] { ++liveCalls; return live != 0; },
                    [&] { ++boxCalls; return box != 0; });
                CHECK(r == (live != 0 && box != 0));
                if (hint) {
                    CHECK(boxCalls == 1);
                    CHECK(liveCalls == (box ? 1 : 0));   // a frustum reject never reads the Geometry
                } else {
                    CHECK(liveCalls == 1);
                    CHECK(boxCalls == (live ? 1 : 0));
                }
            }
    SideRecord r{};
    r.flagsHint = 0x6u; r.detailHint = 1;
    CHECK(Ext::DrawGather::HintPasses(r, 0x2u, 1));
    CHECK(!Ext::DrawGather::HintPasses(r, 0x1u, 1));
    CHECK(!Ext::DrawGather::HintPasses(r, 0x2u, 0));   // signed compare, as retail's
    r.detailHint = -5;
    CHECK(Ext::DrawGather::HintPasses(r, 0x4u, -5));
}

UNIT_TEST(ext_draw_gather_upkeep_reads_the_box_only_on_refresh) {
    SideArray s;
    int owner = 0, other = 0;
    int boxReads = 0;
    auto box = [&](float* b) { ++boxReads; std::memcpy(b, kBox0, sizeof(kBox0)); };
    CHECK(Ext::DrawGather::UpkeepAfterClean(s, 2, &owner, false, box, 1u, 0));   // invalid: refresh
    CHECK(boxReads == 1);
    CHECK(!Ext::DrawGather::UpkeepAfterClean(s, 2, &owner, false, box, 3u, 1));  // valid, unchanged: hints
    CHECK(boxReads == 1);
    CHECK(s.At(2).flagsHint == 3u && s.At(2).detailHint == 1);
    CHECK(Ext::DrawGather::UpkeepAfterClean(s, 2, &owner, true, box, 3u, 1));    // Clean reported a change
    CHECK(boxReads == 2);
    CHECK(Ext::DrawGather::UpkeepAfterClean(s, 2, &other, false, box, 3u, 1));   // a different owner
    CHECK(boxReads == 3);
}

// The dirty epoch: scripted add / move / re-bound / remove / re-add at the same
// address and id, with Cleans and gathers interleaved anywhere (an AddObject
// between Clean and the gather included). Every trusted record is live.
UNIT_TEST(ext_draw_gather_records_stay_live_over_a_scripted_scene) {
    std::mt19937 rng(12345);
    Graphics::SceneAddLog log;
    ModelScene sc;
    sc.log = &log;
    SideArray side;
    std::vector<ModelGeo> pool(64);
    for (ModelGeo& g : pool) std::memcpy(g.local, kBox0, sizeof(kBox0));
    std::vector<int> nodeOf(pool.size(), -1);   // pool index -> node id or -1

    long long checks = 0;
    for (int step = 0; step < 20000; ++step) {
        const int k = static_cast<int>(rng() % pool.size());
        ModelGeo& g = pool[static_cast<size_t>(k)];
        switch (rng() % 7) {
            case 0:   // add (a removed object comes back: LIFO -> often the same id, same address)
                if (nodeOf[k] < 0) nodeOf[k] = static_cast<int>(sc.AddObject(&g));
                break;
            case 1:
                if (nodeOf[k] >= 0) { sc.RemoveObject(static_cast<uint32_t>(nodeOf[k])); nodeOf[k] = -1; }
                break;
            case 2: g.SetTransform(static_cast<float>(rng() % 50)); break;   // in or out of the scene
            case 3: {
                float b[6] = { 0, 0, 0, 1.0f + static_cast<float>(rng() % 4), 1, 1 };
                g.SetBounds(b);
                break;
            }
            case 4: g.flags |= 1u << (rng() % 3); break;   // SetMesh: flags change, nothing dirty
            case 5: ExtClean(sc, side, log); break;
            default:
                CHECK(TrustedRecordsAreLive(sc, log, side));
                ++checks;
                break;
        }
    }
    CHECK(checks > 1000);
}

// Without the add log the same script finds a stale record (the log is load-bearing):
// re-adding a moved geometry at its old id and address between two Cleans.
UNIT_TEST(ext_draw_gather_add_log_is_needed_for_a_readd_at_the_same_id) {
    Graphics::SceneAddLog log;
    ModelScene sc;                       // sc.log stays null: AddObject is not logged
    SideArray side;
    ModelGeo g;
    std::memcpy(g.local, kBox0, sizeof(kBox0));
    const uint32_t id = sc.AddObject(&g);
    ExtClean(sc, side, log);
    ExtClean(sc, side, log);             // settled: the record is valid and live
    CHECK(TrustedRecordsAreLive(sc, log, side));
    sc.RemoveObject(id);
    g.SetTransform(10.0f);
    CHECK(sc.AddObject(&g) == id);       // AddObject's own Clean moves the box
    CHECK(!TrustedRecordsAreLive(sc, log, side));
    sc.log = &log;                       // the same with the log: drained, invalid, refreshed
    sc.RemoveObject(id);
    g.SetTransform(20.0f);
    CHECK(sc.AddObject(&g) == id);
    CHECK(TrustedRecordsAreLive(sc, log, side));
}

// The order: a gather over the records pushes exactly the sequence a gather over
// the live objects pushes (random worlds; the "frustum" is an x-interval and the
// pass mask a bit set, both pure).
UNIT_TEST(ext_draw_gather_record_walk_gives_the_live_sequence) {
    std::mt19937 rng(777);
    for (int world = 0; world < 200; ++world) {
        const int n = 1 + static_cast<int>(rng() % 300);
        std::vector<ModelGeo> geos(static_cast<size_t>(n));
        SideArray side;
        for (int i = 0; i < n; ++i) {
            ModelGeo& g = geos[static_cast<size_t>(i)];
            for (int k = 0; k < 6; ++k) g.world[k] = static_cast<float>(rng() % 100) - 50.0f;
            g.flags  = 1u << (rng() % 4);
            g.detail = static_cast<int32_t>(rng() % 3);
            // Records: most valid with matching hints, some with stale hints, some invalid.
            const unsigned kind = rng() % 4;
            if (kind != 3)
                side.Store(i, &g, g.world, kind == 2 ? ~g.flags : g.flags, kind == 2 ? 9 : g.detail);
        }
        const uint32_t passMask = 1u + rng() % 15u;
        const int32_t  rDetail  = static_cast<int32_t>(rng() % 3);
        const float lo = static_cast<float>(rng() % 60) - 30.0f, hi = lo + 20.0f;
        auto boxOk = [&](const float* b) { return b[3] >= lo && b[0] <= hi; };

        std::vector<int> live, ext;
        int geoReads = 0;
        for (int i = 0; i < n; ++i) {
            const ModelGeo& g = geos[static_cast<size_t>(i)];
            if ((g.flags & passMask) != 0 && g.detail <= rDetail && boxOk(g.world)) live.push_back(i);
        }
        for (int i = 0; i < n; ++i) {
            const ModelGeo& g = geos[static_cast<size_t>(i)];
            if (!side.Valid(i, &g)) side.Store(i, &g, g.world, g.flags, g.detail);
            const SideRecord& r = side.At(i);
            if (Ext::DrawGather::NodeVisible(
                    Ext::DrawGather::HintPasses(r, passMask, rDetail),
                    [&] { ++geoReads; return (g.flags & passMask) != 0 && g.detail <= rDetail; },
                    [&] { return boxOk(r.box); }))
                ext.push_back(i);
        }
        CHECK(live == ext);
        CHECK(geoReads <= n);
    }
}

UNIT_TEST(ext_draw_gather_scene_table_lru) {
    Ext::DrawGather::SceneSides t;
    int s[6];
    for (int i = 0; i < 4; ++i) t.Acquire(&s[i]).Store(0, &s[i], kBox0, 1u, 0);
    CHECK(t.Count() == 4);
    CHECK(t.Find(&s[0]) != nullptr);          // s[0] now the most recent
    t.Acquire(&s[4]);                         // evicts s[1], the least recently used
    CHECK(t.Find(&s[1]) == nullptr);
    CHECK(t.Find(&s[0]) != nullptr && t.Find(&s[0])->Valid(0, &s[0]));
    SideArray& again = t.Acquire(&s[1]);      // comes back empty
    CHECK(!again.Valid(0, &s[1]));
    t.ForgetAll();
    CHECK(t.Count() == 0 && t.Find(&s[0]) == nullptr);
}

UNIT_TEST(ext_draw_gather_add_log_drain_and_overflow) {
    Graphics::SceneAddLog log;
    int scene = 0;
    log.Note(nullptr, 7);
    log.Note(nullptr, 9);
    std::vector<uint32_t> got;
    bool ov = true;
    log.Drain([&](const Graphics::SceneAddLog::Entry& e) { got.push_back(e.id); }, [&](bool o) { ov = o; });
    CHECK(got.size() == 2 && got[0] == 7 && got[1] == 9 && !ov);
    CHECK(log.PendingUnlocked() == 0);
    for (int i = 0; i < Graphics::SceneAddLog::kCapacity + 3; ++i) log.Note(nullptr, static_cast<uint32_t>(i));
    got.clear();
    log.Drain([&](const Graphics::SceneAddLog::Entry& e) { got.push_back(e.id); }, [&](bool o) { ov = o; });
    CHECK(static_cast<int>(got.size()) == Graphics::SceneAddLog::kCapacity && ov);
    log.Drain([&](const Graphics::SceneAddLog::Entry&) { CHECK(false); }, [&](bool o) { ov = o; });
    CHECK(!ov);                               // overflow reported once
    (void)scene;
}

UNIT_TEST(ext_draw_gather_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("draw_gather");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2, ruled 2026-10-18)
    CHECK(e->formatVersion == 0);
}

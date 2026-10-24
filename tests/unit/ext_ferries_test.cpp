// ============================================================================
// tests/unit/ext_ferries_test.cpp -- ext.ferries' pure part (NOT RETAIL)
// ============================================================================
// src/ext/ferries/lanes.cpp: which Ferry Docks are linked across water, the lane
// between them, the path worker's types along it and the zones joined.
// docs/fork/FERRIES.md.
// ============================================================================
#include "unit.h"

#include "ext/ferries/lanes.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Ext::Ferries;

namespace {

// A made-up map from rows of characters: 'a'..'e' open ground (type 2) in zone 1..5, '~' deep
// water (8), '-' shallow water (7), 'x' high ground (6, not walkable). Short rows and the square's
// missing rows are high ground.
struct TestMap {
    int size = 0;
    std::vector<uint8_t> type;
    std::vector<uint16_t> zone;

    explicit TestMap(const std::vector<std::string>& rows) {
        size = static_cast<int>(rows.size());
        for (const std::string& r : rows) size = std::max(size, static_cast<int>(r.size()));
        type.assign(static_cast<size_t>(size) * size, 6);
        zone.assign(type.size(), 0);
        for (int y = 0; y < static_cast<int>(rows.size()); ++y)
            for (int x = 0; x < static_cast<int>(rows[y].size()); ++x) {
                const char ch = rows[y][x];
                const size_t i = static_cast<size_t>(y) * size + x;
                if (ch >= 'a' && ch <= 'e') {
                    type[i] = 2;
                    zone[i] = static_cast<uint16_t>(ch - 'a' + 1);
                } else if (ch == '~') {
                    type[i] = 8;
                } else if (ch == '-') {
                    type[i] = 7;
                }
            }
    }
    MapView View() const { return MapView{ size, type.data(), zone.data() }; }
    int Cell(int x, int y) const { return y * size + x; }
};

DockSite Dock(const TestMap& m, int x, int y, int w, int h, bool built = true) {
    DockSite d;
    d.built = built;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) d.cells.push_back(m.Cell(xx, yy));
    return d;
}

bool Touching(int size, int a, int b) {
    const int dx = std::abs(a % size - b % size), dy = std::abs(a / size - b / size);
    return dx <= 1 && dy <= 1 && a != b;
}

// A lake between two shores, zone 1 west and zone 2 east.
const std::vector<std::string> kLake = {
    "aaaa~~~~~~bbbb",
    "aaaa~~~~~~bbbb",
    "aaaa~~~~~~bbbb",
    "aaaa~~~~~~bbbb",
    "aaaa~~~~~~bbbb",
    "aaaa~~~~~~bbbb",
};

}  // namespace

UNIT_TEST(ext_ferries_links_two_shores) {
    const TestMap m(kLake);
    const std::vector<DockSite> docks{ Dock(m, 2, 2, 2, 2), Dock(m, 10, 2, 2, 2) };
    const std::vector<Link> links = FindLinks(m.View(), docks);
    CHECK_EQ(links.size(), static_cast<size_t>(1));
    const Link& l = links[0];
    CHECK_EQ(l.dockA, 0);
    CHECK_EQ(l.dockB, 1);
    CHECK_EQ(l.zoneA, static_cast<uint16_t>(1));
    CHECK_EQ(l.zoneB, static_cast<uint16_t>(2));
    CHECK(!l.lane.empty());
    // Every lane cell is water and each touches the one before it; the ends touch the landings.
    for (size_t i = 0; i < l.lane.size(); ++i) {
        CHECK(IsWater(m.type[static_cast<size_t>(l.lane[i])]));
        if (i > 0) CHECK(Touching(m.size, l.lane[i - 1], l.lane[i]));
    }
    CHECK(Touching(m.size, l.landA, l.lane.front()));
    CHECK(Touching(m.size, l.landB, l.lane.back()));
    CHECK_EQ(m.zone[static_cast<size_t>(l.landA)], static_cast<uint16_t>(1));
    CHECK_EQ(m.zone[static_cast<size_t>(l.landB)], static_cast<uint16_t>(2));
    // Straight across: the six water columns, no detour.
    CHECK_EQ(l.lane.size(), static_cast<size_t>(6));
}

UNIT_TEST(ext_ferries_needs_one_finished_dock) {
    const TestMap m(kLake);
    // Two construction sites: nothing yet. One finished: the builders can row over.
    CHECK(FindLinks(m.View(), { Dock(m, 2, 2, 2, 2, false), Dock(m, 10, 2, 2, 2, false) }).empty());
    CHECK_EQ(FindLinks(m.View(), { Dock(m, 2, 2, 2, 2, true), Dock(m, 10, 2, 2, 2, false) }).size(),
             static_cast<size_t>(1));
    CHECK_EQ(FindLinks(m.View(), { Dock(m, 2, 2, 2, 2, false), Dock(m, 10, 2, 2, 2, true) }).size(),
             static_cast<size_t>(1));
}

UNIT_TEST(ext_ferries_same_land_is_not_linked) {
    // A bay: both docks on zone 1, water between them. Walking round is the way.
    const TestMap m({
        "aaaaaaaaaaaa",
        "aa~~~~~~~~aa",
        "aa~~~~~~~~aa",
        "aa~~~~~~~~aa",
        "aaaaaaaaaaaa",
    });
    CHECK(FindLinks(m.View(), { Dock(m, 0, 2, 2, 1), Dock(m, 10, 2, 2, 1) }).empty());
}

UNIT_TEST(ext_ferries_lane_length_is_capped) {
    const TestMap m(kLake);
    const std::vector<DockSite> docks{ Dock(m, 2, 2, 2, 2), Dock(m, 10, 2, 2, 2) };
    CHECK(FindLinks(m.View(), docks, 5).empty());   // six cells of water: too far
    CHECK_EQ(FindLinks(m.View(), docks, 6).size(), static_cast<size_t>(1));
}

UNIT_TEST(ext_ferries_no_corner_cutting) {
    // The two pools meet only at a corner between two land cells: a boat cannot slip through.
    const TestMap m({
        "aaa~~bbbbb",
        "aaa~~bbbbb",
        "aaaaa~~bbb",
        "aaaaa~~bbb",
    });
    // Dock A by the west pool (zone 1), dock B on the far side of the east pool (zone 2), and the
    // only water path between the pools is the diagonal at (4,1)-(5,2).
    const std::vector<DockSite> docks{ Dock(m, 2, 0, 1, 1), Dock(m, 7, 3, 1, 1) };
    CHECK(FindLinks(m.View(), docks).empty());
}

UNIT_TEST(ext_ferries_each_dock_takes_its_nearest_partner) {
    // Three lands: zone 1 west, zone 2 just across, zone 3 far east.
    const TestMap m({
        "aaa~~bbb~~~~~~~~~~~~ccc",
        "aaa~~bbb~~~~~~~~~~~~ccc",
        "aaa~~bbb~~~~~~~~~~~~ccc",
    });
    // B's dock spans its land, so it boards from the west and the east water.
    const std::vector<DockSite> docks{ Dock(m, 1, 1, 2, 1), Dock(m, 5, 1, 3, 1), Dock(m, 20, 1, 2, 1) };
    const std::vector<Link> links = FindLinks(m.View(), docks);
    // A <-> B (nearest to each other), C <-> B (B is C's nearest); never A <-> C.
    CHECK_EQ(links.size(), static_cast<size_t>(2));
    CHECK_EQ(links[0].dockA, 0);
    CHECK_EQ(links[0].dockB, 1);
    CHECK_EQ(links[1].dockA, 2);
    CHECK_EQ(links[1].dockB, 1);
    // The same input gives the same links and lanes.
    const std::vector<Link> again = FindLinks(m.View(), docks);
    CHECK_EQ(again.size(), links.size());
    for (size_t i = 0; i < links.size(); ++i) CHECK(again[i].lane == links[i].lane);
    // LinkFrom answers for one dock as FindLinks pairs it.
    Link one;
    CHECK(LinkFrom(m.View(), docks, 2, &one));
    CHECK_EQ(one.dockB, 1);
    CHECK(one.lane == links[1].lane);
}

UNIT_TEST(ext_ferries_prefers_deep_water) {
    // Shallow water straight across, deep water a little round: the boat takes the deep way when
    // the detour is short.
    const TestMap m({
        "aaa---bbb",
        "aaa~~~bbb",
        "aaa~~~bbb",
    });
    const std::vector<Link> links = FindLinks(m.View(), { Dock(m, 2, 0, 1, 1), Dock(m, 6, 0, 1, 1) });
    CHECK_EQ(links.size(), static_cast<size_t>(1));
    int shallow = 0;
    for (int c : links[0].lane) shallow += m.type[static_cast<size_t>(c)] == kShallowWater ? 1 : 0;
    CHECK(shallow < 3);
}

UNIT_TEST(ext_ferries_dock_without_a_shore_is_ignored) {
    // A dock out in the water with no walkable cell beside it has nowhere to board.
    const TestMap m(kLake);
    CHECK(FindLinks(m.View(), { Dock(m, 6, 2, 1, 1), Dock(m, 10, 2, 2, 2) }).empty());
}

UNIT_TEST(ext_ferries_lane_cell_types) {
    Link l;
    l.lane = { 10, 11, 12, 13, 14, 15, 16 };
    const auto types = LaneCellTypes(l);
    CHECK_EQ(types.size(), static_cast<size_t>(7));
    const uint8_t want[] = { kBoardType, kBoardType, kLaneType, kLaneType, kLaneType, kBoardType, kBoardType };
    for (size_t i = 0; i < types.size(); ++i) {
        CHECK_EQ(types[i].first, l.lane[i]);
        CHECK_EQ(types[i].second, want[i]);
    }
    // Both lane types are in the citizens' walk mask, and the lane's in the boats' too.
    CHECK(IsWalkable(kLaneType));
    CHECK(IsWalkable(kBoardType));
    CHECK(((0x380u >> kLaneType) & 1u) != 0);
    // A short lane is all waiting.
    l.lane = { 1, 2, 3 };
    for (const auto& t : LaneCellTypes(l)) CHECK_EQ(t.second, kBoardType);
}

UNIT_TEST(ext_ferries_join_zones) {
    const std::vector<uint16_t> remap = JoinZones({ { 3, 1 }, { 5, 3 }, { 0, 4 }, { 9, 2 } }, 6);
    CHECK_EQ(remap.size(), static_cast<size_t>(7));
    const uint16_t want[] = { 0, 1, 2, 1, 4, 1, 6 };   // 1-3-5 join as 1; 0 and an id past 6 are ignored
    for (size_t i = 0; i < remap.size(); ++i) CHECK_EQ(remap[i], want[i]);
    // No pairs: every zone is itself.
    const std::vector<uint16_t> same = JoinZones({}, 3);
    for (size_t i = 0; i < same.size(); ++i) CHECK_EQ(same[i], static_cast<uint16_t>(i));
}

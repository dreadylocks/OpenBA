// ============================================================================
// tests/unit/ext_neighbour_restate_test.cpp -- ext.neighbour's restate (NOT RETAIL)
// ============================================================================
// src/ext/neighbour/restate.cpp: a town's whole list cut into `inv` parts, its road land as row runs,
// the neighbour's roads a join's replay did not name, and the relay's `want` lists.
// ============================================================================
#include "unit.h"

#include "ext/neighbour/restate.h"

#include <string>
#include <utility>
#include <vector>

namespace RS = Neighbour::Restate;

UNIT_TEST(ext_neighbour_restate_pack_keeps_every_entry_within_the_limit) {
    std::vector<std::string> builds;
    std::vector<std::string> roads;
    std::vector<std::string> land;
    for (int i = 0; i < 400; ++i)
        builds.push_back("s" + std::to_string(100 + i) + "_" + std::to_string(200 + i) + ":100");
    for (int i = 0; i < 900; ++i) roads.push_back(std::to_string(70000 + i) + ":1");
    for (int i = 0; i < 300; ++i) land.push_back(std::to_string(i) + "," + std::to_string(i) + ",3,3");
    const std::vector<RS::Part> parts = RS::Pack(builds, roads, land, 2000);
    CHECK(parts.size() > 1);
    std::string allB;
    std::string allR;
    std::string allL;
    for (const RS::Part& p : parts) {
        CHECK(p.builds.size() + p.roads.size() + p.land.size() <= 2000);
        CHECK(!(p.builds.empty() && p.roads.empty() && p.land.empty()));
        const std::pair<std::string*, const std::string*> lists[3] = {
            {&allB, &p.builds}, {&allR, &p.roads}, {&allL, &p.land}};
        for (const auto& pr : lists) {
            if (pr.second->empty()) continue;
            if (!pr.first->empty()) *pr.first += ';';
            *pr.first += *pr.second;
        }
    }
    auto joined = [](const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& e : v) {
            if (!s.empty()) s += ';';
            s += e;
        }
        return s;
    };
    CHECK(allB == joined(builds));   // every entry once, in order, none cut
    CHECK(allR == joined(roads));
    CHECK(allL == joined(land));
}

UNIT_TEST(ext_neighbour_restate_pack_an_empty_town_is_one_empty_part) {
    const std::vector<RS::Part> parts = RS::Pack({}, {}, {}, 12000);
    CHECK_EQ(parts.size(), static_cast<size_t>(1));
    CHECK(parts[0].builds.empty() && parts[0].roads.empty() && parts[0].land.empty());
    const std::vector<RS::Part> one = RS::Pack({"s1_2:100"}, {"5:1"}, {"1,2,3,4"}, 12000);
    CHECK_EQ(one.size(), static_cast<size_t>(1));
    CHECK_STREQ(one[0].builds.c_str(), "s1_2:100");
    CHECK_STREQ(one[0].roads.c_str(), "5:1");
    CHECK_STREQ(one[0].land.c_str(), "1,2,3,4");
}

UNIT_TEST(ext_neighbour_restate_row_runs) {
    // A 5 x 5 map: a run of three on row 0, two single tiles on row 1, a run to the row's end on row 2.
    const std::vector<uint8_t> mask = {0, 1, 1, 1, 0,
                                       1, 0, 1, 0, 0,
                                       0, 0, 0, 1, 1,
                                       0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0};
    const std::vector<std::string> runs = RS::RowRuns(mask, 5);
    CHECK_EQ(runs.size(), static_cast<size_t>(4));
    if (runs.size() == 4) {
        CHECK_STREQ(runs[0].c_str(), "1,0,3,1");
        CHECK_STREQ(runs[1].c_str(), "0,1,1,1");
        CHECK_STREQ(runs[2].c_str(), "2,1,1,1");
        CHECK_STREQ(runs[3].c_str(), "3,2,2,1");   // a run never wraps to the next row
    }
    CHECK(RS::RowRuns(std::vector<uint8_t>(3, 1), 5).empty());   // a mask smaller than the map: nothing
}

UNIT_TEST(ext_neighbour_restate_stale_roads_are_only_the_neighbours_unnamed_ones) {
    // Cells 0-5: 0 the neighbour's road named in this join (7); 1 its road not named (stale); 2 its road
    // named in an older join (stale); 3 a road on our land (ours: never touched); 4 the neighbour's land
    // with no road; 5 the neighbour's land with no finished road (the mask says 0): both left alone.
    const std::vector<uint8_t>  peer = {1, 1, 1, 0, 1, 1};
    const std::vector<uint8_t>  road = {1, 1, 1, 1, 0, 0};
    const std::vector<uint32_t> seen = {7, 0, 6, 0, 0, 0};
    const std::vector<int> stale = RS::StaleRoads(peer, road, seen, 7);
    CHECK_EQ(stale.size(), static_cast<size_t>(2));
    if (stale.size() == 2) {
        CHECK_EQ(stale[0], 1);
        CHECK_EQ(stale[1], 2);
    }
}

UNIT_TEST(ext_neighbour_restate_wanted_lists) {
    const std::vector<std::string> ids = RS::WantedIds("s10_20,Bad Id,s3_4,,x" + std::string(41, 'a'));
    CHECK_EQ(ids.size(), static_cast<size_t>(2));
    if (ids.size() == 2) {
        CHECK_STREQ(ids[0].c_str(), "s10_20");
        CHECK_STREQ(ids[1].c_str(), "s3_4");
    }
    const std::vector<int> cells = RS::WantedCells("5,147455,147456,-3,12a,0", 384 * 384);
    CHECK_EQ(cells.size(), static_cast<size_t>(3));
    if (cells.size() == 3) {
        CHECK_EQ(cells[0], 5);
        CHECK_EQ(cells[1], 147455);   // the last cell of a 384 map; 147456 is past it
        CHECK_EQ(cells[2], 0);
    }
    CHECK(RS::WantedIds("").empty());
    CHECK(RS::WantedCells("", 100).empty());
}

// ============================================================================
// tests/unit/ext_neighbour_look_test.cpp -- ext.neighbour: how a mirror looks (NOT RETAIL)
// ============================================================================
// src/ext/neighbour/mirror_look.cpp: a structure's stage as a percent and back (a site waiting for its
// materials draws no mesh), and a stockpile's piles as `piles` carries them.
// ============================================================================
#include "unit.h"

#include "ext/neighbour/mirror_look.h"

#include <string>
#include <vector>

namespace ML = Neighbour::MirrorLook;

UNIT_TEST(ext_neighbour_look_a_waiting_site_draws_no_mesh) {
    // BuildComponent hides the model (mesh index -1) until every material is in.
    CHECK(ML::StagePercent(-1, 4) == 0);
    CHECK(ML::StagePercent(-1, 1) == 0);
    CHECK(ML::StageOf(0, 4) == -1);
    CHECK(ML::StageOf(-5, 4) == -1);
    // A one-stage model shown is finished; no stages draws nothing.
    CHECK(ML::StagePercent(0, 1) == 100);
    CHECK(ML::StageOf(100, 0) == -1);
}

UNIT_TEST(ext_neighbour_look_a_started_stage_comes_back_as_itself) {
    for (int count = 1; count <= 100; ++count) {
        for (int idx = 0; idx < count; ++idx) {
            const int percent = ML::StagePercent(idx, count);
            CHECK(percent >= 1);
            CHECK(percent <= 100);
            CHECK(ML::StageOf(percent, count) == idx);
        }
    }
    // An index past the last stage is the last; a percent past 100 is the finished building.
    CHECK(ML::StagePercent(9, 3) == 100);
    CHECK(ML::StageOf(250, 3) == 2);
    // Over 100 stages: the first is still shown.
    CHECK(ML::StagePercent(0, 150) == 1);
    CHECK(ML::StageOf(1, 150) == 1);
}

UNIT_TEST(ext_neighbour_look_piles_round_trip) {
    std::vector<ML::Pile> piles(6);
    piles[0] = {0, 3};
    piles[2] = {1, 0};
    piles[3] = {1, 12};
    piles[5] = {2, 63};
    const std::string list = ML::PileList(piles);
    CHECK(list == "0.3,,1.0,1.12,,2.63");
    std::vector<ML::Pile> back;
    CHECK(ML::ParsePileList(list, 6, 3, &back));
    CHECK(back.size() == 6);
    for (size_t i = 0; i < piles.size(); ++i) {
        CHECK(back[i].good == piles[i].good);
        if (piles[i].good >= 0) CHECK(back[i].mesh == piles[i].mesh);
    }
    // Nothing stored: every tile empty.
    CHECK(ML::ParsePileList(",,", 3, 0, &back));
    CHECK(back.size() == 3);
    CHECK(back[0].good < 0 && back[1].good < 0 && back[2].good < 0);
}

UNIT_TEST(ext_neighbour_look_a_bad_pile_list_is_refused) {
    std::vector<ML::Pile> out;
    CHECK(!ML::ParsePileList("0.1,1.1", 2, 1, &out));    // a good past the list
    CHECK(out.empty());
    CHECK(!ML::ParsePileList("0.64", 1, 1, &out));       // a mesh past kMaxPileMesh
    CHECK(!ML::ParsePileList("0.1,0.1,0.1", 2, 1, &out));   // more tiles than the footprint
    CHECK(!ML::ParsePileList("01", 1, 1, &out));         // no dot
    CHECK(!ML::ParsePileList("0.", 1, 1, &out));         // no mesh
    CHECK(!ML::ParsePileList(".1", 1, 1, &out));         // no good
    CHECK(!ML::ParsePileList("-1.1", 1, 1, &out));       // not a number
    CHECK(!ML::ParsePileList("0.1.2", 1, 1, &out));
    CHECK(!ML::ParsePileList("1234.1", 1, 2000, &out));  // four digits
}

UNIT_TEST(ext_neighbour_look_goods_round_trip) {
    const std::vector<std::string> goods = {"Template\\RawMaterialLogs.rsc",
                                            "Template\\RawMaterialStone.rsc"};
    const std::string list = ML::GoodList(goods);
    CHECK(list == "Template\\RawMaterialLogs.rsc|Template\\RawMaterialStone.rsc");
    std::vector<std::string> back;
    CHECK(ML::ParseGoodList(list, 8, 160, &back));
    CHECK(back == goods);
    CHECK(ML::ParseGoodList("", 8, 160, &back));
    CHECK(back.empty());
    CHECK(!ML::ParseGoodList("a||b", 8, 160, &back));     // an empty template
    CHECK(!ML::ParseGoodList("a|b|c", 2, 160, &back));    // too many
    CHECK(!ML::ParseGoodList("abcdef", 8, 5, &back));     // too long
}

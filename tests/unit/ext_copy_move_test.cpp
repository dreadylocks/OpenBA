// ============================================================================
// tests/unit/ext_copy_move_test.cpp -- the copy / move clipboard's geometry (src/ext/copy_move/layout.h)
// ============================================================================
// The quarter turn agrees with Map::FootprintValidity's 0x5a arm cell by cell, four turns are the identity,
// a building's own rotation steps with the group, the group centres on the cursor by PlacementTool's rule,
// and a drag rect is the same from either corner.
// ============================================================================
#include "unit.h"
#include "ext/copy_move/layout.h"

#include <set>
#include <utility>

namespace {

using Ext::CopyMove::Cell;
using Ext::CopyMove::Item;
using Ext::CopyMove::Kind;
using Ext::CopyMove::Layout;
using Ext::CopyMove::Rect;

// Map::FootprintValidity (tool_base_tile.cpp), rotation 0x5a: for the turned building's cell (dx, dy) of
// its placed W x H rect, the footprint string index is dx * (H + 1) + dy, i.e. the unturned footprint's
// column dy on line dx. Line 0 is the unturned footprint's far row (rotation 0 reads line H - 1 - dy), so
// the turned cell (dx, dy) shows the unturned cell (ox, oy) = (dy, oh - 1 - dx), with oh = W. Inverted:
// the unturned cell (ox, oy) lands on (oh - 1 - oy, ox).
Cell RetailTurn(int ox, int oy, int ow, int oh) {
    (void)ow;
    Cell c;
    c.col = oh - 1 - oy;
    c.row = ox;
    return c;
}

bool SameRect(const Rect& a, const Rect& b) {
    return a.col == b.col && a.row == b.row && a.w == b.w && a.h == b.h;
}

}  // namespace

UNIT_TEST(ext_copy_move_turn_matches_the_retail_footprint_turn) {
    const int ow = 3;
    const int oh = 5;
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            Cell in;
            in.col = x;
            in.row = y;
            Cell ours = Ext::CopyMove::TurnCell(in, ow, oh);
            Cell retail = RetailTurn(x, y, ow, oh);
            CHECK_EQ(ours.col, retail.col);
            CHECK_EQ(ours.row, retail.row);
        }
    }
}

UNIT_TEST(ext_copy_move_turned_rect_holds_exactly_the_turned_cells) {
    const int boxW = 7;
    const int boxH = 4;
    Rect r;
    r.col = 2;
    r.row = 1;
    r.w = 3;
    r.h = 2;
    Rect t = Ext::CopyMove::TurnRect(r, boxW, boxH);
    std::set<std::pair<int, int>> cells;
    for (int y = r.row; y < r.row + r.h; ++y) {
        for (int x = r.col; x < r.col + r.w; ++x) {
            Cell c;
            c.col = x;
            c.row = y;
            Cell turned = Ext::CopyMove::TurnCell(c, boxW, boxH);
            cells.insert({ turned.col, turned.row });
            CHECK(Ext::CopyMove::Contains(t, turned));
        }
    }
    CHECK_EQ(static_cast<int>(cells.size()), t.w * t.h);
    CHECK(t.col >= 0 && t.row >= 0 && t.col + t.w <= boxH && t.row + t.h <= boxW);
}

UNIT_TEST(ext_copy_move_four_turns_are_the_identity) {
    Layout l;
    Item house;
    house.kind = Kind::Building;
    house.rect = { 10, 20, 3, 2 };
    house.turn = 1;
    Item road;
    road.kind = Kind::Road;
    road.rect = { 14, 22, 1, 1 };
    Item field;
    field.kind = Kind::Field;
    field.rect = { 9, 25, 6, 4 };
    l.Add(house);
    l.Add(road);
    l.Add(field);
    l.End();
    const std::vector<Item> before = l.Items();
    const int w = l.BoxW();
    const int h = l.BoxH();
    for (int i = 0; i < 4; ++i) {
        l.Turn(+1);
    }
    CHECK_EQ(l.BoxW(), w);
    CHECK_EQ(l.BoxH(), h);
    CHECK_EQ(l.Turns(), 0);
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(SameRect(l.Items()[i].rect, before[i].rect));
        CHECK_EQ(l.Items()[i].turn, before[i].turn);
    }
    l.Turn(+1);
    l.Turn(-1);
    for (size_t i = 0; i < before.size(); ++i) {
        CHECK(SameRect(l.Items()[i].rect, before[i].rect));
    }
}

UNIT_TEST(ext_copy_move_end_moves_the_origin_to_the_box) {
    Layout l;
    Item a;
    a.rect = { 10, 20, 3, 2 };
    Item b;
    b.rect = { 15, 18, 2, 2 };
    l.Add(a);
    l.Add(b);
    l.End();
    CHECK_EQ(l.BoxW(), 7);   // cols 10..16
    CHECK_EQ(l.BoxH(), 4);   // rows 18..21
    CHECK(SameRect(l.Items()[0].rect, Rect{ 0, 2, 3, 2 }));
    CHECK(SameRect(l.Items()[1].rect, Rect{ 5, 0, 2, 2 }));
    // Placed back at the original origin, every rect is where it was copied from.
    Cell origin;
    origin.col = 10;
    origin.row = 18;
    CHECK(SameRect(Layout::Placed(l.Items()[0], origin), a.rect));
    CHECK(SameRect(Layout::Placed(l.Items()[1], origin), b.rect));
}

UNIT_TEST(ext_copy_move_building_rotation_steps_with_the_group) {
    Layout l;
    Item house;
    house.kind = Kind::Building;
    house.rect = { 0, 0, 3, 2 };
    house.turn = 3;
    Item pasture;
    pasture.kind = Kind::Pasture;
    pasture.rect = { 4, 0, 5, 5 };
    pasture.turn = 0;
    Item road;
    road.kind = Kind::Road;
    road.rect = { 3, 0, 1, 1 };
    road.turn = 0;
    l.Add(house);
    l.Add(pasture);
    l.Add(road);
    l.End();
    l.Turn(+1);
    CHECK_EQ(l.Items()[0].turn, 0);
    CHECK_EQ(l.Items()[1].turn, 1);
    CHECK_EQ(l.Items()[2].turn, 0);   // a road has no rotation
    // The 3 x 2 house is 2 x 3 after the turn.
    CHECK_EQ(l.Items()[0].rect.w, 2);
    CHECK_EQ(l.Items()[0].rect.h, 3);
    l.Turn(-1);
    CHECK_EQ(l.Items()[0].turn, 3);
}

UNIT_TEST(ext_copy_move_centred_start_is_placement_tools_rule) {
    // PlacementTool::GetPlacement: even dim d -> col - d/2 + 1; odd -> col - (d - 1)/2.
    for (int d = 1; d <= 9; ++d) {
        for (int c = 0; c < 20; ++c) {
            const int retail = (d % 2 == 0) ? (c - d / 2 + 1) : (c - (d - 1) / 2);
            CHECK_EQ(Ext::CopyMove::CentredStart(c, d), retail);
        }
    }
}

UNIT_TEST(ext_copy_move_drag_rect_from_either_corner) {
    Cell a;
    a.col = 8;
    a.row = 3;
    Cell b;
    b.col = 2;
    b.row = 9;
    CHECK(SameRect(Ext::CopyMove::DragRect(a, b), Rect{ 2, 3, 7, 7 }));
    CHECK(SameRect(Ext::CopyMove::DragRect(b, a), Rect{ 2, 3, 7, 7 }));
    CHECK(SameRect(Ext::CopyMove::DragRect(a, a), Rect{ 8, 3, 1, 1 }));
    CHECK(Ext::CopyMove::Overlaps(Rect{ 0, 0, 2, 2 }, Rect{ 1, 1, 2, 2 }));
    CHECK(!Ext::CopyMove::Overlaps(Rect{ 0, 0, 2, 2 }, Rect{ 2, 0, 2, 2 }));
}

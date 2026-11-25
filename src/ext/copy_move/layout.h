// ============================================================================
// src/ext/copy_move/layout.h -- the copy / move clipboard's geometry (docs/fork/COPY_MOVE.md)
// ============================================================================
// NOT RETAIL. A fork feature, always on (the user, 2026-11-24): the player selects buildings, roads and
// sized areas (fields, pastures, orchards), then pastes copies of them as new construction or moves them
// instantly. This header is the part with no game in it: where each copied thing sits relative to the
// group, how the group follows the cursor, and how a quarter turn of the group turns every thing in it.
// Standard C++ only (tests/unit/ext_copy_move_test.cpp links layout.cpp alone).
//
// CELLS. A map cell is (col, row), index = row * mapSize + col, as everywhere in the game. A rect is its
// min cell and its size in cells, the shape MapComponent keeps (tileX0, tileY0, tileX1 = width,
// tileY1 = height).
//
// THE QUARTER TURN. PlacementTool's rotation r (RotationSetting 0..3) places a building with yaw r * 90 and
// reads its footprint string turned so that one step maps an unturned cell (x, y) of a W x H footprint to
// (H - 1 - y, x) of the turned H x W one (Map::FootprintValidity's 0x5a arm, tool_base_tile.cpp). The group
// turns by the same rule, so a building's own rotation steps with it: turn the group once and every
// building's r goes up by one and every rect, road cell and area moves to its turned place.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace Ext {
namespace CopyMove {

struct Cell {
    int col = 0;
    int row = 0;
};

struct Rect {
    int col = 0;   // min cell
    int row = 0;
    int w = 1;     // size in cells
    int h = 1;
};

// One quarter turn inside a boxW x boxH box (origin 0,0): (x, y) -> (boxH - 1 - y, x). The turned box is
// boxH x boxW.
Cell TurnCell(const Cell& c, int boxW, int boxH);
Rect TurnRect(const Rect& r, int boxW, int boxH);

// A footprint of `dim` cells centred on `cursor` starts at cursor - (dim - 1) / 2:
// PlacementTool::GetPlacement's rule (tool_base_ray.cpp), which writes the even and odd cases apart but gives
// this for both.
int CentredStart(int cursor, int dim);

// What a copied thing is. The placement path differs for each (copy_move_place.cpp).
enum class Kind : uint8_t {
    Building,   // a fixed footprint placed by PlacementTool (CreatePlacedDescription)
    Field,      // a sized area dragged out by DragPlacementTool (CreateDragDescription): crop field, orchard
    Pasture,    // a sized area with a fence and a shelter, FencedTool (CreateFencedDescription)
    Road,       // one road cell (map-cell state, no entity)
};

// One copied thing. `rect` is relative to the group's box. `turn` is the building's RotationSetting value
// (0..3); for a pasture, the shelter's quarter turns in the same sense (copy_move_place.cpp converts to
// FencedTool's setting); unused for a field (DragPlacementTool derives the field's angle from its shape)
// and a road.
struct Item {
    Kind kind = Kind::Building;
    const void* tmpl = nullptr;   // the ComponentDescription the thing was made from (road: null)
    Rect rect;
    int turn = 0;
    int variant = 0;      // building: its mesh group; road: its road type
    void* source = nullptr; // a move: the entity being moved (road: null)
    int sourceCell = -1;  // a move of a road: the source cell index
};

// The group: its items and its box. Built from absolute rects by Clear / Add / End.
class Layout {
public:
    // Absolute rects in; `End` moves the origin to the items' bounding box.
    void Clear();
    void Add(const Item& absolute);
    void End();

    bool Empty() const { return _items.empty(); }
    int BoxW() const { return _boxW; }
    int BoxH() const { return _boxH; }
    const std::vector<Item>& Items() const { return _items; }

    // `dir` = +1 one quarter turn (the game's "structure right"), -1 the other way.
    void Turn(int dir);
    int Turns() const { return _turns; }   // quarter turns applied since End, 0..3

    // The box's min cell when its centre follows the cursor cell.
    Cell OriginFor(const Cell& cursor) const;

    // `item`'s rect / cell on the map with the box at `origin`.
    static Rect Placed(const Item& item, const Cell& origin);

private:
    std::vector<Item> _items;
    int _boxW = 0;
    int _boxH = 0;
    int _turns = 0;
};

// The cells between two corner cells of a drag, as a rect (either corner first).
Rect DragRect(const Cell& a, const Cell& b);
bool Overlaps(const Rect& a, const Rect& b);
bool Contains(const Rect& r, const Cell& c);

}  // namespace CopyMove
}  // namespace Ext

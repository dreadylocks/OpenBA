// ============================================================================
// src/ext/copy_move/place.h -- one copied thing, placed (docs/fork/COPY_MOVE.md)
// ============================================================================
// NOT RETAIL. A Placer is built for each item of the clipboard while the tool pastes or moves it, and gone
// when the tool leaves that mode. It knows where the item may go (the retail placement tests, cell by cell),
// shows a building's ghost, and pastes a copy the way the player's own tool would:
//   building  PlacementTool::CreateAt (tool_base_tile.cpp) with the item's template, rotation and mesh group
//   field     DragPlacementTool::CalcPath over the item's rect (dragplacementtool.cpp)
//   pasture   FencedTool::CalcPath, its RotationSetting set to the item's shelter side
//     (fencedtool_calcpath.cpp)
//   road      Map::PlaceRoadCell with the road tool's template for the item's road type
//     (roadtool_calcpath.cpp)
// so a copy is new construction: builders put it up and it costs what the retail tool charges.
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "ext/copy_move/layout.h"
#include "ext/copy_move/world.h"

class ComponentDescription;
class CreatePlacedDescription;
class DragTool;
class MapDescription;
class ModelDescription;
class PlayerToolManager;
class RotationSetting;
namespace Graphics { class MeshInstance; }
namespace Video { class Context; }

namespace Ext {
namespace CopyMove {

// One cell of an item's footprint as the preview draws it.
struct Tile {
    int col = 0;
    int row = 0;
    bool ok = false;
};

class Placer {
public:
    Placer(const Item& item, PlayerToolManager* manager);
    ~Placer();
    Placer(const Placer&) = delete;
    Placer& operator=(const Placer&) = delete;

    // Whether the item fits at `rect` (its map rect for this frame), and the cells to draw. `moving`: what a
    // move takes away, whose cells count as free; null for a paste.
    bool Check(const Item& item, const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles);
    // A building's ghost at `rect`, or none (show false).
    void Ghost(const Item& item, const Rect& rect, bool show);
    // A paste: the copy as new construction. False when the retail tool refused it.
    bool Paste(const Item& item, const Rect& rect);

    // The descriptions a building places with (null for the other kinds).
    const CreatePlacedDescription* Placed() const { return _placed; }
    const MapDescription* MapDesc() const { return _map; }
    const ModelDescription* Model() const { return _model; }

private:
    bool CheckBuilding(const Item& item, const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles);
    bool CheckArea(const Item& item, const Rect& rect, std::vector<Tile>* tiles);
    bool CheckRoad(const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles);

    PlayerToolManager* _manager = nullptr;
    // building
    const CreatePlacedDescription* _placed = nullptr;
    const MapDescription* _map = nullptr;
    const ModelDescription* _model = nullptr;
    Graphics::MeshInstance* _ghost = nullptr;
    int _ghostScene = -1;
    // field / pasture: the retail tool, built and started as PlayerToolManager::SelectTool would
    DragTool* _tool = nullptr;
    RotationSetting* _rotation = nullptr;   // the pasture tool's setting (ours)
    // road
    ComponentDescription* _roadTmpl = nullptr;
};

// The FencedTool rotation (RotationSetting 0..3) whose shelter yaw is `turn` quarter turns: FencedTool maps
// 0 -> 0, 1 -> -90, 2 -> 180, 3 -> 90 degrees (tool_base_edit.cpp CalcAnchor).
int FencedRotationFor(int turn);
// The quarter turns of a pasture's shelter yaw (MapComponent +0x30, degrees).
int TurnOfDegrees(int degrees);

}  // namespace CopyMove
}  // namespace Ext

// ============================================================================
// src/ext/copy_move/world.h -- what the copy / move tool reads from the game (docs/fork/COPY_MOVE.md)
// ============================================================================
// NOT RETAIL. The game-side helpers the tool and its placers share: which entities can be copied and what
// kind they are, whether a remembered entity still exists, the selection outline, what is under the
// cursor, and the road cells and road templates. Nothing here changes the world except SetOutline.
// ============================================================================
#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "ext/copy_move/layout.h"

class ComponentDescription;
class ComponentEntity;
class MapComponent;
class Map;
namespace Math { class Vector3; }

namespace Ext {
namespace CopyMove {

// What `tmpl` (an entity's _description) is to the tool, from the Create*Description among its
// sub-descriptions: the retail tool that places it. False for everything else (citizens, trees, bridges,
// fence pieces, a pasture's shelter...).
bool KindOf(const ComponentDescription* tmpl, Kind* kind);

// A selected or copied entity, with where it was in the EntityList's master array when last seen (the
// array is swap-removed, so the index is a hint, not an identity).
struct EntityRef {
    ComponentEntity* entity = nullptr;
    int hint = -1;
};
// Only for an entity known to be alive now (it reads the entity's master-array index).
EntityRef Remember(ComponentEntity* entity);
// Whether the entity is still in the world and not marked for removal; refreshes the hint. Never reads the
// entity before finding it in the master array (a freed entity is never touched).
bool Alive(EntityRef& ref);

// The entity's map rect (MapComponent: min cell, width, height) and rotation in degrees.
bool RectOf(ComponentEntity* entity, Rect* rect, int* degrees);

// Every copyable entity whose rect overlaps `area`, in master-array order; a multi-part entity's parts
// (a pasture's shelter and fence pieces) are left out, their master stands for them.
std::vector<ComponentEntity*> EntitiesIn(const Rect& area);
// The entity whose MultiObjectComponent holds `entity` (a fence piece's pasture), or `entity` itself.
ComponentEntity* MasterOf(ComponentEntity* entity);
// The copyable entity under the cursor: the pick mesh first (a building's walls), then any whose rect
// holds the cursor's cell (a field, a fresh site). Null when there is none.
ComponentEntity* EntityUnderCursor(int cursorCell);

// The map cell under the cursor (index), or -1.
int CursorCell();
Map* TheMap();
inline Cell CellOf(int index, int size) {
    Cell c;
    c.col = index % size;
    c.row = index / size;
    return c;
}

// The selection outline on an entity and its MultiObject children (EditTool's, tool_base_render.cpp).
void SetOutline(ComponentEntity* entity, bool on);

// ---- roads (map-cell state: MapCell +0x2A type, +0x2B 0x80 = being built) ----
bool HasRoad(int cell);
bool RoadPending(int cell);
uint8_t RoadType(int cell);
// The template the road tool places a road of `type` with (a mod adds types), or null.
ComponentDescription* RoadTemplate(uint8_t type);

// The world position a footprint is placed at: the middle of its two corner cells' terrain positions, z 0
// (PlacementTool::GetPlacement, tool_base_ray.cpp).
void RectCentre(const Rect& r, Math::Vector3* out);

// ---- what a move takes away (its cells count as free while it is previewed) ----
struct MoveSet {
    std::unordered_set<const ComponentEntity*> entities;
    std::unordered_set<int> roadCells;
};

}  // namespace CopyMove
}  // namespace Ext

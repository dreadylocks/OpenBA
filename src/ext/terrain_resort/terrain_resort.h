// ============================================================================
// src/ext/terrain_resort/terrain_resort.h -- fix.terrain_resort: no "red squares" on flat ground
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINRESORT.md, docs/retail_bugs.md §8.
//
// A cell's terrain type (MapCell +0x29) is what the placement overlay tests
// (Map::TestPlacement 0x14009f5d0). Retail writes it from the heights only at
// world creation and when an entity leaves a cell (0x14009dee0). Flattening
// (Map::ResetTerrainHeights 0x14009e0e0: a finished road cell, a building's
// clear area) changes the heights of the cells around it and re-sorts none of
// them, so a cell sorted "too steep" while it was steep stays red after the
// ground under it is flat. The rule is terrain_resort_rule.h.
//
// WHEN. After every ResetTerrainHeights, over its rectangle grown by one cell
// (the cells that share its height words); and once on a load, over the whole
// map, so an existing save heals.
// ============================================================================
#pragma once

class Map;

namespace Ext {
namespace TerrainResort {

// After ResetTerrainHeights(x0, y0, x1, y1): heal the rectangle grown by one,
// through Map::SetTerrainType (the region graph and the pathfinder are live).
void HealAround(Map* map, int x0, int y0, int x1, int y1);

// Map::Serialize's load arm, before the zones and the pathfinder are built:
// heal every cell with a plain byte store. Returns the number healed (and logs it
// on stderr as [TerrainResort] when non-zero).
int HealAllOnLoad(Map* map);

}  // namespace TerrainResort
}  // namespace Ext

#pragma once
// ============================================================================
// src/ext/ferries/lanes.h -- which Ferry Docks are linked, and the lane between
// them (docs/fork/FERRIES.md §3.2). NOT RETAIL. Standard C++ only: the unit
// tests (tests/unit/ext_ferries_test.cpp) drive it over made-up maps.
// ============================================================================
// The map is a square of `size` x `size` cells, index = y * size + x. Each cell
// has the game's terrain type (MapCell::terrainType) and land zone
// (MapCell::zone, from Map::FillZones before any ferry joined two of them).
//
// A dock's landing points are the water cells (types 7 / 8) that touch one of
// its cells or a walkable cell beside it; a dock with none is ignored. From
// each dock the search spreads through water (8 neighbours, deep water
// cheapest) to the nearest landing point of a dock in another land zone, no
// further than `maxLaneCells` steps. Each dock links to that nearest partner;
// a pair found from both sides is one link. A link needs at least one of its
// two docks finished: the builders row over to build the other.
// ============================================================================

#include <cstdint>
#include <utility>
#include <vector>

namespace Ext {
namespace Ferries {

constexpr uint8_t kShallowWater = 7;
constexpr uint8_t kDeepWater    = 8;
constexpr uint8_t kLaneType     = 9;   // road / bridge: citizens' road cost 3, still in the boats' mask
constexpr uint8_t kBoardType    = 4;   // citizens' cost 16: the wait for a boat, at each end
constexpr int     kBoardCells   = 2;   // lane cells at each end priced as the wait
constexpr int     kMaxLaneCells = 150;
constexpr uint32_t kWalkMask    = 0xe1fu;   // the citizens' walk mask (MoveTo passFlags)

inline bool IsWater(uint8_t t) { return t == kShallowWater || t == kDeepWater; }
inline bool IsWalkable(uint8_t t) { return t < 32 && ((kWalkMask >> t) & 1u) != 0; }

struct MapView {
    int size = 0;
    const uint8_t*  type = nullptr;   // size * size terrain types
    const uint16_t* zone = nullptr;   // size * size land zones (0 = none: water, blocked)
};

struct DockSite {
    std::vector<int> cells;   // the footprint's cells
    bool built = false;       // finished (not a construction site)
};

struct Link {
    int dockA = -1;              // indices into the docks passed in
    int dockB = -1;
    std::vector<int> lane;       // water cells from A's landing point to B's, both included
    int landA = -1;              // the walkable cell beside the lane's first cell
    int landB = -1;              // ... beside its last cell
    uint16_t zoneA = 0;          // their land zones (different)
    uint16_t zoneB = 0;
};

// Every link between the docks (see the banner). Deterministic: the same map
// and docks in the same order give the same links and lanes.
std::vector<Link> FindLinks(const MapView& map, const std::vector<DockSite>& docks,
                            int maxLaneCells = kMaxLaneCells);

// The link from `from` alone: its nearest partner as FindLinks pairs them (the
// test aid's siting uses it). False when it has none.
bool LinkFrom(const MapView& map, const std::vector<DockSite>& docks, int from, Link* out,
              int maxLaneCells = kMaxLaneCells);

// The path worker's type for each lane cell: the first and last kBoardCells
// cells are kBoardType, the rest kLaneType (a lane shorter than both ends is
// all kBoardType).
std::vector<std::pair<int, uint8_t>> LaneCellTypes(const Link& link);

// Joins the zones each pair names: every zone of a joined group takes the
// group's smallest id. Returns the remap table (index = old zone id); ids not
// named map to themselves. `maxZone` is the largest zone id in use.
std::vector<uint16_t> JoinZones(const std::vector<std::pair<uint16_t, uint16_t>>& pairs, uint16_t maxZone);

}  // namespace Ferries
}  // namespace Ext

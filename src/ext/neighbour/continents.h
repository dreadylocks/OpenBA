#pragma once
// ============================================================================
// src/ext/neighbour/continents.h -- NOT RETAIL. ext.neighbour: the shape of a Continents room's map
// ============================================================================
// docs/fork/NEIGHBOUR_TOWNS.md §12. A lobby room made with the Continents map gives each town its own
// large land, with sea between the two and all round them, out to the horizon. This file is the map's
// shape: where the lands are (LandDistance), the coast's height profile (CoastHeight), the trader boats'
// sea lane, each town's start. The game side (worldgen, the sea's water, the start spiral, nomads
// landing on the coast) is continents_world.cpp.
//
// The same bits on every platform: everything here is + - * / and sqrt on floats (no libm), from
// integer hashes of the seed, and the build passes -ffp-contract=off. A Mac and a PC therefore build
// the same land, which the relay checks (a town whose land hash differs is refused);
// ext_neighbour_continents_same_bits_everywhere pins a digest.
//
// Coordinates are the terrain's centred ones, the f390 stages' path space: (0, 0) is the middle of the
// map, one unit is one tile, the playable map spans [-mapSize/2, mapSize/2] and the whole terrain
// (twice as wide) [-mapSize, mapSize]. Heightmap vertex (col, row) sits at (col - mapSize, row - mapSize);
// map tile (tx, ty) has its centre at (tx - mapSize/2 + 0.5, ty - mapSize/2 + 0.5).
//
// Standard C++ only (unit tests).
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Neighbour {
namespace Continents {

// A room's map type (RoomSettings::map): "" is one shared land, as retail builds it.
constexpr const char* kMapContinents = "continents";

// The room carries its map type in front of the TerrainGenerator's path in the `terrain` setting, so
// the relay and the protocol need no new field: "continents:Game/Terrain/Valleys.rsc".
// JoinMap("", p) is p. SplitMap takes off a known map type only; anything else stays in the path,
// which then does not load, so a game that does not know a map type refuses the room instead of
// building other land.
std::string JoinMap(const std::string& map, const std::string& terrainPath);
void        SplitMap(const std::string& terrain, std::string& map, std::string& terrainPath);

struct Layout {
    uint32_t seed = 0;
    int      mapSize = 0;           // tiles along the playable map's edge (256 / 384 / 512)
    bool     eastWest = false;      // false: the lands lie west and east, and the sea between runs north-south
    float    across[2] = {0, 0};    // each land's middle, across that sea: town 1's below 0, town 2's above
    float    halfAcross = 0.0f;     // each land's half-width and half-length before its coast wanders
    float    halfAlong = 0.0f;
    float    corner = 0.0f;         // the radius of its rounded corners
    float    wobble = 0.0f;         // how far the coast wanders either way, in tiles, on a land's outer side
    float    wobbleInner = 0.0f;    // and on the side facing the other land
    float    narrows = 0.0f;        // the sea between the lands is never narrower than this
    float    edgeSea = 0.0f;        // and the playable map's edge is always this far out at sea
    float    blend = 0.0f;          // the two limits round off over this many tiles
    float    straitWidth = 0.0f;    // the sea between the lands, its mean width in tiles
    float    noiseScale = 0.0f;     // 1 / the coast noise's finest wavelength
};

Layout MakeLayout(uint32_t seed, int mapSize);

// Signed distance in tiles (approximate) from (x, y) to the nearest coast: above 0 on land, below 0
// at sea. `land`, when given, is the land it belongs to or is nearest: 0 = town 1's, 1 = town 2's.
float LandDistance(const Layout& L, float x, float y, int* land = nullptr);

// The height a heightmap vertex gets at distance `d` from the coast (LandDistance), from its height
// `h` as the game's own generator left it. Inland past the beach band the land is untouched; near
// the coast hills come down to a flat beach (valleys, rivers and lakes lower than the beach stay); at
// sea the floor drops to deep water within a few tiles.
float CoastHeight(float h, float d);

// The trader boats' route (the map's path 0): a straight lane down the middle of the sea between the
// lands, from one edge of the terrain to the other, one point per tile: 2 * mapSize + 1 (x, y) pairs.
std::vector<float> SeaLane(const Layout& L);

// Seat 0 / 1's start-spiral centre, in map tiles: the middle of its own land.
void StartCentre(const Layout& L, int seat, int* tx, int* ty);
// Map tile (tx, ty) is on seat's own land.
bool OnOwnLand(const Layout& L, int seat, int tx, int ty);

// Nomads coming ashore (continents_world.cpp NomadLanding). `typeAt(x, y)` is a map tile's terrain
// type (maptilegrid.cpp: 7 shore water, 8 deep water) on a w x w map.
// RingedBySea: its four corners and four edge middles are water, which no retail map has (its rim is
// >= 2.0 high). WalkToShore: from (fromX, fromY) along (dx, dy) out to the open sea (kOpenSea water
// tiles in a row, so a river on the way does not stop it), the last walkable tile before it; false
// when there is none.
using TileType = std::function<uint8_t(int x, int y)>;
constexpr int kOpenSea = 12;
bool RingedBySea(int w, const TileType& typeAt);
bool WalkToShore(int w, float fromX, float fromY, float dx, float dy, const TileType& typeAt, int* x, int* y);

}  // namespace Continents
}  // namespace Neighbour

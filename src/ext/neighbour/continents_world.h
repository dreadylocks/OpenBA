#pragma once
// ============================================================================
// src/ext/neighbour/continents_world.h -- NOT RETAIL. ext.neighbour: a Continents map in the game
// ============================================================================
// docs/fork/NEIGHBOUR_TOWNS.md §12. The EXT SEAMs that build a Continents room's map (continents.h has
// its shape) and play on it. Every one does nothing unless this new game is a Continents room's
// (SetForGame), except NomadLanding, which works from the map itself so a loaded game needs no saved
// state: retail's maps never have water at their corners.
//
//   worldgen.cpp wg_HeightmapPipeline_f390   PlaceSeaLane (instead of the coastline stage),
//                                            KeepRiversOnLand (after the paths stage),
//                                            ShapeCoasts (after the last smoothing)
//   water_contour_mesh.cpp BuildWaterMesh    AddSea (before the buffers are made)
//   starttown.cpp StartTown_FindSpawnTile    Neighbour::StartCentre / StartTileAllowed (neighbour.cpp)
//   immigrationcomponent_states.cpp Spawn    NomadLanding
// ============================================================================

#include <cstdint>

#include "ext/neighbour/continents.h"

struct TerrainHeightfield;
class WorldGeometry;
class Map;
namespace System { class Random; }

namespace Neighbour {
namespace Continents {

// Neighbour::ResetForGame: this new game is (or is not) a Continents room's, with the room's seed.
void SetForGame(bool continents, uint32_t seed);
bool ForThisGame();
// The layout for this game's map, once worldgen has said how big it is; null for any other game.
const Layout* ThisLayout();

// Worldgen. PlaceSeaLane returns false (the caller runs the coastline stage, as retail) for any other
// game; true when it has put the sea lane in as the map's path 0.
bool PlaceSeaLane(TerrainHeightfield* hf);
void KeepRiversOnLand(TerrainHeightfield* hf);
void ShapeCoasts(TerrainHeightfield* hf);
void AddSea(WorldGeometry* wg, const TerrainHeightfield* hf);

// ImmigrationComponent::Spawn, after it picked a point at the map's edge and found the Town Hall's
// cell: on a map ringed by sea in a game with ext.neighbour, a point on the town's own coast instead
// (x, y in map tiles). False, x and y untouched, anywhere else.
bool NomadLanding(const Map* map, int hallCell, System::Random* rng, int* x, int* y);

}  // namespace Continents
}  // namespace Neighbour

#pragma once
// ============================================================================
// src/ext/ferries/ferries.h -- ext.ferries: Ferry Docks (docs/fork/FERRIES.md)
// ============================================================================
// NOT RETAIL. A Ferry Dock is a building of our own template, built in memory
// from the Fisherman's Dock's parts in the player's install (nothing copied
// into the tree). Two docks on different land across the same water are
// linked; the lane between them is written as bridge cells into the path
// worker's own copy of the map, the two shores' zones are joined, and a
// citizen on a water cell rides a small boat. Class B: the links are worked
// out again from the docks on load.
//
// The faithful sites call the functions below through one-line EXT SEAMs; each
// does nothing while ext.ferries is off for the game.
// ============================================================================

class CharacterComponent;
class ComponentEntity;
class Map;
struct RegionGraph;

namespace Ext {
namespace Ferries {

// The dock's template and its build-menu entry, both served by our provider from
// the one name (the provider table is small: Resources.h).
constexpr const char* kDockTemplate = "Template\\ExtFerryDock.rsc";
constexpr const char* kToolbarItem  = "Template\\ExtFerryDock.rsc:ferrydock";

// Boot (registry_boot.cpp, beside the other providers): register the provider
// for the two objects above. Never under the command-line faithful profile.
void InitProviders();

// GameUpdateLoop::Load, once the class B set is final for the game (a load's
// manifest read): shows or hides the build-menu entry and, when on, reads the
// docks the save holds.
void SelectForGame();

// GameUpdateLoop::Update, once a frame: looks at the docks now and then (a new
// dock, a finished one, one gone: the zones are worked out again), and moves the
// riders' boats.
void BeginFrame();

// The first Ferry Dock the last look found (the test aid opens its panel), or null.
ComponentEntity* FirstDock();

// GameUpdateLoop's destructor: forget the game (its scene goes with it).
void EndGame(const void* owner);

// Map::FillZones, after the zones were worked out again: links the docks, writes
// the lanes into the path worker's copy and joins each link's two zones.
void AfterFillZones(Map* map);

// RegionGraph::Flush, after the dirty blocks were rebuilt: each link's two
// landing regions become neighbours (job and storage choice see the crossing).
void AfterRegionFlush(RegionGraph* graph);

// Map::SetTerrainType, after the type went to the worker: a lane cell keeps its
// lane type there (a bridge built over the lane, then taken down).
void AfterSetTerrainType(Map* map, int cell);

// The character move tick, on the last iteration once it wrote the walker's
// transform (just past its COPY-PINNED tail): a walker on a water cell rides a
// boat, held at the water's height (a boat itself, sailing at its fixed height,
// does not). `pos` is the transform's translation (x, y, z). Called only while g_riding (a link exists), so the faithful tick pays one
// test.
extern bool g_riding;
void Ride(const CharacterComponent* rider, float* pos, float yaw);

}  // namespace Ferries
}  // namespace Ext

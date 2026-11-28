#pragma once
// ============================================================================
// src/ext/neighbour/shared_map.h -- NOT RETAIL. ext.neighbour M2: land claims and the
// neighbour's buildings on a lobby room's shared map (docs/fork/NEIGHBOUR_TOWNS.md section 2)
// ============================================================================
// Active only in a game started from the Multiplayer lobby (both towns on one map). Main thread.
//
// CLAIMS. Every structure this town places (a building, a field, the start town's buildings)
// claims its footprint at the relay; a road tile claims its tile. The relay grants first come
// first served and tells the neighbour; this town keeps a byte per tile of what the NEIGHBOUR
// holds, and Map::TestPlacement / Map::PlaceRoadCell refuse those tiles (their EXT SEAMs). A
// structure's claim is released when the structure is gone.
//
// MIRRORS. Every structure is also reported (template path, position, rotation, footprint);
// the neighbour's are drawn here as see-through models: a bare Graphics::MeshInstance in the
// scene, as PlacementTool draws its preview -- not an entity, so this town's manager never
// counts them, and nothing here can pick, remove or work on them. A structure's ground (a
// stockpile's yard, a field's soil, a foundation: its DecalDescription) is painted as well, as a
// terrain decal (WorldGeometry::AddDecal, as DecalComponent paints it). Their solid tiles are marked in
// the path worker's own copy of the terrain (never in Map::cells, so nothing of it is saved):
// citizens walk round them.
//
// M3. ROADS: a road tile this town finishes (Map::FinishRoadCell) is sent; the neighbour's become
// finished roads here at once, by the same call. RESOURCES: a tree, rock or deposit that goes
// (EntityList::DrainRemovals) is sent and the same one goes here; a sapling a forester plants
// (CreateNaturalResource) is sent and planted here. Natural spreading: each half of the map spreads in
// its owner's game only (SpreadsHere), which sends the new plants. NATURE SYNC: the town that does not
// own a half checks its copy of it against the owner's now and then and mends the blocks that differ
// (nature_sync.h), so a change that never arrived does not keep the copies apart.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

class ComponentEntity;
class ComponentDescription;

namespace Neighbour {

class Link;
class Message;
struct SavedState;

namespace SharedMap {

// The game started (a lobby game: `on`), or ended. Drops everything of the last game.
void Begin(bool on);
void End();
// Sharing stops while the game goes on (a refusal): the mirrors leave the scene first, then End.
void Stop();

// EXT SEAMs: true when the neighbour holds the tile (only ever in a lobby game).
bool PeerHolds(int x, int y);
bool PeerHoldsCell(uint32_t cellIdx);
// EXT SEAM in PathfinderThread::SetCellTerrain: the terrain type the path worker gets for `cell`. A tile
// under a solid part of the neighbour's building stays blocked whatever this town writes there (a tree
// felled under it, a field's soil); `terrainType` otherwise, and always when the extension is off.
int PathTerrain(uint32_t cell, int terrainType);

// EXT SEAMs at the places structures are made: claim + mirror the entity's footprint.
void NoteStructure(ComponentEntity* entity);
// EXT SEAM in Map::PlaceRoadCell: a road tile this town lays.
void NoteRoadCell(uint32_t cellIdx);
// A lobby game loaded from its save: report every structure standing (its claim and mirror).
void ScanExisting();

// The changes still waiting for the relay -- roads, resource changes, structures that are gone, land
// to claim or give back -- go into the save, so a town that quits while offline sends them on its next
// connect (structures standing are claimed and mirrored again on every join anyway). Main thread:
// WaitingGeneration moves whenever they change; SaveWaiting copies them into `s`'s map* fields;
// RestoreWaiting queues a loaded save's again (after ScanExisting).
uint64_t WaitingGeneration();
void     SaveWaiting(SavedState& s);
void     RestoreWaiting(const SavedState& s);

// EXT SEAM in RoadTool::CalcPath: the road tool's template for a road type (sent with the road).
void NoteRoadTemplate(uint8_t roadType, const ComponentDescription* tool);
// EXT SEAM at the end of Map::FinishRoadCell: a road tile finished (terrainType 2: removed).
void NoteRoadFinished(int cellIdx, uint32_t terrainType);
// EXT SEAM in EntityList::DrainRemovals: an entity is freed (a natural resource: sent).
void NoteRemoved(ComponentEntity* entity);
// EXT SEAM in CreateNaturalResource: a forester planted a sapling.
void NotePlanted(ComponentEntity* entity);
// Nature spreading by itself (a grown plant seeding a new one: GrowthComponent::SpawnSubResource,
// SpawnComponent::DoSpawn) is run by one town per half of the map, so the two copies of the land grow
// the same forest: the town in seat 0 spreads the west half (x < size / 2), seat 1 the east; the
// other game takes those plants from the relay (NotePlanted / "nat add"). False where this game must
// not spread: the other half, a tile the neighbour holds, or a shared map whose seat is not known yet
// (a loaded game before the relay's welcome). Always true outside a shared map and before play (world
// generation must stay the same in both games).
bool SpreadsHere(int x, int y);
// A plant's end by itself (past its growth threshold, or frozen below nothing: GrowthComponent::Update)
// is decided by the same game: on a tile under one of this town's own structures (its field, its
// orchard) this town's, on a tile the neighbour holds the neighbour's, elsewhere the half's owner's.
// Each copy rolls each plant's threshold and has its own weather, so two copies deciding alone let
// every plant die at the earlier of two times, in both. False where this copy waits for the other's
// removal; always true outside a shared map and before play. Like spreading, a half whose owner is not
// in the room (or a loaded game before the relay's welcome) does not end its plants here: that half
// stands still until its town plays.
bool DecidesNature(int x, int y);
// This town's seat in the room (the relay's welcome), for SpreadsHere.
void SetSeat(int seat);
// Whether the neighbour's game is in the room (the welcome, then "peer"): the nature sync asks it, now
// and then, how its half stands and mends this copy of that half (nature_sync.h).
void SetPeerOnline(bool online);
// This town's livestock (LivestockComponent: in a pasture or roaming free), for the villager samples
// (walkers.h): the neighbour draws them as it draws our villagers. As of the last walk of the entity list
// (every few seconds), less those removed since.
void OwnLivestock(std::vector<ComponentEntity*>* out);

// From Neighbour::BeginFrame: send what is queued, notice structures that are gone.
void Tick(Link* link, uint64_t frame);
// The relay joined us: send every structure again (the relay keeps the latest per id).
// A welcome from the relay (a join or a reconnect), before the neighbour's land and structures arrive:
// what this game holds of them from before is forgotten at the end of that list (mirrorEnd).
void OnWelcome();
void OnJoined(Link* link);
// The line to the relay dropped: no list goes until the next welcome (restate.h).
void OnLineDown();
// A loaded game's first join (hello `reload`): the relay forgets what this town's seat held, since the
// save may be older than it. Once the neighbour's land is in (its `mirrorEnd`), this town's roads go
// again: a claim for each road tile of its own, a road message for each finished one.
void ExpectReload();
// A relay message for the shared map; true when it was one.
bool OnMessage(const Message& m, std::string* notice);

}  // namespace SharedMap
}  // namespace Neighbour

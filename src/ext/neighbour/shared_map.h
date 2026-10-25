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
// (CreateNaturalResource) is sent and planted here. Natural spreading stays local to each copy.
// ============================================================================

#include <cstdint>
#include <string>

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

// From Neighbour::BeginFrame: send what is queued, notice structures that are gone.
void Tick(Link* link, uint64_t frame);
// The relay joined us: send every structure again (the relay keeps the latest per id).
void OnJoined(Link* link);
// A loaded game's first join (hello `reload`): the relay forgets what this town's seat held, since the
// save may be older than it. Once the neighbour's land is in (its `mirrorEnd`), this town's roads go
// again: a claim for each road tile of its own, a road message for each finished one.
void ExpectReload();
// A relay message for the shared map; true when it was one.
bool OnMessage(const Message& m, std::string* notice);

}  // namespace SharedMap
}  // namespace Neighbour

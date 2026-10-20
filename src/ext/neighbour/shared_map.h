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
// counts them, and nothing here can pick, remove or work on them.
// ============================================================================

#include <cstdint>
#include <string>

class ComponentEntity;

namespace Neighbour {

class Link;
class Message;

namespace SharedMap {

// The game started (a lobby game: `on`), or ended. Drops everything of the last game.
void Begin(bool on);
void End();

// EXT SEAMs: true when the neighbour holds the tile (only ever in a lobby game).
bool PeerHolds(int x, int y);
bool PeerHoldsCell(uint32_t cellIdx);

// EXT SEAMs at the places structures are made: claim + mirror the entity's footprint.
void NoteStructure(ComponentEntity* entity);
// EXT SEAM in Map::PlaceRoadCell: a road tile this town lays.
void NoteRoadCell(uint32_t cellIdx);

// From Neighbour::BeginFrame: send what is queued, notice structures that are gone.
void Tick(Link* link, uint64_t frame);
// The relay joined us: send every structure again (the relay keeps the latest per id).
void OnJoined(Link* link);
// A relay message for the shared map; true when it was one.
bool OnMessage(const Message& m, std::string* notice);

}  // namespace SharedMap
}  // namespace Neighbour

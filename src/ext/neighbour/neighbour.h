#pragma once
// ============================================================================
// src/ext/neighbour/neighbour.h -- NOT RETAIL. ext.neighbour, class C (locked for the game,
// pseudo-mod ext:neighbour, formatVersion 1). docs/fork/NEIGHBOUR_TOWNS.md.
// ============================================================================
// Two players, two PCs, two ordinary single-player games; this extension is the postman
// between them, through a relay (tools/neighbour_server/, a Cloudflare Worker). Milestone M0:
// the game joins a room and the neighbour's chat lines appear in the event log.
//
// SETTINGS (settings.cfg, read when a game starts):
//   ext.neighbour=1                  the extension (class C: off by default, locked per game)
//   neighbour.server=wss://...       the relay's address (ws:// for a local test relay)
//   neighbour.room=<name>            the room both towns join
//   neighbour.password=<text>        the room's password (the first town in sets it)
//   neighbour.name=<text>            this town's name, as the neighbour sees it
// No server: the extension stays offline and says so once in the event log.
//
// THE SEAMS (each one line; the faithful path pays nothing while the extension is off):
//   GameUpdateLoop::Load start   -> ResetForGame(this)  (beside Ext::LockForGame)
//   GameUpdateLoop::Load, final  -> SelectForGame()  (beside FastPaths::SelectForGame, 2nd call)
//   GameUpdateLoop::Update       -> BeginFrame()     (beside the other EXT SEAM BeginFrames)
//   ~GameUpdateLoop              -> EndGame(this)    (beside Ext::UnlockForGame)
// The save: one payload in the class C tail record (protocol.h SavedState, savetail.h).
// ============================================================================

#include <cstdint>
#include <memory>
#include <functional>
#include <string>

#include "ext/neighbour/lobby.h"   // RoomSettings

class ComponentDescription;
namespace Object { class External; }

class GameUpdateLoop;

namespace Neighbour {

class Transport;

// `owner` is the game's GameUpdateLoop, as for Ext::LockForGame / UnlockForGame: a reload's new
// loop may start its game before the old loop is deleted, and the old one must not hang up
// the new one's line.
void ResetForGame(const void* owner);   // hang up, forget the last game's room state, register the payload
void SelectForGame();   // connect when ext.neighbour is on for this game, else stay idle
void BeginFrame();      // main thread, once per frame: what arrived -> the game
void EndGame(const void* owner);   // hang up (joins the network threads) if `owner` owns the line

// Queue a chat line to the neighbour (no-op while the extension is off for this game).
void SendChat(const std::string& text);

// What the Neighbour panel shows.
struct Status {
    bool        active = false;      // ext.neighbour on for this game
    bool        serverSet = false;
    bool        connected = false;   // the line is up
    bool        joined = false;      // the relay welcomed us into the room
    std::string room, name, peer;
    bool        peerOnline = false;
    int         unsent = 0;          // parcels the relay has not confirmed yet
    int         waiting = 0;         // received parcels not fully delivered yet
};
Status GetStatus();
// Send goods / a family now (offline: they wait in the outbox). "" on success, else why not.
std::string SendGoods(const ComponentDescription* resource, int count);
std::string SendFamily();

// ---- M2: the Multiplayer lobby --------------------------------------------------------------
// The next new game belongs to a lobby room: set by the lobby right before the game starts,
// taken by the next ResetForGame (a new game only), dropped by a refused BeforeNewGame. While set
// it also turns ext.neighbour on for that one game (Ext::SetRequested, restored once the game has
// taken its lock), so starting from the lobby needs no settings.cfg editing.
struct LobbyStart {
    bool         create = false;   // true: the room is created when the game starts (BeforeNewGame)
    std::string  server, room, pass, town;
    int          seat = -1;        // a join knows its seat at once; a create learns 0
    RoomSettings settings;         // a join: the room's (StartNewGame is called with them)
};
void SetLobbyStart(const LobbyStart& start);
bool HasLobbyStart();
void ClearLobbyStart();

// EXT SEAM, MainMenuDialog::StartNewGame (its first line): a lobby create registers the room with
// the settings the player chose before anything is built. False = refused (`why` says why); the
// caller then does not start the game. Anything else: true, nothing done.
bool BeforeNewGame(uint32_t seed, uint32_t terrainSize, bool disastersOff, const Object::External* mapGen,
                   const Object::External* terrainGen, const Object::External* climateGen, std::string& why);
// EXT SEAM, StartTown_FindSpawnTile: the centre of the start spiral on a lobby room's shared map
// (the two towns start apart); false = the map centre, as retail.
bool StartCentre(int dim, int* cx, int* cy);
// EXT SEAMs, StartTown_SpawnResources (M3): on a lobby room's shared map both towns scatter the
// same trees and rocks. Begin / End swap the world's random numbers for the scatter's own, seeded
// from the map seed; ScatterCell restarts them for each candidate tile (a tile one copy skips --
// its own buildings stand there -- cannot shift the others); ScatterExcluded keeps both towns'
// sites clear, not just this one's. Outside a lobby game all four do nothing.
// NoteSpawnTiles (StartTown_SpawnEntities, right after its own spawn tile): the other town's spawn
// tile, found by the same search from the other seat's centre (`find` runs the game's search) --
// both towns' real sites, the same on both PCs, are what the scatter keeps clear.
void NoteSpawnTiles(int ownX, int ownY, int townSize, const std::function<void(int*, int*)>& find);
void ScatterBegin(GameUpdateLoop* world);
void ScatterEnd(GameUpdateLoop* world);
void ScatterCell(int pool, int cellIdx);
bool ScatterExcluded(int dim, int x, int y, int sizeSq);
// EXT SEAM, WorldGen_BuildTerrain: the height hash it prints ([WorldHash] height f32), the
// fingerprint of the land alone (seed, size, terrain), compared with the neighbour's on join.
void NoteWorldHash(uint64_t hash);

// neighbour_panel.cpp: the in-game Neighbour panel (N opens and closes it).
void PanelPollHotkey();   // from BeginFrame
void PanelClose();        // the game ends: close it if open

// link_platform.cpp: the Link's transport over the Platform WebSocket seam.
std::unique_ptr<Transport> MakePlatformTransport();

}  // namespace Neighbour

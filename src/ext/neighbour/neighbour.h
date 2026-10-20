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

#include <memory>
#include <string>

class ComponentDescription;

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

// neighbour_panel.cpp: the in-game Neighbour panel (N opens and closes it).
void PanelPollHotkey();   // from BeginFrame
void PanelClose();        // the game ends: close it if open

// link_platform.cpp: the Link's transport over the Platform WebSocket seam.
std::unique_ptr<Transport> MakePlatformTransport();

}  // namespace Neighbour

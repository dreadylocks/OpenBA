#pragma once
// ============================================================================
// src/ext/neighbour/lobby.h -- NOT RETAIL. ext.neighbour's Multiplayer lobby: talking to the relay
// ============================================================================
// docs/fork/NEIGHBOUR_TOWNS.md (M2). Three short conversations with the relay, each over its own
// connection that closes when the answer is in: list the rooms (/lobby), create a room, join a
// room (/room/<name>). They run from the main menu, where nothing else is going on, so they wait
// for the answer (at most `timeoutMs`) on the calling thread while a Link does the talking.
// Standard C++ only (the unit tests drive them with a fake transport).
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "ext/neighbour/handshake.h"
#include "ext/neighbour/link.h"

namespace Neighbour {

// A room's new-game settings: the seed, the map size row (0/1/2 = 256/384/512), disasters on or
// off, the start condition / terrain / climate generators by resource path, and the map type: "" for
// one shared land, "continents" for a land each with sea between (continents.h). On the wire the map
// type rides in front of the terrain path ("continents:<path>"); here `terrain` is the path alone.
struct RoomSettings {
    int64_t     seed = 0;
    int         size = 0;
    bool        disasters = true;
    std::string start, terrain, climate;
    std::string map;
};

// One line of the lobby's list.
struct RoomInfo {
    std::string  name, creator;
    int          seats = 0;          // towns in it (0..2)
    int64_t      lastActive = 0;     // seconds since 1970
    RoomSettings settings;
};

// The result of a create or a join.
struct RoomJoin {
    int          seat = -1;          // 0 = the creator's town, 1 = the joiner's
    std::string  token;              // this town's seat token (protocol.h ValidToken); keep it
    std::string  creator, peer;      // the neighbour town, if any
    RoomSettings settings;
    bool         created = false;    // the room carries settings (a lobby room)
};

// There is no built-in relay (interop spec layer 0: no default address tied to one person's
// account): the relay is settings.cfg neighbour.server or what the player types in the lobby.

// `server` is a ws:// or wss:// base (RoomUrl / LobbyUrl add the path). Each returns false with
// a short reason in `why` on any failure: no answer, a refusal from the relay, a bad reply.
// `info` is this game's layer-1 facts (handshake.h), sent with the create / hello: the relay
// refuses a join whose retail data differs from the room's.
bool ListRooms(TransportFactory factory, const std::string& server, std::vector<RoomInfo>& out,
               std::string& why, uint32_t timeoutMs = 8000);
bool CreateRoom(TransportFactory factory, const std::string& server, const std::string& room,
                const std::string& pass, const std::string& town, const RoomSettings& settings,
                const LocalInfo& info, RoomJoin& out, std::string& why, uint32_t timeoutMs = 8000);
// `token`: this town's seat token for the room when it has one (a town coming back), else "".
bool JoinRoom(TransportFactory factory, const std::string& server, const std::string& room,
              const std::string& pass, const std::string& town, const std::string& token,
              const LocalInfo& info, RoomJoin& out, std::string& why, uint32_t timeoutMs = 8000);

std::string LobbyUrl(const std::string& server);
// The settings read back from a welcome or a room line; false when they are missing or bad.
bool ReadSettings(const Message& m, RoomSettings& out);
// "Medium map, Valleys, Mild, Easy start, disasters on" ("Continents (Valleys)" for a Continents room):
// for the lobby's list.
std::string DescribeSettings(const RoomSettings& s);

}  // namespace Neighbour

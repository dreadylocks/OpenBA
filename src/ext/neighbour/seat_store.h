#pragma once
// ============================================================================
// src/ext/neighbour/seat_store.h -- NOT RETAIL. ext.neighbour: this machine's seat tokens
// ============================================================================
// docs/fork/NEIGHBOUR_PROTOCOL.md "Seats". The relay seats a town by the token it handed that town
// on its first join, never by name. The game keeps the token in its save, but a town that joins and
// quits (or crashes) before saving would lose it, and the room would never let it back in. So every
// token is also written, the moment it arrives, to a small file beside settings.cfg
// (`<Documents>/Banished/neighbour_seats.txt`), keyed by relay, room and town name; a hello without a
// token from the save looks there. The file is this machine's only: the relay never sees a name used
// to pick a seat.
//
// One line per seat: token, relay, room, town, separated by tabs (control characters are dropped
// from every field). Lines that do not parse are skipped. The newest kMaxSeats are kept.
// Standard C++ only (unit tests); the file I/O is LoadSeatToken / RememberSeat (neighbour.cpp).
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace Neighbour {

constexpr size_t kMaxSeats = 200;

struct SeatKey {
    std::string server, room, town, token;
};

std::string          EncodeSeats(const std::vector<SeatKey>& seats);
std::vector<SeatKey> DecodeSeats(const std::string& text);

// Add or replace the entry for (server, room, town), as the newest; the oldest go past kMaxSeats.
// A trailing '/' on the server is ignored. False (nothing changed) for an invalid token or an
// empty server, room or town.
bool PutSeat(std::vector<SeatKey>& seats, const SeatKey& seat);
// The token for (server, room, town); "" when none.
std::string FindSeat(const std::vector<SeatKey>& seats, const std::string& server, const std::string& room,
                     const std::string& town);
// The newest seat for (server, room) whatever the town's name (the lobby: "this is your room, as
// <town>"); a SeatKey with an empty token when none.
SeatKey FindSeatInRoom(const std::vector<SeatKey>& seats, const std::string& server, const std::string& room);

// The file itself (neighbour.cpp, game side; main thread). LoadSeatToken: "" when there is none or the
// file cannot be read. RememberSeat re-reads the file, puts the seat and replaces the file in one write.
std::string LoadSeatToken(const std::string& server, const std::string& room, const std::string& town);
// Every seat in the file; empty when there is none or it cannot be read.
std::vector<SeatKey> LoadSeats();
void        RememberSeat(const std::string& server, const std::string& room, const std::string& town,
                         const std::string& token);

}  // namespace Neighbour

// ============================================================================
// src/ext/neighbour/seat_store.cpp -- NOT RETAIL. ext.neighbour's seat-token file format (seat_store.h)
// ============================================================================

#include "ext/neighbour/seat_store.h"

#include "ext/neighbour/protocol.h"   // ValidToken

namespace Neighbour {

namespace {

constexpr size_t kMaxFieldChars = 256;

std::string Field(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) continue;
        out += static_cast<char>(c);
        if (out.size() >= kMaxFieldChars) break;
    }
    return out;
}

std::string Server(const std::string& s) {
    std::string out = Field(s);
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

bool Same(const SeatKey& k, const std::string& server, const std::string& room, const std::string& town) {
    return k.server == server && k.room == room && k.town == town;
}

}  // namespace

std::string EncodeSeats(const std::vector<SeatKey>& seats) {
    std::string out;
    for (const SeatKey& k : seats) {
        out += k.token;
        out += '\t';
        out += k.server;
        out += '\t';
        out += k.room;
        out += '\t';
        out += k.town;
        out += '\n';
    }
    return out;
}

std::vector<SeatKey> DecodeSeats(const std::string& text) {
    std::vector<SeatKey> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t at = 0;
        for (;;) {
            const size_t tab = line.find('\t', at);
            f.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (f.size() != 4) continue;
        SeatKey k{Server(f[1]), Field(f[2]), Field(f[3]), f[0]};
        PutSeat(out, k);   // validates; a later line for the same seat wins
    }
    return out;
}

bool PutSeat(std::vector<SeatKey>& seats, const SeatKey& seat) {
    SeatKey k{Server(seat.server), Field(seat.room), Field(seat.town), seat.token};
    if (k.server.empty() || k.room.empty() || k.town.empty() || k.token.empty() || !ValidToken(k.token))
        return false;
    for (size_t i = 0; i < seats.size(); ++i) {
        if (Same(seats[i], k.server, k.room, k.town)) {
            seats.erase(seats.begin() + static_cast<long>(i));
            break;
        }
    }
    seats.push_back(k);
    if (seats.size() > kMaxSeats) {
        const long drop = static_cast<long>(seats.size() - kMaxSeats);
        seats.erase(seats.begin(), seats.begin() + drop);
    }
    return true;
}

std::string FindSeat(const std::vector<SeatKey>& seats, const std::string& server, const std::string& room,
                     const std::string& town) {
    const std::string s = Server(server);
    const std::string r = Field(room);
    const std::string t = Field(town);
    for (size_t i = seats.size(); i-- > 0;)
        if (Same(seats[i], s, r, t)) return seats[i].token;
    return std::string();
}

}  // namespace Neighbour

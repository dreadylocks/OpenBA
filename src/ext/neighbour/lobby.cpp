// ============================================================================
// src/ext/neighbour/lobby.cpp -- NOT RETAIL. ext.neighbour's lobby conversations (lobby.h)
// ============================================================================

#include "ext/neighbour/lobby.h"

#include <chrono>
#include <thread>

namespace Neighbour {

namespace {

constexpr size_t kMaxRooms = 500;
constexpr size_t kMaxPathChars = 160;

// Run one conversation: start a Link with `hello`, collect messages until `done` says so (true =
// finished) or the time runs out. A refusal ({"t":"error"}) ends it with the relay's reason.
template <class Done>
bool Converse(TransportFactory factory, const std::string& url, const Message& hello, uint32_t timeoutMs,
              std::string& why, Done done) {
    Link link(factory);
    Link::Options opt;
    opt.url = url;
    opt.hello = hello;
    opt.firstRetryMs = 1000;
    opt.maxRetryMs = 2000;
    link.Start(opt);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string lastProblem;
    for (;;) {
        std::vector<Incoming> got;
        link.Poll(got);
        for (Incoming& in : got) {
            if (in.kind == Incoming::Kind::Disconnected && !in.detail.empty()) lastProblem = in.detail;
            if (in.kind != Incoming::Kind::Message) continue;
            if (in.message.type == "error") {
                why = Sanitize(in.message.GetOr("why", "the server refused"), 160);
                link.Stop();
                return false;
            }
            if (done(in.message)) {
                link.Stop();
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    link.Stop();
    why = lastProblem.empty() ? std::string("the server did not answer in time") : "cannot reach the server (" + lastProblem + ")";
    return false;
}

bool ShortPath(const std::string& s) {
    if (s.empty() || s.size() > kMaxPathChars) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}

// "Game/Terrain/Valleys.rsc:resource" -> "Valleys".
std::string ShortName(const std::string& path) {
    std::string n = path;
    const size_t colon = n.find(':');
    if (colon != std::string::npos) n = n.substr(0, colon);
    const size_t slash = n.find_last_of("/\\");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    const size_t dot = n.rfind('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    return n.empty() ? std::string("?") : n;
}

bool ReadJoin(const Message& m, RoomJoin& out) {
    int64_t seat = -1;
    if (!m.GetInt("seat", seat) || seat < 0 || seat > 1) return false;
    out.seat = static_cast<int>(seat);
    out.peer = Sanitize(m.GetOr("peer", ""), kMaxNameChars);
    out.creator = Sanitize(m.GetOr("creator", ""), kMaxNameChars);
    bool created = false;
    m.GetBool("created", created);
    out.created = created && ReadSettings(m, out.settings);
    return true;
}

}  // namespace

const char* DefaultServer() { return "wss://openba-neighbour.spellpress.workers.dev"; }

std::string LobbyUrl(const std::string& server) {
    std::string out = server;
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out + "/lobby";
}

bool ReadSettings(const Message& m, RoomSettings& out) {
    RoomSettings s;
    int64_t size = 0, disasters = 0;
    if (!m.GetInt("seed", s.seed) || !m.GetInt("size", size) || !m.GetInt("disasters", disasters)) return false;
    if (s.seed < 0 || s.seed > 0xFFFFFFFFll || size < 0 || size > 2 || disasters < 0 || disasters > 1) return false;
    s.size = static_cast<int>(size);
    s.disasters = disasters != 0;
    if (!m.Get("start", s.start) || !m.Get("terrain", s.terrain) || !m.Get("climate", s.climate)) return false;
    if (!ShortPath(s.start) || !ShortPath(s.terrain) || !ShortPath(s.climate)) return false;
    out = s;
    return true;
}

std::string DescribeSettings(const RoomSettings& s) {
    static const char* kSizes[] = {"Small", "Medium", "Large"};
    const char* size = (s.size >= 0 && s.size <= 2) ? kSizes[s.size] : "?";
    return std::string(size) + " map, " + ShortName(s.terrain) + ", " + ShortName(s.climate) + ", " +
           ShortName(s.start) + " start, disasters " + (s.disasters ? "on" : "off");
}

bool ListRooms(TransportFactory factory, const std::string& server, std::vector<RoomInfo>& out,
               std::string& why, uint32_t timeoutMs) {
    std::vector<RoomInfo> rooms;
    const bool ok = Converse(factory, LobbyUrl(server), Message("list"), timeoutMs, why, [&](const Message& m) {
        if (m.type == "room" && rooms.size() < kMaxRooms) {
            RoomInfo r;
            r.name = Sanitize(m.GetOr("name", ""), 64);
            r.creator = Sanitize(m.GetOr("creator", ""), kMaxNameChars);
            int64_t seats = 0, last = 0;
            m.GetInt("seats", seats);
            m.GetInt("lastActive", last);
            r.seats = seats < 0 ? 0 : (seats > 2 ? 2 : static_cast<int>(seats));
            r.lastActive = last;
            // A room line carries the settings except the seed: read them with a stand-in seed.
            Message withSeed = m;
            withSeed.SetInt("seed", 0);
            if (!r.name.empty() && ReadSettings(withSeed, r.settings)) rooms.push_back(r);
            return false;
        }
        return m.type == "roomsEnd";
    });
    if (ok) out = std::move(rooms);
    return ok;
}

bool CreateRoom(TransportFactory factory, const std::string& server, const std::string& room,
                const std::string& pass, const std::string& town, const RoomSettings& settings,
                RoomJoin& out, std::string& why, uint32_t timeoutMs) {
    Message create("create");
    create.SetInt("v", kProtocolVersion).Set("pass", pass).Set("name", town).SetInt("seed", settings.seed)
          .SetInt("size", settings.size).SetInt("disasters", settings.disasters ? 1 : 0)
          .Set("start", settings.start).Set("terrain", settings.terrain).Set("climate", settings.climate);
    RoomJoin j;
    bool bad = false;
    const bool ok = Converse(factory, RoomUrl(server, room), create, timeoutMs, why, [&](const Message& m) {
        if (m.type != "welcome") return false;
        bad = !ReadJoin(m, j);
        return true;
    });
    if (ok && bad) { why = "the server's answer could not be read"; return false; }
    if (ok) out = j;
    return ok;
}

bool JoinRoom(TransportFactory factory, const std::string& server, const std::string& room,
              const std::string& pass, const std::string& town, RoomJoin& out, std::string& why,
              uint32_t timeoutMs) {
    Message hello("hello");
    hello.SetInt("v", kProtocolVersion).Set("room", room).Set("pass", pass).Set("name", town)
         .SetInt("lastSeq", 0).SetBool("join", true);
    RoomJoin j;
    bool bad = false;
    const bool ok = Converse(factory, RoomUrl(server, room), hello, timeoutMs, why, [&](const Message& m) {
        if (m.type != "welcome") return false;
        bad = !ReadJoin(m, j);
        return true;
    });
    if (ok && bad) { why = "the server's answer could not be read"; return false; }
    if (ok && !j.created) { why = "that room was not made in the lobby (it has no map settings)"; return false; }
    if (ok) out = j;
    return ok;
}

}  // namespace Neighbour

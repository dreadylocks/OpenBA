// ============================================================================
// src/ext/neighbour/neighbour.cpp -- NOT RETAIL. ext.neighbour's game side (neighbour.h)
// ============================================================================
// Everything here runs on the game's main thread. The saved state (save_state.h) is shared with
// the save tail's codec, which the background-save worker may run: touch it only under `s_savedMu`.
// ============================================================================

#include "ext/neighbour/neighbour.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

#include "ext/neighbour/handshake.h"   // the layer-1 hello fields, LocalInfoNow
#include "ext/neighbour/link.h"
#include "ext/neighbour/protocol.h"
#include "ext/neighbour/town.h"
#include "ext/neighbour/shared_map.h"
#include "ext/neighbour/walkers.h"      // the neighbour's villagers, seen
#include "ext/registry.h"   // Ext::Ext().neighbour
#include "ext/neighbour/save_state.h"   // the saved state, shared with the save-tail codec
#include "ext/neighbour/seat_store.h"   // seat tokens kept beside settings.cfg
#include "ext/neighbour/world_maths.h"  // SetPortableWorldMaths: a shared map is built with the ported maths
#include "ext/neighbour/continents_world.h"   // a Continents room's map (§12)

#include "Game/EventLog.h"         // EventLogEntry
#include "Game/GameUpdateLoop.h"   // GameUpdateLoop::_singleton->m_eventNotifier
#include "Game/PlayerToolManager.h"   // PlayerToolManager::paused: the shared pause
#include "io/Persistence.h"        // the settings map (neighbour.* keys)
#include "io/settings_store.h"      // SettingsStore::DefaultPath: where settings.cfg (and the seat file) live
#include "platform/common/platform_settings.h"   // ReadWholeFile / WriteFileAtomic
#include "system/String.h"
#include "system/Value.h"      // System::Random
#include "system/Filename.h"
#include "object/External.h"
#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/Map.h"

namespace Neighbour {

namespace {

// The event log row this extension writes. Its own text, colour and icon; an eventType no
// retail writer uses (the notification pop-up files it under the "other" category, on by
// default; retail's event log window reads only the colour, text and tile, so a save that
// holds such a row still loads in retail).
constexpr int32_t  kEventType     = 0x7E;
constexpr int32_t  kEventIcon     = 0;            // "EventIcon0", the general event icon
constexpr uint32_t kColourChat    = 0xFF8FE3FFu;  // light blue: a neighbour's words
constexpr uint32_t kColourStatus  = 0xFFB8D8B0u;  // soft green: joins, leaves, line state
constexpr uint32_t kColourProblem = 0xFFFFB070u;  // amber: something needs the player

struct Config {
    std::string server, room, password, name;
};

bool                  s_active = false;     // ext.neighbour on for this game and configured
const void*           s_owner = nullptr;    // the GameUpdateLoop whose game this is
std::unique_ptr<Link> s_link;
Config                s_config;
bool                  s_announcedOffline = false;   // one "cannot reach" line per outage
bool                  s_everConnected = false;
bool                  s_refused = false;   // the relay refused this town for good (a "fatal" error)
bool                  s_joined = false;      // a welcome arrived on the current line
std::string           s_peerName;            // the neighbour town, as the relay names it
bool                  s_peerOnline = false;
uint64_t              s_frame = 0;
// M2: the lobby. s_pending is set by the lobby for the next new game; ResetForGame moves it to
// s_lobby for the game's lifetime.
bool                  s_hasPending = false;
LobbyStart            s_pending;
bool                  s_pendingPrevRequested = false;
bool                  s_lobbyGame = false;
LobbyStart            s_lobby;
bool                  s_restoreRequested = false;
bool                  s_restoreTo = false;
std::string           s_worldHashNew;     // NoteWorldHash during this Load (a new game's map)
// A new game (not a save) that the relay has not yet welcomed: its hellos say `fresh`, so the relay
// drops what an earlier game left in this town's seat. The first welcome ends it.
bool                  s_freshGame = false;
uint64_t              s_parcelCounter = 0;
constexpr uint64_t    kRetryWaitingFrames = 600;   // ~10 s at 60 fps: try waiting parcels again
// The shared map's changes waiting for the relay are copied into the saved state when they change, at
// most this often (the save may be written on the save worker, so the copy is made here, in the frame).
constexpr uint64_t    kWaitingSyncFrames = 15;
uint64_t              s_waitingSynced = 0;     // SharedMap::WaitingGeneration at the last copy

std::mutex& s_savedMu        = SavedMutex();     // the save-tail codec may run on the save worker
SavedState& s_saved          = Saved();          // what goes into the save (save_state.cpp)
bool&       s_loadedFromSave = LoadedFromSave();

// --- settings -------------------------------------------------------------

std::string Setting(const char* key) {
    const ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr) return std::string();
    System::String k(key);
    const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
    if (node == nullptr || node->element.value._data == nullptr) return std::string();
    return node->element.value._data;
}

Config ReadConfig() {
    Config c;
    c.server   = Setting("neighbour.server");   // no default: interop spec layer 0
    c.room     = Sanitize(Setting("neighbour.room"), 64);
    c.password = Setting("neighbour.password");
    c.name     = Sanitize(Setting("neighbour.name"), kMaxNameChars);
    if (c.room.empty()) c.room = "neighbours";
    if (c.name.empty()) c.name = "A town";
    return c;
}

// --- the event log --------------------------------------------------------

void Post(const std::string& text, uint32_t colour) {
    fprintf(stderr, "[Neighbour] %s\n", text.c_str());
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_eventNotifier == nullptr) return;
    EventLog* log = g->m_eventNotifier;
    const System::String str(text.c_str());
    const EventLogEntry entry(colour, str, kEventType, /*severity*/ 1, /*tile*/ -1, kEventIcon,
                              /*popup*/ -1);
    const int count = log->entries.GetSize();
    log->entries.SetSize(count + 1, entry);
}

Message Hello() {
    int64_t lastSeq = 0;
    std::string token;
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        lastSeq = s_saved.lastSeq;
        token = s_saved.token;
    }
    Message m("hello");
    m.SetInt("v", kProtocolVersion)
     .Set("room", s_config.room)
     .Set("pass", s_config.password)
     .Set("name", s_config.name)
     .SetInt("lastSeq", lastSeq);
    if (!token.empty()) m.Set("token", token);   // our seat (the relay never seats a town by name)
    if (s_freshGame && !s_loadedFromSave) m.SetBool("fresh", true);
    AddHandshake(m, LocalInfoNow());
    return m;
}

// --- parcels --------------------------------------------------------------

std::string PeerLabel() { return s_peerName.empty() ? std::string("your neighbor") : s_peerName; }

// --- the shared pause -----------------------------------------------------
// Either town pausing pauses both, and either carrying on resumes both (the user's call). The toolbar's
// pause / play buttons and the TogglePause key all write PlayerToolManager::paused; the frame reads it,
// and a change the player made here goes to the neighbour. One the neighbour made is applied here and
// is not sent back.
bool s_pauseSeen = false;    // PlayerToolManager::paused at the last look
bool s_pauseKnown = false;   // s_pauseSeen is this game's (a game's first frame reads it)

PlayerToolManager* ToolManager() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_toolManager : nullptr;
}

void SendPause(bool on) {
    if (!s_link || !s_joined) return;
    Message m("pause");
    m.SetBool("on", on);
    s_link->Send(m);
}

void PollPause() {
    PlayerToolManager* t = ToolManager();
    if (t == nullptr) return;
    const bool now = t->paused;
    if (!s_pauseKnown) {
        s_pauseSeen = now;
        s_pauseKnown = true;
        return;
    }
    if (now == s_pauseSeen) return;
    s_pauseSeen = now;
    SendPause(now);
}

void ApplyPause(const Message& m) {
    bool on = false;
    if (!m.GetBool("on", on)) return;
    PlayerToolManager* t = ToolManager();
    if (t == nullptr) return;
    const std::string from = Sanitize(m.GetOr("from", ""), kMaxNameChars);
    if (t->paused != on) {
        t->paused = on;
        Post((from.empty() ? PeerLabel() : from) + (on ? " paused the game." : " carried on."), kColourStatus);
    }
    s_pauseSeen = on;   // the neighbour's change: not news to send back
    s_pauseKnown = true;
}

// A neighbour arriving, or this town joining, while this game is paused: the other pauses too.
void ShareCurrentPause() {
    PlayerToolManager* t = ToolManager();
    if (t != nullptr && t->paused) SendPause(true);
}

// A fresh parcel id: this town's clock and a counter, in base 36 (ValidParcelId's alphabet).
std::string NewParcelId() {
    static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const uint64_t t = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const uint64_t parts[2] = {t, ++s_parcelCounter};
    std::string id;
    for (uint64_t v : parts) {
        std::string chunk;
        do { chunk += kDigits[v % 36]; v /= 36; } while (v != 0);
        if (!id.empty()) id += '-';
        id.append(chunk.rbegin(), chunk.rend());
    }
    return id;
}

Message ParcelMessage(const Parcel& p) {
    Message m("parcel");
    m.Set("id", p.id).Set("kind", p.kind).Set("body", p.body);
    return m;
}

// Every parcel the relay has not confirmed, again (each join: the line may have dropped first).
void ResendOutbox() {
    if (!s_link) return;
    std::vector<Parcel> out;
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        out = s_saved.outbox;
    }
    for (const Parcel& p : out) s_link->Send(ParcelMessage(p));
}

void Queue(const Parcel& p) {
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        s_saved.outbox.push_back(p);
    }
    if (s_link && s_joined) s_link->Send(ParcelMessage(p));   // else the next welcome sends it
}

std::string GoodsText(const std::vector<std::pair<std::string, int>>& goods) {
    std::string out;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    for (size_t i = 0; i < goods.size(); ++i) {
        if (i > 0) out += (i + 1 == goods.size()) ? " and " : ", ";
        ComponentDescription* res = f != nullptr
            ? f->FindOrLoadAs<ComponentDescription>(System::Filename(goods[i].first.c_str())) : nullptr;
        out += std::to_string(goods[i].second) + " " + Sanitize(Town::DisplayName(res), 40);
    }
    return out;
}

// Deliver as much of a goods parcel as fits; `p.body` keeps what did not. True when all of it
// is delivered (or the parcel is unusable and dropped).
bool DeliverGoods(Parcel& p, bool firstTry) {
    std::vector<std::pair<std::string, int>> goods, left, given;
    if (!DecodeGoods(p.body, goods)) {
        Post("A parcel from " + PeerLabel() + " could not be read; it was dropped.", kColourProblem);
        return true;
    }
    for (const auto& g : goods) {
        const int n = Town::GiveGoods(g.first, g.second);
        if (n < 0) {
            Post("A parcel from " + PeerLabel() + " holds goods this game does not know (" +
                     Sanitize(g.first, 60) + "); they were dropped.", kColourProblem);
            continue;
        }
        if (n > 0) given.emplace_back(g.first, n);
        if (n < g.second) left.emplace_back(g.first, g.second - n);
    }
    const std::string from = p.from.empty() ? PeerLabel() : p.from;
    if (!given.empty())
        Post(firstTry ? from + " sent you " + GoodsText(given) + "."
                      : "More of " + from + "'s parcel fitted: " + GoodsText(given) + ".", kColourChat);
    if (!left.empty() && firstTry)
        Post("Not everything fitted in your storage (" + GoodsText(left) +
                 "); the rest arrives when there is room.", kColourStatus);
    if (!left.empty()) p.body = EncodeGoods(left);
    return left.empty();
}

bool DeliverFamily(const Parcel& p) {
    std::vector<FamilyMember> members;
    if (!DecodeMembers(p.body, members)) {
        Post("A family from " + PeerLabel() + " could not be read; the parcel was dropped.", kColourProblem);
        return true;
    }
    const int made = Town::ReceiveFamily(members);
    if (made == 0) return false;   // no townsperson to copy yet: later
    const std::string from = p.from.empty() ? PeerLabel() : p.from;
    Post("A family of " + std::to_string(made) + " from " + from + " has joined your town.", kColourChat);
    return true;
}

// A parcel the relay delivered: apply it once, remember it, acknowledge it.
void OnParcel(const Message& m) {
    Parcel p;
    if (!m.Get("id", p.id) || !ValidParcelId(p.id) || !m.Get("kind", p.kind) || !m.Get("body", p.body)) return;
    if (p.kind != "goods" && p.kind != "family") return;
    if (!Town::InGame()) return;   // not acknowledged: the relay keeps it
    p.from = Sanitize(m.GetOr("from", ""), kMaxNameChars);
    bool seen = false;
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        for (const std::string& id : s_saved.received) seen |= id == p.id;
    }
    if (!seen) {
        const bool done = p.kind == "goods" ? DeliverGoods(p, true) : DeliverFamily(p);
        std::lock_guard<std::mutex> lock(s_savedMu);
        s_saved.received.push_back(p.id);
        while (s_saved.received.size() > kKeepReceivedIds) s_saved.received.erase(s_saved.received.begin());
        if (!done && s_saved.waiting.size() < kMaxSavedParcels) s_saved.waiting.push_back(p);
    }
    if (s_link) {
        Message ack("ack");
        ack.Set("id", p.id);
        s_link->Send(ack);
    }
}

// Waiting parcels (goods that did not fit, a family with nobody to copy yet): try again.
void RetryWaiting() {
    std::vector<Parcel> waiting;
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        if (s_saved.waiting.empty()) return;
        waiting.swap(s_saved.waiting);
    }
    std::vector<Parcel> still;
    for (Parcel& p : waiting) {
        const bool done = p.kind == "goods" ? DeliverGoods(p, false) : DeliverFamily(p);
        if (!done) still.push_back(p);
    }
    std::lock_guard<std::mutex> lock(s_savedMu);
    for (Parcel& p : still) s_saved.waiting.push_back(std::move(p));
}

// --- incoming -------------------------------------------------------------

void OnMessage(const Message& m) {
    if (Walkers::OnMessage(m)) return;   // the neighbour's villagers (lobby games)
    {
        std::string notice;
        if (SharedMap::OnMessage(m, &notice)) {   // claims and mirrors (lobby games)
            if (!notice.empty()) Post(notice, kColourProblem);
            return;
        }
    }
    if (m.type == "chat") {
        const std::string from = Sanitize(m.GetOr("from", "Neighbor"), kMaxNameChars);
        const std::string text = Sanitize(m.GetOr("text", ""), kMaxChatChars);
        int64_t seq = 0;
        if (m.GetInt("seq", seq)) {
            std::lock_guard<std::mutex> lock(s_savedMu);
            if (seq <= s_saved.lastSeq) return;   // already shown (a replay after a reconnect)
            s_saved.lastSeq = seq;
        }
        if (!text.empty()) Post(from + ": " + text, kColourChat);
        if (s_link) s_link->SetHello(Hello());   // the next reconnect asks only for newer lines
    } else if (m.type == "welcome") {
        const std::string peer = Sanitize(m.GetOr("peer", ""), kMaxNameChars);
        bool online = false;
        m.GetBool("online", online);
        const std::string token = m.GetOr("token", "");
        if (!token.empty() && ValidToken(token)) {   // a first join: keep the seat's token (saved)
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(s_savedMu);
                changed = s_saved.token != token;
                s_saved.token = token;
            }
            // And at once beside settings.cfg: a town that quits before saving keeps its seat.
            RememberSeat(s_config.server, s_config.room, s_config.name, token);
            if (changed && s_link) s_link->SetHello(Hello());
        }
        s_joined = true;
        s_peerName = peer;
        s_peerOnline = online;
        if (s_freshGame) {   // told once: a reconnect must not drop this game's own buildings
            s_freshGame = false;
            if (s_link) s_link->SetHello(Hello());
        }
        ResendOutbox();
        SharedMap::OnJoined(s_link.get());
        std::string hash;
        {
            std::lock_guard<std::mutex> lock(s_savedMu);
            hash = s_saved.worldHash;
        }
        if (!hash.empty() && s_link) {
            Message h("hash");
            h.Set("hash", hash);
            s_link->Send(h);
        }
        if (online) ShareCurrentPause();
        std::string line = "Joined the room \"" + s_config.room + "\".";
        if (peer.empty())  line += " No neighbor has joined yet.";
        else if (online)   line += " " + peer + " is here.";
        else               line += " " + peer + " is away.";
        Post(line, kColourStatus);
    } else if (m.type == "peer") {
        const std::string peer = Sanitize(m.GetOr("name", "Your neighbor"), kMaxNameChars);
        bool online = false;
        m.GetBool("online", online);
        s_peerName = peer;
        s_peerOnline = online;
        Post(peer + (online ? " has arrived." : " has left."), kColourStatus);
        if (online) ShareCurrentPause();
        else Walkers::Clear();
    } else if (m.type == "pause") {
        ApplyPause(m);
    } else if (m.type == "parcel") {
        OnParcel(m);
    } else if (m.type == "hashCheck") {
        bool same = false;
        m.GetBool("same", same);
        if (same) Post("Your map and " + PeerLabel() + "'s are the same land.", kColourStatus);
        else Post("Your map and " + PeerLabel() + "'s are NOT the same land: the two games differ (a different "
                  "version of the game, different mods, or a different seed). Shared-map play will not line up.",
                  kColourProblem);
    } else if (m.type == "stored") {
        std::string id;
        if (m.Get("id", id)) {
            std::lock_guard<std::mutex> lock(s_savedMu);
            auto& out = s_saved.outbox;
            for (size_t i = 0; i < out.size(); ++i)
                if (out[i].id == id) { out.erase(out.begin() + static_cast<long>(i)); break; }
        }
    } else if (m.type == "compat") {
        // A difference the separate-town profile only warns about (a different build or mod list).
        Post(Sanitize(m.GetOr("warn", "Your game and your neighbor's differ."), kMaxChatChars), kColourProblem);
    } else if (m.type == "error") {
        bool fatal = false;
        m.GetBool("fatal", fatal);
        Post("The neighbor server says: " + Sanitize(m.GetOr("why", "an unknown problem"), kMaxChatChars),
             kColourProblem);
        if (fatal && s_link) {   // refused for good (another game's data, another land): stop retrying
            s_refused = true;
            SharedMap::Stop();   // the game goes on: the mirrors and villagers leave its scene
            Walkers::Clear();
            Walkers::End();
            s_link->Stop();
            Post("Neighbor towns stopped for this game.", kColourProblem);
        }
    }
    // "pong" and anything newer: ignored.
}

void OnIncoming(Incoming& in) {
    switch (in.kind) {
    case Incoming::Kind::Message:
        OnMessage(in.message);
        break;
    case Incoming::Kind::Connected:
        fprintf(stderr, "[Neighbour] connected to %s\n", s_config.server.c_str());
        s_everConnected = true;
        s_announcedOffline = false;
        break;
    case Incoming::Kind::Disconnected:
        s_joined = false;
        s_peerOnline = false;
        if (in.detail.empty()) break;   // our own Stop
        fprintf(stderr, "[Neighbour] offline: %s\n", in.detail.c_str());
        if (!s_announcedOffline) {
            s_announcedOffline = true;
            Post(s_everConnected ? "Lost the line to the neighbor server; retrying."
                                 : "Cannot reach the neighbor server (" + Sanitize(in.detail, 120) +
                                       "); retrying.",
                 kColourProblem);
        }
        break;
    }
}

}  // namespace

namespace {
void HangUp() {
    PanelClose();
    SharedMap::End();
    Walkers::End();
    if (s_link) {
        s_link->Stop();
        s_link.reset();
    }
    s_active = false;
    s_joined = false;
}
}  // namespace

// --- seat tokens beside settings.cfg (seat_store.h) -------------------------

namespace {

// `<Documents>/Banished/neighbour_seats.txt`: settings.cfg's folder (IO::SettingsStore::DefaultPath).
bool SeatFilePath(std::string& out) {
    std::string settings;
    if (!IO::SettingsStore::DefaultPath(settings)) return false;
    const size_t cut = settings.find_last_of("/\\");
    if (cut == std::string::npos) return false;
    out = settings.substr(0, cut + 1) + "neighbour_seats.txt";
    return true;
}

}  // namespace

std::string LoadSeatToken(const std::string& server, const std::string& room, const std::string& town) {
    std::string path, text;
    if (!SeatFilePath(path) || !Platform::ReadWholeFile(path.c_str(), text)) return std::string();
    return FindSeat(DecodeSeats(text), server, room, town);
}

std::vector<SeatKey> LoadSeats() {
    std::string path, text;
    if (!SeatFilePath(path) || !Platform::ReadWholeFile(path.c_str(), text)) return {};
    return DecodeSeats(text);
}

void RememberSeat(const std::string& server, const std::string& room, const std::string& town,
                  const std::string& token) {
    std::string path;
    if (token.empty() || !SeatFilePath(path)) return;
    std::string text;
    Platform::ReadWholeFile(path.c_str(), text);   // absent the first time
    std::vector<SeatKey> seats = DecodeSeats(text);
    if (!PutSeat(seats, SeatKey{server, room, town, token})) return;
    const std::string out = EncodeSeats(seats);
    if (!Platform::WriteFileAtomic(path.c_str(), out.data(), out.size()))
        fprintf(stderr, "[Neighbour] could not write the seat file %s\n", path.c_str());
    else
        fprintf(stderr, "[Neighbour] seat for room \"%s\" kept in %s\n", room.c_str(), path.c_str());
}

void ResetForGame(const void* owner) {
    HangUp();
    s_owner = owner;
    s_lobbyGame = false;
    s_worldHashNew.clear();
    if (s_hasPending) {   // the lobby's room, for this game
        s_lobby = s_pending;
        s_lobbyGame = true;
        s_hasPending = false;
        s_restoreRequested = true;
        s_restoreTo = s_pendingPrevRequested;
    }
    SetPortableWorldMaths(s_lobbyGame);   // before this Load generates the world (world_maths.h)
    Continents::SetForGame(s_lobbyGame && s_lobby.settings.map == Continents::kMapContinents,
                           static_cast<uint32_t>(s_lobby.settings.seed));
    // A lobby game is a shared map from its first structure on (the start town is built during
    // this Load, before SelectForGame).
    SharedMap::Begin(s_lobbyGame);
    Walkers::Begin(s_lobbyGame);
    s_freshGame = true;   // until the save (if this Load is one) says otherwise
    s_pauseKnown = false;
    s_waitingSynced = 0;
    std::lock_guard<std::mutex> lock(s_savedMu);
    s_saved = SavedState();
    s_loadedFromSave = false;
}

void SetLobbyStart(const LobbyStart& start) {
    if (const Ext::Entry* e = Ext::FindEntry("neighbour")) {
        if (!s_hasPending) s_pendingPrevRequested = Ext::Requested(*e);
        Ext::SetRequested(*e, true);   // this one game; restored in SelectForGame
    }
    s_pending = start;
    s_hasPending = true;
}

bool HasLobbyStart() { return s_hasPending; }

bool PendingJoinSettings(RoomSettings& out) {
    if (!s_hasPending || s_pending.create) return false;
    out = s_pending.settings;
    return true;
}

void ClearLobbyStart() {
    if (!s_hasPending) return;
    if (const Ext::Entry* e = Ext::FindEntry("neighbour")) Ext::SetRequested(*e, s_pendingPrevRequested);
    s_hasPending = false;
}

bool BeforeNewGame(uint32_t seed, uint32_t terrainSize, bool disastersOff, const ::Object::External* mapGen,
                   const ::Object::External* terrainGen, const ::Object::External* climateGen, std::string& why) {
    if (!s_hasPending || !s_pending.create) return true;
    auto path = [](const ::Object::External* x) {
        if (x == nullptr) return std::string();
        const System::Filename p = x->GetPath();
        return p.AsString()._data != nullptr ? std::string(p.AsString()._data) : std::string();
    };
    RoomSettings rs;
    rs.seed = static_cast<int64_t>(seed);
    rs.size = static_cast<int>(terrainSize);
    rs.disasters = !disastersOff;
    rs.start = path(mapGen);
    rs.terrain = path(terrainGen);
    rs.climate = path(climateGen);
    rs.map = s_pending.settings.map;   // the lobby's New room: one land or Continents
    RoomJoin j;
    if (!CreateRoom(&MakePlatformTransport, s_pending.server, s_pending.room, s_pending.pass, s_pending.town, rs,
                    LocalInfoNow(), j, why)) {
        fprintf(stderr, "[Neighbour] creating room \"%s\" refused: %s\n", s_pending.room.c_str(), why.c_str());
        ClearLobbyStart();
        return false;
    }
    s_pending.seat = j.seat;
    s_pending.token = j.token;
    s_pending.settings = rs;
    RememberSeat(s_pending.server, s_pending.room, s_pending.town, j.token);   // before the game can crash
    fprintf(stderr, "[Neighbour] room \"%s\" created (seed %u, %s)\n", s_pending.room.c_str(), seed,
            DescribeSettings(rs).c_str());
    return true;
}

bool StartCentre(int dim, int* cx, int* cy) {
    if (!s_lobbyGame || s_lobby.seat < 0 || dim <= 0) return false;
    // On a Continents room's map, each town in the middle of its own land.
    const Continents::Layout* land = Continents::ThisLayout();
    if (land != nullptr && land->mapSize == dim) {
        Continents::StartCentre(*land, s_lobby.seat, cx, cy);
        return true;
    }
    // The two towns start a quarter of the map apart, either side of the centre.
    *cx = s_lobby.seat == 0 ? (dim * 3) / 8 : (dim * 5) / 8;
    *cy = dim / 2;
    return true;
}

bool StartTileAllowed(int dim, int tx, int ty) {
    const Continents::Layout* land = Continents::ThisLayout();
    if (!s_lobbyGame || s_lobby.seat < 0 || land == nullptr || land->mapSize != dim) return true;
    return Continents::OnOwnLand(*land, s_lobby.seat, tx, ty);
}

namespace {
System::Random* s_scatter = nullptr;        // the scatter's own random numbers (a lobby game)
System::Random* s_scatterSaved = nullptr;   // the world's, put back by ScatterEnd
uint32_t        s_scatterSeed = 0;
bool            s_haveSpawns = false;   // both towns' spawn tiles are known (NoteSpawnTiles)
int             s_spawnX[2] = {0, 0}, s_spawnY[2] = {0, 0};
int             s_townSize = 0;

// Inside the clear disk of either town: twice the town's own radius round each real site.
bool NearATown(int x, int y, int townSize) {
    const int r2 = townSize * townSize * 4;
    for (int s = 0; s < 2; ++s) {
        const int dx = x - s_spawnX[s], dy = y - s_spawnY[s];
        if (dx * dx + dy * dy < r2) return true;
    }
    return false;
}
alignas(System::Random) unsigned char s_scatterMem[sizeof(System::Random)];
}  // namespace

void NoteSpawnTiles(int ownX, int ownY, int townSize, const std::function<void(int*, int*)>& find) {
    s_haveSpawns = false;
    if (!s_lobbyGame || s_lobby.seat < 0 || s_lobby.seat > 1 || !find) return;
    const int seat = s_lobby.seat;
    int ox = 0, oy = 0;
    s_lobby.seat = 1 - seat;   // StartCentre answers for the other town while its search runs
    find(&ox, &oy);
    s_lobby.seat = seat;
    s_spawnX[seat] = ownX;
    s_spawnY[seat] = ownY;
    s_spawnX[1 - seat] = ox;
    s_spawnY[1 - seat] = oy;
    s_townSize = townSize;
    s_haveSpawns = true;
    fprintf(stderr, "[Neighbour] start sites: town 1 at %d,%d, town 2 at %d,%d (this town: %d)\n", s_spawnX[0],
            s_spawnY[0], s_spawnX[1], s_spawnY[1], seat + 1);
    if (const Continents::Layout* land = Continents::ThisLayout()) {
        const bool own0 = Continents::OnOwnLand(*land, 0, s_spawnX[0], s_spawnY[0]);
        const bool own1 = Continents::OnOwnLand(*land, 1, s_spawnX[1], s_spawnY[1]);
        fprintf(stderr, "[Neighbour] continents: town 1 starts on its own land: %s, town 2 on its own: %s\n",
                own0 ? "yes" : "NO", own1 ? "yes" : "NO");
    }
}

void ScatterBegin(GameUpdateLoop* world) {
    if (!s_lobbyGame || world == nullptr || s_scatter != nullptr) return;
    s_scatterSeed = static_cast<uint32_t>(world->m_seed) ^ 0x5ca77e12u;
    s_scatter = ::new (s_scatterMem) System::Random(s_scatterSeed);
    s_scatterSaved = world->m_random;
    world->m_random = s_scatter;
}

void ScatterEnd(GameUpdateLoop* world) {
    if (s_scatter == nullptr || world == nullptr) return;
    world->m_random = s_scatterSaved;
    // The fingerprint of the scatter away from both towns' sites: the same in both towns' logs
    // when the forests match ([Neighbour] scatter).
    if (world->m_entityList != nullptr && world->m_game != nullptr) {
        const int dim = world->m_game->size;
        uint64_t h = 1469598103934665603ull;
        int n = 0;
        int sampleCell = -1;
        std::string sample;
        const auto& all = world->m_entityList->masterArray;
        for (int i = 0; i < all._size; ++i) {
            ComponentEntity* e = all._data[i].Get();
            if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
            MapComponent* mc = e->FindComponent<MapComponent>();
            if (mc == nullptr) continue;
            const int x = mc->tileX0, y = mc->tileY0;
            if (s_haveSpawns ? NearATown(x, y, s_townSize + 4) : false) continue;
            const uint64_t v = static_cast<uint64_t>(y * dim + x);
            h = (h ^ v) * 1099511628211ull;   // the order of the list is the scatter's own
            if (n++ == 0) {   // one of them, named (handy for testing a felled tree by hand)
                sampleCell = y * dim + x;
                const System::Filename path = e->_description->GetPath();
                if (path.AsString()._data != nullptr) sample = path.AsString()._data;
            }
        }
        fprintf(stderr, "[Neighbour] scatter: %d resources away from the towns, fingerprint %016llx (first: cell %d %s)\n", n,
                static_cast<unsigned long long>(h), sampleCell, sample.c_str());
    }
    s_scatter = nullptr;
    s_scatterSaved = nullptr;
}

void ScatterCell(int pool, int cellIdx) {
    if (s_scatter == nullptr) return;
    uint32_t h = s_scatterSeed;
    for (uint32_t v : {static_cast<uint32_t>(pool), static_cast<uint32_t>(cellIdx)}) {
        h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
        h *= 0x85ebca6bu;
        h ^= h >> 13;
    }
    ::new (s_scatterMem) System::Random(h);
}

bool ScatterExcluded(int dim, int x, int y, int sizeSq) {
    if (s_scatter == nullptr || dim <= 0 || !s_haveSpawns) return false;
    (void)sizeSq;   // the town's own disk (radius = its size) is inside this one
    return NearATown(x, y, s_townSize);
}

void NoteWorldHash(uint64_t hash) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    s_worldHashNew = buf;
}

void SelectForGame() {
    const bool want = Ext::Ext().neighbour;
    if (!want) {
        HangUp();
        return;
    }
    if (s_active) return;   // already running for this game
    if (s_restoreRequested) {   // the lobby turned the extension on for this game only
        if (const Ext::Entry* e = Ext::FindEntry("neighbour")) Ext::SetRequested(*e, s_restoreTo);
        s_restoreRequested = false;
    }
    s_config = ReadConfig();
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        if (s_lobbyGame) {   // a new game from the lobby: its room, its password, its name
            s_config.server = s_lobby.server;
            s_config.room = s_lobby.room;
            s_config.password = s_lobby.pass;
            s_config.name = s_lobby.town;
            s_saved.token = s_lobby.token;
        } else if (s_loadedFromSave && !s_saved.pass.empty() && !s_saved.room.empty()) {
            // A lobby game loaded from its save: it knows its own way back in.
            if (!s_saved.server.empty()) s_config.server = s_saved.server;
            s_config.room = s_saved.room;
            s_config.password = s_saved.pass;
            if (!s_saved.town.empty()) s_config.name = s_saved.town;
        }
        if (!s_worldHashNew.empty()) s_saved.worldHash = s_worldHashNew;   // a new game's map
        if (!s_lobbyGame && s_loadedFromSave && !s_saved.pass.empty()) {   // a lobby save: its structures again
            SharedMap::Begin(true);
            Walkers::Begin(true);
            SharedMap::ScanExisting();
            SharedMap::RestoreWaiting(s_saved);   // and what it had not sent yet
        }
        s_saved.town = s_config.name;
        s_saved.pass = s_lobbyGame || !s_saved.pass.empty() ? s_config.password : std::string();
        s_saved.server = s_config.server;
    }
    // No token in the save (a town that quit before saving, or a settings.cfg town on this machine
    // before): the one kept beside settings.cfg for this relay, room and town. Read before the lock.
    const std::string keptSeat = LoadSeatToken(s_config.server, s_config.room, s_config.name);
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        // A loaded town keeps its room unless settings.cfg names another one now.
        if (s_loadedFromSave && !s_saved.room.empty() && s_saved.room != s_config.room) {
            fprintf(stderr, "[Neighbour] the save was in room \"%s\"; settings.cfg names \"%s\" -- "
                    "joining the settings' room (%u unsent parcel(s) for the old room dropped)\n",
                    s_saved.room.c_str(), s_config.room.c_str(), static_cast<unsigned>(s_saved.outbox.size()));
            s_saved.lastSeq = 0;   // another room's sequence numbers mean nothing here
            s_saved.outbox.clear();
            s_saved.token.clear();   // and its seat token is the old room's
        }
        s_saved.room = s_config.room;
        if (s_saved.token.empty()) s_saved.token = keptSeat;
    }
    s_active = true;
    s_announcedOffline = false;
    s_everConnected = false;
    s_refused = false;
    s_joined = false;
    s_peerName.clear();
    s_peerOnline = false;
    if (s_config.server.empty()) {
        Post("Neighbor towns is on, but no server is set (neighbour.server in settings.cfg).",
             kColourProblem);
        return;
    }
    Link::Options opt;
    opt.url = RoomUrl(s_config.server, s_config.room);
    opt.hello = Hello();
    s_link.reset(new Link(&MakePlatformTransport));
    s_link->Start(opt);
    fprintf(stderr, "[Neighbour] on for this game: room \"%s\" as \"%s\" via %s\n",
            s_config.room.c_str(), s_config.name.c_str(), s_config.server.c_str());
}

void BeginFrame() {
    if (!s_active) return;
    ++s_frame;
    PanelPollHotkey();
    if (s_frame % kRetryWaitingFrames == 0) RetryWaiting();
    if (!s_link) return;
    SharedMap::Tick(s_link.get(), s_frame);
    {
        PlayerToolManager* t = ToolManager();
        Walkers::Tick(s_link.get(), s_peerOnline, t != nullptr && t->paused);
    }
    if (s_frame % kWaitingSyncFrames == 0) {
        const uint64_t gen = SharedMap::WaitingGeneration();
        if (gen != s_waitingSynced) {
            SavedState w;
            SharedMap::SaveWaiting(w);
            std::lock_guard<std::mutex> lock(s_savedMu);
            s_saved.mapWaiting.swap(w.mapWaiting);
            s_saved.mapUnbuilds.swap(w.mapUnbuilds);
            s_saved.mapClaims.swap(w.mapClaims);
            s_saved.mapReleases.swap(w.mapReleases);
            s_waitingSynced = gen;
        }
    }
    std::vector<Incoming> got;
    s_link->Poll(got);
    for (Incoming& in : got) OnIncoming(in);
    PollPause();
}

void EndGame(const void* owner) {
    if (owner == nullptr || owner != s_owner) return;
    HangUp();
    s_owner = nullptr;
}

Status GetStatus() {
    Status st;
    st.active = s_active;
    st.serverSet = !s_config.server.empty();
    st.connected = s_link && s_link->Connected();
    st.joined = s_joined;
    st.room = s_config.room;
    st.name = s_config.name;
    st.peer = s_peerName;
    st.peerOnline = s_peerOnline;
    std::lock_guard<std::mutex> lock(s_savedMu);
    st.unsent = static_cast<int>(s_saved.outbox.size());
    st.waiting = static_cast<int>(s_saved.waiting.size());
    return st;
}

std::string SendGoods(const ComponentDescription* resource, int count) {
    if (!s_active) return "Neighbor towns is not on for this game.";
    if (resource == nullptr || count <= 0) return "Pick some goods first.";
    const int taken = Town::TakeGoods(resource, count);
    if (taken <= 0) return "None of that is left in storage.";
    const System::Filename path = resource->GetPath();
    const char* name = path.AsString()._data;
    std::vector<std::pair<std::string, int>> goods;
    goods.emplace_back(name != nullptr ? name : "", taken);
    Parcel p;
    p.id = NewParcelId();
    p.kind = "goods";
    p.body = EncodeGoods(goods);
    Queue(p);
    Post("You sent " + GoodsText(goods) + " to " + PeerLabel() + ".", kColourChat);
    return std::string();
}

std::string SendFamily() {
    if (!s_active) return "Neighbor towns is not on for this game.";
    const std::vector<FamilyMember> members = Town::SendFamily();
    if (members.empty()) return "No family can leave right now (the town keeps at least one family).";
    Parcel p;
    p.id = NewParcelId();
    p.kind = "family";
    p.body = EncodeMembers(members);
    Queue(p);
    Post("A family of " + std::to_string(members.size()) + " moved to " + PeerLabel() + ".", kColourChat);
    return std::string();
}

void SendChat(const std::string& text) {
    if (!s_active || !s_link) return;
    const std::string clean = Sanitize(text, kMaxChatChars);
    if (clean.empty()) return;
    Message m("chat");
    m.Set("text", clean);
    s_link->Send(m);
    Post(s_config.name + ": " + clean, kColourChat);
}

}  // namespace Neighbour

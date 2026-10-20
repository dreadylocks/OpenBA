// ============================================================================
// src/ext/neighbour/neighbour.cpp -- NOT RETAIL. ext.neighbour's game side (neighbour.h)
// ============================================================================
// Everything here runs on the game's main thread except SavePayload, which the
// background-save worker may call: it only copies `s_saved` under `s_savedMu`.
// ============================================================================

#include "ext/neighbour/neighbour.h"

#include <cstdio>
#include <mutex>
#include <vector>

#include "ext/neighbour/link.h"
#include "ext/neighbour/protocol.h"
#include "ext/registry.h"   // Ext::Ext().neighbour
#include "ext/savetail.h"   // Ext::SaveTail::RegisterPayload

#include "Game/EventLog.h"         // EventLogEntry
#include "Game/GameUpdateLoop.h"   // GameUpdateLoop::_singleton->m_eventNotifier
#include "io/Persistence.h"        // the settings map (neighbour.* keys)
#include "system/String.h"

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

std::mutex  s_savedMu;   // SavePayload may run on the background-save worker
SavedState  s_saved;     // what goes into the save
bool        s_loadedFromSave = false;

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
    c.server   = Setting("neighbour.server");
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

// --- the save payload -----------------------------------------------------

std::vector<uint8_t> SavePayload() {
    std::lock_guard<std::mutex> lock(s_savedMu);
    return EncodeState(s_saved);
}

void LoadPayload(int32_t formatVersion, const std::vector<uint8_t>& bytes) {
    SavedState st;
    if (!DecodeState(formatVersion, bytes, st)) {
        fprintf(stderr, "[Neighbour] the save's neighbour record is unreadable (v%d, %u bytes) -- "
                "starting fresh\n", formatVersion, static_cast<unsigned>(bytes.size()));
        return;
    }
    std::lock_guard<std::mutex> lock(s_savedMu);
    s_saved = st;
    s_loadedFromSave = true;
}

Message Hello() {
    int64_t lastSeq = 0;
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        lastSeq = s_saved.lastSeq;
    }
    Message m("hello");
    m.SetInt("v", kProtocolVersion)
     .Set("room", s_config.room)
     .Set("pass", s_config.password)
     .Set("name", s_config.name)
     .SetInt("lastSeq", lastSeq);
    return m;
}

// --- incoming -------------------------------------------------------------

void OnMessage(const Message& m) {
    if (m.type == "chat") {
        const std::string from = Sanitize(m.GetOr("from", "Neighbour"), kMaxNameChars);
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
        std::string line = "Joined the room \"" + s_config.room + "\".";
        if (peer.empty())  line += " No neighbour has joined yet.";
        else if (online)   line += " " + peer + " is here.";
        else               line += " " + peer + " is away.";
        Post(line, kColourStatus);
    } else if (m.type == "peer") {
        const std::string peer = Sanitize(m.GetOr("name", "Your neighbour"), kMaxNameChars);
        bool online = false;
        m.GetBool("online", online);
        Post(peer + (online ? " has arrived." : " has left."), kColourStatus);
    } else if (m.type == "error") {
        Post("The neighbour server says: " + Sanitize(m.GetOr("why", "an unknown problem"), kMaxChatChars),
             kColourProblem);
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
        if (in.detail.empty()) break;   // our own Stop
        fprintf(stderr, "[Neighbour] offline: %s\n", in.detail.c_str());
        if (!s_announcedOffline) {
            s_announcedOffline = true;
            Post(s_everConnected ? "Lost the line to the neighbour server; retrying."
                                 : "Cannot reach the neighbour server (" + Sanitize(in.detail, 120) +
                                       "); retrying.",
                 kColourProblem);
        }
        break;
    }
}

}  // namespace

namespace {
void HangUp() {
    if (s_link) {
        s_link->Stop();
        s_link.reset();
    }
    s_active = false;
}
}  // namespace

void ResetForGame(const void* owner) {
    HangUp();
    s_owner = owner;
    Ext::SaveTail::RegisterPayload("neighbour", &SavePayload, &LoadPayload);
    std::lock_guard<std::mutex> lock(s_savedMu);
    s_saved = SavedState();
    s_loadedFromSave = false;
}

void SelectForGame() {
    const bool want = Ext::Ext().neighbour;
    if (!want) {
        HangUp();
        return;
    }
    if (s_active) return;   // already running for this game
    s_config = ReadConfig();
    {
        std::lock_guard<std::mutex> lock(s_savedMu);
        // A loaded town keeps its room unless settings.cfg names another one now.
        if (s_loadedFromSave && !s_saved.room.empty() && s_saved.room != s_config.room) {
            fprintf(stderr, "[Neighbour] the save was in room \"%s\"; settings.cfg names \"%s\" -- "
                    "joining the settings' room\n", s_saved.room.c_str(), s_config.room.c_str());
            s_saved.lastSeq = 0;   // another room's sequence numbers mean nothing here
        }
        s_saved.room = s_config.room;
    }
    s_active = true;
    s_announcedOffline = false;
    s_everConnected = false;
    if (s_config.server.empty()) {
        Post("Neighbour towns is on, but no server is set (neighbour.server in settings.cfg).",
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
    if (!s_active || !s_link) return;
    std::vector<Incoming> got;
    s_link->Poll(got);
    for (Incoming& in : got) OnIncoming(in);
}

void EndGame(const void* owner) {
    if (owner == nullptr || owner != s_owner) return;
    HangUp();
    s_owner = nullptr;
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

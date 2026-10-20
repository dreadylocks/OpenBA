#pragma once
// ============================================================================
// src/ext/neighbour/protocol.h -- NOT RETAIL. ext.neighbour (class C): the wire messages
// ============================================================================
// docs/fork/NEIGHBOUR_TOWNS.md §3. The game and the relay (tools/neighbour_server/) swap
// small JSON objects, one per WebSocket text frame. Every object has a string "t" (its
// type); the rest are flat fields: strings, integers, booleans. Nested values from a newer
// server are skipped, never trusted.
//
// The client sends:   hello {v, room, pass, name}    chat {text}
// The server sends:   welcome {seat, peer, online}   peer {name, online}
//                     chat {from, text, seq}         error {why}
//
// Everything here is standard C++ with no game or socket code: tests/unit/ext_neighbour_test.cpp
// links it alone. EVERY incoming byte is untrusted (a bad message must never crash a town):
// Parse bounds every length and fails closed.
// ============================================================================

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Neighbour {

constexpr int      kProtocolVersion = 1;
constexpr size_t   kMaxMessageBytes = 16 * 1024;  // a frame larger than this is dropped
constexpr size_t   kMaxChatChars    = 200;        // after Sanitize
constexpr size_t   kMaxNameChars    = 24;

// One flat JSON object. Field order is kept (the encoder writes it as given).
class Message {
public:
    std::string type;   // "t"

    Message() = default;
    explicit Message(std::string t) : type(std::move(t)) {}

    Message& Set(const std::string& key, const std::string& value);
    Message& SetInt(const std::string& key, int64_t value);
    Message& SetBool(const std::string& key, bool value);

    // False (and `out` untouched) when the field is absent or of another kind.
    bool Get(const std::string& key, std::string& out) const;
    bool GetInt(const std::string& key, int64_t& out) const;
    bool GetBool(const std::string& key, bool& out) const;
    std::string GetOr(const std::string& key, const std::string& fallback) const;

    std::string Encode() const;

private:
    enum class Kind : uint8_t { String, Int, Bool };
    struct Field {
        std::string key;
        Kind        kind;
        std::string s;
        int64_t     i = 0;
    };
    std::vector<Field> fields_;
    Field*       Find(const std::string& key);
    const Field* Find(const std::string& key) const;
    friend bool Parse(const std::string& text, Message& out);
};

// Parse one object. False on anything malformed, oversized, or without a string "t".
bool Parse(const std::string& text, Message& out);

// Text for the game's UI: UTF-8 and control characters become '?' or are dropped (the
// game's fonts are not known to cover more than printable ASCII), whitespace runs
// collapse to one space, ends are trimmed, and the result is cut to `maxChars`.
std::string Sanitize(const std::string& text, size_t maxChars);

// The address the game connects to: `server` (any trailing '/' dropped) + "/room/" + the room
// name, percent-encoded. The relay keeps one Durable Object per room name.
std::string RoomUrl(const std::string& server, const std::string& room);

// A parcel's contents on the wire. Goods: "path:count,path:count" (a path has no ',' and its
// count is the text after the LAST ':'; 1..kMaxParcelCount each, at most kMaxParcelLines lines).
// A family: "age/gender/child;..." (age in years, gender 0|1, child 0|1; 1..kMaxFamily members).
constexpr int kMaxParcelCount = 100000;
constexpr int kMaxParcelLines = 16;
constexpr int kMaxFamily      = 12;
struct FamilyMember {
    float   age = 0.0f;
    int32_t gender = 0;
    bool    child = false;
};
std::string EncodeGoods(const std::vector<std::pair<std::string, int>>& goods);
bool DecodeGoods(const std::string& text, std::vector<std::pair<std::string, int>>& out);
std::string EncodeMembers(const std::vector<FamilyMember>& members);
bool DecodeMembers(const std::string& text, std::vector<FamilyMember>& out);

// A parcel: goods or a family, with an id unique to its sender (the relay and the receiver use it
// to never deliver one twice). `body` is EncodeGoods / EncodeMembers text.
struct Parcel {
    std::string id;     // 1..kMaxParcelIdChars of [0-9a-z_-]
    std::string kind;   // "goods" | "family"
    std::string body;
    std::string from;   // the sending town's name (received parcels)
};
constexpr size_t kMaxParcelIdChars = 40;
bool ValidParcelId(const std::string& id);

// The class C save payload (savetail.h RegisterPayload): what a town remembers about its
// room between sessions.
//   v1: the room name and the last chat sequence number seen (the relay replays anything newer).
//   v2: + the OUTBOX (parcels this town sent that the relay has not yet confirmed it holds; they
//       are sent again on the next connect), the RECEIVED ids (the last kKeepReceivedIds parcels
//       applied here; a repeat is acknowledged and dropped) and the WAITING parcels (goods that
//       did not fit in storage yet; delivered as room appears).
//   v3: + how to get back in: the town's name, the room password and the relay address (a game
//       started from the Multiplayer lobby has no settings.cfg keys), and the world hash its map
//       was built with (empty when unknown).
struct SavedState {
    std::string              room;
    int64_t                  lastSeq = 0;
    std::vector<Parcel>      outbox;
    std::vector<std::string> received;
    std::vector<Parcel>      waiting;
    std::string              town, pass, server, worldHash;
};
constexpr int32_t kPayloadFormat   = 3;   // the registry row's formatVersion
constexpr size_t  kKeepReceivedIds = 128;
constexpr size_t  kMaxSavedParcels = 64;   // per list
std::vector<uint8_t> EncodeState(const SavedState& s);
bool DecodeState(int32_t formatVersion, const std::vector<uint8_t>& bytes, SavedState& out);

}  // namespace Neighbour

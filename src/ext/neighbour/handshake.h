#pragma once
// ============================================================================
// src/ext/neighbour/handshake.h -- NOT RETAIL. ext.neighbour: what a town says about itself
// ============================================================================
// docs/fork/NEIGHBOUR_PROTOCOL.md "hello". Layer 1 of the interop spec
// (fran-cap/Banished-Interop-Spec, issue #1): a hello carries the protocol version, the engine id and
// version, the sim build hash, the retail data hash, the ordered mod list, the active extension set
// and the most players this game takes. The separate-town rules settled there: a different protocol
// version or retail data hash REFUSES the join; a different sim build or mod list only WARNS. The
// relay applies them (tools/neighbour_server/src/index.js, CheckCompat).
//
// THE MOD LIST follows fran-cap's open proposal in #1 (not merged yet): each mod as name + size
// (required) + an optional xxHash64 of the package file, in load order. Its wire spelling is
// EncodeMods / DecodeMods alone, so a change to the proposal is one place here and one in the relay.
//
// Standard C++ only: tests/unit/ext_neighbour_test.cpp links it. What the game fills in (its
// packages, its mods, its build) is local_info.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ext/neighbour/protocol.h"

namespace Neighbour {

constexpr const char* kEngineId   = "openba";
constexpr int         kMaxPlayers = 2;      // this game's most towns in a room
constexpr size_t      kMaxMods    = 256;    // longer lists are cut (the message must fit kMaxMessageBytes)

// xxHash64 (Yann Collet's XXH64), written from the published algorithm. Streaming: Update any
// number of times, then Digest. A consistency check between two players' files, not a security one.
class Xxh64 {
public:
    explicit Xxh64(uint64_t seed = 0);
    void     Update(const void* data, size_t size);
    uint64_t Digest() const;

private:
    uint64_t v_[4];
    uint64_t seed_;
    uint64_t total_ = 0;
    uint8_t  buf_[32];
    size_t   bufLen_ = 0;
};
uint64_t    Xxh64Of(const void* data, size_t size, uint64_t seed = 0);
std::string Hex64(uint64_t v);   // 16 lowercase hex digits

struct ModEntry {
    std::string name;            // the package file's name, e.g. "BetterFields.pkm"
    uint64_t    size = 0;        // its size in bytes
    bool        hasHash = false;
    uint64_t    hash = 0;        // xxHash64 of the whole file
};

struct ExtEntry {
    std::string id;
    int32_t     formatVersion = 0;
};

// Everything a hello says about this game beside the room, password, name, token and lastSeq.
struct LocalInfo {
    std::string           engineVersion;   // OPENBA_VERSION
    std::string           simBuild;        // Hex64 of the build: version, commit, compiler, target
    std::string           dataHash;        // Hex64 over the retail packages; "" when unreadable
    std::vector<ModEntry> mods;            // the enabled mods, in load order
    std::vector<ExtEntry> exts;            // the active class B / C extensions
};

// "name|size|hash;name|size|;..." (an empty hash = none). '%', '|' and ';' in a name are written
// %25, %7C and %3B. Decode refuses anything else malformed (and more than kMaxMods entries).
std::string EncodeMods(const std::vector<ModEntry>& mods);
bool        DecodeMods(const std::string& text, std::vector<ModEntry>& out);
// "id:formatVersion,..." in registry order.
std::string EncodeExts(const std::vector<ExtEntry>& exts);

// The layer-1 fields of a hello or a create (beside v, room, pass, name, token, lastSeq).
void AddHandshake(Message& m, const LocalInfo& info);

// This game's facts (local_info.cpp, game side: not in the unit tests). The first call reads the
// retail packages and the mods (about a second); later calls reuse that and re-read only the
// active extension set.
LocalInfo LocalInfoNow();

}  // namespace Neighbour

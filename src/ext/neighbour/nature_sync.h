#pragma once
// ============================================================================
// src/ext/neighbour/nature_sync.h -- NOT RETAIL. ext.neighbour: mending the shared map's nature
// ============================================================================
// On a shared map each town's game spreads plants on its own half only (shared_map.h SpreadsHere) and
// sends the other every change; the half's owner is the one whose copy of it is right. A change that
// never arrives (a relay that could not write, a queue that overflowed, a game that crashed before its
// save) used to leave the two copies apart for good. So the town that does not own a half asks the
// owner, now and then, for that half's digest: per kBlock x kBlock tiles, how many natural resources
// and an order-free hash of their (tile, template). It compares the digest with its own copy, asks for
// the full list of every block that differs, and makes those blocks match the owner's.
//
// This file is the part without game objects (shared_map.cpp walks the entities and sends the
// messages); the unit tests run it as it is.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Neighbour {
namespace NatureSync {

constexpr int kBlock = 16;   // a block's side in tiles

// One natural resource as the sync sees it. -1: not known (an add then rolls its own).
struct Plant {
    int         cell = -1;
    std::string tmpl;
    int         grp = -1;    // the model's mesh group and variant
    int         var = -1;
    int         prog = -1;   // growth, per mille of the template's full size
};

// A block's natural resources: how many, and the sum of their PlantHash.
struct BlockSum {
    uint32_t count = 0;
    uint64_t hash = 0;
    bool operator==(const BlockSum& o) const { return count == o.count && hash == o.hash; }
    bool operator!=(const BlockSum& o) const { return !(*this == o); }
};

// The block a tile is in, and its half of the map (0 west, x < size / 2; 1 east).
int BlockOf(int cell, int mapSize);
int HalfOf(int cell, int mapSize);
// A resource's hash: its tile and template, order-free once summed (the map fingerprint's).
uint64_t PlantHash(int cell, const std::string& tmpl);
void AddTo(std::map<int, BlockSum>& sums, int mapSize, int cell, const std::string& tmpl);

// The digest as text, "block,count,hash;...", cut into lists of at most maxChars (one empty list for
// an empty digest). DecodeDigest adds a list's blocks to `out`; false when it is malformed.
std::vector<std::string> EncodeDigest(const std::map<int, BlockSum>& sums, size_t maxChars);
bool DecodeDigest(const std::string& list, std::map<int, BlockSum>& out);

// The blocks where the owner's digest and this copy's differ, in order. A block in `skip` (one this
// town changed after it asked) is left out and counted in *skipped.
std::vector<int> Differing(const std::map<int, BlockSum>& owner, const std::map<int, BlockSum>& here,
                           const std::set<int>& skip, int* skipped);

// The owner's answer, in messages of at most about maxChars. Each carries its own template table, its
// entries "cell,template,grp,var,prog" and the blocks it completes: a block is named done in the message
// with its last entry or a later one (never an earlier one), so a full block may span messages and the
// receiver applies a block only once it is named done.
struct BlockMessage {
    std::string tmpls;   // '\n'-joined
    std::string list;    // ';'-joined entries
    std::string done;    // ','-joined block numbers
};
std::vector<BlockMessage> PackBlocks(const std::vector<int>& blocks,
                                     const std::map<int, std::vector<Plant>>& plants, size_t maxChars);
// Adds a message's entries to `into` (by their block on a map of mapSize) and lists the blocks it
// completes in `done`. False when it is malformed (nothing is added then).
bool UnpackBlocks(const BlockMessage& m, int mapSize, std::map<int, std::vector<Plant>>& into,
                  std::vector<int>& done);

// What makes a block here match the owner's: which of `here` go (indices into it) and which of the
// owner's plants come. Matched by tile and template; a plant both copies have stays as it is.
struct Mend {
    std::vector<size_t> remove;
    std::vector<Plant>  add;
};
Mend PlanMend(const std::vector<Plant>& here, const std::vector<Plant>& owner);

// "1,2,3" <-> {1, 2, 3}. ParseInts: false on anything but integers (an empty string is an empty list).
bool ParseInts(const std::string& s, std::vector<int>& out);
std::string JoinInts(const std::vector<int>& v);

}  // namespace NatureSync
}  // namespace Neighbour

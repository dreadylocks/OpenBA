#pragma once
// ============================================================================
// src/ext/neighbour/town.h -- NOT RETAIL. ext.neighbour: what a parcel does to the town
// ============================================================================
// The game-touching half of milestone M1 (docs/fork/NEIGHBOUR_TOWNS.md): goods out of and into
// the town's storage, a family out of the town, a family into it. Main thread only, during a
// game. Every function checks the world is there and returns 0 / false when it is not.
//
// GOODS are named on the wire by their resource's path (ComponentDescription::GetPath), e.g. the
// template a storage slot holds; both PCs run the same game data, so the path resolves to the
// same material on the other side. An unknown path, or one that is not a raw material, is refused.
//
// A FAMILY leaving is removed in one frame, children first (CitizenComponent::OnRemove kills an
// under-age child whose last parent is gone: a tombstone and a death line, not a departure). An
// arriving family is new citizens made the way a birth makes one (a copy of a townsperson's own
// template, CitizenComponent::Initialize, RegisterEntity), standing where a townsperson stands;
// they find a house and work on their own, like a nomad group the player accepted.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "ext/neighbour/protocol.h"   // FamilyMember

class ComponentDescription;

namespace Neighbour {
namespace Town {

bool InGame();

// One raw material the town holds, summed over every storage (count minus what is reserved for
// a pickup).
struct Stock {
    ComponentDescription* resource = nullptr;
    std::string           path;        // the wire name
    int                   available = 0;
};
// Every material with available > 0, in a stable order (by path).
std::vector<Stock> ListGoods();
// The material's display name (its own string table), or the path's file name.
std::string DisplayName(const ComponentDescription* resource);

// Take up to `count` of `resource` out of the town's storage; returns what was taken.
int TakeGoods(const ComponentDescription* resource, int count);
// Put up to `count` of the material at `path` into the town's storage, round robin over every
// storage that accepts it and has room; returns what fitted (-1: the path is not a raw material).
int GiveGoods(const std::string& path, int count);

// A family as it travels (protocol.h FamilyMember: adults first, then children).
using Member = FamilyMember;
// The family that would leave if the player sent one now (empty: none can go -- the town must
// keep at least one other family, and the family needs an adult). `familyCount` (when given) is
// the number of families in town.
std::vector<Member> PreviewFamily(int* familyCount);
// Remove that family from the town; returns its members (empty: nobody left).
std::vector<Member> SendFamily();
// Make an arriving family; returns how many citizens were made (0: no townsperson to copy).
int ReceiveFamily(const std::vector<Member>& members);

}  // namespace Town
}  // namespace Neighbour

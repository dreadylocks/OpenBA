#pragma once
// ============================================================================
// src/ext/neighbour/mirror_look.h -- NOT RETAIL. ext.neighbour: how a mirror looks
// ============================================================================
// A mirror (shared_map.cpp AddMirror) draws one of the neighbour's structures as its owner sees it:
// - its stage: the model group's mesh for how far the building is along. A site still waiting for its
//   materials has its model hidden (BuildComponent::OnAdded stamps ModelComponent::meshIndex -1, and
//   state 8 shows frame 0 only once every material is in), so its owner sees only the dark ground under
//   it; such a site is 0 percent and its mirror draws no mesh;
// - a stockpile's piles: StorageDisplayComponent draws one pile a footprint tile, each a mesh of the
//   stored good's model (its ModelDescription's group 1, the mesh for how full the tile is). The owner
//   sends which good and which mesh each tile shows (`piles`); the mirror draws the same.
//
// The pure parts, no game types: tests/unit/ext_neighbour_look_test.cpp checks them.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Neighbour {
namespace MirrorLook {

// How far along a model is, in percent, from its mesh index among `stageCount` stages (foundation
// first, the finished building last): a hidden model (index < 0) is 0, a model of one stage 100, and
// otherwise (index + 1) * 100 / stageCount, so a started building is at least 1.
int StagePercent(int meshIndex, int stageCount);

// The stage a mirror draws for `percent`: -1 (no mesh, the ground alone) for 0 or less, else the
// stage StagePercent came from, within [0, stageCount).
int StageOf(int64_t percent, int stageCount);

// One footprint tile's pile: the good (an index into the stockpile's list of goods) and the mesh of
// that good's pile model; good < 0 is a tile with nothing on it.
struct Pile {
    int good = -1;
    int mesh = 0;
};

// The piles as `piles` carries them: a tile each, in footprint order, comma-separated; an empty tile is
// empty, a pile "good.mesh".
std::string PileList(const std::vector<Pile>& piles);

// A `piles` list back: false (and `out` empty) when it is not one, has more than `maxTiles` tiles, or
// names a good at or past `goodCount` or a mesh past kMaxPileMesh.
constexpr int kMaxPileMesh = 63;
bool ParsePileList(const std::string& list, size_t maxTiles, int goodCount, std::vector<Pile>* out);

// The goods' templates as `piles` carries them ('|' between them), and back; false when one is empty,
// longer than maxChars, or holds a '|'.
std::string GoodList(const std::vector<std::string>& goods);
bool ParseGoodList(const std::string& list, size_t maxGoods, size_t maxChars, std::vector<std::string>* out);

}  // namespace MirrorLook
}  // namespace Neighbour

#pragma once
// ============================================================================
// src/ext/neighbour/restate.h -- NOT RETAIL. ext.neighbour: the restate
// ============================================================================
// A change sent down a line that died without closing never reached the relay (a demolition, land
// given back, a road laid or taken up), and one the relay sent this town down such a line never
// arrived. Nothing would send either again, so the two maps would differ until a town loaded a save.
// Two things put that right (docs/fork/NEIGHBOUR_PROTOCOL.md, `inv` and `roadsEnd`):
// - this town sends its whole list -- its structures and their stages, its finished road tiles, its
//   land -- a little after every join and once a minute (`inv`), and the relay puts its record right
//   from it and asks for what it lacks (`want`);
// - a join's replay of the neighbour's roads ends with `roadsEnd`, and a road of the neighbour's that
//   this copy holds and the replay did not name was taken up while the line was down.
// The mirrors (mirrorEnd) and the neighbour's land (claimBlock) were already replayed whole.
//
// The pure parts, no game types: tests/unit/ext_neighbour_restate_test.cpp checks them.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Neighbour {
namespace Restate {

// One `inv` part's three lists.
struct Part {
    std::string builds;   // "id:built;..."
    std::string roads;    // "cell:type;..."
    std::string land;     // "x,y,w,h;..."
};

// The entries in parts whose three lists together stay within `maxChars`, every entry once and in
// order. Always at least one part: an empty list says the town holds nothing.
std::vector<Part> Pack(const std::vector<std::string>& builds, const std::vector<std::string>& roads,
                       const std::vector<std::string>& land, size_t maxChars);

// The cells set in `mask` (dim x dim, row by row) as rectangles "x,y,w,1", a row's run of them in one.
std::vector<std::string> RowRuns(const std::vector<uint8_t>& mask, int dim);

// The neighbour's road tiles this copy still holds that the join's replay did not name: cells the
// neighbour holds (`peer`) with a finished road here (`road`) whose `seen` is not `join`.
std::vector<int> StaleRoads(const std::vector<uint8_t>& peer, const std::vector<uint8_t>& road,
                            const std::vector<uint32_t>& seen, uint32_t join);

// A `want` list, comma-separated: structure ids ([0-9a-z_-]{1,40}), or cells in [0, cellCount).
// Anything else in it is skipped.
std::vector<std::string> WantedIds(const std::string& list);
std::vector<int> WantedCells(const std::string& list, int cellCount);

}  // namespace Restate
}  // namespace Neighbour

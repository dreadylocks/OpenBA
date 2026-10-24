// ============================================================================
// src/ext/ferries/lanes.cpp -- docks paired across water, the lane traced
// (lanes.h). NOT RETAIL. Standard C++ only.
// ============================================================================
#include "ext/ferries/lanes.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <queue>
#include <tuple>

namespace Ext {
namespace Ferries {

namespace {

constexpr int kOrthoCost = 10;
constexpr int kDiagCost  = 14;
constexpr int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
constexpr int kDy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// A landing point: a water cell a citizen can step into from `land`.
struct Landing {
    int water = -1;
    int land = -1;
    uint16_t zone = 0;
};

bool InMap(int size, int x, int y) { return x >= 0 && y >= 0 && x < size && y < size; }

// The dock's landing points, in cell order (deterministic).
std::vector<Landing> LandingsOf(const MapView& m, const DockSite& dock) {
    const int n = m.size;
    // The dock's cells and the walkable cells round them.
    std::vector<int> shore;
    for (int c : dock.cells) {
        if (c < 0 || c >= n * n) continue;
        shore.push_back(c);
        const int x = c % n, y = c / n;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (InMap(n, nx, ny)) shore.push_back(ny * n + nx);
        }
    }
    std::sort(shore.begin(), shore.end());
    shore.erase(std::unique(shore.begin(), shore.end()), shore.end());

    std::map<int, Landing> byWater;   // water cell -> its landing (the smallest land cell wins)
    for (int c : shore) {
        if (!IsWalkable(m.type[c]) || m.zone[c] == 0) continue;
        const int x = c % n, y = c / n;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (!InMap(n, nx, ny)) continue;
            const int w = ny * n + nx;
            if (!IsWater(m.type[w])) continue;
            auto it = byWater.find(w);
            if (it == byWater.end() || c < it->second.land) byWater[w] = Landing{ w, c, m.zone[c] };
        }
    }
    std::vector<Landing> out;
    out.reserve(byWater.size());
    for (const auto& kv : byWater) out.push_back(kv.second);
    return out;
}

struct Owner {
    int dock = -1;
    int landing = -1;   // index into that dock's landings
};

}  // namespace

namespace {

// The search state for one map, reused dock after dock (only the cells a search touched are reset).
class Searcher {
public:
    Searcher(const MapView& m, const std::vector<DockSite>& docks, int maxLaneCells)
        : m_(m), docks_(docks), max_(maxLaneCells), landings_(docks.size()) {
        const size_t cells = static_cast<size_t>(m.size) * m.size;
        cost_.assign(cells, -1);
        steps_.assign(cells, 0);
        parent_.assign(cells, -1);
        source_.assign(cells, -1);
        for (size_t d = 0; d < docks.size(); ++d) {
            landings_[d] = LandingsOf(m, docks[d]);
            for (size_t i = 0; i < landings_[d].size(); ++i)
                owners_.emplace(landings_[d][i].water, Owner{ static_cast<int>(d), static_cast<int>(i) });
        }
    }

    // Spreads through water from dock d's landing points to the nearest landing point of a dock in
    // another land zone (one of the two built). False when none is in reach.
    bool From(int d, Link* out) {
        const int n = m_.size;
        const std::vector<Landing>& mine = landings_[static_cast<size_t>(d)];
        if (mine.empty()) return false;
        for (int c : touched_) {
            cost_[c] = -1;
            parent_[c] = -1;
            source_[c] = -1;
        }
        touched_.clear();

        using Node = std::tuple<int, int>;   // (cost, cell): ties by the lower cell, deterministic
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        for (size_t i = 0; i < mine.size(); ++i) {
            const int w = mine[i].water;
            if (cost_[w] >= 0) continue;
            cost_[w] = 0;
            steps_[w] = 0;
            source_[w] = static_cast<int>(i);
            touched_.push_back(w);
            open.emplace(0, w);
        }

        int found = -1;
        while (!open.empty()) {
            const int g = std::get<0>(open.top());
            const int c = std::get<1>(open.top());
            open.pop();
            if (g != cost_[c]) continue;   // a stale entry
            auto own = owners_.find(c);
            if (own != owners_.end() && own->second.dock != d) {
                const Owner& o = own->second;
                const uint16_t theirZone = landings_[o.dock][o.landing].zone;
                const uint16_t myZone = mine[source_[c]].zone;
                const bool oneBuilt = docks_[d].built || docks_[o.dock].built;
                if (theirZone != myZone && oneBuilt) {
                    found = c;
                    break;
                }
            }
            if (steps_[c] >= max_ - 1) continue;
            const int x = c % n, y = c / n;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + kDx[k], ny = y + kDy[k];
                if (!InMap(n, nx, ny)) continue;
                const int nc = ny * n + nx;
                if (!IsWater(m_.type[nc])) continue;
                if (k >= 4) {
                    // No corner cutting: a boat does not slip between two land cells.
                    if (!IsWater(m_.type[y * n + nx]) || !IsWater(m_.type[ny * n + x])) continue;
                }
                int step = k < 4 ? kOrthoCost : kDiagCost;
                if (m_.type[nc] == kShallowWater) step += step / 2;   // deep water is the boat's way
                const int ng = g + step;
                if (cost_[nc] >= 0 && cost_[nc] <= ng) continue;
                if (cost_[nc] < 0) touched_.push_back(nc);
                cost_[nc] = ng;
                steps_[nc] = steps_[c] + 1;
                parent_[nc] = c;
                source_[nc] = source_[c];
                open.emplace(ng, nc);
            }
        }
        if (found < 0) return false;

        const Owner& o = owners_.find(found)->second;
        Link link;
        link.dockA = d;
        link.dockB = o.dock;
        for (int c = found; c >= 0; c = parent_[c]) link.lane.push_back(c);
        std::reverse(link.lane.begin(), link.lane.end());
        const Landing& la = mine[source_[found]];
        const Landing& lb = landings_[o.dock][o.landing];
        link.landA = la.land;
        link.landB = lb.land;
        link.zoneA = la.zone;
        link.zoneB = lb.zone;
        *out = link;
        return true;
    }

private:
    const MapView& m_;
    const std::vector<DockSite>& docks_;
    int max_;
    std::vector<std::vector<Landing>> landings_;
    std::map<int, Owner> owners_;   // water cell -> the dock landing there (the lowest dock wins)
    std::vector<int> cost_, steps_, parent_, source_;
    std::vector<int> touched_;
};

bool Usable(const MapView& m, const std::vector<DockSite>& docks) {
    return m.size > 0 && m.type != nullptr && m.zone != nullptr && docks.size() >= 2;
}

}  // namespace

std::vector<Link> FindLinks(const MapView& m, const std::vector<DockSite>& docks, int maxLaneCells) {
    std::vector<Link> links;
    if (!Usable(m, docks)) return links;
    Searcher search(m, docks, maxLaneCells);
    std::map<std::pair<int, int>, bool> paired;
    for (size_t d = 0; d < docks.size(); ++d) {
        Link link;
        if (!search.From(static_cast<int>(d), &link)) continue;
        const std::pair<int, int> key(std::min(link.dockA, link.dockB), std::max(link.dockA, link.dockB));
        if (paired.count(key) != 0) continue;   // found from the other dock already
        paired[key] = true;
        links.push_back(link);
    }
    return links;
}

bool LinkFrom(const MapView& m, const std::vector<DockSite>& docks, int from, Link* out, int maxLaneCells) {
    if (!Usable(m, docks) || from < 0 || static_cast<size_t>(from) >= docks.size() || out == nullptr) return false;
    Searcher search(m, docks, maxLaneCells);
    return search.From(from, out);
}

std::vector<std::pair<int, uint8_t>> LaneCellTypes(const Link& link) {
    std::vector<std::pair<int, uint8_t>> out;
    const int len = static_cast<int>(link.lane.size());
    out.reserve(link.lane.size());
    for (int i = 0; i < len; ++i) {
        const bool end = i < kBoardCells || i >= len - kBoardCells;
        out.emplace_back(link.lane[i], end ? kBoardType : kLaneType);
    }
    return out;
}

std::vector<uint16_t> JoinZones(const std::vector<std::pair<uint16_t, uint16_t>>& pairs, uint16_t maxZone) {
    std::vector<uint16_t> root(static_cast<size_t>(maxZone) + 1);
    for (size_t i = 0; i < root.size(); ++i) root[i] = static_cast<uint16_t>(i);
    std::function<uint16_t(uint16_t)> find = [&](uint16_t z) -> uint16_t {
        while (root[z] != z) {
            root[z] = root[root[z]];
            z = root[z];
        }
        return z;
    };
    for (const auto& p : pairs) {
        if (p.first == 0 || p.second == 0 || p.first > maxZone || p.second > maxZone) continue;
        const uint16_t ra = find(p.first), rb = find(p.second);
        if (ra == rb) continue;
        if (ra < rb)
            root[rb] = ra;
        else
            root[ra] = rb;
    }
    std::vector<uint16_t> remap(root.size());
    for (size_t i = 0; i < root.size(); ++i) remap[i] = find(static_cast<uint16_t>(i));
    return remap;
}

}  // namespace Ferries
}  // namespace Ext

// ============================================================================
// src/ext/fast_paths/fast_search.cpp -- ext.fast_paths: the grid search
// ============================================================================
// NOT RETAIL. fast_search.h has the contract and the rule list R1-R10; each
// rule is marked below where it is reproduced, with the retail line it
// mirrors (src/game/Action/action_pf_astar.cpp unless named otherwise).
//
// WHY THE SCALED HEURISTIC KEEPS RETAIL'S COST (rule (a)).
//   Retail's h(n) = H(n) = the unit-cost distance to the goal: 10*(dx+dy) -
//   6*min(dx,dy) with diagonals, else 10*(dx+dy) (Manhattan) or 10*max(dx,dy)
//   (Chebyshev). Ours is h(n) = s * H(n), s = heuristicScale >= 0.
//   An edge n -> n' (n' a cell the search may enter, R2) costs
//       c = stepCost[t(n')] * 10   orthogonal,   stepCost[t(n')] * 14   diagonal.
//   One move changes H by at most 10 (orthogonal) or 14 (diagonal): dx and dy
//   each change by at most 1, and 10*(dx+dy) - 6*min moves by 10 per axis
//   changed, less 6 when both change and min moves with them, so a diagonal
//   changes it by at most 20 - 6 = 14; Manhattan / Chebyshev by at most 10
//   (they only run without diagonals). So
//       h(n) - h(n') = s * (H(n) - H(n')) <= s * 10 (or 14) <= c
//   whenever s <= stepCost[t(n')]: h is CONSISTENT on every edge the search
//   can take as long as s is at most the cheapest step cost of any enterable
//   cell, which is what HeuristicScale returns (min over the passable types
//   present in the grid). With h(goal) = 0, consistency gives admissibility,
//   so A* returns a cost-optimal path: exactly retail's cost (retail's own h
//   is s = 1, also consistent since every cost is >= 1 in the profiles).
//   Consistency also makes f non-decreasing along every expansion, so a
//   closed cell is never reopened (R5 still reproduced, never taken) and the
//   popped f never goes down -- what the bucket queue needs.
//
// WHY THE BUCKETS ARE BOUNDED (rule (b)).
//   Let F be the f of the cell being expanded (the queue's minimum). A
//   neighbour gets f' = F - h(n) + c + h(n') = F + c + (h(n') - h(n)), so
//       F <= f' <= F + cMax + s * 14 = F + D,   D = 14 * (maxCost + s)
//   (10 instead of 14 without diagonals): the lower bound is consistency, the
//   upper the move bounds above. Every live entry therefore lies in
//   [F, F + D], and a ring of R > D buckets indexed f mod R never aliases.
//   With retail's profiles maxCost <= 16 and s <= 16: D <= 448, R = 512. The
//   ring is sized per request; an f outside [F, F + D] (inputs that break the
//   assumptions) returns Unsupported, so the caller falls back to retail.
//
// WHY THE POP ORDER IS RETAIL'S (rule (b), R7).
//   Retail's open set is an AVL tree keyed (fcost asc, seq desc) and the
//   leftmost node is popped (PathOpenSet::Insert 0x14001d450 sends a key left
//   when `fcost < node.fcost || (fcost == node.fcost && node.seq < key.seq)`;
//   FindPath descends leftmost). seq is PathSearch::sequence, incremented on
//   every Relax 0x14001d320. A re-key deletes the old node and inserts a new
//   one with a new seq. Here every relax pushes (cell, seq) on top of bucket
//   f's stack and records seq as the cell's current one; an entry whose seq is
//   no longer the cell's, or whose cell is no longer open, is stale and
//   skipped. Within one bucket the live entries sit in push order = seq order,
//   so the top live entry is the maximum seq: the same cell retail pops.
// ============================================================================
#include "ext/fast_paths/fast_search.h"

#include <climits>
#include <cstdint>

namespace Ext {
namespace FastPaths {

namespace {

constexpr int32_t kWall     = -1;   // not walkable (R2)
constexpr int32_t kOffTable = -2;   // walkable but its type indexes past stepCost[0x20]

constexpr uint32_t kOpen   = 1;
constexpr uint32_t kClosed = 2;

constexpr int kMaxBuckets = 1 << 16;

// R1: Neighbour 0x1400a5ec0's direction order (action_pf_geom.cpp:66):
// 0 col+1, 1 col-1, 2 row+1, 3 row-1, 4 col+1 row+1, 5 col+1 row-1,
// 6 col-1 row-1, 7 col-1 row+1. 0-3 orthogonal, 4-7 diagonal.
constexpr int kDx[8] = { +1, -1,  0,  0, +1, +1, -1, -1 };
constexpr int kDy[8] = {  0,  0, +1, -1, +1, -1, -1, +1 };

inline int Abs(int v) { return v < 0 ? -v : v; }

// Heuristic 0x1400a6030 (action_pf_geom.cpp:101), inlined in FindPath at
// action_pf_astar.cpp:355-376, at unit cost.
inline int32_t UnitDistance(int dx, int dy, bool diagonal, bool manhattan) {
    if (diagonal) {
        const int mn = dx < dy ? dx : dy;
        return (dx + dy) * 10 - 6 * mn;
    }
    if (manhattan) return (dx + dy) * 10;
    return (dx > dy ? dx : dy) * 10;
}

}  // namespace

int32_t HeuristicScale(const int32_t* stepCost, uint32_t passableTypes, uint32_t presentTypes) {
    int32_t best = INT32_MAX;
    const uint32_t enterable = passableTypes & presentTypes;
    for (int t = 0; t < kStepCosts; ++t) {
        if (((enterable >> t) & 1u) == 0) continue;
        if (stepCost[t] < 0) return -1;
        if (stepCost[t] < best) best = stepCost[t];
    }
    return best == INT32_MAX ? 0 : best;
}

uint32_t PresentTypes(const uint8_t* cellFlags, int cellCount) {
    bool seen[0x80] = {};
    for (int i = 0; i < cellCount; ++i) seen[cellFlags[i] & 0x7f] = true;
    uint32_t mask = 0;
    for (int t = 0; t < kStepCosts; ++t)
        if (seen[t]) mask |= 1u << t;
    return mask;
}

void Searcher::Prepare(int cellCount, int bucketCount) {
    if (static_cast<int>(mark_.size()) != cellCount) {
        g_.assign(cellCount, 0);
        parent_.assign(cellCount, -1);
        seq_.assign(cellCount, 0);
        mark_.assign(cellCount, 0u);
        gen_ = 0;
    }
    // Retail bumps PathSearch::generation per run (Run 0x14001ce00, :403) and
    // never clears its node array; so do we, resetting on the wrap.
    if (++gen_ >= (1u << 30)) {
        for (uint32_t& m : mark_) m = 0u;
        gen_ = 1;
    }
    if (static_cast<int>(head_.size()) < bucketCount) head_.resize(bucketCount);
    for (int i = 0; i < bucketCount; ++i) head_[i] = -1;
    entries_.clear();
}

SearchResult Searcher::Run(const SearchInput& in, int32_t* path) {
    SearchResult out;
    const int w = in.width;
    const int cellCount = w * w;
    const int32_t s = in.heuristicScale;
    if (w <= 0 || s < 0) { out.status = Status::Unsupported; return out; }

    // R2 + R3 folded into one table over the whole flag byte: the walkable
    // test of Neighbour (action_pf_geom.cpp:85-88: `respectBlocked == 0 ||
    // (b & 0x80) == 0`, then `(passableTypes >> (b & 0x1f)) & 1`) and the cost
    // index of FindPath (action_pf_astar.cpp:338: `cellFlags[nb] & 0x7f`).
    int32_t cost[256];
    int32_t maxCost = 0;
    for (int b = 0; b < 256; ++b) {
        int32_t c;
        if (in.respectBlocked && (b & 0x80) != 0) {
            c = kWall;
        } else if (((in.passableTypes >> (b & 0x1f)) & 1u) == 0) {
            c = kWall;
        } else if ((b & 0x7f) >= kStepCosts) {
            c = kOffTable;
        } else {
            c = in.stepCost[b & 0x7f];
            if (c < 0) { out.status = Status::Unsupported; return out; }
            if (c > maxCost) maxCost = c;
        }
        cost[b] = c;
    }

    const int dirs = in.allowDiagonal ? 8 : 4;          // FindPath :323-326: 4, or 8 when allowDiagonal
    const int moveMax = in.allowDiagonal ? 14 : 10;
    // Int32 exactness: the largest g is below cellCount * moveMax * maxCost and
    // the largest h below 2 * w * 10 * s. Retail computes in int; refusing
    // inputs that could overflow keeps both searches exact and identical.
    const int64_t gBound = static_cast<int64_t>(cellCount) * moveMax * maxCost;
    const int64_t hBound = static_cast<int64_t>(w) * 20 * s;
    const int64_t span = static_cast<int64_t>(moveMax) * (static_cast<int64_t>(maxCost) + s);
    if (gBound + hBound >= INT32_MAX || span >= kMaxBuckets) {
        out.status = Status::Unsupported;
        return out;
    }
    int buckets = 1;
    while (buckets <= span) buckets <<= 1;
    const int32_t bucketMask = buckets - 1;
    const uint32_t D = static_cast<uint32_t>(span);

    Prepare(cellCount, buckets);
    const uint32_t openMark = (gen_ << 2) | kOpen;
    const uint32_t closedMark = (gen_ << 2) | kClosed;

    const int gx = in.goal % w;
    const int gy = in.goal / w;
    int32_t* const g = g_.data();
    int32_t* const parent = parent_.data();
    int32_t* const seqOf = seq_.data();
    uint32_t* const mark = mark_.data();
    int32_t* const head = head_.data();
    const uint8_t* const flags = in.cellFlags;
    const bool diag = in.allowDiagonal;
    const bool manh = in.manhattan;

    int32_t sequence = 0;   // PathSearch::sequence, reset per FindPath (:297)
    int liveOpen = 0;       // PathOpenSet::count

    // Relax 0x14001d320 (:215): set parent / g, mark open, push (cell, seq) at f.
    // The old entry of an already open cell stays behind, stale (R6).
    auto relax = [&](int cell, int par, int32_t gCost, int32_t f) {
        if (mark[cell] != openMark) ++liveOpen;
        mark[cell] = openMark;
        parent[cell] = par;
        g[cell] = gCost;
        seqOf[cell] = sequence;
        entries_.push_back(Entry{cell, sequence, head[f & bucketMask]});
        head[f & bucketMask] = static_cast<int32_t>(entries_.size()) - 1;
        ++sequence;
    };

    // FindPath :298-299: Relax(start, -1, 0, Heuristic(start, goal)).
    int32_t cur;
    {
        const int sx = in.start % w;
        const int sy = in.start / w;
        cur = s * UnitDistance(Abs(sx - gx), Abs(sy - gy), diag, manh);
        relax(in.start, -1, 0, cur);
    }

    bool found = false;
    for (;;) {
        // R8: FindPath :303-305: an empty open set is "no path".
        if (liveOpen < 1) break;
        // R7: the minimum f, newest first. Live entries lie in [cur, cur + D].
        int32_t e = head[cur & bucketMask];
        while (e < 0) {
            ++cur;
            e = head[cur & bucketMask];
        }
        const Entry top = entries_[e];
        head[cur & bucketMask] = top.next;
        const int cell = top.cell;
        if (mark[cell] != openMark || seqOf[cell] != top.seq) continue;   // stale
        // R4: FindPath :314-316: the goal is tested when POPPED.
        if (cell == in.goal) { found = true; break; }
        // FindPath :317-322: delete, close.
        --liveOpen;
        mark[cell] = closedMark;
        ++out.expanded;

        // (c) one division per expansion; the neighbour index is an offset.
        const int x = cell % w;
        const int y = cell / w;
        const int32_t gHere = g[cell];
        for (int dir = 0; dir < dirs; ++dir) {
            const int nx = x + kDx[dir];
            const int ny = y + kDy[dir];
            // R1: Neighbour's bounds test (action_pf_geom.cpp:81).
            if (static_cast<unsigned>(nx) >= static_cast<unsigned>(w) ||
                static_cast<unsigned>(ny) >= static_cast<unsigned>(w)) continue;
            const int nb = ny * w + nx;
            const int32_t c = cost[flags[nb]];
            if (c < 0) {
                if (c == kOffTable) { out.status = Status::Unsupported; return out; }
                continue;   // R2: Neighbour returned -1
            }
            // R3: FindPath :333-343: |dcol| + |drow| == 1 -> * 10, else * 14.
            const int32_t t = gHere + c * (dir < 4 ? 10 : 14);
            // R5: FindPath :347-350: closed and not cheaper -> skip; cheaper ->
            // un-close (closedStamp - 1) and fall through to the open test.
            if (mark[nb] == closedMark) {
                if (g[nb] <= t) continue;
                mark[nb] = 0u;
            }
            // R6: FindPath :352-353, Relax :377: relax when not open, or strictly cheaper.
            if (mark[nb] != openMark || t < g[nb]) {
                const int32_t f = t + s * UnitDistance(Abs(nx - gx), Abs(ny - gy), diag, manh);
                // The bucket bound (header): a violation means the scale broke
                // consistency for these inputs -- let retail run instead.
                if (static_cast<uint32_t>(f - cur) > D) { out.status = Status::Unsupported; return out; }
                relax(nb, cell, t, f);
            }
        }
    }

    out.inserted = sequence;
    if (!found) {
        out.status = Status::NotFound;   // Run :404-407: return 0
        return out;
    }
    out.status = Status::Found;
    out.cost = g[in.goal];

    // R9: Run 0x14001ce00 (:408-439): count the chain from the goal; with a
    // buffer, write it back to front keeping indices < 0x4000, clamp the count.
    int len = 0;
    for (int c = in.goal; c != -1; c = parent[c]) ++len;
    if (path != nullptr) {
        int c = in.goal;
        for (int i = len - 1; i >= 0; --i) {
            if (i < kPathCap) path[i] = c;
            c = parent[c];
        }
        if (len > kPathCap) len = kPathCap;
    }
    out.count = len;
    return out;
}

}  // namespace FastPaths
}  // namespace Ext

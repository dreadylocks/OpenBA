// ============================================================================
// tests/unit/cell_stamp_test.cpp -- AMBMEMO: the map cell write clock
// (src/game/Game/cell_stamp.h, cell_stamp.cpp) and the memo rule
// AmbientEnvironmentComponent::Update builds on it.
// ============================================================================
// A fake cell array (0x30-byte records, the MapCell stride) holds one content byte
// per cell. A set of "regions" (aligned, unaligned, one cell, edge, out of the grid)
// each memoise a scan -- a hash of their cells' bytes -- exactly as the component
// does: hit iff the rect is cacheable and RectStamp(rect) <= the clock recorded
// right after the last scan. Random single-cell writes (each followed by NoteWrite),
// Invalidate, a second array at another base and a different dim are interleaved;
// every memo answer must equal a fresh scan. A broken variant that skips NoteWrite
// on some writes must diverge (the test can tell).
// ============================================================================
#include "unit.h"
#include "Game/cell_stamp.h"

#include <cstdint>
#include <vector>

namespace {

struct FakeCell { alignas(8) unsigned char b[CellStamp::kCellStride]; };
static_assert(sizeof(FakeCell) == CellStamp::kCellStride, "fake cell stride");

uint64_t Mix(uint64_t x) {   // splitmix64
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

const MapCell* AsCells(const FakeCell* c) { return reinterpret_cast<const MapCell*>(c); }   // CAST-OK: elem_stride

struct Grid { std::vector<FakeCell> cells; int dim; };

uint64_t Scan(const Grid& g, int x, int y, int w, int h) {
    uint64_t s = 0x1234;
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const long long i = (long long)(y + yy) * g.dim + (x + xx);
            if (i < 0 || i >= (long long)g.cells.size()) { s = Mix(s ^ 0xdead); continue; }
            s = Mix(s ^ g.cells[(size_t)i].b[0x29] ^ ((uint64_t)g.cells[(size_t)i].b[0] << 8));
        }
    return s;
}

struct Region { int x, y, w, h; uint64_t clock = 0, value = 0; };

struct Stats { long long queries = 0, hits = 0, uncached = 0, mismatches = 0; };

Stats Run(uint64_t seed, bool dropSomeNotes) {
    Stats st;
    CellStamp::Invalidate();
    Grid ga{ std::vector<FakeCell>(64 * 64), 64 };
    Grid gb{ std::vector<FakeCell>(48 * 48), 48 };
    for (auto* g : { &ga, &gb })
        for (size_t i = 0; i < g->cells.size(); ++i) {
            for (int k = 0; k < CellStamp::kCellStride; ++k) g->cells[i].b[k] = 0;
            g->cells[i].b[0x29] = (unsigned char)(Mix(seed + i) % 11);
        }
    std::vector<Region> regions = {
        {0, 0, 32, 32}, {32, 32, 32, 32}, {5, 7, 20, 3}, {63, 63, 1, 1}, {15, 15, 2, 2},
        {40, 0, 24, 47}, {60, 10, 8, 8} /* out of the 64 grid: uncacheable */, {0, 0, 0, 4},
        {16, 16, 16, 16}, {47, 47, 1, 1},
    };
    Grid* cur = &ga;
    for (int step = 0; step < 4000; ++step) {
        const uint64_t r = Mix(seed * 7919 + (uint64_t)step);
        const int op = (int)(r % 100);
        if (op < 12) {
            // one cell write: the terrain byte or the entity slot, then the hook
            const int idx = (int)((r >> 8) % cur->cells.size());
            FakeCell& c = cur->cells[(size_t)idx];
            if ((r >> 40) & 1) c.b[0x29] = (unsigned char)((r >> 32) % 11);
            else               c.b[0] = (unsigned char)((r >> 32) & 0xff);
            const bool skip = dropSomeNotes && ((r >> 48) % 4 == 0);
            if (!skip) CellStamp::NoteWrite(AsCells(&c));
        } else if (op < 13) {
            CellStamp::Invalidate();
        } else if (op < 14) {
            cur = (cur == &ga) ? &gb : &ga;   // another array at another base and dim
        } else {
            Region& rg = regions[(size_t)((r >> 8) % regions.size())];
            ++st.queries;
            const uint64_t stamp = CellStamp::RectStamp(AsCells(cur->cells.data()), cur->dim, rg.x, rg.y, rg.w, rg.h);
            uint64_t v;
            if (stamp != CellStamp::kUncacheable && rg.clock != 0 && stamp <= rg.clock) {
                ++st.hits;
                v = rg.value;
            } else {
                v = Scan(*cur, rg.x, rg.y, rg.w, rg.h);
                if (stamp != CellStamp::kUncacheable) { rg.clock = CellStamp::Now(); rg.value = v; }
                else { ++st.uncached; rg.clock = 0; }
            }
            if (v != Scan(*cur, rg.x, rg.y, rg.w, rg.h)) ++st.mismatches;
        }
    }
    CellStamp::Invalidate();
    return st;
}

}  // namespace

UNIT_TEST(cell_stamp_rect_bounds) {
    CellStamp::Invalidate();
    std::vector<FakeCell> cells(20 * 20);
    const MapCell* base = AsCells(cells.data());
    CHECK(CellStamp::RectStamp(base, 20, 0, 0, 20, 20) != CellStamp::kUncacheable);
    CHECK(CellStamp::RectStamp(base, 20, 1, 0, 20, 20) == CellStamp::kUncacheable);   // past the right edge
    CHECK(CellStamp::RectStamp(base, 20, 0, 19, 4, 2) == CellStamp::kUncacheable);    // past the bottom
    CHECK(CellStamp::RectStamp(base, 20, -1, 0, 4, 4) == CellStamp::kUncacheable);
    CHECK(CellStamp::RectStamp(base, 20, 0, 0, 0, 4) == CellStamp::kUncacheable);     // empty
    CHECK(CellStamp::RectStamp(nullptr, 20, 0, 0, 4, 4) == CellStamp::kUncacheable);
    // a write is seen by every rect covering its block, and only after it
    const uint64_t before = CellStamp::Now();
    CHECK(CellStamp::RectStamp(base, 20, 0, 0, 4, 4) <= before);
    CellStamp::NoteWrite(AsCells(&cells[(size_t)(2 * 20 + 3)]));
    CHECK(CellStamp::RectStamp(base, 20, 0, 0, 4, 4) > before);
    CHECK(CellStamp::RectStamp(base, 20, 16, 16, 4, 4) <= before);   // another block
    // a write outside the tracked array is ignored, Invalidate makes everything newer
    std::vector<FakeCell> other(4);
    CellStamp::NoteWrite(AsCells(&other[0]));
    const uint64_t t = CellStamp::Now();
    CellStamp::Invalidate();
    CHECK(CellStamp::RectStamp(base, 20, 16, 16, 4, 4) > t);
    CellStamp::Invalidate();
}

UNIT_TEST(cell_stamp_memo_exact) {
    long long q = 0, hits = 0, unc = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        const Stats st = Run(seed, false);
        CHECK_EQ(st.mismatches, 0LL);
        q += st.queries; hits += st.hits; unc += st.uncached;
    }
    std::printf("  cell_stamp: %lld queries, %lld memo hits, %lld uncached\n", q, hits, unc);
    CHECK(hits > (q - unc) / 4);
    CHECK(unc > 0);
}

UNIT_TEST(cell_stamp_missing_hook_diverges) {
    long long mism = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed) mism += Run(seed, true).mismatches;
    CHECK(mism > 0);
}

// src/game/Game/cell_stamp.cpp -- see cell_stamp.h (AMBMEMO).
#include "cell_stamp.h"
#include <cstdlib>

namespace CellStamp {

const char* g_base    = nullptr;
int         g_dim     = 0;
int         g_blocksW = 0;
uint64_t*   g_blocks  = nullptr;
uint64_t    g_clock   = 0;

namespace {
uint64_t* s_buf    = nullptr;   // owned allocation, reused across rebuilds
int       s_bufCap = 0;

void Rebuild(const char* base, int dim) {
    const int bw = (dim + (1 << kBlockShift) - 1) >> kBlockShift;
    const int n  = bw * bw;
    if (n > s_bufCap) {
        std::free(s_buf);
        s_buf = static_cast<uint64_t*>(std::malloc(sizeof(uint64_t) * (size_t)n));
        s_bufCap = s_buf ? n : 0;
    }
    if (!s_buf) { Invalidate(); return; }
    // One fresh value for every block: newer than anything a reader recorded before.
    const uint64_t t = ++g_clock;
    for (int i = 0; i < n; ++i) s_buf[i] = t;
    g_blocks = s_buf; g_base = base; g_dim = dim; g_blocksW = bw;
}
}  // namespace

uint64_t RectStamp(const MapCell* cells, int dim, int x, int y, int w, int h) {
    if (!cells || dim <= 0 || w <= 0 || h <= 0 || x < 0 || y < 0 || x > dim - w || y > dim - h)
        return kUncacheable;
    const char* base = reinterpret_cast<const char*>(cells);   // CAST-OK: elem_stride
    if (base != g_base || dim != g_dim || !g_blocks) Rebuild(base, dim);
    if (!g_blocks) return kUncacheable;
    const int bx0 = x >> kBlockShift, bx1 = (x + w - 1) >> kBlockShift;
    const int by0 = y >> kBlockShift, by1 = (y + h - 1) >> kBlockShift;
    uint64_t m = 0;
    for (int by = by0; by <= by1; ++by) {
        const uint64_t* row = g_blocks + (size_t)by * g_blocksW;
        for (int bx = bx0; bx <= bx1; ++bx)
            if (row[bx] > m) m = row[bx];
    }
    return m;
}

void Invalidate() {
    g_blocks = nullptr; g_base = nullptr; g_dim = 0; g_blocksW = 0;
}

}  // namespace CellStamp

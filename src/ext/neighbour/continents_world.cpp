// ============================================================================
// src/ext/neighbour/continents_world.cpp -- NOT RETAIL. ext.neighbour: a Continents map in the game
// ============================================================================
// continents_world.h lists the seams. The map's shape is continents.cpp's; here it meets the game's
// own records: the heightfield's feature paths and lakes, the heightmap, the water mesh, the map's
// cells. docs/fork/NEIGHBOUR_TOWNS.md §12.
// ============================================================================

#include "ext/neighbour/continents_world.h"

#include "Game/TerrainHeightfield.h"
#include "Game/WorldGeometry.h"
#include "Game/WorldGeometryElements.h"
#include "Game/Map.h"
#include "system/Value.h"            // System::Random
#include "system/parallel_for.h"     // System::ParallelFor
#include "ext/registry.h"            // Ext::Ext().neighbour

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Neighbour {
namespace Continents {

namespace {

// The sea lane's brushes (TerrainPathPoint): the retail coastline ring's, with a narrower flat band, so
// carving it (e910 / ec50) deepens the sea and leaves the lands' coasts to ShapeCoasts.
constexpr float kLaneStrength     = 2.0f;
constexpr float kLaneFlat         = 4.0f;
constexpr float kLaneFlatFalloff  = 1.25f;
constexpr float kLaneHalfWidth    = 4.0f;
constexpr float kLaneWidthFalloff = 4.0f;

constexpr float   kRiverMouth = 1.0f;     // a river is kept to where it is this far inland
constexpr float   kSeaReach   = 1.0f;     // the sea's water covers underwater cells to this far inland
constexpr int16_t kWaterLine  = -192;     // -0.75 in the int16 heightmap: b260 phase 2's underwater test
constexpr int     kSeaTile    = 128;      // the sea is drawn in pieces this many cells a side (16-bit indices)
constexpr int     kSeaBlock   = 8;        // a block of this many cells a side that is all sea is one quad

bool     s_on = false;
uint32_t s_seed = 0;
Layout   s_layout;
bool     s_haveLayout = false;

template <class T>
void Append(RawArray<T>& to, const std::vector<T>& from) {
    if (from.empty()) return;
    const int at = to.size;
    RawArraySetSize(to, at + static_cast<int>(from.size()));
    if (to.data != nullptr) std::memcpy(to.data + at, from.data(), from.size() * sizeof(T));
}

}  // namespace

void SetForGame(bool continents, uint32_t seed) {
    s_on = continents;
    s_seed = seed;
    s_haveLayout = false;
}

bool ForThisGame() { return s_on; }

const Layout* ThisLayout() { return s_on && s_haveLayout ? &s_layout : nullptr; }

bool PlaceSeaLane(TerrainHeightfield* hf) {
    if (!s_on || hf == nullptr) return false;
    s_layout = MakeLayout(s_seed, hf->gridSize / 2);
    s_haveLayout = true;
    const std::vector<float> xy = SeaLane(s_layout);
    std::vector<TerrainPathPoint> lane(xy.size() / 2);
    for (size_t i = 0; i < lane.size(); ++i) {
        TerrainPathPoint& p = lane[i];
        p.x = xy[2 * i];
        p.y = xy[2 * i + 1];
        p.strength = kLaneStrength;
        p.innerRadius = kLaneFlat;
        p.innerFalloff = kLaneFlatFalloff;
        p.outerRadius = kLaneHalfWidth;
        p.outerFalloff = kLaneWidthFalloff;
    }
    const int32_t count = static_cast<int32_t>(lane.size());
    RawArray<TerrainPathPoint> path{lane.data(), count, count};
    hf->paths.SetSizeFill(hf->paths._size + 1, &path);   // deep-copied (kSpec_WgPathVec), as the coastline stage does
    fprintf(stderr, "[Neighbour] continents: map %d, the sea between the lands runs %s, %.0f tiles wide; "
            "town 1's land %s, town 2's %s\n", s_layout.mapSize, s_layout.eastWest ? "east-west" : "north-south",
            s_layout.straitWidth, s_layout.eastWest ? "north" : "west", s_layout.eastWest ? "south" : "east");
    return true;
}

void KeepRiversOnLand(TerrainHeightfield* hf) {
    const Layout* L = ThisLayout();
    if (L == nullptr || hf == nullptr) return;
    // Rivers (the paths after the sea lane): only their longest stretch on land, so none is drawn out
    // over the sea; one with no stretch on land is emptied (a path of no points draws nothing).
    int cut = 0;
    for (int i = 1; i < hf->paths._size; ++i) {
        RawArray<TerrainPathPoint>& p = hf->paths._data[i];
        int bestAt = 0, bestLen = 0, runAt = 0, runLen = 0;
        for (int j = 0; j < p.size; ++j) {
            if (LandDistance(*L, p.data[j].x, p.data[j].y) > kRiverMouth) {
                if (runLen == 0) runAt = j;
                ++runLen;
                if (runLen > bestLen) {
                    bestLen = runLen;
                    bestAt = runAt;
                }
            } else {
                runLen = 0;
            }
        }
        if (bestLen < 3) bestLen = 0;
        if (bestLen != p.size) ++cut;
        if (bestLen > 0 && bestAt > 0)
            std::memmove(p.data, p.data + bestAt, static_cast<size_t>(bestLen) * sizeof(TerrainPathPoint));
        p.size = bestLen;
    }
    // Lakes whose middle is at sea: the sea is drawn over them anyway.
    int kept = 0;
    const int lakes = hf->waterFeatures._size;
    for (int i = 0; i < lakes; ++i) {
        const TerrainStamp lake = hf->waterFeatures._data[i];
        if (LandDistance(*L, lake.x, lake.y) > 0.0f) hf->waterFeatures._data[kept++] = lake;
    }
    hf->waterFeatures.SetSize(kept);
    fprintf(stderr, "[Neighbour] continents: %d of %d river(s) end at the coast; %d of %d lake(s) on land\n", cut,
            hf->paths._size - 1, kept, lakes);
}

void ShapeCoasts(TerrainHeightfield* hf) {
    const Layout* L = ThisLayout();
    if (L == nullptr || hf == nullptr || hf->heightmap.data == nullptr) return;
    const int dim = hf->gridSize;
    const int edge = dim + 1;
    const int half = dim / 2;
    float* hm = hf->heightmap.data;
    // Each vertex is a pure function of its own height and place: the rows can go to any thread.
    System::ParallelFor(0, edge, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            for (int col = 0; col < edge; ++col) {
                const float d = LandDistance(*L, static_cast<float>(col - half), static_cast<float>(row - half));
                float& h = hm[row * edge + col];
                h = CoastHeight(h, d);
            }
        }
    });
}

void AddSea(WorldGeometry* wg, const TerrainHeightfield* hf) {
    const Layout* L = ThisLayout();
    if (L == nullptr || wg == nullptr || hf == nullptr || wg->_heights.data == nullptr) return;

    // The sea lane is the boats' route, not a river: its strip (body 0) is never drawn.
    if (wg->_contourRangesA.size > 0 && wg->_contourRangesA.data != nullptr) {
        wg->_contourRangesA.data[0].values[2] = 0;
        wg->_contourRangesA.data[0].values[3] = 0;
    }

    const int dim = wg->_gridSize;
    const int n = wg->_fieldD0;   // dim + 1 heights a row
    const int half = dim >> 1;
    const int16_t* h16 = wg->_heights.data;

    // The lakes b260 draws (phase 2) cover every underwater cell of their box: the sea leaves those.
    struct Box {
        int x0, x1, y0, y1;
    };
    std::vector<Box> lakes;
    for (int i = 0; i < hf->waterFeatures._size; ++i) {
        const TerrainStamp& f = hf->waterFeatures._data[i];
        const float r = f.radius + 2.0f;   // b260 phase 2's box: centre +/- (radius + 2)
        lakes.push_back({static_cast<int>(f.x - r) + half, static_cast<int>(f.x + r) + half,
                         static_cast<int>(f.y - r) + half, static_cast<int>(f.y + r) + half});
    }

    // Which cells the sea covers: underwater (as b260 phase 2 tests a cell) and at sea.
    std::vector<uint8_t> sea(static_cast<size_t>(dim) * dim, 0);
    System::ParallelFor(0, dim, 8, [&](int rowBegin, int rowEnd) {
        for (int row = rowBegin; row < rowEnd; ++row) {
            for (int col = 0; col < dim; ++col) {
                const int16_t* c = h16 + static_cast<size_t>(row) * n + col;
                if (c[0] > kWaterLine && c[1] > kWaterLine && c[n] > kWaterLine && c[n + 1] > kWaterLine) continue;
                const float x = static_cast<float>(col - half) + 0.5f;
                const float y = static_cast<float>(row - half) + 0.5f;
                if (LandDistance(*L, x, y) >= kSeaReach) continue;
                bool inLake = false;
                for (const Box& b : lakes)
                    if (col >= b.x0 && col < b.x1 && row >= b.y0 && row < b.y1) inLake = true;
                if (!inLake) sea[static_cast<size_t>(row) * dim + col] = 1;
            }
        }
    });

    // One draw range a piece, drawn with the lakes' water (mode 0x8000), at the next lake's level as b260
    // numbers them (z = feature index - 191, phase 2), with full water everywhere (b260's weight 255).
    const int16_t level = static_cast<int16_t>(hf->waterFeatures._size - 191);
    int pieces = 0, vertsTotal = 0;
    std::vector<int32_t> slot(static_cast<size_t>(kSeaTile + 1) * (kSeaTile + 1));
    std::vector<TerrainVertex> verts;
    std::vector<uint16_t> indices;
    for (int ty = 0; ty < dim; ty += kSeaTile) {
        for (int tx = 0; tx < dim; tx += kSeaTile) {
            const int tw = (dim - tx < kSeaTile) ? dim - tx : kSeaTile;
            const int th = (dim - ty < kSeaTile) ? dim - ty : kSeaTile;
            std::fill(slot.begin(), slot.end(), -1);
            verts.clear();
            indices.clear();
            auto corner = [&](int col, int row) -> uint16_t {
                int32_t& s = slot[static_cast<size_t>(row - ty) * (kSeaTile + 1) + (col - tx)];
                if (s < 0) {
                    TerrainVertex v;
                    const int16_t px = static_cast<int16_t>(static_cast<int>(static_cast<float>(col - half) * 48.0f));
                    const int16_t py = static_cast<int16_t>(static_cast<int>(static_cast<float>(row - half) * 48.0f));
                    v.position[0] = px;
                    v.position[1] = py;
                    v.position[2] = level;
                    v.position[3] = 0;
                    v.uv[0] = px;   // planar, as b260 phase 2
                    v.uv[1] = py;
                    v.normal[0] = 0x7f;
                    v.normal[1] = 0x7f;
                    v.normal[2] = 0xff;
                    v.normal[3] = 0x00;
                    s = static_cast<int32_t>(verts.size());
                    verts.push_back(v);
                }
                return static_cast<uint16_t>(s);
            };
            auto quad = [&](int col0, int row0, int col1, int row1) {
                const uint16_t i0 = corner(col0, row0);
                const uint16_t i1 = corner(col1, row0);
                const uint16_t i2 = corner(col1, row1);
                const uint16_t i3 = corner(col0, row1);
                const uint16_t q[6] = {i0, i1, i2, i0, i2, i3};   // b260 phase 2's winding
                indices.insert(indices.end(), q, q + 6);
            };
            for (int by = ty; by < ty + th; by += kSeaBlock) {
                for (int bx = tx; bx < tx + tw; bx += kSeaBlock) {
                    const int bw = (tx + tw - bx < kSeaBlock) ? tx + tw - bx : kSeaBlock;
                    const int bh = (ty + th - by < kSeaBlock) ? ty + th - by : kSeaBlock;
                    int count = 0;
                    for (int row = by; row < by + bh; ++row)
                        for (int col = bx; col < bx + bw; ++col) count += sea[static_cast<size_t>(row) * dim + col];
                    if (count == 0) continue;
                    if (count == bw * bh) {
                        quad(bx, by, bx + bw, by + bh);
                        continue;
                    }
                    for (int row = by; row < by + bh; ++row)
                        for (int col = bx; col < bx + bw; ++col)
                            if (sea[static_cast<size_t>(row) * dim + col] != 0) quad(col, row, col + 1, row + 1);
                }
            }
            if (indices.empty()) continue;
            GeometryDrawRange range;
            range.values[0] = wg->_contourIndices.size;    // idxStart
            range.values[1] = wg->_contourVertices.size;   // vtxStart
            range.values[2] = static_cast<int32_t>(verts.size());
            range.values[3] = static_cast<int32_t>(indices.size());
            Append<TerrainVertex>(wg->_contourVertices, verts);
            Append<uint16_t>(wg->_contourIndices, indices);
            Append<GeometryDrawRange>(wg->_contourRangesB, std::vector<GeometryDrawRange>{range});
            ++pieces;
            vertsTotal += static_cast<int>(verts.size());
        }
    }
    fprintf(stderr, "[Neighbour] continents: the sea drawn in %d piece(s), %d vertices\n", pieces, vertsTotal);
}

bool NomadLanding(const Map* map, int hallCell, System::Random* rng, int* x, int* y) {
    if (!Ext::Ext().neighbour || map == nullptr || rng == nullptr) return false;
    const int w = map->size;
    const MapCell* cells = map->cells.GetData();
    if (cells == nullptr || w <= 0 || hallCell < 0 || hallCell >= w * w) return false;
    const TileType typeAt = [cells, w](int tx, int ty) { return cells[ty * w + tx].terrainType; };
    if (!RingedBySea(w, typeAt)) return false;
    // A random direction from the Town Hall, walked out to the open sea: the group comes ashore on the
    // last walkable tile before it. Each town's nomads are its own, so the platform's maths.
    const float angle = rng->RandFloat() * 6.2831853f;
    return WalkToShore(w, static_cast<float>(hallCell % w) + 0.5f, static_cast<float>(hallCell / w) + 0.5f,
                       std::cos(angle), std::sin(angle), typeAt, x, y);
}

}  // namespace Continents
}  // namespace Neighbour

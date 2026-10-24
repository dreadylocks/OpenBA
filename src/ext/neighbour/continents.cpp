// ============================================================================
// src/ext/neighbour/continents.cpp -- NOT RETAIL. ext.neighbour: the shape of a Continents map (continents.h)
// ============================================================================
// Only + - * / and sqrt on floats, no libm: the bits must be the same on every platform (continents.h).

#include "ext/neighbour/continents.h"

#include <cmath>

namespace Neighbour {
namespace Continents {

namespace {

// The shape, for a 256-tile map; it scales with the map (mapSize / 256).
constexpr float kStraitWidth = 45.0f;   // the sea between the lands, mean width (the user's "wide", 2026-10-24)
constexpr float kWobbleOuter = 30.0f;   // the coast wanders this far either way on a land's outer side...
constexpr float kWobbleInner = 14.0f;   // ...and this far on the side facing the other land
constexpr float kNarrows     = 20.0f;   // but the sea between the lands is never narrower than this
constexpr float kEdgeSea     = 4.0f;    // and the playable map's edge is always this far out at sea
constexpr float kCorner      = 0.75f;   // a land's corners are rounded by this much of its half-width
constexpr float kFinestWave  = 12.0f;   // the coast noise's finest wavelength; three more, each twice as long
constexpr float kBlend       = 10.0f;   // the two limits above round off over this many tiles

// The coast's height profile (CoastHeight).
constexpr float kBeach    = 0.3f;    // a flat beach, buildable (maptilegrid.cpp: -0.25 .. 0.75 and flat)
constexpr float kPlateau  = 6.0f;    // tiles of beach before the land starts to rise again
constexpr float kRise     = 12.0f;   // ...and over these it climbs back to the land the generator made
constexpr float kSeaFloor = -2.5f;   // the open sea's floor (deep water is below -1.4)
constexpr float kShelf    = 5.0f;    // tiles from the coast to the open sea's floor

constexpr const char* kMapSeparator = ":";

uint32_t Mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

uint32_t Hash(uint32_t seed, int32_t ix, int32_t iy) {
    return Mix(seed ^ Mix(static_cast<uint32_t>(ix) * 0x9e3779b1u ^ Mix(static_cast<uint32_t>(iy) + 0x632be5abu)));
}

// A lattice value in [-1, 1): 24 bits of the hash, which a float holds exactly.
float Lattice(uint32_t seed, int32_t ix, int32_t iy) {
    return static_cast<float>(Hash(seed, ix, iy) >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

int32_t FloorToInt(float v) {
    int32_t i = static_cast<int32_t>(v);
    if (static_cast<float>(i) > v) --i;
    return i;
}

float Smooth(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// min(a, b) with the corner rounded over `k` (a polynomial smooth minimum): never above min(a, b).
float SmoothMin(float a, float b, float k) {
    const float gap = a > b ? a - b : b - a;
    const float h = gap < k ? (k - gap) / k : 0.0f;
    return (a < b ? a : b) - h * h * k * 0.25f;
}

// Value noise in [-1, 1]: the four lattice values round (x, y), blended with a smoothstep.
float ValueNoise(uint32_t seed, float x, float y) {
    const int32_t ix = FloorToInt(x);
    const int32_t iy = FloorToInt(y);
    const float sx = Smooth(x - static_cast<float>(ix));
    const float sy = Smooth(y - static_cast<float>(iy));
    const float a = Lattice(seed, ix, iy);
    const float b = Lattice(seed, ix + 1, iy);
    const float c = Lattice(seed, ix, iy + 1);
    const float d = Lattice(seed, ix + 1, iy + 1);
    const float top = a + (b - a) * sx;
    const float bottom = c + (d - c) * sx;
    return top + (bottom - top) * sy;
}

// The coast's wandering, in [-1, 1]: four octaves, the longest wave first.
float CoastNoise(const Layout& L, float x, float y) {
    static constexpr float kAmp[4] = {0.55f, 0.25f, 0.12f, 0.08f};
    static constexpr float kFreq[4] = {0.125f, 0.25f, 0.5f, 1.0f};
    float n = 0.0f;
    for (int o = 0; o < 4; ++o) {
        const float f = L.noiseScale * kFreq[o];
        n += kAmp[o] * ValueNoise(L.seed + 0x2545f491u * static_cast<uint32_t>(o + 1), x * f, y * f);
    }
    return n;
}

}  // namespace

std::string JoinMap(const std::string& map, const std::string& terrainPath) {
    if (map.empty()) return terrainPath;
    return map + kMapSeparator + terrainPath;
}

void SplitMap(const std::string& terrain, std::string& map, std::string& terrainPath) {
    const std::string prefix = std::string(kMapContinents) + kMapSeparator;
    if (terrain.compare(0, prefix.size(), prefix) == 0) {
        map = kMapContinents;
        terrainPath = terrain.substr(prefix.size());
        return;
    }
    map.clear();
    terrainPath = terrain;
}

Layout MakeLayout(uint32_t seed, int mapSize) {
    Layout L;
    L.seed = seed;
    L.mapSize = mapSize;
    const float k = static_cast<float>(mapSize) / 256.0f;
    L.eastWest = (Hash(seed, 7, 11) & 1u) != 0;
    L.straitWidth = kStraitWidth * k;
    L.wobble = kWobbleOuter * k;
    L.wobbleInner = kWobbleInner * k;
    const float half = static_cast<float>(mapSize) * 0.5f;
    const float edge = L.wobble * 0.5f + kEdgeSea * k;   // the coast's mean distance from the playable edge
    L.halfAcross = (half - edge - L.straitWidth * 0.5f) * 0.5f;
    L.halfAlong = half - edge;
    L.corner = L.halfAcross * kCorner;
    const float middle = L.straitWidth * 0.5f + L.halfAcross;
    L.across[0] = -middle;
    L.across[1] = middle;
    L.narrows = kNarrows * k;
    L.edgeSea = kEdgeSea * k;
    L.blend = kBlend * k;
    L.noiseScale = 1.0f / (kFinestWave * k);
    return L;
}

float LandDistance(const Layout& L, float x, float y, int* land) {
    const float across = L.eastWest ? y : x;
    const float along = L.eastWest ? x : y;
    const float noise = CoastNoise(L, x, y);
    float best = 0.0f;
    int which = 0;
    for (int i = 0; i < 2; ++i) {
        // The distance into a rectangle with rounded corners (above 0 inside).
        const float u = across - L.across[i];
        const float qu = std::fabs(u) - (L.halfAcross - L.corner);
        const float qv = std::fabs(along) - (L.halfAlong - L.corner);
        const float ou = qu > 0.0f ? qu : 0.0f;
        const float ov = qv > 0.0f ? qv : 0.0f;
        const float outside = std::sqrt(ou * ou + ov * ov);
        const float inside = qu > qv ? qu : qv;
        const float box = L.corner - outside - (inside < 0.0f ? inside : 0.0f);
        // The coast wanders less on the side that faces the other land, so the sea between stays open.
        const float side = (i == 0 ? -u : u) / L.halfAcross;
        const float reach = L.wobbleInner + (L.wobble - L.wobbleInner) * Smooth(0.5f + 0.5f * side);
        float d = box + reach * noise;
        // The limits: the sea between the lands stays open, and the playable map's edge stays at sea.
        d = SmoothMin(d, (i == 0 ? -across : across) - L.narrows * 0.5f, L.blend);
        if (i == 0 || d > best) {
            best = d;
            which = i;
        }
    }
    if (land != nullptr) *land = which;
    const float half = static_cast<float>(L.mapSize) * 0.5f;
    const float toEdgeX = half - std::fabs(x);
    const float toEdgeY = half - std::fabs(y);
    return SmoothMin(best, (toEdgeX < toEdgeY ? toEdgeX : toEdgeY) - L.edgeSea, L.blend);
}

float CoastHeight(float h, float d) {
    if (d >= 0.0f) {
        if (h <= kBeach) return h;   // a valley, river or lake lower than the beach stays
        const float t = Smooth((d - kPlateau) / kRise);
        return kBeach + (h - kBeach) * t;
    }
    const float floor = kBeach + (kSeaFloor - kBeach) * Smooth(-d / kShelf);
    return h < floor ? h : floor;
}

std::vector<float> SeaLane(const Layout& L) {
    const int n = 2 * L.mapSize + 1;
    std::vector<float> xy;
    xy.reserve(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i - L.mapSize);
        xy.push_back(L.eastWest ? t : 0.0f);
        xy.push_back(L.eastWest ? 0.0f : t);
    }
    return xy;
}

void StartCentre(const Layout& L, int seat, int* tx, int* ty) {
    const int half = L.mapSize / 2;
    const int across = static_cast<int>(L.across[seat == 1 ? 1 : 0]) + half;
    *tx = L.eastWest ? half : across;
    *ty = L.eastWest ? across : half;
}

bool OnOwnLand(const Layout& L, int seat, int tx, int ty) {
    const float centre = static_cast<float>(L.mapSize / 2) - 0.5f;
    int land = -1;
    const float d = LandDistance(L, static_cast<float>(tx) - centre, static_cast<float>(ty) - centre, &land);
    return d > 0.0f && land == seat;
}

namespace {
bool IsWater(uint8_t type) { return type == 7 || type == 8; }                     // shore, deep water
bool IsWalkable(uint8_t type) { return ((1u << (type & 0x1f)) & 0xe07u) != 0; }   // FindSpawnCell's mask
}  // namespace

bool RingedBySea(int w, const TileType& typeAt) {
    if (w <= 0) return false;
    const int last = w - 1, mid = w / 2;
    const int at[8][2] = {{0, 0}, {last, 0}, {0, last}, {last, last}, {mid, 0}, {mid, last}, {0, mid}, {last, mid}};
    for (const auto& p : at)
        if (!IsWater(typeAt(p[0], p[1]))) return false;
    return true;
}

bool WalkToShore(int w, float fromX, float fromY, float dx, float dy, const TileType& typeAt, int* x, int* y) {
    int landX = -1, landY = -1, water = 0;
    for (int t = 1; t < 2 * w; ++t) {
        const int cx = static_cast<int>(fromX + dx * static_cast<float>(t));
        const int cy = static_cast<int>(fromY + dy * static_cast<float>(t));
        if (cx < 0 || cy < 0 || cx >= w || cy >= w) break;
        const uint8_t type = typeAt(cx, cy);
        if (IsWater(type)) {
            if (++water >= kOpenSea) break;
            continue;
        }
        water = 0;
        if (IsWalkable(type)) {
            landX = cx;
            landY = cy;
        }
    }
    if (landX < 0) return false;
    *x = landX;
    *y = landY;
    return true;
}

}  // namespace Continents
}  // namespace Neighbour

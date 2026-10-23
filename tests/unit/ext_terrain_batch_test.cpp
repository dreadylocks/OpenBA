// ============================================================================
// tests/unit/ext_terrain_batch_test.cpp -- ext.terrain_batch's pure parts
// ============================================================================
// src/ext/terrain_batch/terrain_batch_rules.h: the run key (layout, encode / decode, sort
// order), the run split of a grid row, the run index list (the same vertex sequence as the
// per-chunk draws it replaces) and the shadow caster box (every shadow ray segment of the
// chunk, and every point within the margin of one, lies inside it). Plus the sun box width
// the margin is taken from, read through the engine's own Frustum transform.
// ============================================================================
#include "unit.h"
#include "ext/terrain_batch/terrain_batch_rules.h"
#include "math/Math.h"
#include "volume/Volume.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using namespace Ext::TerrainBatch;

// Retail's chunk index list (WorldGeometry::BuildTerrainMesh, 0x1400ac1e0 step 6): two
// triangles per quad, the diagonal alternating by parity.
std::vector<uint16_t> RetailChunkIndices(int chunkSize, int vertsPerSide) {
    std::vector<uint16_t> out;
    for (int row = 0; row < chunkSize; ++row) {
        for (int col = 0; col < chunkSize; ++col) {
            const int tl = row * vertsPerSide + col;
            const int tr = tl + 1;
            const int bl = (row + 1) * vertsPerSide + col;
            const int br = bl + 1;
            out.push_back(static_cast<uint16_t>(tl));
            if (((col + row) & 1) == 0) {
                out.push_back(static_cast<uint16_t>(bl));
                out.push_back(static_cast<uint16_t>(tr));
                out.push_back(static_cast<uint16_t>(tr));
            } else {
                out.push_back(static_cast<uint16_t>(br));
                out.push_back(static_cast<uint16_t>(tr));
                out.push_back(static_cast<uint16_t>(tl));
            }
            out.push_back(static_cast<uint16_t>(bl));
            out.push_back(static_cast<uint16_t>(br));
        }
    }
    return out;
}

bool Inside(const Aabb& b, const float p[3], float eps) {
    for (int a = 0; a < 3; ++a) {
        if (p[a] < b.min[a] - eps || p[a] > b.max[a] + eps) return false;
    }
    return true;
}

}  // namespace

UNIT_TEST(ext_terrain_batch_layout_fits_the_grids) {
    // Retail's chunks: 32 cells, 33 x 33 = 1089 vertices; maps up to 32 chunks a side.
    RunLayout l = LayoutFor(24, 1089);
    CHECK_EQ(l.coordBits, 5);
    CHECK_EQ(l.lenBits, 4);
    CHECK_EQ(l.maxRun, 16);
    l = LayoutFor(32, 1089);
    CHECK_EQ(l.coordBits, 5);
    CHECK_EQ(l.maxRun, 16);
    l = LayoutFor(16, 1089);   // 4 coordinate bits, 6 length bits, capped by the row
    CHECK_EQ(l.coordBits, 4);
    CHECK_EQ(l.lenBits, 6);
    CHECK_EQ(l.maxRun, 16);
    l = LayoutFor(64, 1089);   // retail's key allows 64 a side: runs of 4
    CHECK_EQ(l.coordBits, 6);
    CHECK_EQ(l.maxRun, 4);
    l = LayoutFor(8, 1089);    // 16-bit indices cap a run at 65536 / 1089 = 60, the row at 8
    CHECK_EQ(l.maxRun, 8);
    l = LayoutFor(4, 1089);
    CHECK(l.maxRun == 4);
    CHECK_EQ(LayoutFor(65, 1089).maxRun, 0);   // does not fit: the row stays per chunk
    CHECK_EQ(LayoutFor(0, 1089).maxRun, 0);
    CHECK_EQ(LayoutFor(32, 0).maxRun, 0);
    CHECK_EQ(LayoutFor(32, 40000).maxRun, 1);   // one chunk per draw, still a valid key
}

UNIT_TEST(ext_terrain_batch_run_key_round_trips_and_sorts_row_major) {
    for (int n : { 1, 2, 7, 16, 24, 32, 33, 64 }) {
        const RunLayout l = LayoutFor(n, 1089);
        CHECK(l.maxRun >= 1);
        uint32_t previous = 0;
        bool first = true;
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                for (int len = 1; len <= l.maxRun && col + len <= n; ++len) {
                    const uint16_t key = EncodeRun(l, row, col, len);
                    CHECK(IsRunKey(key));
                    const Run r = DecodeRun(l, key);
                    CHECK_EQ(r.row, row);
                    CHECK_EQ(r.col, col);
                    CHECK_EQ(r.len, len);
                }
                // Ascending key = row-major by the run's first chunk (the draw-item sort
                // orders terrain items by this key, retail's chunk order).
                const uint16_t key = EncodeRun(l, row, col, 1);
                if (!first) CHECK(key > previous);
                previous = key;
                first = false;
            }
        }
    }
    // Retail never emits a run key: chunk keys are (row << 6) | col, and the other draw
    // modes are 0x2000 / 0x4000 / 0x6000 / 0x8000 / 0xA000.
    for (uint32_t i = 0; i < 64; ++i) {
        for (uint32_t j = 0; j < 64; ++j) {
            CHECK(!IsRunKey((i << 6) | j));
        }
    }
    for (uint32_t mode : { 0x2000u, 0x4000u, 0x6000u, 0x8000u, 0xA000u }) {
        CHECK(!IsRunKey(mode | 0x0FFFu));
    }
}

UNIT_TEST(ext_terrain_batch_rows_split_into_runs) {
    struct Got {
        int col;
        int len;
    };
    auto runs = [](const std::vector<uint8_t>& v, int maxRun) {
        std::vector<Got> out;
        ForEachRun(v.data(), static_cast<int32_t>(v.size()), maxRun, [&](int32_t c, int32_t l) {
            out.push_back(Got{ c, l });
        });
        return out;
    };
    std::vector<Got> g = runs({ 0, 1, 1, 1, 0, 0, 1, 0, 1, 1 }, 16);
    CHECK_EQ(g.size(), static_cast<size_t>(3));
    CHECK(g[0].col == 1 && g[0].len == 3);
    CHECK(g[1].col == 6 && g[1].len == 1);
    CHECK(g[2].col == 8 && g[2].len == 2);
    CHECK(runs({ 0, 0, 0 }, 16).empty());
    g = runs(std::vector<uint8_t>(10, 1), 4);   // split every maxRun chunks
    CHECK_EQ(g.size(), static_cast<size_t>(3));
    CHECK(g[0].col == 0 && g[0].len == 4);
    CHECK(g[1].col == 4 && g[1].len == 4);
    CHECK(g[2].col == 8 && g[2].len == 2);

    // Every row of a random grid: the runs cover exactly the visible chunks, left to right.
    std::mt19937 rng(1234);
    for (int trial = 0; trial < 2000; ++trial) {
        const int n = 1 + static_cast<int>(rng() % 40);
        const int maxRun = 1 + static_cast<int>(rng() % 20);
        std::vector<uint8_t> v(static_cast<size_t>(n));
        for (auto& x : v) x = static_cast<uint8_t>(rng() % 3 != 0);
        std::vector<uint8_t> covered(static_cast<size_t>(n), 0);
        int next = 0;
        bool ok = true;
        for (const Got& r : runs(v, maxRun)) {
            ok = ok && r.col >= next && r.len >= 1 && r.len <= maxRun && r.col + r.len <= n;
            for (int k = r.col; k < r.col + r.len && k < n; ++k) covered[static_cast<size_t>(k)] = 1;
            next = r.col + r.len;
        }
        CHECK(ok);
        CHECK(covered == v);
    }
}

UNIT_TEST(ext_terrain_batch_run_draw_fetches_the_chunk_draws_vertices) {
    const int chunkSize = 32;
    const int vertsPerSide = 33;
    const int vertsPerChunk = vertsPerSide * vertsPerSide;
    const std::vector<uint16_t> pattern = RetailChunkIndices(chunkSize, vertsPerSide);
    const int perChunk = static_cast<int>(pattern.size());
    CHECK_EQ(perChunk, chunkSize * chunkSize * 6);

    const RunLayout l = LayoutFor(32, vertsPerChunk);
    std::vector<uint16_t> run(static_cast<size_t>(perChunk) * static_cast<size_t>(l.maxRun));
    FillRunIndices(pattern.data(), perChunk, vertsPerChunk, l.maxRun, run.data());

    // A run of `len` chunks starting at vertex `start` is DrawIndexed(base = start, first index
    // 0, count = len x perChunk). Retail draws chunk m as base = start + m x vertsPerChunk over
    // the pattern. The vertex each index slot fetches is the same, in the same order.
    const int32_t start = (32 * 5 + 3) * vertsPerChunk;
    for (int len = 1; len <= l.maxRun; ++len) {
        bool same = true;
        for (int m = 0; m < len; ++m) {
            for (int i = 0; i < perChunk; ++i) {
                const int64_t ours = static_cast<int64_t>(start) + run[static_cast<size_t>(m * perChunk + i)];
                const int64_t retail = static_cast<int64_t>(start) + static_cast<int64_t>(m) * vertsPerChunk +
                                       pattern[static_cast<size_t>(i)];
                same = same && ours == retail;
            }
        }
        CHECK(same);
    }
    // The last index of the longest run still fits 16 bits.
    uint32_t biggest = 0;
    for (uint16_t x : run) biggest = x > biggest ? x : biggest;
    CHECK_EQ(biggest, static_cast<uint32_t>(l.maxRun * vertsPerChunk - 1));
    CHECK(biggest < 65536u);
}

UNIT_TEST(ext_terrain_batch_caster_box_holds_every_shadow_ray) {
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (int trial = 0; trial < 3000; ++trial) {
        // A downward light at 2..88 degrees elevation, any azimuth.
        const float elev = (2.0f + 86.0f * u01(rng)) * 3.14159265f / 180.0f;
        const float az = 6.2831853f * u01(rng);
        const float light[3] = { std::cos(elev) * std::cos(az), std::cos(elev) * std::sin(az), -std::sin(elev) };
        const float x0 = -400.0f + 800.0f * u01(rng);
        const float y0 = -400.0f + 800.0f * u01(rng);
        const Aabb chunk = { { x0, y0, -16.0f }, { x0 + 32.0f, y0 + 32.0f, 16.0f } };
        const float margin = CasterMargin(50.0f + 900.0f * u01(rng));
        Aabb reach;
        CHECK(CasterSweepBox(chunk, light, margin, reach));

        // A point of the chunk, its shadow ray down to the lowest terrain (z = -16) and a
        // receiver within `margin` of the ray point (the shadow filter's reach).
        for (int k = 0; k < 8; ++k) {
            const float p[3] = { x0 + 32.0f * u01(rng), y0 + 32.0f * u01(rng), -16.0f + 32.0f * u01(rng) };
            const float tMax = (p[2] - -16.0f) / -light[2];
            const float t = tMax * u01(rng);
            float q[3];
            for (int a = 0; a < 3; ++a) {
                q[a] = p[a] + light[a] * t + margin * (2.0f * u01(rng) - 1.0f);
            }
            CHECK(Inside(reach, q, 1e-2f));
        }
        // At least retail's 11 units along the light from the chunk's every corner.
        const float corner[3] = { chunk.max[0] + light[0] * 11.0f, chunk.max[1] + light[1] * 11.0f,
                                  chunk.max[2] + light[2] * 11.0f };
        CHECK(Inside(reach, corner, 1e-3f));
    }
    // A light that is not downwards has no finite reach: the chunk is kept.
    const Aabb chunk = { { 0.0f, 0.0f, -16.0f }, { 32.0f, 32.0f, 16.0f } };
    Aabb reach;
    const float flat[3] = { 1.0f, 0.0f, 0.0f };
    const float up[3] = { 0.0f, 0.0f, 1.0f };
    CHECK(!CasterSweepBox(chunk, flat, 2.0f, reach));
    CHECK(!CasterSweepBox(chunk, up, 2.0f, reach));
    CHECK_EQ(CasterMargin(0.0f), 2.0f);
    CHECK_EQ(CasterMargin(512.0f), 8.0f);
}

UNIT_TEST(ext_terrain_batch_sun_box_width_survives_the_transform) {
    // The sun camera's frustum (sun_camera.cpp GetSunCamera): side planes x >= -R and
    // -x >= -R. terrain_batch.cpp reads the box width as -(w2 + w3) after the frustum is
    // moved into the geometry's space (RtVolume_Frustum_MulTransform3, the engine's own).
    const float R = 237.5f;
    ::Frustum f = {};
    f.planes[0] = { 0.0f, 1.0f, 0.0f, 1.0f };
    f.planes[1] = { 0.0f, -1.0f, 0.0f, -256.0f };
    f.planes[2] = { 1.0f, 0.0f, 0.0f, -R };
    f.planes[3] = { -1.0f, 0.0f, 0.0f, -R };
    f.planes[4] = { 0.0f, 0.0f, -1.0f, -R };
    f.planes[5] = { 0.0f, 0.0f, 1.0f, -R };
    f.isOrthographic = true;

    // A rigid transform: forward = a downward sun direction, translation far off the origin.
    const float fx = 0.3f;
    const float fy = 0.5f;
    const float fz = -0.81240384f;   // |f| = 1
    Math::Transform3 t;
    t.axisY = Math::Vector3(fx, fy, fz);
    // axisX = normalize(cross(forward, up)), axisZ = cross(axisX, forward)
    float xx = fy * 1.0f - fz * 0.0f;
    float xy = fz * 0.0f - fx * 1.0f;
    float xz = 0.0f;
    const float xl = std::sqrt(xx * xx + xy * xy + xz * xz);
    xx /= xl;
    xy /= xl;
    t.axisX = Math::Vector3(xx, xy, xz);
    t.axisZ = Math::Vector3(xy * fz - xz * fy, xz * fx - xx * fz, xx * fy - xy * fx);
    t.translation = Math::Vector3(120.0f, -340.0f, 128.0f);

    ::Frustum world = {};
    RtVolume_Frustum_MulTransform3(&f, &world, &t);
    const float width = -(world.planes[2].w + world.planes[3].w);
    CHECK(std::fabs(width - 2.0f * R) < 1e-2f);
    // Plane 0's normal is the camera's forward, the direction the light travels.
    CHECK(std::fabs(world.planes[0].x - fx) < 1e-5f);
    CHECK(std::fabs(world.planes[0].y - fy) < 1e-5f);
    CHECK(std::fabs(world.planes[0].z - fz) < 1e-5f);
}

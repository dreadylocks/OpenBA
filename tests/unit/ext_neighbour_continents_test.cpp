// ============================================================================
// tests/unit/ext_neighbour_continents_test.cpp -- ext.neighbour's Continents map (NOT RETAIL)
// ============================================================================
// src/ext/neighbour/continents.cpp: the map type in a room's settings, the shape of the two lands
// (the sea between them stays open, the playable map's edge is always at sea, each town's start is on
// its own land, the boats' lane is at sea), the coast's height profile, and a digest of it all that
// must be the same on Windows, Linux and the Mac. docs/fork/NEIGHBOUR_TOWNS.md §12.
// ============================================================================
#include "unit.h"

#include "ext/neighbour/continents.h"
#include "ext/neighbour/handshake.h"   // Xxh64
#include "ext/neighbour/lobby.h"
#include "ext/neighbour/protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace C = Neighbour::Continents;

namespace {

const uint32_t kSeeds[] = {1u, 7u, 99u, 4242u, 12345u, 0xdeadbeefu, 2026u, 31337u};
const int      kSizes[] = {256, 384, 512};

// Map tile (tx, ty) -> the centred coordinates LandDistance takes (continents.h).
float TileX(int mapSize, int tx) { return static_cast<float>(tx) - (static_cast<float>(mapSize / 2) - 0.5f); }

}  // namespace

UNIT_TEST(ext_neighbour_continents_map_rides_in_the_terrain_setting) {
    CHECK_STREQ(C::JoinMap("", "Game/Terrain/Valleys.rsc").c_str(), "Game/Terrain/Valleys.rsc");
    CHECK_STREQ(C::JoinMap("continents", "Game/Terrain/Valleys.rsc").c_str(), "continents:Game/Terrain/Valleys.rsc");
    std::string map, path;
    C::SplitMap("continents:Game\\Terrain\\Valleys.rsc:resource", map, path);
    CHECK_STREQ(map.c_str(), "continents");
    CHECK_STREQ(path.c_str(), "Game\\Terrain\\Valleys.rsc:resource");
    C::SplitMap("Game/Terrain/Valleys.rsc", map, path);
    CHECK(map.empty());
    CHECK_STREQ(path.c_str(), "Game/Terrain/Valleys.rsc");
    // A map type this game does not know stays in the path, which then does not load: refused, not misbuilt.
    C::SplitMap("islands:Game/Terrain/Valleys.rsc", map, path);
    CHECK(map.empty());
    CHECK_STREQ(path.c_str(), "islands:Game/Terrain/Valleys.rsc");

    // A room line / welcome with a Continents terrain.
    Neighbour::Message m("welcome");
    m.SetInt("seed", 4242).SetInt("size", 0).SetInt("disasters", 1)
     .Set("start", "Game/StartCondition/StartEasy.rsc").Set("terrain", "continents:Game\\Terrain\\Valleys.rsc")
     .Set("climate", "Game/Climate/Mild.rsc");
    Neighbour::RoomSettings s;
    CHECK(Neighbour::ReadSettings(m, s));
    CHECK_STREQ(s.map.c_str(), "continents");
    CHECK_STREQ(s.terrain.c_str(), "Game\\Terrain\\Valleys.rsc");
    CHECK_STREQ(Neighbour::DescribeSettings(s).c_str(),
                "Small map, Continents (Valleys), Mild, StartEasy start, disasters on");
    // The prefix alone is no terrain.
    Neighbour::Message empty = m;
    empty.Set("terrain", "continents:");
    CHECK(!Neighbour::ReadSettings(empty, s));
}

UNIT_TEST(ext_neighbour_continents_two_lands_ringed_by_sea) {
    for (uint32_t seed : kSeeds) {
        for (int size : kSizes) {
            const C::Layout L = C::MakeLayout(seed, size);
            const float half = static_cast<float>(size) * 0.5f;
            // The playable map's edge is at sea all the way round (nomads come ashore, boats sail in).
            bool edgeAtSea = true;
            for (int t = 0; t < size; ++t) {
                const float a = TileX(size, t);
                edgeAtSea = edgeAtSea && C::LandDistance(L, a, -half + 0.5f) < 0.0f;
                edgeAtSea = edgeAtSea && C::LandDistance(L, a, half - 0.5f) < 0.0f;
                edgeAtSea = edgeAtSea && C::LandDistance(L, -half + 0.5f, a) < 0.0f;
                edgeAtSea = edgeAtSea && C::LandDistance(L, half - 0.5f, a) < 0.0f;
            }
            CHECK(edgeAtSea);
            // The boats' lane runs edge to edge and is at sea the whole way, in deep water inside the map.
            const std::vector<float> lane = C::SeaLane(L);
            CHECK_EQ(static_cast<int>(lane.size()), 2 * (2 * size + 1));
            bool laneDeep = true;
            for (size_t i = 0; i + 1 < lane.size(); i += 2)
                laneDeep = laneDeep && C::CoastHeight(0.3f, C::LandDistance(L, lane[i], lane[i + 1])) < -1.4f;
            CHECK(laneDeep);
            // The two lands never touch: no tile across the middle of the sea between them is land.
            bool straitOpen = true;
            for (int t = 0; t < size; ++t) {
                const float along = TileX(size, t);
                for (float across = -8.0f; across <= 8.0f; across += 1.0f) {
                    const float x = L.eastWest ? along : across;
                    const float y = L.eastWest ? across : along;
                    straitOpen = straitOpen && C::LandDistance(L, x, y) < 0.0f;
                }
            }
            CHECK(straitOpen);
            // Each town's start is on its own land, and each land is large.
            int land[2] = {0, 0};
            for (int ty = 0; ty < size; ty += 2)
                for (int tx = 0; tx < size; tx += 2) {
                    int which = -1;
                    if (C::LandDistance(L, TileX(size, tx), TileX(size, ty), &which) > 0.0f) ++land[which];
                }
            for (int seat = 0; seat < 2; ++seat) {
                int sx = 0, sy = 0;
                C::StartCentre(L, seat, &sx, &sy);
                CHECK(C::OnOwnLand(L, seat, sx, sy));
                CHECK(!C::OnOwnLand(L, 1 - seat, sx, sy));
                const double share = static_cast<double>(land[seat]) * 4.0 / (static_cast<double>(size) * size);
                CHECK(share > 0.15 && share < 0.40);
            }
        }
    }
}

UNIT_TEST(ext_neighbour_continents_coast_profile) {
    // Inland past the beach band the generator's land is untouched; so is anything below the beach.
    CHECK_EQ(C::CoastHeight(8.0f, 40.0f), 8.0f);
    CHECK_EQ(C::CoastHeight(-2.0f, 3.0f), -2.0f);
    CHECK_EQ(C::CoastHeight(0.1f, 9.0f), 0.1f);
    // A hill at the coast comes down to a flat, buildable beach (-0.25 .. 0.75) for a few tiles...
    for (float d = 0.0f; d <= 6.0f; d += 0.5f) {
        const float h = C::CoastHeight(8.0f, d);
        CHECK(h <= 0.75f && h >= -0.25f);
    }
    // ...and rises steadily back to it.
    float last = C::CoastHeight(8.0f, 6.0f);
    for (float d = 6.5f; d <= 20.0f; d += 0.5f) {
        const float h = C::CoastHeight(8.0f, d);
        CHECK(h >= last);
        last = h;
    }
    // No step at the coastline, and the sea is deep a few tiles out.
    CHECK_EQ(C::CoastHeight(8.0f, 0.0f), C::CoastHeight(8.0f, -0.0f));
    CHECK(C::CoastHeight(8.0f, -0.25f) > -0.25f);
    CHECK(C::CoastHeight(8.0f, -3.5f) < -1.4f);
    CHECK_EQ(C::CoastHeight(8.0f, -30.0f), -2.5f);
    CHECK_EQ(C::CoastHeight(-4.0f, -30.0f), -4.0f);   // a deeper pit stays
}

UNIT_TEST(ext_neighbour_continents_nomads_come_ashore) {
    // A 64 x 64 map: deep sea (8) round one land (2) from x 10..30, y 10..50, crossed by a river two
    // tiles wide (7) at x 20..21, with a mountain (6) on its east coast at y 40.
    constexpr int w = 64;
    std::vector<uint8_t> tiles(static_cast<size_t>(w) * w, 8);
    for (int y = 10; y <= 50; ++y)
        for (int x = 10; x <= 30; ++x) tiles[static_cast<size_t>(y) * w + x] = (x == 20 || x == 21) ? 7 : 2;
    tiles[40 * w + 30] = 6;
    const C::TileType typeAt = [&tiles](int x, int y) { return tiles[static_cast<size_t>(y) * w + x]; };
    CHECK(C::RingedBySea(w, typeAt));
    int x = -1, y = -1;
    // East from the hall at (15, 30): over the river, ashore at the far coast.
    CHECK(C::WalkToShore(w, 15.5f, 30.5f, 1.0f, 0.0f, typeAt, &x, &y));
    CHECK_EQ(x, 30);
    CHECK_EQ(y, 30);
    // West: the near coast.
    CHECK(C::WalkToShore(w, 15.5f, 30.5f, -1.0f, 0.0f, typeAt, &x, &y));
    CHECK_EQ(x, 10);
    CHECK_EQ(y, 30);
    // A mountain on the coast: the last tile a group can stand on before it.
    CHECK(C::WalkToShore(w, 15.5f, 40.5f, 1.0f, 0.0f, typeAt, &x, &y));
    CHECK_EQ(x, 29);
    CHECK_EQ(y, 40);
    // From the open sea outwards there is no land.
    CHECK(!C::WalkToShore(w, 2.5f, 2.5f, -1.0f, 0.0f, typeAt, &x, &y));
    // A retail map: land at its rim.
    tiles[0] = 6;
    CHECK(!C::RingedBySea(w, typeAt));
}

// The relay refuses a town whose land differs from its neighbour's: a Mac and a PC must compute every
// one of these to the same bits. This digest was taken on an arm64 Mac (clang); CI prints it on
// Windows (GCC, x86-64) and Linux, and fails anywhere it differs.
UNIT_TEST(ext_neighbour_continents_same_bits_everywhere) {
    Neighbour::Xxh64 x(0);
    auto feedF = [&x](float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        x.Update(&u, 4);
    };
    auto feedI = [&x](int32_t i) { x.Update(&i, 4); };
    for (uint32_t seed : {1u, 4242u, 12345u, 0xdeadbeefu}) {
        for (int size : kSizes) {
            const C::Layout L = C::MakeLayout(seed, size);
            feedI(L.eastWest ? 1 : 0);
            // The whole terrain (twice the map) every 3 tiles, at vertex and cell-centre positions.
            for (int row = -size; row <= size; row += 3)
                for (int col = -size; col <= size; col += 3) {
                    int land = -1;
                    feedF(C::LandDistance(L, static_cast<float>(col), static_cast<float>(row), &land));
                    feedI(land);
                    feedF(C::LandDistance(L, static_cast<float>(col) + 0.5f, static_cast<float>(row) + 0.5f));
                }
            for (float v : C::SeaLane(L)) feedF(v);
            for (int seat = 0; seat < 2; ++seat) {
                int sx = 0, sy = 0;
                C::StartCentre(L, seat, &sx, &sy);
                feedI(sx);
                feedI(sy);
            }
        }
    }
    for (int i = -400; i <= 400; ++i)
        for (float h : {-4.0f, -0.6f, 0.2f, 0.5f, 3.0f, 8.0f, 17.25f})
            feedF(C::CoastHeight(h, static_cast<float>(i) * 0.0625f));
    const uint64_t h = x.Digest();
    std::printf("  continents digest %016llx\n", static_cast<unsigned long long>(h));
    CHECK(h == 0xfc32ec476b6efe5cull);   // arm64 Mac, clang (2026-10-24)
}

// tools/erosion_bench/bench.cpp -- block EROSION: times wg_f390_erosion (the tree's body, compiled with the perf
// flags) against the pre-EROSION body (copied below as ref_erosion) on the same heightfield and the
// same seed, and memcmp's the heightmap and the Random state afterwards.
#include "Game/TerrainHeightfield.h"
#include "system/MemoryAllocator.h"
#include "Game/worldgen_erosion_par.h"
#include "system/parallel_for.h"
#include "platform/common/cpu/topology.h"
#include "platform/common/cpu/roster.h"
#include "diag/diag_threads.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// ---- bench-only link stand-ins (the real MT19937 of value_types.cpp, transcribed) ----
namespace {
void NextGroupImpl(System::Random* self) {
    constexpr uint32_t kUpper = 0x80000000u, kLower = 0x7fffffffu, kMatrixA = 0x9908b0dfu;
    for (int i = 0; i < 624; ++i) {
        uint32_t y = (self->_mt[i] & kUpper) | (self->_mt[(i + 1) % 624] & kLower);
        uint32_t next = self->_mt[(i + 397) % 624] ^ (y >> 1);
        if (y & 1u) next ^= kMatrixA;
        self->_mt[i] = next;
    }
}
uint32_t RandImpl(System::Random* self) {
    if (self->_index == 0) NextGroupImpl(self);
    uint32_t y = self->_mt[self->_index];
    y ^= y >> 11;
    y ^= (y & 0xff3a58adu) << 7;
    y ^= (y & 0xffffdf8cu) << 15;
    uint32_t next = self->_index + 1;
    self->_index = (next == 0x270) ? 0 : next;
    return y ^ (y >> 18);
}
}
System::Random::Random(uint32_t seed) {
    _index = 0;
    _mt[0] = seed;
    for (int i = 1; i < 624; ++i) {
        uint32_t prev = _mt[i - 1];
        _mt[i] = (prev ^ (prev >> 30)) * 0x6c078965u + static_cast<uint32_t>(i);
    }
}
System::Random::Random() { _index = 0; }
uint32_t System::Random::Rand(uint32_t bound) { return bound == 0 ? 0u : RandImpl(this) % bound; }
void System::MemoryAllocator::MemFree(void* p) { std::free(p); }
namespace rtcoll {
void SetSizeCore(ArrayHdr*, int, const uint8_t*, const ArraySpec*) { std::abort(); }
}
// parallel_for.cpp's links: the bench sets the worker count itself (SetWorkers), so the roster
// and the topology are never asked.
namespace DiagThreads { void Register(char, const volatile uint32_t*) {} }
namespace Cpu {
const Topology& Get() { std::abort(); }
int ParallelWorkers(int) { std::abort(); }
}

static constexpr float kOne = 1.0f, kErodeFactor = 0.0625f, kErodeMaxXfer = 0.8f;
static long long g_steps = 0;

// ---- the pre-EROSION body, verbatim but for the name and a step counter ----
template <bool kCount>
static void ref_erosion(TerrainHeightfield* hfv) {
    int dim  = hfv->gridSize;
    int edge = dim + 1;
    const TerrainSizeRow* sizeRow = hfv->sizeRow;
    if (!sizeRow) return;
    int iters = sizeRow->erosionIterations;
    if (iters <= 0) return;
    System::Random* rng = &hfv->random;
    for (int it = 0; it < iters; ++it) {
        int col = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
        int row = (int)rng->Rand((uint32_t)(dim - 1)) + 1;
        while (true) {
            float* hm = hfv->heightmap.data;
            int curIdx = row * edge + col;
            float hCur = hm[curIdx];
            if (hCur <= 0.0f) break;
            if (kCount) ++g_steps;
            int bestCol = -1, bestRow = -1;
            int bestIdx = curIdx;
            float bestH = hCur;
            int rowTop = (row - 1) * edge;
            {
                float h = hm[rowTop + col - 1];
                if (h < bestH) { bestCol = col - 1; bestIdx = col - 1 + rowTop; bestRow = row - 1; bestH = h; }
                h = hm[rowTop + col];
                if (h < bestH) { bestIdx = rowTop + col; bestRow = row - 1; bestH = h; bestCol = col; }
                h = hm[rowTop + col + 1];
                if (h < bestH) { bestCol = col + 1; bestIdx = col + 1 + rowTop; bestRow = row - 1; bestH = h; }
            }
            int rowMid = row * edge;
            {
                float h = hm[rowMid + col - 1];
                if (h < bestH) { bestCol = col - 1; bestIdx = col - 1 + rowMid; bestH = h; bestRow = row; }
                h = hm[rowMid + col];
                if (h < bestH) { bestIdx = rowMid + col; bestH = h; bestCol = col; bestRow = row; }
                h = hm[rowMid + col + 1];
                if (h < bestH) { bestCol = col + 1; bestIdx = col + 1 + rowMid; bestH = h; bestRow = row; }
            }
            int rowBot = (row + 1) * edge;
            {
                float h = hm[rowBot + col - 1];
                if (h < bestH) { bestCol = col - 1; bestIdx = col - 1 + rowBot; bestH = h; bestRow = row + 1; }
                h = hm[rowBot + col];
                if (h < bestH) { bestIdx = rowBot + col; bestH = h; bestCol = col; bestRow = row + 1; }
                h = hm[rowBot + col + 1];
                if (h < bestH) { bestIdx = col + 1 + rowBot; bestH = h; bestCol = col + 1; bestRow = row + 1; }
            }
            float t = (hCur - bestH) * kErodeFactor;
            if (t > kErodeMaxXfer) t = kErodeMaxXfer;
            hm[curIdx] = (kOne - t) * hCur + t * bestH;
            hfv->heightmap.data[bestIdx] = (kOne - t) * bestH + t * hCur;
            col = bestCol;
            row = bestRow;
            if (bestIdx == curIdx || col < 1 || row < 1 || col >= dim || row >= dim)
                break;
        }
    }
}


// ---- conflict census: the reference run, instrumented. Droplet d "conflicts" in a window of B
// consecutive droplets when it reads a cell an earlier droplet of the same window wrote.
static void conflict_census(const char* name, int dim, int iters, const std::vector<float>& h0, uint32_t seed) {
    const int Bs[] = {8, 32, 128, 512};
    std::vector<float> hv = h0;
    float* hm = hv.data();
    int edge = dim + 1;
    std::vector<int> lastWriter(hv.size(), -1);
    long long conflicts[4] = {0, 0, 0, 0}, reads = 0, writes = 0, steps = 0;
    System::Random rng(seed);
    std::vector<int> rd; rd.reserve(4096);
    for (int it = 0; it < iters; ++it) {
        rd.clear();
        int col = (int)rng.Rand((uint32_t)(dim - 1)) + 1;
        int row = (int)rng.Rand((uint32_t)(dim - 1)) + 1;
        std::vector<int> wr;
        while (true) {
            int cur = row * edge + col;
            rd.push_back(cur);
            float hCur = hm[cur];
            if (hCur <= 0.0f) break;
            ++steps;
            int bi = cur; float bh = hCur; int bc = -1, br = -1;
            for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
                int i = cur + dr * edge + dc; rd.push_back(i);
                if (hm[i] < bh) { bh = hm[i]; bi = i; bc = col + dc; br = row + dr; }
            }
            float t = (hCur - bh) * kErodeFactor; if (t > kErodeMaxXfer) t = kErodeMaxXfer;
            hm[cur] = (kOne - t) * hCur + t * bh;
            hm[bi] = (kOne - t) * bh + t * hCur;
            wr.push_back(cur); wr.push_back(bi);
            col = bc; row = br;
            if (bi == cur || col < 1 || row < 1 || col >= dim || row >= dim) break;
        }
        for (int b = 0; b < 4; ++b) {
            int ws = it / Bs[b] * Bs[b];
            for (int c : rd) if (lastWriter[c] >= ws) { ++conflicts[b]; break; }
        }
        std::sort(rd.begin(), rd.end()); reads += std::unique(rd.begin(), rd.end()) - rd.begin();
        std::sort(wr.begin(), wr.end()); writes += std::unique(wr.begin(), wr.end()) - wr.begin();
        for (int c : wr) lastWriter[c] = it;
    }
    std::printf("%-14s census: steps/droplet %.1f  distinct reads %.1f writes %.1f  conflict rate B=8 %.2f%%  B=32 %.2f%%  B=128 %.2f%%  B=512 %.2f%%\n",
                name, (double)steps / iters, (double)reads / iters, (double)writes / iters,
                100.0 * conflicts[0] / iters, 100.0 * conflicts[1] / iters, 100.0 * conflicts[2] / iters, 100.0 * conflicts[3] / iters);
}

// ---- inputs ----
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static float lattice(int x, int y, uint32_t s) { return (hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ s) & 0xffffff) / 16777215.0f; }
static float vnoise(float x, float y, uint32_t s) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = lattice(xi, yi, s), b = lattice(xi + 1, yi, s), c = lattice(xi, yi + 1, s), d = lattice(xi + 1, yi + 1, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
static std::vector<float> synth(int dim, uint32_t seed) {
    int edge = dim + 1;
    std::vector<float> h((size_t)edge * edge);
    for (int y = 0; y < edge; ++y)
        for (int x = 0; x < edge; ++x) {
            float v = 0, amp = 1, f = 1.0f / 128;
            for (int o = 0; o < 6; ++o) { v += amp * vnoise(x * f, y * f, seed + o); amp *= 0.5f; f *= 2; }
            h[(size_t)y * edge + x] = v * 60.0f - 18.0f;   // ~25% <= 0
        }
    return h;
}

struct Case { const char* name; int dim; int iters; std::vector<float> h; };

static double run(void (*fn)(TerrainHeightfield*), const Case& c, uint32_t seed, std::vector<float>& out,
                  System::Random& rngOut) {
    static TerrainHeightfield hf;   // 0xA30 POD-ish record; only the read fields are set
    std::memset((void*)&hf, 0, sizeof(hf));
    TerrainSizeRow row{};
    row.erosionIterations = c.iters;
    out = c.h;
    hf.gridSize = c.dim;
    hf.heightmap.data = out.data();
    hf.heightmap.size = (int)out.size();
    new (&hf.random) System::Random(seed);
    hf.sizeRow = &row;
    auto t0 = std::chrono::steady_clock::now();
    fn(&hf);
    auto t1 = std::chrono::steady_clock::now();
    rngOut = hf.random;
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

static WgErosionStats g_stats;
static WgErosionTuning g_tuning = WgErosionDefaultTuning();
static void tree_erosion(TerrainHeightfield* hf) { wg_f390_erosion_tuned(hf, g_tuning, &g_stats); }

int main(int argc, char** argv) {
    int reps = argc > 1 ? std::atoi(argv[1]) : 3;
    std::vector<int> workerCounts;
    for (int a = 2; a < argc; ++a) if (argv[a][0] >= '0' && argv[a][0] <= '9') workerCounts.push_back(std::atoi(argv[a]));
    if (workerCounts.empty()) workerCounts = {0, 3, 7};
    const char* only = nullptr;   // a case-name prefix (e.g. dump, synth1024, ramp); L=<lookahead> S=<speculators> G=<gap>
    for (int a = 2; a < argc; ++a) {
        if (std::strncmp(argv[a], "L=", 2) == 0) g_tuning.lookahead = std::atoi(argv[a] + 2);
        else if (std::strncmp(argv[a], "S=", 2) == 0) g_tuning.speculators = std::atoi(argv[a] + 2);
        else if (std::strncmp(argv[a], "G=", 2) == 0) g_tuning.gap = std::atoi(argv[a] + 2);
        else if (std::strcmp(argv[a], "census") != 0 && !(argv[a][0] >= '0' && argv[a][0] <= '9')) only = argv[a];
    }
    std::vector<Case> cases;
    cases.push_back({"synth1024 16M", 1024, 16000000, synth(1024, 7)});
    cases.push_back({"synth768 8M", 768, 8000000, synth(768, 11)});
    {
        std::vector<float> r((size_t)1025 * 1025);
        for (int y = 0; y < 1025; ++y) for (int x = 0; x < 1025; ++x) r[(size_t)y * 1025 + x] = 100.0f + x * 3.0f + y * 1.0f;
        cases.push_back({"ramp1024 200k", 1024, 200000, r});
    }
    {
        // The dump lives in the game directory: env BANISHED_DIR (as tools/build.sh), default the GOG
        // install. getenv is acceptable HERE ONLY because this is a standalone bench, not the game
        // (the game reads its flags once at boot into Options; CLAUDE.md "Never getenv").
        const char* gameDir = std::getenv("BANISHED_DIR");
        std::string dumpPath = std::string(gameDir && *gameDir ? gameDir : "C:/GOG Games/Banished") + "/heightmap_dump.raw";
        FILE* f = std::fopen(dumpPath.c_str(), "rb");
        if (f) {
            int d = 0;
            if (std::fread(&d, 4, 1, f) == 1 && d == 513) {
                std::vector<float> h((size_t)513 * 513);
                if (std::fread(h.data(), 4, h.size(), f) == h.size())
                    cases.push_back({"dump512 4M", 512, 4000000, h});
            }
            std::fclose(f);
        }
    }
    if (argc > 2 && std::strcmp(argv[2], "census") == 0) { for (const Case& c : cases) conflict_census(c.name, c.dim, c.iters, c.h, 1u); return 0; }
    int fails = 0;
    for (const Case& c : cases) {
        if (only && std::strncmp(c.name, only, std::strlen(only)) != 0) continue;
        for (uint32_t seed : {1u, 0xBADC0DEu}) {
            std::vector<float> a, b;
            System::Random ra(0), rb(0);
            g_steps = 0;
            run(&ref_erosion<true>, c, seed, a, ra);
            long long steps = g_steps;
            double tr = 1e30;
            for (int r = 0; r < reps; ++r) tr = std::min(tr, run(&ref_erosion<false>, c, seed, a, ra));
            std::printf("%-14s seed %08x  steps/droplet %.2f  ref %8.1f ms (%.2f ns/step)\n", c.name, seed,
                        (double)steps / c.iters, tr, tr * 1e6 / steps);
            for (int w : workerCounts) {
                System::Parallel::SetWorkers(w);
                double tn = 1e30;
                for (int r = 0; r < reps; ++r) tn = std::min(tn, run(&tree_erosion, c, seed, b, rb));
                bool same = std::memcmp(a.data(), b.data(), a.size() * 4) == 0 &&
                            std::memcmp((const void*)&ra, (const void*)&rb, sizeof(ra)) == 0;
                if (!same) ++fails;
                const WgErosionStats& s = g_stats;
                const double n = s.droplets ? (double)s.droplets : 1.0;
                std::printf("    workers %d  new %8.1f ms  x%.2f  spec-ok %.1f%%  conflict %.2f%%  overflow %.2f%%  unspeculated %.2f%%  cellchk %.2f%%  solo %.1f%%  %s\n",
                            w, tn, tr / tn, 100.0 * s.committedSpec / n, 100.0 * s.conflicts / n,
                            100.0 * s.overflows / n, 100.0 * s.unspeculated / n, 100.0 * s.cellChecks / n, 100.0 * s.solo / n, same ? "IDENTICAL" : "DIFFERENT");
                std::fflush(stdout);
            }
        }
    }
    System::Parallel::Shutdown();
    return fails;
}

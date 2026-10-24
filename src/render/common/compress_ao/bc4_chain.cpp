// ============================================================================
// src/render/common/compress_ao/bc4_chain.cpp -- ext.compress_ao: R8 levels to BC4 (NOT RETAIL)
// ============================================================================
// bc4_chain.h has the contract. docs/extensions/handins/COMPRESSAO.md, issue #13.
// ============================================================================
#include "compress_ao/bc4_chain.h"

#include "bc4/bc4.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace CompressAo {

namespace {

// Encode workers, made on the first texture and never torn down (the process exits with them
// parked, like async_gpu_create's).
class Pool {
public:
    explicit Pool(int n) {
        for (int i = 0; i < n; ++i)
            std::thread([this] { Run(); }).detach();
    }
    void Push(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(_m);
            _q.push_back(std::move(job));
        }
        _cv.notify_one();
    }

private:
    void Run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(_m);
                _cv.wait(lock, [this] { return !_q.empty(); });
                job = std::move(_q.front());
                _q.pop_front();
            }
            job();
        }
    }
    std::mutex _m;
    std::condition_variable _cv;
    std::deque<std::function<void()> > _q;
};

Pool& Workers() {
    static Pool* pool = [] {
        const unsigned hw = std::thread::hardware_concurrency();
        int n = hw > 2 ? static_cast<int>(hw / 2) : 1;
        if (n > 8) n = 8;
        std::fprintf(stderr, "[CompressAo] %d encode worker thread(s)\n", n);
        return new Pool(n);
    }();
    return *pool;
}

// The caller's wait for its own bands.
struct Latch {
    std::mutex m;
    std::condition_variable cv;
    int left = 0;
    void Done() {
        std::lock_guard<std::mutex> lock(m);
        if (--left == 0) cv.notify_all();
    }
    void Wait() {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [this] { return left == 0; });
    }
};

std::atomic<Ext::DerivedCache::Store*> g_cache{nullptr};

// The summary LoadEnd prints (relaxed: a log line, not a synchronisation).
std::atomic<uint64_t> g_textures{0};
std::atomic<uint64_t> g_hits{0};
std::atomic<uint64_t> g_encoded{0};
std::atomic<uint64_t> g_skipped{0};
std::atomic<uint64_t> g_r8Bytes{0};
std::atomic<uint64_t> g_bc4Bytes{0};
std::atomic<int64_t>  g_encodeNs{0};   // wall, on the calling threads
std::atomic<int64_t>  g_lookupNs{0};   // hash + cache lookup, on the calling threads
uint64_t g_printed = 0;                // g_textures + g_skipped at the last line (LoadEnd's thread)

int Dim(int size, int level) {
    const int d = size >> level;
    return d > 0 ? d : 1;
}

int64_t Since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}

void EncodeParallel(const Level* levels, int count, int width, int height, const std::vector<size_t>& offsets,
                    uint8_t* out) {
    struct Band {
        int level;
        int row0;    // first block row
        int rows;    // block rows
    };
    std::vector<Band> bands;
    for (int i = 0; i < count; ++i) {
        const int blockRows = (Dim(height, i) + 3) / 4;
        for (int r = 0; r < blockRows; r += 16)
            bands.push_back({ i, r, blockRows - r < 16 ? blockRows - r : 16 });
    }
    Latch latch;
    latch.left = static_cast<int>(bands.size());
    for (const Band& band : bands) {
        Workers().Push([&, band] {
            const int w  = Dim(width, band.level);
            const int h  = Dim(height, band.level);
            const int y0 = band.row0 * 4;
            const int bh = h - y0 < band.rows * 4 ? h - y0 : band.rows * 4;
            const uint8_t* src = levels[band.level].data + static_cast<size_t>(y0) * static_cast<size_t>(w);
            uint8_t* dst = out + offsets[band.level] +
                           static_cast<size_t>(band.row0) * static_cast<size_t>(Bc4::BlockRowPitch(w));
            Bc4::EncodeBC4(src, w, bh, w, dst);
            latch.Done();
        });
    }
    latch.Wait();
}

}  // namespace

void SetCache(Ext::DerivedCache::Store* cache) {
    g_cache.store(cache, std::memory_order_release);
}

bool Encode(const Level* levels, int count, int width, int height, uint32_t sourceFormat,
            std::vector<uint8_t>* out, std::vector<size_t>* offsets) {
    if (count <= 0 || width <= 0 || height <= 0) return false;
    uint64_t r8 = 0;
    offsets->assign(static_cast<size_t>(count) + 1, 0);
    for (int i = 0; i < count; ++i) {
        const int64_t expect = static_cast<int64_t>(Dim(width, i)) * Dim(height, i);
        if (levels[i].data == nullptr || levels[i].size != expect) {
            g_skipped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        r8 += static_cast<uint64_t>(expect);
        (*offsets)[i + 1] = (*offsets)[i] + Bc4::EncodedSize(Dim(width, i), Dim(height, i));
    }
    const size_t total = (*offsets)[count];

    Ext::DerivedCache::Store* const cache = g_cache.load(std::memory_order_acquire);
    Ext::DerivedCache::Key key;
    if (cache != nullptr) {
        const auto t0 = std::chrono::steady_clock::now();
        key.producer = Ext::DerivedCache::MakeProducer('A', 'O', 'B', '4');
        key.version  = kEncoderVersion;
        key.shape[0] = static_cast<uint32_t>(width);
        key.shape[1] = static_cast<uint32_t>(height);
        key.shape[2] = static_cast<uint32_t>(count);
        key.shape[3] = sourceFormat;
        key.sourceSize = r8;
        uint64_t h = 0;
        for (int i = 0; i < count; ++i)
            h = Ext::DerivedCache::Hash64(levels[i].data, static_cast<size_t>(levels[i].size), h);
        key.sourceHash = h;
        const bool hit = cache->Find(key, *out) && out->size() == total;
        g_lookupNs.fetch_add(Since(t0), std::memory_order_relaxed);
        if (hit) {
            g_textures.fetch_add(1, std::memory_order_relaxed);
            g_hits.fetch_add(1, std::memory_order_relaxed);
            g_r8Bytes.fetch_add(r8, std::memory_order_relaxed);
            g_bc4Bytes.fetch_add(total, std::memory_order_relaxed);
            return true;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    out->assign(total, 0);
    EncodeParallel(levels, count, width, height, *offsets, out->data());
    g_encodeNs.fetch_add(Since(t0), std::memory_order_relaxed);
    g_textures.fetch_add(1, std::memory_order_relaxed);
    g_encoded.fetch_add(1, std::memory_order_relaxed);
    g_r8Bytes.fetch_add(r8, std::memory_order_relaxed);
    g_bc4Bytes.fetch_add(total, std::memory_order_relaxed);
    if (cache != nullptr) cache->Put(key, *out);   // a copy: `out` goes on to the texture
    return true;
}

void LoadEnd() {
    const uint64_t seen = g_textures.load(std::memory_order_relaxed) + g_skipped.load(std::memory_order_relaxed);
    if (seen == g_printed) return;
    g_printed = seen;
    Ext::DerivedCache::Store* const cache = g_cache.load(std::memory_order_acquire);
    std::fprintf(stderr,
                 "[CompressAo] textures=%llu cache_hits=%llu encoded=%llu skipped=%llu r8=%.3f GB bc4=%.3f GB "
                 "encode=%.0f ms lookup=%.0f ms cache=%s\n",
                 static_cast<unsigned long long>(g_textures.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(g_hits.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(g_encoded.load(std::memory_order_relaxed)),
                 static_cast<unsigned long long>(g_skipped.load(std::memory_order_relaxed)),
                 static_cast<double>(g_r8Bytes.load(std::memory_order_relaxed)) / 1073741824.0,
                 static_cast<double>(g_bc4Bytes.load(std::memory_order_relaxed)) / 1073741824.0,
                 static_cast<double>(g_encodeNs.load(std::memory_order_relaxed)) / 1e6,
                 static_cast<double>(g_lookupNs.load(std::memory_order_relaxed)) / 1e6, cache ? "on" : "off");
    if (cache != nullptr) {
        cache->RequestFlush();
        cache->ReleaseMapping();   // the load that read it is over: its pages leave the working set
    }
}

}  // namespace CompressAo

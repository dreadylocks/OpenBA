// ============================================================================
// src/render/metal/share_gpu_data/share_create.mm -- ext.share_gpu_data, the Metal side (NOT RETAIL)
// ============================================================================
// share_create.h has the contract; docs/extensions/handins/SHAREGPUDATA.md. Compiled with ARC.
// ============================================================================

#include "share_gpu_data/share_create.h"

#include "share_gpu_data/share_gpu_data_backend.h"
#include "share_gpu_data/share_table.h"
#include "ext/derived_cache/hash64.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace VideoMetal {
namespace ShareGpuData {

namespace {

std::atomic<bool>     g_requested{false};
std::atomic<uint64_t> g_hashNs{0};      // time spent hashing seed bytes, all threads
std::atomic<uint64_t> g_refused{0};     // kind-2 textures whose levels did not qualify
GpuShare::Stats       g_lastPrinted;    // LoadEnd only (loader thread)

GpuShare::Table& Table() {
    static GpuShare::Table t;
    return t;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

template <class T>
void AppendDesc(GpuShare::Key* key, const T& value) {
    static_assert(sizeof(T) <= GpuShare::kMaxDesc, "descriptor blob too large");
    std::memcpy(key->desc + key->descSize, &value, sizeof(T));
    key->descSize += static_cast<uint32_t>(sizeof(T));
}

// The table's own +1 on an object (ARC: a retain the compiler does not balance).
void* TableRetain(id object) {
    return const_cast<void*>(CFBridgingRetain(object));
}

void TableRelease(void* handle) {
    if (handle != nullptr) {
        (void)CFBridgingRelease(handle);
    }
}

}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

id<MTLBuffer> CreateBuffer(id<MTLDevice> device, bool index, bool dynamic, const void* data, size_t size,
                           id<MTLBuffer> (^make)(void), bool* shared) {
    *shared = false;
    if (!Requested() || dynamic || data == nullptr || size == 0 || device == nil) {
        return make();
    }

    GpuShare::Key key;
    key.device = (__bridge const void*)device;
    key.kind   = index ? GpuShare::kIndexBuffer : GpuShare::kVertexBuffer;
    const uint64_t desc[2] = { static_cast<uint64_t>(size), static_cast<uint64_t>(MTLResourceStorageModeShared) };
    AppendDesc(&key, desc);
    key.payloadBytes = size;
    const uint64_t t0 = NowNs();
    key.payloadHash  = Ext::DerivedCache::Hash64(data, size);
    g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);

    void* handles[GpuShare::kHandles] = {};
    if (Table().Acquire(key, handles)) {
        *shared = true;
        return (__bridge id<MTLBuffer>)handles[0];   // the caller's strong member is its own reference
    }
    id<MTLBuffer> made = make();
    if (made == nil) {
        return nil;
    }
    handles[0] = TableRetain(made);
    if (Table().Insert(key, handles)) {
        *shared = true;
        return made;
    }
    // Two creates of an equal buffer raced (the loader and the main thread) and the other one's is in
    // the table: nobody has seen ours yet, so it goes and the winner's is handed out instead.
    TableRelease(handles[0]);
    void* winner[GpuShare::kHandles] = {};
    if (Table().Acquire(key, winner)) {
        *shared = true;
        return (__bridge id<MTLBuffer>)winner[0];
    }
    return made;
}

id<MTLTexture> CreateTexture(id<MTLDevice> device, MTLTextureDescriptor* desc, const LevelBytes* levels, int count,
                             bool qualifies, id<MTLTexture> (^make)(void), bool* shared) {
    *shared = false;
    if (!Requested()) {
        return make();
    }
    if (!qualifies || levels == nullptr || count <= 0 || device == nil) {
        g_refused.fetch_add(1, std::memory_order_relaxed);
        return make();
    }

    GpuShare::Key key;
    key.device = (__bridge const void*)device;
    key.kind   = GpuShare::kTexture;
    const uint64_t shape[8] = {
        static_cast<uint64_t>(desc.pixelFormat),      static_cast<uint64_t>(desc.textureType),
        static_cast<uint64_t>(desc.width),            static_cast<uint64_t>(desc.height),
        static_cast<uint64_t>(desc.mipmapLevelCount), static_cast<uint64_t>(desc.arrayLength),
        static_cast<uint64_t>(desc.usage),            static_cast<uint64_t>(desc.storageMode),
    };
    AppendDesc(&key, shape);
    const uint64_t t0 = NowNs();
    uint64_t h     = 0;
    uint64_t total = 0;
    for (int i = 0; i < count; ++i) {
        const uint64_t meta[2] = { static_cast<uint64_t>(levels[i].row), static_cast<uint64_t>(levels[i].size) };
        h = Ext::DerivedCache::Hash64(meta, sizeof(meta), h);
        h = Ext::DerivedCache::Hash64(levels[i].data, levels[i].size, h);
        total += levels[i].size;
    }
    g_hashNs.fetch_add(NowNs() - t0, std::memory_order_relaxed);
    key.payloadBytes = total;
    key.payloadHash  = h;

    void* handles[GpuShare::kHandles] = {};
    if (Table().Acquire(key, handles)) {
        *shared = true;
        return (__bridge id<MTLTexture>)handles[0];
    }
    id<MTLTexture> made = make();
    if (made == nil) {
        return nil;
    }
    handles[0] = TableRetain(made);   // a Metal texture has no separate view: handles[1] stays null
    if (Table().Insert(key, handles)) {
        *shared = true;
        return made;
    }
    // A raced equal create is in the table: ours goes, the winner's is handed out (as CreateBuffer).
    TableRelease(handles[0]);
    void* winner[GpuShare::kHandles] = {};
    if (Table().Acquire(key, winner)) {
        *shared = true;
        return (__bridge id<MTLTexture>)winner[0];
    }
    return made;
}

void DropBuffer(id<MTLBuffer> buffer) {
    if (buffer == nil) {
        return;
    }
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop((__bridge const void*)buffer, handles)) {
        return;
    }
    TableRelease(handles[0]);
}

void DropTexture(id<MTLTexture> texture) {
    if (texture == nil) {
        return;
    }
    void* handles[GpuShare::kHandles] = {};
    if (!Table().Drop((__bridge const void*)texture, handles)) {
        return;
    }
    TableRelease(handles[1]);
    TableRelease(handles[0]);
}

void Clear() {
    std::vector<void*> handles;
    Table().Clear(&handles);
    for (void* h : handles) {
        TableRelease(h);
    }
    if (!handles.empty()) {
        std::fprintf(stderr, "[ShareGpuData] device going away: the table released %zu reference(s)\n",
                     handles.size());
    }
}

void LoadEnd() {
    if (!Requested()) {
        return;
    }
    const GpuShare::Stats s = Table().GetStats();
    uint64_t hits = 0;
    for (uint32_t k = 0; k < GpuShare::kKindCount; ++k) {
        hits += s.hits[k] - g_lastPrinted.hits[k];
    }
    if (hits == 0 && s.inserts[GpuShare::kTexture] == g_lastPrinted.inserts[GpuShare::kTexture] &&
        s.inserts[GpuShare::kVertexBuffer] == g_lastPrinted.inserts[GpuShare::kVertexBuffer]) {
        return;
    }
    g_lastPrinted = s;
    const double mb = 1.0 / (1024.0 * 1024.0);
    std::fprintf(stderr,
                 "[ShareGpuData] shared (run total): vb hits=%llu (%.1f MB) ib hits=%llu (%.1f MB) "
                 "tex hits=%llu (%.1f MB); entries=%llu (vb %llu / ib %llu / tex %llu made), freed=%llu, "
                 "raced=%llu, tex refused=%llu, hash %.0f ms\n",
                 static_cast<unsigned long long>(s.hits[GpuShare::kVertexBuffer]),
                 s.hitBytes[GpuShare::kVertexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kIndexBuffer]),
                 s.hitBytes[GpuShare::kIndexBuffer] * mb,
                 static_cast<unsigned long long>(s.hits[GpuShare::kTexture]), s.hitBytes[GpuShare::kTexture] * mb,
                 static_cast<unsigned long long>(s.live),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kVertexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kIndexBuffer]),
                 static_cast<unsigned long long>(s.inserts[GpuShare::kTexture]),
                 static_cast<unsigned long long>(s.freed), static_cast<unsigned long long>(s.raced),
                 static_cast<unsigned long long>(g_refused.load(std::memory_order_relaxed)),
                 static_cast<double>(g_hashNs.load(std::memory_order_relaxed)) / 1e6);
}

}  // namespace ShareGpuData
}  // namespace VideoMetal

// ---- the backend-neutral interface (share_gpu_data_backend.h) -------------------------------------
namespace ShareGpuDataBackend {

// Sharing needs nothing of the device: every Metal device supports it.
void Request(bool on) {
    VideoMetal::ShareGpuData::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[ShareGpuData] %s\n",
                 on ? "on: equal immutable buffers and textures share one GPU object" : "off");
}

bool Supported() {
    return true;
}

}  // namespace ShareGpuDataBackend

// ============================================================================
// src/ext/derived_cache/derived_cache_shared.cpp -- the exe's one derived-data cache (NOT RETAIL)
// ============================================================================
// derived_cache.h ("the exe's shared store") has the contract; docs/extensions/handins/COMPRESSAO.md.
// The folder is <Documents>\Banished\Cache, beside settings.cfg, from the same path code
// (IO::SettingsStore::DefaultPath over Platform::DocumentsDir: the platform twins decide where the
// documents folder is, nothing here does). Exe only: the unit build drives Store directly.
// ============================================================================
#include "ext/derived_cache/derived_cache.h"

#include "ext/registry.h"
#include "io/settings_store.h"

#include <cstdio>
#include <mutex>

namespace Ext {
namespace DerivedCache {

namespace {

std::mutex g_mutex;
Store*     g_store       = nullptr;   // made once, never destroyed (its writer runs until exit)
bool       g_tried       = false;
bool       g_clearPending = false;

}  // namespace

bool CacheDir(std::string& out) {
    std::string settings;
    if (!IO::SettingsStore::DefaultPath(settings)) return false;
    const size_t slash = settings.find_last_of("\\/");
    if (slash == std::string::npos) return false;
    out = settings.substr(0, slash + 1) + "Cache";
    return true;
}

Store* OpenShared() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_tried) return g_store;
    g_tried = true;
    if (CurrentProfile() == Profile::Faithful) return nullptr;   // the faithful profile reads and writes nothing
    std::string dir;
    if (!CacheDir(dir)) {
        std::fprintf(stderr, "[DerivedCache] the documents folder is unknown: no cache this run\n");
        return nullptr;
    }
    Store* s = new Store();
    const uint64_t cap = static_cast<uint64_t>(Val().derived_cache_mb) << 20;
    if (s->Open(dir, cap) != Store::OpenStatus::Opened) {
        delete s;
        return nullptr;
    }
    g_store = s;
    return g_store;
}

Store* Shared() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_store;
}

void ClearShared() {
    std::string dir;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store != nullptr) g_store->Disable();
    g_clearPending = true;
    if (CacheDir(dir) && !RequestClear(dir))
        std::fprintf(stderr, "[DerivedCache] the clear request could not be written in %s\n", dir.c_str());
    std::fprintf(stderr, "[DerivedCache] clear requested: the cache is deleted at the next start\n");
}

bool SharedClearPending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_clearPending) return true;
    std::string dir;
    return CacheDir(dir) && ClearRequested(dir);
}

uint64_t SharedBytes() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_clearPending) return 0;
    if (g_store != nullptr) return g_store->BytesOnDisk();
    std::string dir;
    return CacheDir(dir) ? BytesInDir(dir) : 0;
}

}  // namespace DerivedCache
}  // namespace Ext

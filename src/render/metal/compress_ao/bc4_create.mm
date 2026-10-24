// ============================================================================
// src/render/metal/compress_ao/bc4_create.mm -- ext.compress_ao, the Metal side (NOT RETAIL)
// ============================================================================
// bc4_create.h has the contract; docs/extensions/handins/COMPRESSAO.md. Compiled with ARC.
// ============================================================================
#include "compress_ao/bc4_create.h"

#include "VideoMetalData.h"
#include "compress_ao/bc4_chain.h"
#include "compress_ao/compress_ao_backend.h"

#include <atomic>
#include <cstdio>

namespace VideoMetal {
namespace CompressAo {

namespace {
std::atomic<bool> g_requested{false};
}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

}  // namespace CompressAo
}  // namespace VideoMetal

// ---- the backend-neutral interface (compress_ao_backend.h) ----------------------------------------
namespace CompressAoBackend {

void Request(bool on, Ext::DerivedCache::Store* cache) {
    if (on && !Supported()) {
        on = false;
    }
    ::CompressAo::SetCache(on ? cache : nullptr);
    VideoMetal::CompressAo::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[CompressAo] %s (derived cache %s)\n", on ? "on: R8 AO maps load as BC4" : "off",
                 on && cache != nullptr ? "on" : "off");
}

// BC4 (MTLPixelFormatBC4_RUnorm) needs BC texture compression on the live device. Asked at boot
// (Ext::CompressAo::Boot runs after Engine::Startup made the device); the reason for a "no" is logged once.
bool Supported() {
    static std::atomic<bool> logged{false};
    id<MTLDevice> device = VideoMetal::LiveDevice();
    const char* why = nullptr;
    if (device == nil) {
        why = "no live Metal device";
    } else if (@available(macOS 11.0, *)) {
        if (![device supportsBCTextureCompression]) {
            why = "the GPU has no BC texture compression";
        }
    } else {
        why = "BC texture compression needs macOS 11";
    }
    if (why == nullptr) {
        return true;
    }
    if (!logged.exchange(true)) {
        std::fprintf(stderr, "[CompressAo] Metal: BC4 not supported (%s)\n", why);
    }
    return false;
}

}  // namespace CompressAoBackend

// ============================================================================
// src/ext/host_codecs.cpp -- the shared hosts' save-tail codecs, one line each (savetail.h)
// ============================================================================
// NOT RETAIL. DESIGN.md "Host records" (ruled 2026-10-21). A host record is the per-town state of a shared
// surface, presentation only, never in the lock set. Only the hosts of DESIGN.md's Surfaces table are listed.
// ============================================================================

#include "ext/savetail.h"

#include "ext/toolbar/window_state.h"   // Toolbar::WindowCodec()

#include <cstring>

namespace Ext {
namespace SaveTail {
namespace {

const HostCodec* const* Hosts(int& n) {
    static const HostCodec* const kHosts[] = { &Toolbar::WindowCodec() };
    n = static_cast<int>(sizeof(kHosts) / sizeof(kHosts[0]));
    return kHosts;
}

}  // namespace

const HostCodec* HostCodecAt(int index) {
    int n = 0;
    const HostCodec* const* hosts = Hosts(n);
    if (index < 0 || index >= n) {
        return nullptr;
    }
    return hosts[index];
}

const HostCodec* FindHostCodec(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    int n = 0;
    const HostCodec* const* hosts = Hosts(n);
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(hosts[i]->id, id) == 0) {
            return hosts[i];
        }
    }
    return nullptr;
}

void ResetAllHostState() {
    int n = 0;
    const HostCodec* const* hosts = Hosts(n);
    for (int i = 0; i < n; ++i) {
        hosts[i]->reset();
    }
}

}  // namespace SaveTail
}  // namespace Ext

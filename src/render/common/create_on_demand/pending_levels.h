#pragma once
// ============================================================================
// src/render/common/create_on_demand/pending_levels.h -- ext.create_on_demand: a texture's skipped
// level bytes, between the engine's level reader and the backend's create
// (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. Video::ImageBuffer::PlatformSerialize reads an image's level blobs and then calls its
// Create with them. With the row on, the extension's level reader (Video::ImageBuffer::
// _extLevelSource, src/ext/create_on_demand/) reads each level's size as retail does but skips the
// bytes, leaving one Locator per level here, on the loading thread, for that image; the backend's
// Create (reached from the same call, on the same thread) takes them and records the deferred create.
// A Create that does not take them (an image whose Create returns early) leaves them to the next
// image's reader, which replaces them.
// ============================================================================

#include <vector>

#include "create_on_demand/payload.h"

namespace CreateOnDemand {

struct PendingLevels {
    const void*          image = nullptr;   // the Video::ImageBuffer whose Create may take them
    std::vector<Locator> levels;            // one per level blob, in order

    void Clear() {
        image = nullptr;
        levels.clear();
    }
};

// The calling thread's.
inline PendingLevels& ThreadPendingLevels() {
    static thread_local PendingLevels pending;
    return pending;
}

}  // namespace CreateOnDemand

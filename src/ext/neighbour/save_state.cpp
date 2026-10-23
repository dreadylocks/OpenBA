// ============================================================================
// src/ext/neighbour/save_state.cpp -- NOT RETAIL. ext.neighbour's save-tail codec (save_state.h)
// ============================================================================

#include "ext/neighbour/save_state.h"

#include "ext/savetail.h"

namespace Neighbour {

std::mutex& SavedMutex() {
    static std::mutex mu;
    return mu;
}

SavedState& Saved() {
    static SavedState state;
    return state;
}

bool& LoadedFromSave() {
    static bool loaded = false;
    return loaded;
}

namespace {

void Encode(std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(SavedMutex());
    const std::vector<uint8_t> bytes = EncodeState(Saved());
    payload.insert(payload.end(), bytes.begin(), bytes.end());
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    // The save tail already refused a NEWER version; DecodeState reads every older one.
    SavedState st;
    if (!DecodeState(savedVersion, std::vector<uint8_t>(bytes, bytes + size), st)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(SavedMutex());
    Saved() = st;
    LoadedFromSave() = true;
    return true;
}

void Reset() {
    std::lock_guard<std::mutex> lock(SavedMutex());
    Saved() = SavedState();
    LoadedFromSave() = false;
}

}  // namespace

const Ext::SaveTail::StateCodec& Codec() {
    static const Ext::SaveTail::StateCodec codec = { "neighbour", &Encode, &Decode, &Reset };
    return codec;
}

}  // namespace Neighbour

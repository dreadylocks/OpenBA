// ============================================================================
// src/render/common/create_on_demand/use_tracker.cpp -- ext.create_on_demand's use stamps
// (docs/extensions/handins/CREATEONDEMAND.md section 7). The contract is in use_tracker.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "create_on_demand/use_tracker.h"

namespace CreateOnDemand {

UseStamps::UseStamps() : _slots(new Slot[kSlots]) {}

bool UseStamps::Insert(const void* obj, uint32_t epoch) {
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);   // CAST-OK: a pointer used as a hash key
    uint32_t i = Home(key);
    int64_t reuse = -1;
    for (uint32_t n = 0; n < kMaxProbe; ++n) {
        const uintptr_t k = _slots[i].key.load(std::memory_order_relaxed);
        if (k == key) {
            _slots[i].epoch.store(epoch, std::memory_order_relaxed);
            return true;
        }
        if (k == kTomb && reuse < 0) reuse = i;
        if (k == kEmpty) {
            if (reuse < 0) reuse = i;
            break;
        }
        i = (i + 1) & (kSlots - 1);
    }
    if (reuse < 0) return false;
    // The epoch first: a Touch that sees the key sees a meaningful stamp.
    _slots[reuse].epoch.store(epoch, std::memory_order_relaxed);
    _slots[reuse].key.store(key, std::memory_order_release);
    return true;
}

void UseStamps::Remove(const void* obj) {
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);   // CAST-OK: a pointer used as a hash key
    uint32_t i = Home(key);
    for (uint32_t n = 0; n < kMaxProbe; ++n) {
        const uintptr_t k = _slots[i].key.load(std::memory_order_relaxed);
        if (k == key) {
            _slots[i].key.store(kTomb, std::memory_order_release);
            return;
        }
        if (k == kEmpty) return;
        i = (i + 1) & (kSlots - 1);
    }
}

bool UseStamps::LastUse(const void* obj, uint32_t* epoch) const {
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);   // CAST-OK: a pointer used as a hash key
    uint32_t i = Home(key);
    for (uint32_t n = 0; n < kMaxProbe; ++n) {
        const uintptr_t k = _slots[i].key.load(std::memory_order_acquire);
        if (k == key) {
            *epoch = _slots[i].epoch.load(std::memory_order_relaxed);
            return true;
        }
        if (k == kEmpty) return false;
        i = (i + 1) & (kSlots - 1);
    }
    return false;
}

}  // namespace CreateOnDemand

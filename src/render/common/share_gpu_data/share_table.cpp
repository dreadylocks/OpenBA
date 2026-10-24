// ============================================================================
// src/render/common/share_gpu_data/share_table.cpp -- NOT RETAIL. ext.share_gpu_data's table
// ============================================================================
// share_table.h has the contract (docs/extensions/handins/SHAREGPUDATA.md).
// ============================================================================

#include "share_gpu_data/share_table.h"

#include <cstring>

namespace GpuShare {

bool Key::operator==(const Key& o) const {
    return device == o.device && kind == o.kind && descSize == o.descSize && payloadBytes == o.payloadBytes &&
           payloadHash == o.payloadHash && std::memcmp(desc, o.desc, descSize) == 0;
}

bool Table::Acquire(const Key& key, void* out[kHandles]) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byKey_.find(key);
    if (it == byKey_.end()) return false;
    ++it->second.users;
    for (int i = 0; i < kHandles; ++i) {
        out[i] = it->second.handles[i];
    }
    if (key.kind < kKindCount) {
        ++stats_.hits[key.kind];
        stats_.hitBytes[key.kind] += key.payloadBytes;
    }
    return true;
}

bool Table::Insert(const Key& key, void* const handles[kHandles]) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handles[0] == nullptr || byKey_.find(key) != byKey_.end() ||
        byResource_.find(handles[0]) != byResource_.end()) {
        ++stats_.raced;
        return false;
    }
    Entry e;
    for (int i = 0; i < kHandles; ++i) {
        e.handles[i] = handles[i];
    }
    e.users = 1;
    byKey_.emplace(key, e);
    byResource_.emplace(handles[0], key);
    size_.store(byKey_.size(), std::memory_order_release);
    if (key.kind < kKindCount) {
        ++stats_.inserts[key.kind];
        stats_.insertBytes[key.kind] += key.payloadBytes;
    }
    return true;
}

bool Table::Drop(const void* resource, void* out[kHandles]) {
    if (resource == nullptr || size_.load(std::memory_order_acquire) == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    auto r = byResource_.find(resource);
    if (r == byResource_.end()) return false;
    auto it = byKey_.find(r->second);
    if (--it->second.users > 0) return false;
    for (int i = 0; i < kHandles; ++i) {
        out[i] = it->second.handles[i];
    }
    byKey_.erase(it);
    byResource_.erase(r);
    size_.store(byKey_.size(), std::memory_order_release);
    ++stats_.freed;
    return true;
}

void Table::Clear(std::vector<void*>* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : byKey_) {
        for (int i = 0; i < kHandles; ++i) {
            if (kv.second.handles[i] != nullptr) out->push_back(kv.second.handles[i]);
        }
    }
    byKey_.clear();
    byResource_.clear();
    size_.store(0, std::memory_order_release);
}

Stats Table::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    s.live  = byKey_.size();
    return s;
}

}  // namespace GpuShare

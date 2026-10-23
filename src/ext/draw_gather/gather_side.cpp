// ============================================================================
// src/ext/draw_gather/gather_side.cpp -- ext.draw_gather's side array (pure)
// ============================================================================
// NOT RETAIL. gather_side.h has the contract. Standard C++ only.
// ============================================================================
#include "ext/draw_gather/gather_side.h"

#include <cstring>

namespace Ext {
namespace DrawGather {

void SideArray::Store(int32_t id, const void* owner, const float box[6], uint32_t flags,
                      int32_t detail) {
    if (id < 0) return;
    const size_t need = static_cast<size_t>(id) + 1;
    if (_owner.size() < need) {
        // Grow geometrically; the new slots are invalid (owner null).
        size_t cap = _owner.size() < 1024 ? 1024 : _owner.size();
        while (cap < need) cap *= 2;
        _owner.resize(cap, nullptr);
        _rec.resize(cap, SideRecord{});
    }
    SideRecord& r = _rec[static_cast<size_t>(id)];
    std::memcpy(r.box, box, sizeof(r.box));
    r.flagsHint  = flags;
    r.detailHint = detail;
    _owner[static_cast<size_t>(id)] = owner;
}

void SideArray::Reserve(int32_t n) {
    if (n <= 0) return;
    const size_t need = static_cast<size_t>(n);
    if (_owner.size() < need) {
        // Store's growth rule, so a reserved array has the size Store would give it.
        size_t cap = _owner.size() < 1024 ? 1024 : _owner.size();
        while (cap < need) cap *= 2;
        _owner.resize(cap, nullptr);
        _rec.resize(cap, SideRecord{});
    }
}

void SideArray::Clear() {
    std::vector<SideRecord>().swap(_rec);
    std::vector<const void*>().swap(_owner);
}

SideArray* SceneSides::Find(const void* scene) {
    for (int i = 0; i < _n; ++i)
        if (_slots[i].scene == scene) { _slots[i].lastUse = ++_clock; return &_slots[i].side; }
    return nullptr;
}

SideArray& SceneSides::Acquire(const void* scene) {
    if (SideArray* s = Find(scene)) return *s;
    int pick;
    if (_n < kMaxScenes) {
        pick = _n++;
    } else {
        pick = 0;
        for (int i = 1; i < _n; ++i)
            if (_slots[i].lastUse < _slots[pick].lastUse) pick = i;
        _slots[pick].side.Clear();
    }
    _slots[pick].scene   = scene;
    _slots[pick].lastUse = ++_clock;
    return _slots[pick].side;
}

void SceneSides::ForgetAll() {
    for (int i = 0; i < _n; ++i) {
        _slots[i].side.Clear();
        _slots[i].scene = nullptr;
    }
    _n = 0;
}

}  // namespace DrawGather
}  // namespace Ext

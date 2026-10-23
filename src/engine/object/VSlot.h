// ============================================================================
// src/engine/object/VSlot.h — the compiler's vtable slot of a virtual member
// ============================================================================
//
// For the few places that must look INTO a compiler vtable (diagnostics that
// print or compare a slot's address, harnesses that patch one): the slot is
// taken from the member, never written as a literal. A literal is retail's
// index, and the compiler's index of a class with `virtual ~T()` is one higher
// (GCC emits two destructor slots where MSVC emits one — B3X_RULES X3).
//
// Dispatch never uses this: a call is `p->Member(...)`.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>

namespace Object {

// Itanium pointer-to-member-function: { ptr, adj }. A virtual member stores
// 1 + the byte offset of its slot in `ptr`. Returns -1 for a non-virtual or
// this-adjusting member.
template <typename PMF>
inline int32_t CompilerSlotOf(PMF pmf) {
    static_assert(sizeof(PMF) == 2 * sizeof(uintptr_t), "Itanium PMF is two words");
    uintptr_t words[2];
    std::memcpy(words, &pmf, sizeof words);
#if defined(__aarch64__)
    // The AArch64 C++ ABI (Apple and Linux alike) puts the virtual discriminator in adj's low bit.
    if (words[1] != 1) return -1; // virtual, with zero this-adjustment
    return static_cast<int32_t>(words[0] / sizeof(void*));
#else
    if ((words[0] & 1u) == 0) return -1;
    if (words[1] != 0) return -1;
    return static_cast<int32_t>((words[0] - 1) / sizeof(void*));
#endif
}

// The address stored in `obj`'s table for `pmf` (what a diagnostic prints).
template <typename PMF>
inline void* SlotAddressOf(const void* obj, PMF pmf) {
    const int32_t slot = CompilerSlotOf(pmf);
    if (slot < 0) return nullptr;
    return (*reinterpret_cast<void** const*>(obj))[slot];  // CAST-OK: vslot
}

}  // namespace Object

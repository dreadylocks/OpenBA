// ============================================================================
// src/engine/object/factory_dtors.cpp — the three Object factory destructors
//                                        (Wave 11, lane OBJLIFE)
// ============================================================================
//
//   ??1Factory@Object@@QEAA@XZ          retail 0x1800071a0 -> body 0x180008110
//   ??1InternalFactory@Object@@QEAA@XZ  retail 0x18002f860
//   ??1ExternalFactory@Object@@QEAA@XZ  retail 0x1800be760
//
// All three are `QEAA` (non-virtual) in the shipped export table — checked
// against Runtime-x64.dll's own export directory, not assumed, because the
// wrong spelling resolves NULL silently:
//
//     0x1800071a0 ??1Factory@Object@@QEAA@XZ
//     0x18002f860 ??1InternalFactory@Object@@QEAA@XZ
//     0x1800be760 ??1ExternalFactory@Object@@QEAA@XZ
//
// ----------------------------------------------------------------------------
// THE CHECK, PORTED BEFORE THE CODE
// ----------------------------------------------------------------------------
// The lane brief warned that a dtor which only frees its block instead of
// destroying every element is a leak shipped under a false byte-identity
// claim. That is not a hypothetical for this row set — ONE of these three
// really does iterate and destroy, and the other two really do only free:
//
//   ??1Factory          ITERATES AND DESTROYS. 0x180008110 walks the whole
//                       TypeNode tree (0x180008250 recurses left +0x38 then
//                       right +0x40), destroys each node's
//                       `value.children` Array (node+0x20) and frees the node.
//                       A port that only zeroed `root` would leak the entire
//                       type registry — every TypeNode and every child array.
//
//   ??1InternalFactory  ONLY FREES. Two Array buffers, no element destruction:
//                       `_objects` holds NON-OWNING Internal* (each Internal
//                       removes itself in ~Internal), `_persistIds` holds
//                       int32. Destroying elements here would be a double
//                       free, not a fix.
//
//   ??1ExternalFactory  BOTH, per member. `_pathStack` destroys its elements
//                       (two Strings each) then frees the buffer;
//                       `_references` and `_resources` walk their trees and
//                       release ONE String per node.
//
// Each verdict below is written from the disassembly at the address named on
// the function, not transcribed from another lane's recipe.
//
// ----------------------------------------------------------------------------
// WHAT THIS FILE DOES *NOT* CLAIM
// ----------------------------------------------------------------------------
// *** THE ONLY CALLER OF ALL THREE, IN OUR WHOLE TREE, IS
//     `RtEngine_Engine_Shutdown` (src/engine/engine/engine_lifecycle.cpp),
//     AND THAT BODY ITSELF HAS calls=0. *** game_main.cpp runs its own
// `ShutdownEngineStaged()` instead and never calls the real Engine::Shutdown.
// So no gameplay run this wave can execute a single line below, and the
// `calls=` column for these three addresses will read 0 for the reason
// "nothing in the tree calls the caller" — which is one of the four causes,
// stated so nobody has to guess which (reference_uncalled_port_is_not_verified,
// reference_calls_zero_can_mean_always_took_the_uncovered_branch).
//
// "No route" is not "no oracle", so the verification here is a SYNTHESISED
// differential run — see factory_dtors_probe.cpp. What it can and cannot
// reach is stated there rather than implied by a green line.
//
// ----------------------------------------------------------------------------
// HELPERS REACHED THROUGH THE PORT-AWARE ADDRESS DOOR
// ----------------------------------------------------------------------------
// Four of the callees below are already ported and REGISTERED by other lanes.
// They are reached with `ResolveAddr(Module::Runtime, …)`, which consults the
// port registry FIRST (reference_resolveaddr_consults_port_registry_first), so
// these are our bodies at run time and stay ours at cutover. They are NOT
// re-registered here: a duplicate registration is last-binding-wins and would
// silently make one of the two bodies dead code
// (reference_grep_for_the_port_before_writing_it).
//
//   0x1800093e0  Collection::Array<T*>::~Array   owner src/engine/collection/avl_trees.cpp
//   0x180009c30  SetNodeFree, arg 1 IGNORED      owner src/engine/system/allocator.cpp
//   0x18001c750  Collection::Array<T>::~Array    owner src/engine/collection/array.cpp
//   0x18002fb70  Collection::Array<T>::~Array    owner src/engine/collection/array.cpp
//                (the SEVENTEENTH J1 address — added there by this lane, with
//                 the byte proof in the comment at its J1_ENTRY)
//
// `SetNodeFree`'s FIRST PARAMETER IS IGNORED by the callee, and the two call
// sites in ~Factory pass whatever RCX last held. That is reproduced rather
// than tidied: a one-argument version would free the ignored `this`.
//
// No C++ exceptions anywhere in this file (PLAN.md §4.3 risk 5).
// ============================================================================

#include <cstdint>
#include <cstdio>

// *** INCLUDE ORDER IS LOAD-BEARING AND THAT IS A TRAP, NOT A STYLE CHOICE. ***
// src/engine/object/External.h #includes TypeInfo.h at its line 322, which
// is INSIDE its own `namespace RtObject { … }` block. So whichever of the two
// is seen first decides whether the reflection types are `RtObject::TypeNode`
// or `RtObject::RtObject::TypeNode` — the include guard makes the second
// inclusion a no-op, so the loser's spelling silently wins. TypeInfo.h is
// pulled in FIRST here to get the un-nested spelling. Reordering these three
// lines will not fail to compile; it will change what `TypeNode` means.
#include "object/TypeInfo.h"
#include "object/Internal.h"
#include "object/External.h"
#include "object/external_set.h"     // block PATHINDEX: rtresset::IndexClear
#include "system/string_internal.h"
#include "system/MemoryAllocator.h"   // B3R: rtalloc::FreeLockedIgnoringSelf (0x180009c30)

namespace RtObjFactoryDtor {

using RtObject::TypeNode;
using RtObject::ExternalFactory;
using RtObject::PathStackEntry;

// ---------------------------------------------------------------------------
// Local views. These are deliberately NOT added to the shared type headers:
// each is the two or three fields the destructor touches, and asserting a
// fuller layout here would be asserting something this lane did not measure.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The two already-ported callees, called by name (B3R RT-Core):
//   0x1800093e0  `Collection::Array<TypeRecord*>::~Array` over
//                `TypeNode::value.children` (B7: the real member; the free
//                `rtavl::PtrArrayDtor` over the raw triple is gone), called
//                explicitly because the node is freed raw, as retail does.
//   0x180009c30  rtalloc::FreeLockedIgnoringSelf -- arg 1 IS IGNORED by the
//                callee, and kept for exactly that reason.
// ---------------------------------------------------------------------------

// ENDGAME B.3.2 step 2 — `ArrayDtor_18001c750` and `ArrayDtor_18002fb70`
// (0x18001c750, 0x18002fb70) are GONE: their one caller, RtObject_
// InternalFactory_Dtor below, now calls `.~Array()` directly on the two real
// `Collection::Array<T>` members. Both rows stay registered in array.cpp for
// the rest of the tree.

// ============================================================================
// 0x180008250 — the recursive half of ~Factory's tree teardown.
// ============================================================================
//
//     if (!node) return;                            ; TEST RDX,RDX / JZ
//     TypeSubtreeFree(this, node->left);            ; [RDX+0x38]
//     TypeSubtreeFree(this, node->right);           ; [RBX+0x40]
//     ~Array<TypeRecord*>(&node->value.children);   ; LEA RCX,[RBX+8] / ADD RCX,0x18
//     SetNodeFree(this, node);                      ; 0x180009c30
//
// `LEA RCX,[RBX+8]` then `ADD RCX,0x18` is node+0x20, which is
// `TypeNode::value.children` — the Array<TypeRecord*> whose three fields are
// TypeRecord::{children, childCount, childCapacity}. The two-step LEA is MSVC
// materialising `&node->value` for the EH funclet's spill slot and then
// stepping to the member; it is one address, not two objects.
//
// *** THIS IS THE "ITERATES AND DESTROYS" HALF. *** Every node's child array
// is freed before the node is. Collapsing this to `root = nullptr` would leak
// one TypeNode plus one heap buffer per registered type.
//
// RECURSION is retail's, and its depth is the AVL tree's height over the
// registered type count (a few hundred types => under 20 frames). An explicit
// stack would be a different program with a different overflow point.
// ============================================================================
void TypeSubtreeFree(void* set, ::Object::Factory::TypeSet::Node* node) {
    if (node == nullptr)
        return;
    TypeSubtreeFree(set, node->left);
    TypeSubtreeFree(set, node->right);
    node->element.record.children.~Array();                // node + 0x20, 0x1800093e0
    rtalloc::FreeLockedIgnoringSelf(set, node);
}

// ============================================================================
// ??1Factory@Object@@QEAA@XZ   retail 0x1800071a0, body 0x180008110
// ============================================================================
//
// 0x1800071a0 is an MSVC EH-frame thunk — it spills RCX, writes the -2 unwind
// state slot, and tail-jumps to 0x180008110. The frame slots have no effect on
// the object, so the thunk is not reproduced as a separate function; this body
// IS 0x180008110 and both addresses are registered onto it.
//
// 0x180008110 is the ROOT case of TypeSubtreeFree, open-coded: identical
// instructions with the leading null test moved to a branch that still runs
// the two closing stores.
//
//     root = this->root;
//     if (root) {
//         TypeSubtreeFree(root->left);      ; [RDI+0x38], `this` in RCX
//         TypeSubtreeFree(this, root->right);
//         ~Array<TypeRecord*>(&root->value.children);
//         SetNodeFree(this, root);
//     }
//     this->root  = nullptr;               ; qword
//     this->count = 0;                     ; DWORD — not a second qword
//
// The `MOV [RBX+8],EAX` is a 32-bit store. Widening it to a qword would clear
// four bytes retail leaves alone, and this lane did not measure what is at
// +0x0c.
//
// NOTE ON THE FIRST RECURSIVE CALL: retail's `CALL 0x180008250` at 0x180008138
// is reached with RCX still holding `this` from the prologue, and the second
// with an explicit `MOV RCX,RBX`. Both therefore pass `this`, which is the
// argument SetNodeFree ignores anyway.
// ============================================================================
void RtObject_Factory_Dtor(::Object::Factory* self) {
    // `~Factory` writes exactly the Set header's two fields at the end —
    // `MOV [RBX],RAX` (qword) and `MOV [RBX+8],EAX` (DWORD, not a qword).
    ::Object::Factory::TypeSet& s = self->_types;
    ::Object::Factory::TypeSet::Node* root = s._root;
    if (root != nullptr) {
        TypeSubtreeFree(self, root->left);
        TypeSubtreeFree(self, root->right);
        root->element.record.children.~Array();                // root + 0x20, 0x1800093e0
        rtalloc::FreeLockedIgnoringSelf(self, root);
    }
    s._root  = nullptr;
    s._count = 0;
}

// ============================================================================
// ?Shutdown@Factory@Object@@QEAAXXZ   retail 0x1800c0600
// ============================================================================
//
// THE WHOLE BODY, read off the disassembly (Runtime-x64.dll, image base
// 0x180000000, 2026-09-18) — eight instructions:
//
//     1800c0600  SUB  RSP,0x28
//     1800c0604  MOV  RCX,qword ptr [0x1801fc6d0]
//     1800c060b  TEST RCX,RCX
//     1800c060e  JZ   0x1800c0616
//     1800c0610  CALL qword ptr [0x18014b118]        ; PTR_FreeLibrary
//     1800c0616  MOV  qword ptr [0x1801fc6d0],0x0
//     1800c0621  ADD  RSP,0x28
//     1800c0625  RET
//
// *** IT IS NOT THE TYPE-TREE WALK. *** B33_RULES §9 step 7 describes
// `Factory::Shutdown` as "0x1800071a0 -> 0x180008110 walks the whole TypeNode
// tree"; that is `??1Factory@Object@@`, ported above as `RtObject_Factory_Dtor`,
// a different function at a different address. `Shutdown` never reads `root`,
// `count` or `_singleton` — it releases the COMPILER PLUGIN module, the same
// unexported HMODULE at 0x1801fc6d0 that `?UnloadCompilers@Factory@Object@@`
// (0x1800bfad4/0x1800bfae6) reads and writes, and that `Engine::Shutdown`
// (0x1800db62d) releases with the identical three-step sequence. So porting it
// is NOT a prerequisite for moving `?_singleton@Factory@Object@@` off the mint:
// that cell is untouched here. Recorded rather than quietly worked around.
//
// `this` IS IGNORED by the body (RCX is overwritten at 0x1800c0604 before any
// read), which is why the shim takes a void* it never uses.
//
// THE ONE STANDALONE DIFFERENCE, stated the way engine_lifecycle.cpp:813-821
// states the identical one on the identical global: 0x1801fc6d0 is UNEXPORTED
// and, with no DLL mapped, our side has no such variable unless the mint's
// address-only row for it is registered. `ResolveAddr` then hands back 0 and the
// step is skipped. That is a real environment difference, not a guard over a
// faithful fault — retail always has its own .data here and we may have nothing
// at all — and it is spelled exactly as its sibling spells it.
// ============================================================================
// The body and the member definition are at GLOBAL scope, below this file's
// `namespace RtObjFactoryDtor` block — see `RtObject_Factory_Shutdown` there.

// ============================================================================
// ??1InternalFactory@Object@@QEAA@XZ   retail 0x18002f860
// ============================================================================
//
//     ~Array<int32>(this + 0x10);        ; CALL 0x18001c750   (_persistIds)
//     ~Array<Internal*>(this + 0x00);    ; JMP  0x18002fb70   (_objects)
//
// That is the whole body. Reverse declaration order over
// `{ objects@+0x00, persistIds@+0x10, random@+0x20 }`, with `random` trivially
// destructible — matching src/engine/object/Internal.h's asserted layout
// exactly.
//
// *** THIS IS AN "ONLY FREES" DTOR AND THAT IS CORRECT. *** `_objects` holds
// `Internal*` that it does NOT own: every Internal registers itself in
// `??0Internal` and removes itself by swap-remove in `??1Internal`, so by the
// time the factory dies the array is either empty or holds objects some other
// owner will destroy. Adding a "destroy each element" loop here — the tidy
// symmetry a reviewer might ask for — would be a double free on every live
// object, not a leak fix.
//
// 0x18002fb70 is the SEVENTEENTH member of the J1 `Array<T>::~Array` family.
// That is a measured claim: normalising both bodies' RIP displacements and
// rel32s to their ABSOLUTE targets leaves 29 of 29 instructions identical to
// the canonical 0x18001c750, with the only textual difference an INTERNAL
// forward branch whose encoded displacement (0x36) is the same byte on both
// sides (reference_position_only_diffs_hide_one_body). Both resolve the same
// four external operands: 0x1801fc718, 0x18014b390, 0x1800c6c00, 0x18014b388.
// ============================================================================
void RtObject_InternalFactory_Dtor(::Object::InternalFactory* self) {
    // ENDGAME B.3.2 step 2 — both calls are now direct member destructors:
    // `InternalFactory::persistIds` (Array<int32_t>) and `::objects`
    // (Array<Internal*>) are already real `Collection::Array<T>` members
    // (src/engine/object/Internal.h), and both 0x18001c750 and 0x18002fb70
    // are J1 bodies (trivial destroy), which a trivially destructible
    // `int32_t`/`Internal*` element's trait-derived `~Array()` IS byte for
    // byte. Reverse declaration order is kept: persistIds (+0x10) before
    // objects (+0x00), matching the banner above.
    self->GetPersistIds().~Array();   // retail 0x18001c750
    self->GetObjects().~Array();      // retail 0x18002fb70
}

// ============================================================================
// 0x18002e8d0 — destroy a run of PathStackEntry.
// ============================================================================
//
//     if (n <= 0) return;                    ; TEST R8D,R8D / JLE
//     for (i = 0; i < n; ++i, p += 0x10) {
//         Release(&p->filename);             ; LEA RAX,[RBX+8]  — SECOND first
//         Release(&p->directory);            ; [RBX]
//     }
//
// Reverse member order within the element, which is what a compiler-generated
// `~PathStackEntry` emits over `{ Filename directory; Filename filename; }`.
// The stride is 0x10, pinned independently by both path-stack accessors'
// `SHL RDX,0x4` (src/engine/object/External.h).
//
// The release idiom at both slots is byte-for-byte `RtString_ReleaseInline`
// (src/engine/system/string.cpp) including its asymmetry: on the freeing
// branch the slot is left holding `data - 8`, on the sharing branch it is
// nulled. Retail's extra `JE` after `ADD [RAX],-8` — which skips the free when
// `data - 8` is zero — is subsumed by MemFree's own null test, which lives
// inside 0x1800c7ad0's first instruction.
//
// `n` is passed as the array's `_size`, so the elements beyond `_size` up to
// `_capacity` are NOT released. That is retail's contract and this keeps it.
//
// ARG 1 IS IGNORED (retail spills RCX and never reads it again); kept in the
// signature for the same reason SetNodeFree's is.
// ============================================================================
void PathStackDestroyRange(void* /*ignoredThis*/, PathStackEntry* p, int n) {
    if (n <= 0)
        return;
    for (int i = 0; i < n; ++i, ++p) {
        // ENDGAME B.3.1 step 2: retail's inlined release IS `~Filename`
        // (0x180006980), asymmetry included: ~PathStackEntry runs it on
        // +0x08 (filename) BEFORE +0x00 (directory), which is retail's order.
        p->~PathStackEntry();
    }
}

// ============================================================================
// 0x18002e9e0 — ~Array<PathStackEntry>.
// ============================================================================
//
//     DestroyRange(this, this->_data, this->_size);   ; 0x18002e8d0
//     <the canonical J1 free, open-coded>             ; identical to 0x18001c750
//
// The second half is the J1 body inlined rather than called, with the same
// four absolute operands. It is written out here rather than routed through
// the J1 registry because retail inlined it: routing it would add an indirect
// call retail does not make, and the J1 addresses are per-ADDRESS diff sites.
//
// *** THIS IS THE "DESTROYS ITS ELEMENTS" MEMBER of ~ExternalFactory. ***
// ============================================================================
void PathStackArrayDtor(RtObject::PathStackArray* a) {
    PathStackDestroyRange(a, a->_data, a->_size);
    RtSystem::RtMemFree(a->_data);   // retail's `if (p)` lives inside 0x1800c7ad0
    a->_data     = nullptr;
    a->_size     = 0;
    a->_capacity = 0;
}

// ============================================================================
// 0x18002ebf0 — free the `_references` subtree.
// ============================================================================
//
//     if (!node) return;
//     ReferenceSubtreeFree(this, node->left);    ; [RDX+0x18]
//     ReferenceSubtreeFree(this, node->right);   ; [RBX+0x20]
//     Release(node + 0x08);                      ; the SECOND element slot
//     SetNodeFree(this, node);                   ; 0x180009c30
//
// *** THE SLOT RELEASED HERE IS +0x08, NOT +0x00, AND THAT IS THE WHOLE
//     DIFFERENCE FROM ITS SIBLING. *** avl_trees.cpp's `SubtreeFree`
// (0x180008340) is the same 0x30-node body releasing +0x00 and explicitly NOT
// +0x08, with a comment warning that a "tidy" symmetric release of the second
// slot would corrupt the heap. This instantiation is the mirror image: its
// String is the value, not the key. Releasing both slots in either body would
// be a heap corruption, and releasing the wrong one is a leak plus a wild
// decrement. They are separate functions in retail for exactly this reason and
// are kept separate here.
// ============================================================================
void ReferenceSubtreeFree(void* set, RtObject::ReferenceSet::Node* node) {
    if (node == nullptr)
        return;
    ReferenceSubtreeFree(set, node->left);    // +0x18
    ReferenceSubtreeFree(set, node->right);   // +0x20
    // ~Filename (0x180006980) over `ReferenceEntry::path` (+0x08).
    node->element.path.~Filename();
    rtalloc::FreeLockedIgnoringSelf(set, node);
}

// ============================================================================
// 0x18002eea0 — free the `_resources` subtree.
// ============================================================================
//
//     if (!node) return;
//     ResourceSubtreeFree(this, node->left);     ; [RDX+0x20]
//     ResourceSubtreeFree(this, node->right);    ; [RBX+0x28]
//     Release(node + 0x00);                      ; the FIRST element slot
//     SetNodeFree(this, node);                   ; 0x180009c30
//
// A LARGER NODE than the two above: its children live at +0x20/+0x28 rather
// than +0x18/+0x20, so its element pair is 0x20 bytes wide. The released slot
// is +0x00 — the key String — and nothing at +0x08..+0x1f is touched.
//
// The three redundant `MOV [RSP+0x48],RBX` stores at 0x18002eed9-0x18002eee3
// are the EH funclet's spill slot being re-written for three consecutive
// destruction states. They have no effect on the object and are not
// reproduced.
// ============================================================================
void ResourceSubtreeFree(void* set, RtObject::ResourceSet::Node* node) {
    if (node == nullptr)
        return;
    ResourceSubtreeFree(set, node->left);     // +0x20
    ResourceSubtreeFree(set, node->right);    // +0x28
    // ~Filename over `ResourceEntry::path`, the key at +0x00.
    node->element.path.~Filename();
    rtalloc::FreeLockedIgnoringSelf(set, node);
}

// ============================================================================
// ??1ExternalFactory@Object@@QEAA@XZ   retail 0x1800be760
// ============================================================================
//
//     ~Array<PathStackEntry>(this + 0x30);          ; CALL 0x18002e9e0
//     ReferenceSubtreeFree(this+0x20, [this+0x20]); ; CALL 0x18002ebf0
//     this->_references.root  = nullptr;            ; MOV [RBX],RSI
//     this->_references.count = 0;                  ; MOV [RBX+8],ESI  (DWORD)
//     ResourceSubtreeFree(this+0x10, [this+0x10]);  ; CALL 0x18002eea0
//     this->_resources.root   = nullptr;
//     this->_resources.count  = 0;                  ; DWORD
//
// Reverse declaration order over `{ _resources@+0x10, _references@+0x20,
// _pathStack@+0x30 }` — exactly the layout External.h asserts. Note there is
// NO vptr store: ExternalFactory is not polymorphic, which is also why its
// export is `QEAA` and not `UEAA`.
//
// THE `this` PASSED TO EACH SUBTREE FREE IS THE SET, NOT THE FACTORY: retail
// loads `RCX = RBX` where RBX is `this+0x20` / `this+0x10`. It reaches
// SetNodeFree, which ignores it — but a port that passed the factory would be
// relying on that, and this one does not.
//
// The two closing store PAIRS are qword-then-DWORD, like ~Factory's. Nothing
// at +0x0c of either Set header is written.
// ============================================================================
void RtObject_ExternalFactory_Dtor(::Object::ExternalFactory* self) {
    ExternalFactory* f = self;

    PathStackArrayDtor(&f->_pathStack);                           // this + 0x30

    RtObject::ReferenceSet& refs = f->_references;               // this + 0x20
    ReferenceSubtreeFree(&refs, refs._root);
    refs._root  = nullptr;
    refs._count = 0;

    RtObject::ResourceSet& res = f->_resources;                  // this + 0x10
    rtresset::IndexClear(&res);   // NOT RETAIL (block PATHINDEX): its nodes go next
    ResourceSubtreeFree(&res, res._root);
    res._root  = nullptr;
    res._count = 0;
}

}  // namespace RtObjFactoryDtor

// ============================================================================
// ?Shutdown@Factory@Object@@QEAAXXZ — the body, at GLOBAL scope
// ============================================================================
// See the banner beside ~Factory above for the disassembly and for why this is
// NOT the type-tree walk. FreeLibrary is Platform::ReleaseLibrary (platform_gfx.h, Phase
// D.4 SDL-5), the same seam Engine::Shutdown's identical step uses.
#include "platform_gfx.h"

// Runtime 0x1801fc6d0 — Object::Factory's compiler-plugin HMODULE, the class
// static (ENDGAME B.3.9 OBJ, B39_RULES D7; TypeInfo.h). The by-address mint row
// is redirected onto it in RtObjectPortInit. The mint never zeroes or re-seeds
// it; the language zero-initialises it.
void* Object::Factory::_compilerModule = nullptr;

extern "C" void RtObject_Factory_Shutdown(void* /*self — RCX is overwritten*/) {
    void** slot = &::Object::Factory::_compilerModule;
    if (*slot) Platform::ReleaseLibrary(*slot);
    *slot = nullptr;
}

namespace Object {
void Factory::Shutdown() { RtObject_Factory_Shutdown(this); }
}  // namespace Object

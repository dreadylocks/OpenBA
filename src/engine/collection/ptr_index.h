#pragma once
// ============================================================================
// src/engine/collection/ptr_index.h -- a pointer-keyed index beside a retail
//                                      tree: which node holds this object?
//                                      (block VISITPASS, NOT RETAIL, exact)
// ============================================================================
//
// WHY. `ExternalFactory::_resources` is keyed by path, and every row holds the
// `External*` it names. The resource-reference visit pass (ExternalPtr<T>::
// Serialize under a VisitorStream, external_visit.cpp) starts FROM the object
// and needs its row: retail gets there by rebuilding the object's path
// (`GetPath`, `ExpandRelativePath`, the private `Find`, `Add`'s own descent).
// This index answers "the row whose `external` is this pointer" with one hash
// of the pointer, and carries one flag per row that external_visit.cpp owns.
//
// THE CLAIM: `Find(ext)` returns the one node of `tree` whose element holds
// `ext`, or null when there is none, or declines. A node's `external` is
// written once, by the pair constructor at insert (rtresset::PairCtor), and
// never again; so the set of (node, external) pairs changes only where nodes
// are linked (InsertCore) and unlinked (Erase), and both call in here at that
// point, as fold_index.h's hooks do. Two live nodes holding the same pointer
// would make "the row" ambiguous: a second insert of a pointer marks the index
// broken and it declines everything from then on (the caller walks).
//
// WHAT KEEPS IT IN STEP. Bound to the first tree it is filled from; `Find`
// declines unless the tree's own node count equals the index's count (plus the
// rows it did not index: a null `external`), so a mutation that went round the
// hooks is a decline, never a stale answer. `Clear` is for a tree whose nodes
// are freed wholesale (~ExternalFactory).
//
// THREADS. Written where its tree is written, read where its tree is read: the
// exclusion the tree already has, no more.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Collection {

class PtrIndex {
public:
    PtrIndex() = default;
    ~PtrIndex() { delete[] _slots; }
    PtrIndex(const PtrIndex&) = delete;
    PtrIndex& operator=(const PtrIndex&) = delete;

    // A node holding `key` was linked into `tree`. A first insert binds the index.
    void Insert(const void* tree, const void* node, const void* key) {
        if (node == nullptr) {
            _broken = true;   // the node allocation failed; the tree's count still moved
            return;
        }
        if (_tree == nullptr)
            _tree = tree;
        if (_tree != tree) {
            _broken = true;   // a second tree: this index answers for neither
            return;
        }
        if (key == nullptr) {
            ++_nullKeys;
            return;
        }
        if ((_used + 1) * 2 > _cap)
            Grow();
        size_t i = Home(key);
        while (_slots[i].node != nullptr) {
            if (_slots[i].key == key) {
                ++_duplicates;
                _broken = true;   // two rows hold one object: "the row" is ambiguous
            }
            i = (i + 1) & (_cap - 1);
        }
        _slots[i].key = key;
        _slots[i].node = node;
        _slots[i].flag = false;
        ++_used;
    }

    // The node holding `key` is about to be unlinked from `tree`.
    void Erase(const void* tree, const void* node, const void* key) {
        if (tree != _tree)
            return;
        if (key == nullptr) {
            --_nullKeys;
            return;
        }
        if (_cap == 0) {
            _broken = true;
            return;
        }
        size_t i = Home(key);
        while (_slots[i].node != node || _slots[i].key != key) {
            if (_slots[i].node == nullptr) {
                _broken = true;   // not here: an insert went round the hooks
                return;
            }
            i = (i + 1) & (_cap - 1);
        }
        // Backward-shift deletion (linear probing): pull every later entry of
        // the run that may sit at or before the hole into it.
        size_t hole = i;
        for (size_t j = (hole + 1) & (_cap - 1); _slots[j].node != nullptr; j = (j + 1) & (_cap - 1)) {
            const size_t home = Home(_slots[j].key);
            if (((j - home) & (_cap - 1)) >= ((j - hole) & (_cap - 1))) {
                _slots[hole] = _slots[j];
                hole = j;
            }
        }
        _slots[hole] = Slot{};
        --_used;
    }

    // Every node of `tree` was freed (or `tree` is about to be refilled from
    // empty). Unbinds the index.
    void Clear(const void* tree) {
        if (tree != _tree && _tree != nullptr)
            return;
        for (size_t i = 0; i < _cap; ++i)
            _slots[i] = Slot{};
        _used = 0;
        _nullKeys = 0;
        _duplicates = 0;
        _broken = false;
        _tree = nullptr;
    }

    // True when the index can answer for `tree` (holding `treeCount` nodes) and
    // `key`: `*node` is then the node holding `key` (null: none does) and `*flag`
    // that row's flag (false when there is no row).
    bool Find(const void* tree, int64_t treeCount, const void* key, const void** node, bool* flag) const {
        *node = nullptr;
        *flag = false;
        if (key == nullptr || _broken)
            return false;
        if (tree != _tree) {
            // An empty tree the index was never bound to: no row holds anything.
            return _tree == nullptr && treeCount == 0;
        }
        if (static_cast<int64_t>(_used) + static_cast<int64_t>(_nullKeys) != treeCount)
            return false;
        if (_cap == 0)
            return true;
        const Slot* s = Lookup(key);
        if (s != nullptr) {
            *node = s->node;
            *flag = s->flag;
        }
        return true;
    }

    // Sets the flag on the row holding `key`. False when there is no such row
    // (or the index is broken).
    bool SetFlag(const void* tree, const void* key, bool value) {
        if (key == nullptr || _broken || tree != _tree || _cap == 0)
            return false;
        Slot* s = const_cast<Slot*>(Lookup(key));   // CAST-OK: own storage
        if (s == nullptr)
            return false;
        s->flag = value;
        return true;
    }

    size_t   Size() const       { return _used; }
    bool     Broken() const     { return _broken; }
    uint64_t Duplicates() const { return _duplicates; }

private:
    struct Slot {
        const void* key  = nullptr;
        const void* node = nullptr;
        bool        flag = false;
    };

    static uint64_t Hash(const void* key) {
        uint64_t h = reinterpret_cast<uintptr_t>(key);   // CAST-OK: the address is the key
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ull;
        return h ^ (h >> 33);
    }

    size_t Home(const void* key) const { return static_cast<size_t>(Hash(key)) & (_cap - 1); }

    const Slot* Lookup(const void* key) const {
        const size_t mask = _cap - 1;
        for (size_t i = Home(key); _slots[i].node != nullptr; i = (i + 1) & mask) {
            if (_slots[i].key == key)
                return &_slots[i];
        }
        return nullptr;
    }

    void Grow() {
        const size_t oldCap = _cap;
        Slot* old = _slots;
        _cap = oldCap == 0 ? 1024 : oldCap * 2;
        _slots = new Slot[_cap];
        for (size_t i = 0; i < oldCap; ++i) {
            if (old[i].node == nullptr)
                continue;
            size_t j = Home(old[i].key);
            while (_slots[j].node != nullptr)
                j = (j + 1) & (_cap - 1);
            _slots[j] = old[i];
        }
        delete[] old;
    }

    Slot*       _slots      = nullptr;
    size_t      _cap        = 0;
    size_t      _used       = 0;
    size_t      _nullKeys   = 0;
    uint64_t    _duplicates = 0;
    bool        _broken     = false;
    const void* _tree       = nullptr;
};

}  // namespace Collection

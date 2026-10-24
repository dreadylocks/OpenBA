#pragma once
// ============================================================================
// src/engine/collection/fold_index.h -- a case-folded hash index beside a
//                                       retail path tree (block PATHINDEX,
//                                       NOT RETAIL, exact)
// ============================================================================
//
// WHY. Two retail AVL trees are searched by path, case-insensitively, many
// thousand times per load: `ExternalFactory::_resources` (the private `Find`,
// `Add`'s inlined descent, `FindNext`'s re-find) and `FileSystem::_packfileIndex`
// (`ExistsInPackfile`, `GetPackfileId`, `OpenFile`, LoadPackages' `IndexFind`).
// Each descent is ~17 `_stricmp` calls over long paths that share their first
// 20-40 characters. This index answers the same question with one hash and one
// compare. The trees stay exactly as they are: every insert, erase, rotation and
// in-order walk is retail's, so anything that iterates in key order (the save's
// reference table numbers resources in in-order key order) is untouched.
//
// THE CLAIM: for a non-null key, the descent returns the node whose key is
// fold-equal to it, or null when there is none. All six descents compare with
// retail's `_stricmp` in the "C" locale (rtcrt::AsciiStrICmp / StriCmpAscii):
// unsigned bytes, 'A'..'Z' lowered, nothing else. That is a total preorder whose
// equivalence classes are "equal after lowering 'A'..'Z'", and each tree is a
// binary search tree under it, because every node was placed by an insert
// descending with the same comparator and an insert that meets a fold-equal key
// inserts nothing (rtresset InsertCore 0x18002cf36; LoadPackages inserts only
// after `IndexFind` missed). Rotations and erase splices keep the in-order
// sequence. So a tree never holds two fold-equal keys, a descent for K reaches
// the one node fold-equal to K when it exists (every node on the way orders
// strictly before or after K, so the descent never leaves the subtree holding
// it), and returns null otherwise. The index maps "fold(key)" to that node: the
// same answer.
//
// NULL KEYS. A tree node with a null key is ordered below everything, and a
// null search key has walk-specific arms (OpenFile's walk STOPS on a null node
// key, unlike the others). The index answers neither: a null search key, or any
// null-keyed node in the tree, makes `Find` decline, and the caller walks.
//
// WHAT KEEPS IT IN STEP. The tree's own mutators call `Insert` / `Erase` /
// `Clear` at the point they link, unlink or free a node, and the index is bound
// to the one tree header it was filled from. `Find` declines unless the tree's
// own count equals the index's, so a mutation that went round the hooks shows
// up as a decline (the walk), never as a stale node. Node addresses are stable:
// the trees move links, never elements, between nodes.
//
// THREADS. The index is written only where its tree is written, and read where
// its tree is read, so it needs exactly the exclusion the tree already has.
//
// The debug exe (OPENBA_PATHINDEX_CHECK) runs the walk behind every answer the
// index gives, returns the walk's, and prints one `[PathIndex] MISMATCH` line
// per difference (`Check` below).
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The debug exe walks the tree behind every answer the index gives and reports
// a difference (and returns the walk's answer).
#if defined(_DEBUG)
#define OPENBA_PATHINDEX_CHECK 1
#else
#define OPENBA_PATHINDEX_CHECK 0
#endif

namespace Collection {

class FoldIndex {
public:
    // Reads a node's key (nullptr when the node's key is null).
    using KeyOf = const char* (*)(const void* node);

    explicit FoldIndex(KeyOf keyOf) : _keyOf(keyOf) {}
    ~FoldIndex() { delete[] _slots; }
    FoldIndex(const FoldIndex&) = delete;
    FoldIndex& operator=(const FoldIndex&) = delete;

    // 'A'..'Z' lowered, every other byte as it is: `_stricmp`'s "C" locale fold.
    static inline unsigned char Fold(unsigned char c) {
        return static_cast<unsigned char>(c - 'A') <= 'Z' - 'A' ? static_cast<unsigned char>(c + 32) : c;
    }

    // `Fold` on eight bytes at once: bit 7 of `upper` is set in each byte that is
    // 'A'..'Z' (its low seven bits in range, its high bit clear), and 0x80 >> 2
    // is the 0x20 that lowers it. No byte's sum carries into the next.
    static uint64_t FoldWord(uint64_t w) {
        const uint64_t ones = 0x0101010101010101ull;
        const uint64_t low7 = w & (0x7f * ones);
        const uint64_t atLeastA = low7 + (0x80 - 'A') * ones;
        const uint64_t pastZ = low7 + (0x80 - 'Z' - 1) * ones;
        const uint64_t upper = atLeastA & ~pastZ & ~w & (0x80 * ones);
        return w | (upper >> 2);
    }

    // Equal for fold-equal keys: both fold to the same bytes, read in the same words.
    static uint64_t Hash(const char* key) {
        const size_t n = std::strlen(key);
        uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
        size_t i = 0;
        for (; i + 8 <= n; i += 8) {
            uint64_t w;
            std::memcpy(&w, key + i, 8);
            h = (h ^ FoldWord(w)) * 0xff51afd7ed558ccdull;
            h ^= h >> 32;
        }
        uint64_t tail = 0;
        for (int shift = 0; i < n; ++i, shift += 8)
            tail |= static_cast<uint64_t>(Fold(static_cast<unsigned char>(key[i]))) << shift;
        h = (h ^ tail) * 0xc4ceb9fe1a85ec53ull;
        return h ^ (h >> 29);
    }

    static bool FoldEqual(const char* a, const char* b) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(a);  // CAST-OK: bytes
        const unsigned char* q = reinterpret_cast<const unsigned char*>(b);  // CAST-OK: bytes
        for (;;) {
            const unsigned char c = Fold(*p++);
            if (c != Fold(*q++))
                return false;
            if (c == 0)
                return true;
        }
    }

    // A node was linked into `tree`. A first insert binds the index to `tree`.
    void Insert(const void* tree, const void* node) {
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
        const char* key = _keyOf(node);
        if (key == nullptr) {
            ++_nullKeys;
            return;
        }
        if ((_used + 1) * 2 > _cap)
            Grow();
        const uint64_t h = Hash(key);
        size_t i = static_cast<size_t>(h) & (_cap - 1);
        while (_slots[i].node != nullptr) {
            if (_slots[i].hash == h && FoldEqual(_keyOf(_slots[i].node), key))
                _broken = true;   // two fold-equal keys: the claim above does not hold
            i = (i + 1) & (_cap - 1);
        }
        _slots[i].hash = h;
        _slots[i].node = node;
        ++_used;
    }

    // A node is about to be unlinked from `tree` (its key still readable).
    void Erase(const void* tree, const void* node) {
        if (tree != _tree)
            return;
        const char* key = _keyOf(node);
        if (key == nullptr) {
            --_nullKeys;
            return;
        }
        if (_cap == 0) {
            _broken = true;
            return;
        }
        size_t i = static_cast<size_t>(Hash(key)) & (_cap - 1);
        while (_slots[i].node != node) {
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
            const size_t home = static_cast<size_t>(_slots[j].hash) & (_cap - 1);
            if (((j - home) & (_cap - 1)) >= ((j - hole) & (_cap - 1))) {
                _slots[hole] = _slots[j];
                hole = j;
            }
        }
        _slots[hole].node = nullptr;
        _slots[hole].hash = 0;
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
        _broken = false;
        _tree = nullptr;
    }

    // True when the index can answer for `tree` (holding `treeCount` nodes) and
    // `key`; `*node` is then the walk's answer.
    bool Find(const void* tree, int64_t treeCount, const char* key, const void** node) const {
        if (key == nullptr || _broken || _nullKeys != 0)
            return false;
        if (tree != _tree) {
            // An empty tree the index was never bound to: the walk's answer is null.
            if (_tree == nullptr && treeCount == 0) {
                *node = nullptr;
                return true;
            }
            return false;
        }
        if (static_cast<int64_t>(_used) != treeCount)
            return false;
        if (_cap == 0) {
            *node = nullptr;
            return true;
        }
        const uint64_t h = Hash(key);
        const size_t mask = _cap - 1;
        for (size_t i = static_cast<size_t>(h) & mask; _slots[i].node != nullptr; i = (i + 1) & mask) {
            if (_slots[i].hash == h && FoldEqual(_keyOf(_slots[i].node), key)) {
                *node = _slots[i].node;
                return true;
            }
        }
        *node = nullptr;
        return true;
    }

    // The check (OPENBA_PATHINDEX_CHECK builds only): `walked` is the descent's
    // answer. One line per difference; a running count of the answers compared
    // at every power of two from 4096, so a clean log is not a silent one.
    static void Check(const char* which, const char* key, bool answered, const void* indexed,
                      const void* walked) {
        if (!OPENBA_PATHINDEX_CHECK || !answered)
            return;
        static std::atomic<uint64_t> checked{0};
        static std::atomic<uint64_t> differ{0};
        if (indexed != walked) {
            differ.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[PathIndex] MISMATCH %s key=\"%s\" index=%p walk=%p\n", which, key, indexed,
                         walked);
            std::fflush(stderr);
        }
        const uint64_t n = checked.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n >= 4096 && (n & (n - 1)) == 0) {
            std::fprintf(stderr, "[PathIndex] checked %llu answers, %llu differ\n",
                         static_cast<unsigned long long>(n),
                         static_cast<unsigned long long>(differ.load(std::memory_order_relaxed)));
            std::fflush(stderr);
        }
    }

    size_t Size() const { return _used; }
    bool Broken() const { return _broken; }

private:
    struct Slot {
        uint64_t    hash = 0;
        const void* node = nullptr;
    };

    void Grow() {
        const size_t ncap = _cap == 0 ? 1024 : _cap * 2;
        Slot* const ns = new Slot[ncap];
        for (size_t i = 0; i < _cap; ++i) {
            if (_slots[i].node == nullptr)
                continue;
            size_t j = static_cast<size_t>(_slots[i].hash) & (ncap - 1);
            while (ns[j].node != nullptr)
                j = (j + 1) & (ncap - 1);
            ns[j] = _slots[i];
        }
        delete[] _slots;
        _slots = ns;
        _cap = ncap;
    }

    KeyOf       _keyOf;
    const void* _tree     = nullptr;
    Slot*       _slots    = nullptr;
    size_t      _cap      = 0;
    size_t      _used     = 0;
    int64_t     _nullKeys = 0;
    bool        _broken   = false;
};

}  // namespace Collection

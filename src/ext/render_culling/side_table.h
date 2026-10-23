#pragma once
// ============================================================================
// src/ext/render_culling/side_table.h -- [PerfX PERFXTBL -- NON-RETAIL side table, IDENTICAL decisions]
// docs/extensions/handins/PERFXTBL.md
//
// PerfX (render_culling.cpp) decides per model per speed iteration whether the
// intermediate root-only MeshInstance::Update may be folded into the last one
// (ANIM_BATCH), and per particle system per iteration whether it is off-screen
// (PARTICLE_CULL). Both decisions used to start with a probe of an open-addressing
// hash table keyed on a heap pointer: the per-frame batch count (`BatchHash`,
// keyed on the model) and the per-frame visibility verdict (CachedVisible, keyed on
// the position's address). This header holds:
//
//   * BatchHash -- that batch table, byte for byte the old BatchEntry (same hash,
//     same 64 linear probes, same claim rule), plus a find-only probe.
//   * Side -- one Slot per entity of the TREE_SNAPSHOT walk array, in the same
//     order. The walk publishes the index of the entity it is ticking, so a tick
//     reaches its slot with one compare; any other caller binary-searches the
//     (pointer-sorted) snapshot. A slot holds the batch count of ONE model of that
//     entity for the current sim frame, and the visibility verdict of ONE position
//     key for the current Update frame.
//
// The batch count's home: for every model and frame there is exactly one
// authoritative count -- the slot of the model's entity when that slot is live
// for (model, frame), otherwise the BatchHash entry (or 0 when there is none).
// Every way a live slot can stop being reachable hands its count to the hash
// first (Spill): the snapshot is rebuilt without the entity, or unsorted; the
// slot is claimed by another model of the same entity. Claiming a slot pulls the
// count from the hash. So the count a model sees is the count of the ticks it
// skipped this frame, whichever path its calls took -- the old table's answer.
//
// The visibility verdict is a cache OF the old cache: a slot miss calls the
// caller's `compute` (CachedVisible: the old probe, verdict and store) and keeps
// its answer for the rest of the Update frame. See the hand-in for where the two
// can differ (an old-table eviction followed by a moved position).
//
// Header-only and keyed on `const void*` so the host unit tests drive it with
// plain pointers (tests/unit/ext_render_culling_test.cpp).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>   // std::less: the pointer order of the update tree
#include <vector>

namespace Ext {
namespace RenderCulling {
namespace SideTable {

// ---- the old per-frame batch table (render_culling.cpp BatchEntry, moved) ----
struct BatchSlot { const void* key; unsigned serial; unsigned skipped; };

template <unsigned N>   // a power of two
struct BatchHash {
    static_assert((N & (N - 1)) == 0, "BatchHash size must be a power of two");
    BatchSlot tbl[N];

    static unsigned Hash(const void* key) {
        return (unsigned)(((reinterpret_cast<uintptr_t>(key) >> 4) * 2654435761u) & (N - 1));  // CAST-OK: hash
    }
    // Find-or-claim, exactly the old BatchEntry: the entry of `key` for `serial`, or the
    // first slot of the probe chain not live for `serial`, claimed with a zero count.
    // nullptr when 64 probes find neither (never at sane model counts).
    BatchSlot* Entry(const void* key, unsigned serial) {
        unsigned h = Hash(key);
        for (int probe = 0; probe < 64; ++probe) {
            BatchSlot& e = tbl[h];
            if (e.serial == serial && e.key == key) return &e;
            if (e.serial != serial) { e.key = key; e.serial = serial; e.skipped = 0; return &e; }
            h = (h + 1) & (N - 1);   // linear probe: a live entry of another model
        }
        return nullptr;
    }
    // Find only. Entry claims the FIRST non-live slot of the chain, so a live entry of
    // `key` can only sit before the first non-live slot: stopping there is exact.
    BatchSlot* Find(const void* key, unsigned serial) {
        unsigned h = Hash(key);
        for (int probe = 0; probe < 64; ++probe) {
            BatchSlot& e = tbl[h];
            if (e.serial != serial) return nullptr;
            if (e.key == key) return &e;
            h = (h + 1) & (N - 1);
        }
        return nullptr;
    }
};

// ---- the per-entity side table -------------------------------------------------------
struct Slot {
    const void* entity;        // snapshot entity this slot belongs to
    const void* model;         // batch: the model whose count this is (nullptr = none)
    unsigned    batchSerial;   // batch: the sim frame of the count
    unsigned    skipped;       // batch: intermediate updates skipped this frame
    const void* visKey;        // visibility: the position address the verdict is for
    unsigned    visSerial;     // visibility: the Update frame of the verdict
    uint8_t     vis;           // visibility: the verdict
    uint8_t     pad[3];
};
static_assert(sizeof(Slot) == 40, "SideTable::Slot is 40 bytes");

template <class Hash>
class Side {
public:
    explicit Side(Hash& hash) : m_hash(hash) {}

    bool   Usable() const { return m_usable; }
    size_t Size() const { return m_slots.size(); }

    // The walk snapshot was rebuilt (`snap`, `n`; `sorted` = strictly ascending by
    // pointer, TickList::StrictlyAscending). Live batch counts of entities still in the
    // snapshot follow them to their new index; the others go to the hash. An unsorted
    // snapshot (or a failed rebuild: n = 0, sorted = false) leaves the side unusable
    // and every live count in the hash. Visibility verdicts follow their entity too.
    template <class Ent>
    void Rebuild(Ent* const* snap, size_t n, bool sorted, unsigned batchSerial) {
        m_prev.swap(m_slots);
        m_slots.clear();
        if (sorted) {
            m_slots.resize(n, Slot{nullptr, nullptr, 0, 0, nullptr, 0, 0, {0, 0, 0}});
            for (size_t i = 0; i < n; ++i) m_slots[i].entity = snap[i];
        }
        const std::less<const void*> lt;
        size_t j = 0;
        for (const Slot& o : m_prev) {   // ascending when it was usable (else empty)
            if (sorted) {
                while (j < n && lt(m_slots[j].entity, o.entity)) ++j;
                if (j < n && m_slots[j].entity == o.entity) { m_slots[j] = o; continue; }
            }
            if (o.model && o.batchSerial == batchSerial) Spill(o);
        }
        m_prev.clear();
        m_usable = sorted;
    }

    // The slot of `entity`, or nullptr when it has none (side unusable, or the entity is
    // not in the snapshot). `hint` = the walk's current index (-1 outside the walk).
    Slot* SlotFor(const void* entity, int64_t hint) {
        if (!m_usable) return nullptr;
        const size_t n = m_slots.size();
        if (hint >= 0 && (size_t)hint < n && m_slots[(size_t)hint].entity == entity)
            return &m_slots[(size_t)hint];
        const std::less<const void*> lt;
        size_t lo = 0, hi = n;
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (lt(m_slots[mid].entity, entity)) lo = mid + 1;
            else hi = mid;
        }
        return (lo < n && m_slots[lo].entity == entity) ? &m_slots[lo] : nullptr;
    }

    // The batch count of `model` for sim frame `serial` (see the banner for its home).
    // nullptr only when the hash is saturated, as the old BatchEntry.
    unsigned* Batch(Slot* s, const void* model, unsigned serial) {
        if (!s) {
            BatchSlot* e = m_hash.Entry(model, serial);
            return e ? &e->skipped : nullptr;
        }
        if (s->model == model && s->batchSerial == serial) return &s->skipped;   // the hot path
        if (s->model && s->batchSerial == serial) Spill(*s);   // another model of this entity
        const BatchSlot* o = m_hash.Find(model, serial);
        s->model = model;
        s->batchSerial = serial;
        s->skipped = o ? o->skipped : 0;
        return &s->skipped;
    }

    // The visibility verdict of `key` for Update frame `frame`: the slot's when it holds
    // that key for that frame, else compute() (the old cache), remembered in the slot.
    template <class F>
    static bool Visible(Slot* s, const void* key, unsigned frame, F&& compute) {
        if (s && s->visKey == key && s->visSerial == frame) return s->vis != 0;
        const bool v = compute();
        if (s) { s->visKey = key; s->visSerial = frame; s->vis = v ? 1 : 0; }
        return v;
    }

private:
    void Spill(const Slot& o) {
        BatchSlot* e = m_hash.Entry(o.model, o.batchSerial);
        if (e) e->skipped = o.skipped;
    }

    Hash&             m_hash;
    std::vector<Slot> m_slots;
    std::vector<Slot> m_prev;
    bool              m_usable = false;
};

}  // namespace SideTable
}  // namespace RenderCulling
}  // namespace Ext

// src/ext/drain_split/drain_split.cpp -- NOT RETAIL. ext.drain_split (class B): the owed
// AssignCitizens' bookkeeping. See the header and docs/extensions/handins/HITCH_DRAINSPLIT.md.
#include "ext/drain_split/drain_split.h"
#include "ext/registry.h"   // Ext::Ext().drain_split, Ext::LockEpoch

namespace Ext {
namespace DrainSplit {

bool g_on = false;
bool g_pending = false;

namespace {
uint64_t s_frame = 0;            // BeginFrame count
const void* s_owner = nullptr;   // the PathGraph that owes
uint32_t s_epoch = 0;            // Ext::LockEpoch() at the drain
uint64_t s_owedFrame = 0;        // s_frame at the drain
int s_waited = 0;                // Take calls that waited
Stats s_stats;

void Clear() {
    g_pending = false;
    s_owner = nullptr;
}
}  // namespace

void BeginFrame() {
    ++s_frame;
    g_on = ::Ext::Ext().drain_split;
}

bool Owe(const void* pg) {
    if (g_pending) {
        ++s_stats.overlaps;
        return false;
    }
    g_pending = true;
    s_owner = pg;
    s_epoch = ::Ext::LockEpoch();
    s_owedFrame = s_frame;
    s_waited = 0;
    ++s_stats.splits;
    return true;
}

Due Take(const void* pg, bool pathBlockRuns) {
    if (!g_pending) return Due::Wait;
    if (s_epoch != ::Ext::LockEpoch() || s_owner != pg) {
        ++s_stats.dropped;
        Clear();
        return Due::Drop;
    }
    const bool nextFrame = s_frame != s_owedFrame;
    if (!nextFrame && !pathBlockRuns) {
        ++s_waited;
        return Due::Wait;
    }
    // The run's tick is the (s_waited + 1)-th CitizenBehavior::Update after the drain's.
    const int gap = s_waited + 1;
    ++s_stats.runs;
    if (!nextFrame) ++s_stats.runsForced;
    s_stats.gapTicks += gap;
    if (gap > s_stats.maxGapTicks) s_stats.maxGapTicks = gap;
    Clear();
    return Due::Run;
}

const Stats& GetStats() { return s_stats; }
Stats& MutableStats() { return s_stats; }
void ResetStats() { s_stats = Stats(); }

}  // namespace DrainSplit
}  // namespace Ext

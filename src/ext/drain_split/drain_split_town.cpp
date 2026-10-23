// src/ext/drain_split/drain_split_town.cpp -- NOT RETAIL. ext.drain_split (class B): the game side
// of the owed AssignCitizens -- the drain seam's DeferAssign and the tick seam's RunDue, with the
// stress harness's gap count. See drain_split.h and docs/extensions/handins/HITCH_DRAINSPLIT.md.
#include "ext/drain_split/drain_split.h"

#include "Entity/CitizenBehavior.h"       // PathGraph, CitizenBehavior::citizens
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/AttendanceComponent.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"          // GameUpdateLoop::_singleton->m_citizenList
#include "diag/stress_harness.h"          // g_pfProfOn, CompProf_Now, g_pathGraphCost, the drain line
#include "diag/err_line.h"

#include <unordered_map>
#include <vector>

namespace Ext {
namespace DrainSplit {

namespace {

// The harness's view of one citizen after ReassignWorkers: only pointer values are kept and
// compared (a citizen gone in the gap is never dereferenced).
struct Seen {
    const void* home;
    const void* work;
    const void* prof;
    bool idle;
};

// AssignCitizens' idle test (citizen_assign_citizens.cpp IsIdleHealthy): adult, no school.
bool Idle(CitizenComponent* c) {
    return !(c->description->adultAge > c->age) && c->school.Get() == nullptr;
}

Seen Look(CitizenComponent* c) {
    return Seen{ c->home.Get(), c->workplace.Get(), c->profession, Idle(c) };
}

std::unordered_map<const CitizenComponent*, Seen> s_seen;   // harness only
int s_pairs = 0;
long long s_reassignTicks = 0;

void Snapshot() {
    s_seen.clear();
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    const int n = town->citizens._size;
    s_seen.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        CitizenComponent* c = town->citizens._data[i].Get();
        s_seen.emplace(c, Look(c));
    }
    MutableStats().citizensAtDrain += n;
}

void CompareGap() {
    Stats& st = MutableStats();
    CitizenBehavior* town = GameUpdateLoop::_singleton->m_citizenList;
    const int n = town->citizens._size;
    long long still = 0, home = 0, work = 0, prof = 0, idle = 0, any = 0;
    for (int i = 0; i < n; ++i) {
        CitizenComponent* c = town->citizens._data[i].Get();
        auto it = s_seen.find(c);
        if (it == s_seen.end()) { ++st.born; continue; }
        ++still;
        const Seen now = Look(c);
        const Seen& was = it->second;
        const bool h = now.home != was.home, w = now.work != was.work,
                   p = now.prof != was.prof, d = now.idle != was.idle;
        home += h; work += w; prof += p; idle += d; any += (h || w || p || d);
    }
    st.gone += static_cast<long long>(s_seen.size()) - still;
    st.homeChanged += home; st.workChanged += work; st.profChanged += prof;
    st.idleChanged += idle; st.anyChanged += any;
    ErrLine("[Stress::drainsplit] owed half: citizens drain=%d run=%d gone=%lld changed=%lld "
            "(home %lld work %lld prof %lld idle %lld)\n",
            static_cast<int>(s_seen.size()), n, static_cast<long long>(s_seen.size()) - still,
            any, home, work, prof, idle);
    s_seen.clear();
}

}  // namespace

bool DeferAssign(PathGraph* pg, int pairs, long long reassignTicks) {
    if (!Owe(pg)) return false;
    s_pairs = pairs;
    s_reassignTicks = reassignTicks;
    if (g_pfProfOn) Snapshot();
    return true;
}

void RunDue(PathGraph* pg, bool pathBlockRuns) {
    const Due d = Take(pg, pathBlockRuns);
    if (d == Due::Drop) { s_seen.clear(); return; }
    if (d != Due::Run) return;
    const long long t0 = g_pfProfOn ? CompProf_Now() : 0;
    if (t0) CompareGap();
    const long long t1 = t0 ? CompProf_Now() : 0;
    pg->AssignCitizens();   // 0x1400d43b0: the drain's second half, one frame late
    if (t0) {
        const PathGraphCostCounts c = g_pathGraphCost;
        StressHarness_PathGraphDrain(s_pairs, s_reassignTicks, CompProf_Now() - t1,
                                     c.calls, c.hits, c.misses, c.direct, c.differ);
        g_pathGraphCost = PathGraphCostCounts{ 0, 0, 0, 0, -1 };
    }
}

}  // namespace DrainSplit
}  // namespace Ext

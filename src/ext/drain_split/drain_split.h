#pragma once
// ============================================================================
// src/ext/drain_split/drain_split.h -- NOT RETAIL. ext.drain_split, class B (locked for the game,
// pseudo-mod ext:drain_split, formatVersion 1). docs/extensions/handins/HITCH_DRAINSPLIT.md.
//
// THE HITCH. Every 300 sim-seconds CitizenBehavior::Update's path-graph branch drains the
// PathGraph (retail 0x1400d6f00's tail): in the tick where ProcessPaths finishes its last pair it
// runs PathGraph::ReassignWorkers (0x1400d62a0, the single-household residence consolidation)
// and then PathGraph::AssignCitizens (0x1400d43b0, the employment allocator), both in that tick.
// On the first drain of a game the consolidation's residence-to-residence searches are cold and
// the drain is ~5-6 ms of one frame at speed 50 (HITCH_PAIRSPREAD section 3, frame 749).
//
// THE EXTENSION. ReassignWorkers stays in the drain tick; AssignCitizens is OWED and runs in the
// first CitizenBehavior::Update of the NEXT FRAME, at the drain's own place in that Update (after
// CountResources / CheckThresholds, before the path-graph branch). Not the next sim tick: at
// speed N the next tick is in the same frame N-1 times out of N, so the frame would carry both
// halves again. At speed 1 the next frame IS the next tick.
//
// WHAT THE TWO HALVES SHARE. Nothing but the world: ReassignWorkers re-homes citizens and moves
// goods; AssignCitizens reads every input fresh (the town's citizen list, each citizen's home,
// school, workplace and profession, the profession -> workplace map, each workplace's roster)
// plus the PathGraph's region-pair cost map (+0x18) that ProcessPaths filled. Only ProcessPaths,
// BuildPathGraph and AssignCitizens' own RegionPairCost write that map, and BuildPathGraph (its
// CostClear) runs only from the path-graph branch -- so the owed half runs BEFORE that branch in
// any tick that would enter it (Take's pathBlockRuns). Whatever the sim changes in the gap (the
// citizens' own AI, deaths, births, the player's tools) AssignCitizens sees, as retail's drain
// sees the previous ticks' changes: it has no precondition a tick boundary can break.
//
// THE OWED HALF ACROSS GAMES. The PathGraph (its pairs, pending index and cost map) is not
// serialized; neither is the debt. A save written in the gap loads without it (that drain's
// AssignCitizens is skipped in the loaded game; the next drain runs one), as retail's load drops
// an unfinished ProcessPaths window. The debt is tagged with Ext::LockEpoch(): a new game, a load
// or the return to the menu changes the epoch, and a stale debt is DROPPED, never run in another
// world (a PathGraph at the same address included). As a class B row the toggle cannot change
// inside a game; the epoch also covers Install (the unit tests).
//
// SELECTION (DESIGN.md hook rules 3 and 4): BeginFrame reads the row once per frame. The drain
// seam tests g_on once per drain; the tick seam tests g_pending once per CitizenBehavior::Update,
// and nothing sets g_pending while the row is off.
// ============================================================================
#include <cstdint>

struct PathGraph;

namespace Ext {
namespace DrainSplit {

extern bool g_on;        // this frame: ext.drain_split (the game's locked value)
extern bool g_pending;   // an AssignCitizens is owed (only ever set while g_on)

// GameUpdateLoop::Update, once per frame beside Ext::SpreadWarmth::BeginFrame: the frame count the
// debt is due against, and g_on.
void BeginFrame();

// ---- The bookkeeping (no game headers; tests/unit/ext_drain_split_test.cpp) ----
// The drain tick, after ReassignWorkers: `pg` owes one AssignCitizens. Returns false (and owes
// nothing) if a debt was already pending: unreachable, since the owed half runs before any tick's
// path-graph branch; counted as `overlaps` if it ever happens, and the caller then runs
// AssignCitizens itself (retail's drain).
bool Owe(const void* pg);
enum class Due { Wait, Run, Drop };
// Each CitizenBehavior::Update while g_pending: Run = run AssignCitizens now (the frame has
// advanced, or the path-graph branch is about to run); Wait = not yet; Drop = the debt is stale
// (another game's epoch, or another PathGraph) and is forgotten. Run and Drop clear g_pending.
Due Take(const void* pg, bool pathBlockRuns);

// ---- The game side (drain_split_town.cpp, exe only) ----
// The ProcessPaths drain seam: owe AssignCitizens. Returns false when nothing was owed (an
// overlap: the caller runs AssignCitizens itself). `pairs` / `reassignTicks` are the drain line's
// (the [Stress::pathgraph] line is printed by the owed half when the harness runs).
bool DeferAssign(PathGraph* pg, int pairs, long long reassignTicks);
// The CitizenBehavior::Update seam (called only while g_pending).
void RunDue(PathGraph* pg, bool pathBlockRuns);

// Stress harness counters (cumulative; main thread).
struct Stats {
    long long splits = 0;          // drains whose AssignCitizens was owed
    long long runs = 0;            // owed halves run
    long long runsForced = 0;      //   ... of them in the drain's frame, the path-graph branch due
    long long dropped = 0;         // owed halves dropped (stale epoch / PathGraph)
    long long overlaps = 0;        // drains while a debt was pending (expected 0)
    int maxGapTicks = 0;           // most sim ticks from a drain to its owed half
    long long gapTicks = 0;        // their sum over all runs
    // What changed in the gaps (harness only: a citizen snapshot after ReassignWorkers, compared
    // just before the owed AssignCitizens).
    long long citizensAtDrain = 0; // citizens in the town at the drains, summed
    long long gone = 0;            // left the citizen list in a gap (death, ...)
    long long born = 0;            // joined it in a gap
    long long homeChanged = 0;     // a different home at the run than after ReassignWorkers
    long long workChanged = 0;     // a different workplace
    long long profChanged = 0;     // a different profession
    long long idleChanged = 0;     // AssignCitizens' idle test (adult, no school) flipped
    long long anyChanged = 0;      // citizens with at least one of the four
};
const Stats& GetStats();
Stats& MutableStats();   // the game side's gap counts
void ResetStats();

}  // namespace DrainSplit
}  // namespace Ext

#pragma once
// ============================================================================
// src/ext/spread_warmth/spread_warmth.h -- NOT RETAIL. ext.spread_warmth, class B (locked for
// the game, pseudo-mod ext:spread_warmth). docs/extensions/handins/HITCH_PFBURST.md.
//
// THE HITCH. CitizenAIComponent::State_ElderTransition (0x14010d2c0, the warmth state 1 / 0xf;
// "ElderTransition" is Ghidra's misnomer) runs, for a citizen whose coldness passed its
// description's _fec, CitizenBehavior::FindElderResidence (0x14008ec70): one Map::PathDistance
// from EVERY residence in the town to the citizen's cell, each a region A* unless the pair
// cache holds it. When a cold spell takes the town over the threshold, dozens of citizens
// cross it inside one frame's N sim iterations (speed N): at speed 50 in the 1180-citizen
// stress town that is ~12,000 PathDistance calls, ~4,700 of them searches, 9-21 ms in one
// frame. Retail does the same work (the port is faithful); it only shows at high speed, where
// one frame holds 50 ticks of decisions.
//
// THE EXTENSION. A per-frame budget on the scans' real cost. Each scan is charged
//   - its region searches: the pair-cache misses it caused, read as the advance of the cache's
//     ring index (RegionPairCache::Store advances `newest` by exactly one per new pair, and a
//     scan's PathDistance calls store every miss), and
//   - its candidates (the residence count: the hits and the per-candidate component reads).
// Once a frame has charged kMissBudget searches or kCandidateBudget candidates, a further scan
// that frame is DEFERRED: State_ElderTransition skips the scan AND the home fallback and takes
// retail's own failure tail -- the 3.75 game-second cooldown on states 1 / 0xf (kCooldown,
// DAT_140172190) -- so the citizen asks again one cooldown later, as retail does after a scan
// that found nothing. (A deferral with no cooldown would re-run TransitionState(1) on every
// tick of the frame.) The first scan of a frame always runs, so one scan may overshoot the
// budget by its own cost and a town always serves one citizen per frame. At speed 1 a frame
// is one tick and the budget binds only if several citizens turn cold on the same tick.
// (A candidate-only budget was tried first: search cost per candidate varies 2x between
// waves, so a capped frame still reached 5-6 ms; HITCH_PFBURST.md section 4.)
//
// WHY CLASS B. A deferred citizen goes for warmth one or more cooldowns later than retail,
// and meanwhile TransitionState goes on down its shuffled candidate list (other states may
// start, the shuffle draws the sim's random stream), so the game diverges from retail. At
// speed 50 one cooldown is ~4.5 frames, about 75 ms of real time; the harness reports the
// deferrals and the longest run of frames with one (HITCH_PFBURST.md section 5).
//
// SELECTION (DESIGN.md hook rules 3 and 4): BeginFrame reads the row once per frame (its
// per-game value under the class B lock); the seam in State_ElderTransition is one test of
// g_on on the cold path only, never in a hot loop. The faithful body is otherwise unchanged.
// ============================================================================
#include <cstdint>

namespace Ext {
namespace SpreadWarmth {

// Per frame: region searches (pair-cache misses) and residence candidates the scans may charge.
constexpr int kMissBudget = 512;
constexpr int kCandidateBudget = 4096;

extern bool g_on;   // this frame: ext.spread_warmth (the game's locked value)

// GameUpdateLoop::Update, once per frame beside Ext::WalkBatch::BeginFrame.
void BeginFrame();

// State_ElderTransition, just before FindElderResidence: true = defer this scan (the caller
// skips the scan and the home fallback and stamps the cooldown); false = scan, `candidates`
// charged.
bool DeferSlow(int candidates);
inline bool Defer(int candidates) { return g_on && DeferSlow(candidates); }

// After a scan that ran: charge its searches. ringAdvance = the pair cache's `newest` after
// minus before (wraps at ringSize).
void ChargeSearches(int ringAdvance, int ringSize);

// The scan's meter, around FindElderResidence: reads the cache's ring index before and after
// (Cache = RegionPairCache; a template so this header needs no game header). Inert while off.
template <class Cache>
struct ScanMeter {
    const Cache* cache;
    int newest0;
    explicit ScanMeter(const Cache* c) : cache(g_on ? c : nullptr), newest0(cache ? cache->newest : 0) {}
    ~ScanMeter() { if (cache) ChargeSearches(cache->newest - newest0, cache->ring.size); }
    ScanMeter(const ScanMeter&) = delete;
    ScanMeter& operator=(const ScanMeter&) = delete;
};

// Stress harness counters (main thread; reset by the harness's row).
struct Stats {
    long long scans = 0;          // scans let through while on
    long long charged = 0;        // residence candidates they charged
    long long searches = 0;       // region searches (pair-cache misses) they charged
    int maxFrameSearches = 0;     // most searches charged in one frame
    long long deferrals = 0;      // scans deferred
    int framesDeferring = 0;      // frames with at least one deferral
    int maxStreak = 0;            // longest run of consecutive frames with a deferral
};
const Stats& GetStats();
void ResetStats();

}  // namespace SpreadWarmth
}  // namespace Ext

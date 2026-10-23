// src/ext/spread_warmth/spread_warmth.cpp -- NOT RETAIL. ext.spread_warmth (class B); see the
// header and docs/extensions/handins/HITCH_PFBURST.md.
#include "ext/spread_warmth/spread_warmth.h"
#include "ext/registry.h"   // Ext::Ext().spread_warmth (B: locked for the game)

namespace Ext {
namespace SpreadWarmth {

bool g_on = false;

namespace {
int s_cand = 0;              // candidates charged this frame
int s_miss = 0;              // searches charged this frame
int s_scans = 0;             // scans let through this frame
bool s_deferredThisFrame = false;
int s_streak = 0;            // consecutive frames with a deferral, up to the last frame
Stats s_stats;
}  // namespace

void BeginFrame() {
    if (s_deferredThisFrame) {
        ++s_stats.framesDeferring;
        ++s_streak;
        if (s_streak > s_stats.maxStreak) s_stats.maxStreak = s_streak;
    } else {
        s_streak = 0;
    }
    s_deferredThisFrame = false;
    s_cand = 0;
    s_miss = 0;
    s_scans = 0;
    g_on = ::Ext::Ext().spread_warmth;
}

bool DeferSlow(int candidates) {
    if (s_scans > 0 && (s_miss >= kMissBudget || s_cand >= kCandidateBudget)) {
        ++s_stats.deferrals;
        s_deferredThisFrame = true;
        return true;
    }
    ++s_scans;
    s_cand += candidates;
    ++s_stats.scans;
    s_stats.charged += candidates;
    return false;
}

void ChargeSearches(int ringAdvance, int ringSize) {
    const int n = ringAdvance < 0 ? ringAdvance + ringSize : ringAdvance;
    s_miss += n;
    s_stats.searches += n;
    if (s_miss > s_stats.maxFrameSearches) s_stats.maxFrameSearches = s_miss;
}

const Stats& GetStats() { return s_stats; }
void ResetStats() { s_stats = Stats(); }

}  // namespace SpreadWarmth
}  // namespace Ext

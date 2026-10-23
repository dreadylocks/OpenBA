// ============================================================================
// src/ext/better_trees/better_trees.cpp -- better_trees.h
// ============================================================================
// NOT RETAIL. ext.better_trees (docs/extensions/handins/BETTER-TREES.md).
// ============================================================================
#include "ext/better_trees/better_trees.h"

#include "ext/registry.h"

#include "better_trees/foliage_aa_backend.h"                // src/render/common/better_trees: no D3D types
#include "better_trees/billboard_shadow_backend.h"    // src/render/common/better_trees: no D3D types
#include "better_trees/receiver_rewrite.h"            // BbsRewrite::Params (pure)
#if BANISHED_TWEAK_OVERLAY
#include "ext/tweak/ext_tweaks.h"   // the debug tweak overlay's k / floor / dapple set (debug flavour only)
#endif

namespace Ext {
namespace BetterTrees {

namespace {

// Both are in the registry's tables (registry.cpp); the unit tests check the
// names, so a miss is a build-time typo, not a run-time state.
const ValueKey& ModeKey() { return *FindValueKey("foliage_aa_mode"); }
const Entry&    Row()     { return *FindEntry("better_trees"); }

}  // namespace

const Params& Current() {
    // Per thread (the AO precedent, ambient_occlusion/settings.cpp): rebuilt on
    // every call, so a reader never sees a half-written copy.
    thread_local Params p;
    p.mode = Ext::Ext().better_trees ? Val().foliage_aa_mode : kOff;
    return p;
}

void Set(const Params& p) {
    const bool on = p.mode > 0;
    if (on) SetValueAndRecord(ModeKey(), p.mode);
    const Entry& row = Row();
    if (Requested(row) != on) SetAndRecord(row, on);   // its Sync hooks run this file's Sync too
    Sync();                                            // a mode change under an unchanged row
}

void Sync() {
    const Params& p = Current();
    const bool fill = p.mode == kDistanceFill;
#if BANISHED_TWEAK_OVERLAY
    // The debug tweak overlay's values (ext_tweaks.h; defaults = the constants of the #else).
    const Tweak::Faa tw = Tweak::FaaValues();
    const BbsRewrite::Params bbs = Tweak::BbsValues().p;
    const float k = tw.fillK, floorV = tw.fillFloor;
#else
    const BbsRewrite::Params bbs = BbsRewrite::kDefaultParams;
    const float k = kDistanceFillK, floorV = kDistanceFillFloor;
#endif
    FaaBackend::Request(p.mode, fill ? k : 0.0f, fill ? floorV : 0.0f, &bbs);
#if BANISHED_TWEAK_OVERLAY
    BbsBackend::RequestParams(bbs);   // before the row: an on request builds with it
#endif
    BbsBackend::Request(Ext::Ext().better_trees);
}

}  // namespace BetterTrees
}  // namespace Ext

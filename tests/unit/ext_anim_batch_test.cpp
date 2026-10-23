// ============================================================================
// tests/unit/ext_anim_batch_test.cpp -- PERFXEXT: ext.anim_batch (class B)
// ============================================================================
// The IsAnimating-at-clock rule behind the anim_batch shadow
// (src/ext/render_culling/render_culling_rules.h, the rules header the row shares with
// ext.render_culling). The two rows' registry behaviour is tests/unit/ext_render_culling_test.cpp.
// docs/extensions/handins/PERFXEXT.md
// ============================================================================
#include "unit.h"
#include "ext/render_culling/render_culling_rules.h"

UNIT_TEST(ext_anim_batch_is_animating_at_clock) {
    // No layer: not animating, however many ticks are pending.
    CHECK(!Ext::RenderCulling::IsAnimatingAt(false, true, 0, 100, 0));
    // A looping clip animates at any clock: batching cannot change the answer.
    CHECK(Ext::RenderCulling::IsAnimatingAt(true, true, 99, 100, 0));
    CHECK(Ext::RenderCulling::IsAnimatingAt(true, true, 99, 100, 1000));
    // A non-looping clip: the batched clock (pending ticks not applied) still animates
    // while retail's per-tick clock has reached the end -- the class B divergence.
    CHECK(Ext::RenderCulling::IsAnimatingAt(true, false, 90, 100, 0));
    CHECK(!Ext::RenderCulling::IsAnimatingAt(true, false, 90, 100, 10));
    CHECK(Ext::RenderCulling::IsAnimatingAt(true, false, 90, 100, 9));
    CHECK(!Ext::RenderCulling::IsAnimatingAt(true, false, 100, 100, 0));
}

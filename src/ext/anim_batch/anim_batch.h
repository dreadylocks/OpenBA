#pragma once
// ============================================================================
// src/ext/anim_batch/anim_batch.h -- NOT RETAIL. ext.anim_batch (class B, locked for the game,
// pseudo-mod ext:anim_batch): at speed N, the N-1 intermediate root-only MeshInstance::Update
// calls of a model whose root offset comes from the instanced pool or is zero are folded into
// the last one. docs/extensions/handins/PERFXEXT.md (§1), PERFXTBL.md (the side table).
//
// The row's body is shared with ext.render_culling and lives in src/ext/render_culling/
// (render_culling.cpp AnimBatchDecide, the per-frame flags its BeginFrame reads from both rows,
// the PERFXTBL side table both rows key their per-entity state on; the clock rule IsAnimatingAt
// in render_culling_rules.h): one frame state serves both rows. This header is the row's own
// view of it: the seam helpers ModelComponent::Tick calls.
// ============================================================================
#include <cstdint>

#include "ext/render_culling/render_culling.h"

namespace Ext {
namespace AnimBatch {

// The row's value for this frame (BeginFrame's read of Ext::Ext().anim_batch): one load.
inline bool On() { return RenderCulling::g_flags.animBatch; }

// ModelComponent tick (call only when On()): true when the update must be SKIPPED this
// iteration; otherwise *timeOut is the Time to pass (RenderCulling::AnimBatchDecide).
inline bool Decide(ModelComponent* modelComponent, const System::Time* time, int64_t* timeOut) {
    return RenderCulling::AnimBatchDecide(modelComponent, time, timeOut);
}

}  // namespace AnimBatch
}  // namespace Ext

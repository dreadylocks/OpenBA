// ============================================================================
// src/ext/draw_batch/draw_batch.h -- ext.draw_batch (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWBATCH.md.
//
// Graphics::Renderer::DrawItemList (retail 0x180105240, batchc_gfx_leaves.cpp) issues one
// GPU draw per draw item, except the instancing batch. The terrain's decals and road meshes
// (WorldGeometry::Draw modes 0x2000 / 0x4000) and the smoke emitters (ParticleInstance::Draw)
// are hundreds of tiny dynamic draws per frame whose only per-item state is the vertex data.
// This extension replaces DrawItemList with a copy of retail's loop that merges a run of
// consecutive such items into one dynamic draw; every other item is drawn by the copy exactly
// as retail draws it. Same items, same order, same GPU input bytes (batch_fill.h).
//
// Selection: one null-by-default static, Graphics::Renderer::_extDrawItemList (Camera.h),
// set by Sync() at boot and on every Extensions-dialog toggle, never in a hot loop.
// ============================================================================
#pragma once

#include <cstdint>

namespace Ext {
namespace DrawBatch {

// Installs or clears the engine seam to match Ext::Ext().draw_batch. Idempotent.
void Sync();
bool Installed();

}  // namespace DrawBatch
}  // namespace Ext

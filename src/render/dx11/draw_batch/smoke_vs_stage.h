#pragma once
// ============================================================================
// src/render/dx11/draw_batch/smoke_vs_stage.h -- ext.draw_batch's D3D side: the rewritten particle
// vertex shaders (docs/extensions/handins/DRAWBATCH.md, Idea B; smoke_vs_rewrite.h)
// ============================================================================
// NOT RETAIL.
//
// THE SEAM (DESIGN.md §3 rule 5): `VideoDX11::g_extDrawBatchVs`, null by default. With the row off it
// is null for the whole run and the three retail bodies that test it pay one load + branch:
//   VertexProgramData::Bind            per vertex-program bind (the stage binds the copy instead)
//   VertexProgramData::PlatformCreate  per program load (make the copies when on)
//   ~VertexProgramData                 (forget what the stage holds)
//
// The copy computes what the retail program computes for every vertex whose +0x0C is 0.0f, which is
// every vertex retail's particle writer emits, so the stage binds it for every draw of the program,
// merged or not. Billboard_shadow's stage tests its own seam after this one: a program this stage has
// no copy for falls through to it.
//
// Set by SmokeVsBackend::Request (the extension, main thread) when the row goes on; cleared by the
// stage itself on the binding thread at the first bind after an off request, once everything is
// released. All state is under one mutex: programs are created on loader threads, bound on the main
// or the render thread. A device change (device lost) releases every copy; they are re-made lazily.
// ============================================================================
#include <atomic>

struct ID3D11DeviceContext;

namespace VideoDX11 {

class VertexProgramData;

namespace DbStage { class Stage; }

// THE SEAM. Null = faithful.
extern std::atomic<DbStage::Stage*> g_extDrawBatchVs;

namespace DbStage {

class Stage {
public:
    // VertexProgramData::Bind with the seam set. `variant` is retail's post-fallback index. True when
    // the stage bound the program (its copy); false = the caller binds as it would without the stage.
    bool BindVertex(ID3D11DeviceContext* ctx, VertexProgramData* p, int variant);
    // VertexProgramData::PlatformCreate succeeded: make this program's copies now.
    void VertexCreated(VertexProgramData* p);
    void VertexDestroyed(VertexProgramData* p);
};

}  // namespace DbStage
}  // namespace VideoDX11

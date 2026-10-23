// ============================================================================
// src/render/metal/integration.mm -- the Metal backend's side of the backend-neutral seams
// ============================================================================
// NOT RETAIL. The bodies of the interface headers in src/render/common (AoBackend::, SmokeVsBackend::), the
// finish seam (Ext::RenderPipe::) and the AO-after-opaque seam (Graphics::ExtAoAfterOpaque),
// plus the Objective-C++ constructor the plain C++ entry points call
// (render_backend_metal.cpp). Nothing here names another backend's namespace.
// ============================================================================

#include "VideoMetalData.h"
#include "metal_entry.h"
#include "video/Context.h"
#include "video/Device.h"
#include "ao/ao_backend.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "draw_batch/smoke_vs_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"
#include "passes/assao_pass.h"
#include "passes/tree_passes.h"

namespace VideoMetal {

Video::DeviceData* NewDeviceData(Video::Device* device) {
    return new DeviceData(device);
}

}  // namespace VideoMetal

namespace AoBackend {

// Ambient occlusion runs only when a Metal device is live AND every ASSAO shader converted.
bool Supported() {
    if (!VideoMetal::DeviceLive()) {
        return false;
    }
    return VideoMetal::AssaoReady();
}

// The Metal depth is created readable already, so the request changes nothing.
void RequestReadableDepth(bool on) {
    (void)on;
}

bool DepthReadable() {
    return VideoMetal::DepthReadable();
}

}  // namespace AoBackend

// ext.better_trees: the extension's requests go to the Metal tree passes (passes/tree_passes.mm),
// which rewrite the retail cutout and billboard programs from the user's own bytecode. Metal
// supports every mode the interface carries, so there is no "not supported" answer to give.
namespace FaaBackend {

void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither) {
    VideoMetal::TreePasses::SetFoliage(mode, fillK, fillFloor, dither);
}

}  // namespace FaaBackend

namespace BbsBackend {

void Request(bool on) {
    VideoMetal::TreePasses::SetBillboardShadow(on);
}

}  // namespace BbsBackend

// ext.draw_batch, Idea B (merged smoke runs): the extension needs a rewritten particle vertex shader
// that writes an emitter slot into each vertex. Metal has no such rewrite, so it answers "not
// supported" through the interface: the stage stays off, Rewritten() is false for every program,
// and the extension draws the faithful path.
namespace SmokeVsBackend {

void Request(bool on) {
    (void)on;
}

bool Rewritten(const Video::VertexProgram* vp) {
    (void)vp;
    return false;
}

}  // namespace SmokeVsBackend

namespace Ext {
namespace RenderPipe {

int32_t FinishPresent(Video::Context& ctx) {
    ctx._contextData->Finish();
    return 0;
}

void FinishRecover(Video::Device& device, int32_t state) {
    (void)device;
    (void)state;
}

}  // namespace RenderPipe
}  // namespace Ext

namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    VideoMetal::Assao::AfterOpaque(context, frame, entry);
}

}  // namespace Graphics

#pragma once
#include "../VideoMetalData.h"
#include "better_trees/receiver_rewrite.h"   // BbsRewrite::Params (pure, no D3D)

namespace VideoMetal::TreePasses {

// The extension's requests (FaaBackend::Request / BbsBackend::Request, integration.mm). Main
// thread; the render thread picks them up in BeginFrame. `dither` null = the shipped set.
void SetFoliage(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither);
void SetBillboardShadow(bool on);

// Snapshot the last request once per command frame, outside draw loops.
void BeginFrame();

// Returns retail on a non-match or conversion failure. Variants are keyed on the retail
// shader's identity string (never its address) and live per device.
Shader* Select(Shader* retail, const std::vector<uint8_t>& bytes,
               id<MTLDevice> device, bool vertex);

// True for a cutout variant that writes coverage in o0.w: it needs alpha-to-coverage and a
// multisampled target, otherwise the caller must run the retail shader.
bool IsCoverage(const Shader* pixel);

// Match DX11: transformed cutout shaders never run with alpha blending.
class BlendGuard {
    Shader*& slot;
    Shader* saved;

public:
    BlendGuard(Shader*& pixel, bool blending);
    ~BlendGuard() { slot = saved; }
};

}  // namespace VideoMetal::TreePasses

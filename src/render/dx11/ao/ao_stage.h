// ============================================================================
// src/render/dx11/ao/ao_stage.h -- the AO stage: the middle of the pass, behind one interface
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/AO.md §1 ruling 1, §3).
//
// The pass (ao_pass.cpp) is: resolve the main depth -> AO stage -> multiply into
// the bound colour. The stage is the only part that knows the algorithm: Intel
// ASSAO today (ao_stage_assao.cpp over assao/), and XeGTAO could replace it
// without touching the resolve, the seam, the apply blend, the settings or the
// menu. Its inputs are the ones every screen-space AO needs: a single-sampled
// raw hardware depth (0 at near, 1 at far; the resolve's output), the
// projection that produced it, and the frame's parameters.
//
// Contract for Draw: it may change any D3D state; the caller (ao_pass.cpp)
// saves and restores everything around it. On entry the render target to
// darken is bound at OM slot 0 (with the main depth as DSV, which the stage must
// neither test nor write); the stage MULTIPLIES its AO term into it (blend ZERO /
// SRC_COLOR: the colour is never read).
// ============================================================================
#pragma once

#include <d3d11.h>

namespace Graphics { struct ExtAoFrame; }

namespace AoStage {

class Stage {
public:
    virtual ~Stage() = default;

    // (Re)allocate for a w x h depth. Cheap when nothing changed. False = unusable.
    virtual bool Resize(ID3D11Device* device, unsigned width, unsigned height) = 0;

    // `depth`: R32_FLOAT raw depth, 1x, `width` x `height` (Resize's).
    // `projection`: row-major, row-vector convention, a standard LEFT-HANDED 0..1
    // perspective (x right, y up, z forward; AO.md §6.2: the camera's Z-up
    // projection with rows 1 and 2 swapped, Ext::AmbientOcclusion::AoMath::AssaoProjection).
    virtual void Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
                      const float projection[16], const Graphics::ExtAoFrame& frame) = 0;
};

// The ASSAO stage (ao_stage_assao.cpp). Null when ASSAO could not create its
// shaders / states on `device` (the [AmbientOcclusion] log says why).
Stage* CreateAssao(ID3D11Device* device);

}  // namespace AoStage

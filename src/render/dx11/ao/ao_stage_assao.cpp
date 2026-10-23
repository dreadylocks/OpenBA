// ============================================================================
// src/render/dx11/ao/ao_stage_assao.cpp -- AoStage::Stage over Intel ASSAO (assao/, MIT)
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/AO.md; handins/AOCORE.md).
//
// The frame's parameters (Graphics::ExtAoFrame, decided by src/ext/ambient_occlusion)
// map 1:1 onto ASSAO_Settings. The fixed inputs:
//   * DepthSRV = the resolve's R32F raw depth, NormalSRV = null (ASSAO derives
//     view-space normals from depth), DrawOpaque = false (ASSAO's own multiply
//     blend, ZERO / SRC_COLOR, into the currently bound RTV), depth test and
//     write off (ASSAO's own depth-stencil state, DepthEnable = FALSE).
//   * ProjectionMatrix = the caller's LH matrix, MatricesRowMajorOrder = true
//     (ASSAO then reads -m[3][2] / m[2][2] as its depth unpack and 1/m[0][0],
//     1/m[1][1] as tan(fov/2): AO.md §6.2).
//   * The temporal fields stay at their no-op defaults (0 / 1): there is no TAA.
// ============================================================================
#include "ao/ao_stage.h"

#include "ao/assao/ASSAO.h"
#include "graphics/renderer_ext_ao.h"

#include <cstdio>

namespace AoStage {
namespace {

class AssaoStage final : public Stage {
public:
    explicit AssaoStage(ASSAO_Effect* effect) : _effect(effect) {}
    ~AssaoStage() override { ASSAO_Effect::DestroyInstance(_effect); }

    bool Resize(ID3D11Device*, unsigned width, unsigned height) override {
        // ASSAO (re)allocates its own targets inside Draw when the viewport size
        // changes (UpdateTextures, which also wants the depth SRV, so it is not
        // called from here); this records the size and logs the next allocation.
        if (width == _width && height == _height) return true;
        _width = width;
        _height = height;
        _logAllocation = true;
        return true;
    }

    void Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
              const float projection[16], const Graphics::ExtAoFrame& f) override {
        ASSAO_Settings s;
        s.Radius                = f.radius;
        s.ShadowMultiplier      = f.shadowMultiplier;
        s.ShadowPower           = f.shadowPower;
        s.HorizonAngleThreshold = f.horizonAngleThreshold;
        s.FadeOutFrom           = f.fadeOutFrom;
        s.FadeOutTo             = f.fadeOutTo;
        s.QualityLevel          = f.qualityLevel;
        s.BlurPassCount         = f.blurPassCount;
        s.Sharpness             = f.sharpness;
        s.DetailShadowStrength  = f.detailShadowStrength;
        ASSAO_InputsDX11 in;
        Fill(in, context, depth, projection);
        _effect->Draw(s, &in);
        if (_logAllocation) {
            _logAllocation = false;
            // (ASSAO's GetAllocatedVideoMemory is never updated upstream: always 0.)
            std::fprintf(stderr, "[AmbientOcclusion] ASSAO targets allocated for %ux%u\n", _width, _height);
        }
    }

private:
    void Fill(ASSAO_InputsDX11& in, ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
              const float* projection) const {
        in.ViewportX      = 0;
        in.ViewportY      = 0;
        in.ViewportWidth  = static_cast<int>(_width);
        in.ViewportHeight = static_cast<int>(_height);
        // Scissor 0 = the whole viewport (ASSAODX11.cpp UpdateConstants).
        in.ScissorLeft = in.ScissorTop = in.ScissorRight = in.ScissorBottom = 0;
        if (projection != nullptr) in.ProjectionMatrix = ASSAO_Float4x4(projection);
        in.MatricesRowMajorOrder = true;
        in.DrawOpaque            = false;
        in.DeviceContext         = context;
        in.DepthSRV              = depth;
        in.NormalSRV             = nullptr;
        in.OverrideOutputRTV     = nullptr;
    }

    ASSAO_Effect* _effect;
    unsigned      _width  = 0;
    unsigned      _height = 0;
    bool          _logAllocation = false;
};

}  // namespace

Stage* CreateAssao(ID3D11Device* device) {
    // The DXBC is linked in (assao_gcc_compat.cpp); ShaderData is not read by the
    // GCC port's CompileShader, but ASSAO's create desc carries it, so it points
    // at something non-null.
    static const char kNoSource[] = "precompiled (tools/ao_shaders.py)";
    ASSAO_CreateDescDX11 desc(device, kNoSource, sizeof(kNoSource));
    ASSAO_Effect* effect = ASSAO_Effect::CreateInstance(&desc);
    if (effect == nullptr) {
        std::fprintf(stderr, "[AmbientOcclusion] ASSAO_Effect::CreateInstance failed: the AO stage stays off\n");
        return nullptr;
    }
    int major = 0, minor = 0;
    effect->GetVersion(major, minor);
    std::fprintf(stderr, "[AmbientOcclusion] ASSAO %d.%d created\n", major, minor);
    return new AssaoStage(effect);
}

}  // namespace AoStage

// ============================================================================
// src/ext/ambient_occlusion/ambient_occlusion.cpp -- ambient_occlusion.h
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/handins/AOCORE.md).
// ============================================================================
#include "ext/ambient_occlusion/ambient_occlusion.h"

#include "ext/ambient_occlusion/ao_math.h"
#include "ext/ambient_occlusion/params.h"
#include "ext/registry.h"

#include "ao/ao_backend.h"            // src/render/common/ao: the depth format, the feature level
#include "Game/PlayerController.h"
#include "graphics/Camera.h"          // Graphics::Renderer::_extAo
#include "graphics/renderer_ext_ao.h"

#include <cstdio>

namespace Ext {
namespace AmbientOcclusion {

namespace detail {
bool g_enabled = false;
}

namespace {
Graphics::ExtAoFrame g_frame = {};   // the record the seam points at while armed (main thread)
}

void Sync() {
    const bool row = Ext::Ext().ambient_occlusion;
    const bool supported = row && AoBackend::Supported();
    const bool was = detail::g_enabled;
    detail::g_enabled = supported;
    AoBackend::RequestReadableDepth(supported);
    if (row && !supported) {
        static bool told = false;
        if (!told) {
            told = true;
            std::fprintf(stderr, "[AmbientOcclusion] ext.ambient_occlusion is on but the device is below feature "
                                 "level 11_0 (ASSAO's shaders are ps_5_0): the row stays inactive\n");
        }
    }
    if (was != detail::g_enabled)
        std::fprintf(stderr, "[AmbientOcclusion] ext.ambient_occlusion %s\n", detail::g_enabled ? "ON" : "OFF");
    if (!detail::g_enabled) Graphics::Renderer::_extAo = nullptr;
}

bool BuildFrame(const PlayerController& pc, Graphics::ExtAoFrame& out) {
    const Ext::AmbientOcclusion::Params& params = Ext::AmbientOcclusion::Current();   // the one read of the frame
    return Ext::AmbientOcclusion::AoMath::FrameFor(params, pc.distance, out);
}

bool Arm(const PlayerController& pc) {
    if (!BuildFrame(pc, g_frame)) return false;
    Graphics::Renderer::_extAo = &g_frame;
    return true;
}

void Disarm() {
    Graphics::Renderer::_extAo = nullptr;
}

}  // namespace AmbientOcclusion
}  // namespace Ext

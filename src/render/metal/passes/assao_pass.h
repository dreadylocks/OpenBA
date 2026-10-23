#pragma once
#include "../VideoMetalData.h"
#include "graphics/renderer_ext_ao.h"
namespace VideoMetal::Assao {
void AfterOpaque(Video::Context& context, const Graphics::ExtAoFrame& frame,
                 const Graphics::RendererSceneEntry& entry);
void EncodeAfterOpaque(id<MTLDevice> device, id<MTLCommandBuffer> commands,
                       Video::Context& context, const Graphics::ExtAoFrame& frame,
                       const Graphics::RendererSceneEntry& entry);
// All encoders end before returning; engine cached bindings are untouched.
bool Draw(id<MTLDevice> device, id<MTLCommandBuffer> commands,
          id<MTLTexture> color, id<MTLTexture> depth,
          const float projection[16], const Graphics::ExtAoFrame& frame);
}  // namespace VideoMetal::Assao

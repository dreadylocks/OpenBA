#pragma once
// ============================================================================
// src/platform/common/cursor_image.h -- the game cursor from a 32 x 32 texel image
// ============================================================================
// The Metal backend has no D3D11 DeviceData::SetImageBuffer (retail slot 8, which builds
// the cursor from the ImageBuffer's pixels). It reads the 32 x 32 BGRA texels itself and
// hands them here; the SDL backend (cursor_sdl.cpp) builds, sets and caches the cursor.
#include "platform_window.h"
#include <cstdint>

namespace Platform {
namespace Cursor {

// Make `texels` (32 x 32 BGRA8, retail's 1-bit classification) the current cursor with
// hotspot (hotX, hotY) in points. Destroys the cursor a previous call made.
void SetFromTexels(const uint32_t* texels, int32_t hotX, int32_t hotY);

}  // namespace Cursor
}  // namespace Platform

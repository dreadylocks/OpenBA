// ============================================================================
// src/platform/win/app_win.cpp -- Platform::App lifecycle hooks (Windows)
// ============================================================================
// The Windows half of src/platform/common/platform_app.h. Every body is empty: Windows
// has no step of its own at these spots. WinMain's own SDL set-up (SDL_RegisterApp,
// the DPI-awareness and timer-resolution hints, SDL_UnregisterApp) stays inline in
// src/app/main.cpp, in its order.
// ============================================================================

#include "platform_app.h"

namespace Platform {
namespace App {

void PreSdlInitHints() {
}

void PostSdlInit() {
}

void Shutdown() {
}

}  // namespace App
}  // namespace Platform

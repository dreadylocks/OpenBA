// ============================================================================
// src/platform/darwin/app_darwin.cpp -- Platform::App lifecycle hooks (macOS)
// ============================================================================
// The Darwin twin of src/platform/posix/app_posix.cpp (src/platform/darwin/README.md): it
// replaces that file whole on Apple. NOT RETAIL. The macOS-only steps main() used to carry
// behind #if defined(__APPLE__):
//   PreSdlInitHints  harness mode (Opt().background): no Dock tile, no activation;
//   PostSdlInit      the hands-on input logger (Diag::InputTrace, Opt().inputTrace);
//   Shutdown         stop the boot-state watchdog before SDL_Quit.
// ============================================================================

#include "platform_app.h"

#include "app/options.h"
#include "boot_state_watchdog.h"
#include "input_trace.h"

#include <SDL3/SDL_hints.h>

namespace Platform {
namespace App {

void PreSdlInitHints() {
    if (!Opt().background) return;
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
}

void PostSdlInit() {
    Diag::InputTrace::Start();   // installs the logger only when Opt().inputTrace is set
}

void Shutdown() {
    BootState::Stop();
}

}  // namespace App
}  // namespace Platform

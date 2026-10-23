#pragma once
// ============================================================================
// src/platform/common/platform_app.h -- app lifecycle hooks around SDL_Init / SDL_Quit
// ============================================================================
// NOT RETAIL. The three spots in src/app/main.cpp where a platform may need a step of
// its own in the process lifecycle. Both entry points (WinMain, POSIX main) call them
// at the same places:
//   PreSdlInitHints() -- after the shared SDL hints, immediately before SDL_Init
//   PostSdlInit()     -- after SDL_Init and the drop-event disables, before Init()
//   Shutdown()        -- immediately before every SDL_Quit (the Init-failed path too)
// D4_RULES C1: namespace `Platform`, free functions, NO OS header here. Bodies:
// src/platform/win/app_win.cpp and src/platform/posix/app_posix.cpp (both empty:
// neither platform has a step there today), and a Darwin twin when one is added
// (src/platform/darwin/README.md).
// ============================================================================

namespace Platform {
namespace App {

void PreSdlInitHints();
void PostSdlInit();
void Shutdown();

}  // namespace App
}  // namespace Platform

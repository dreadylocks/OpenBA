#pragma once
// ============================================================================
// src/platform/common/platform_window_hooks.h -- per-platform window hooks
// ============================================================================
// NOT RETAIL. The steps of the SDL window backend (src/platform/sdl/window_sdl.cpp,
// display_sdl.cpp) that a platform may need to do its own way. Every hook has an identity
// (or "nothing to do") body on Windows and Linux, so those hosts keep retail's behaviour:
//   win/window_win.cpp, posix/window_posix.cpp   the identity bodies;
//   darwin/window_darwin.cpp                      replaces window_posix.cpp on Apple
//                                                 (src/platform/darwin/README.md).
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here; an
// SDL_Window is only forward-declared. Display_geometry.h supplies FrameBorders.
// ============================================================================

#include <cstdint>

#include "display_geometry.h"

struct SDL_Window;

namespace Platform {
namespace Window {

// Called by Create before SDL_CreateWindow. May change the client size it will ask for
// (width / height are in/out, SDL points) and sets the application-specific SDL window
// flags OR-ed onto SDL_WINDOW_HIDDEN: the graphics-API flag (SDL_WINDOW_VULKAN where the
// renderer is DXVK, SDL_WINDOW_METAL on Apple) and, on Apple, the high-pixel-density
// request. Also the place for hints that must be set before the window exists.
uint64_t PrepareCreate(int32_t* width, int32_t* height);

// Harness mode on Apple (Opt().background): keep the window out of the way (no activation,
// no Dock tile, behind other windows). `window` may be nullptr, which only configures the
// application. Nothing to do elsewhere.
void ConfigureBackgroundWindow(SDL_Window* window);

// SetWindowPos hook: fit a framed window's client rect to the usable screen (menu bar,
// Dock): clamp the size, then slide it so the title bar and the bottom edge are on screen.
// All values are SDL points. Identity elsewhere (retail never clamps).
void FitToUsableScreen(SDL_Window* window, const FrameBorders& frame, int32_t* clientX,
                       int32_t* clientY, int32_t* clientW, int32_t* clientH);

// The windowed OPENING geometry, called by the renderer's CreateDevice (saved values are
// Configuration::windowX/Y/Width/Height in engine PIXELS, -1 when none was saved; videoW/H
// the configured video size). Sizes and places the (still hidden) window in points.
// The only caller is the Metal backend (src/render/metal, the renderer PR).
// Nothing to do on a host whose windows open at the size they were created with.
void ApplyInitialWindowGeometry(SDL_Window* window, int savedX, int savedY, int savedPxW,
                                int savedPxH, int videoW, int videoH);

// The primary display as the Options resolution list needs it, in engine PIXELS: the usable
// area less a title bar (maxW / maxH) and the display's own size (nativeW / nativeH).
// The only caller is the Metal backend (src/render/metal, the renderer PR).
// All zero when unknown (BuildResolutionList then does not filter).
void QueryScreenForModes(int* maxW, int* maxH, int* nativeW, int* nativeH);

// The pixel density of the game window (display_sdl.cpp's PixelDensity of the one window);
// 1.0 before it exists. For code that has no SDL_Window (display_density.cpp).
float GamePixelDensity();

}  // namespace Window
}  // namespace Platform

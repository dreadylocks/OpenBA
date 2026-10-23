#pragma once
// ============================================================================
// src/platform/sdl/window_sdl_internal.h -- shared state of the SDL window backend
// ============================================================================
// Phase D.4 SDL step W3. Only src/platform/sdl/*.cpp include this: the engine sees the
// window through platform_window.h's opaque native handle, never an SDL_Window*.
// ============================================================================

#include <cstdint>

struct SDL_Window;

namespace Platform {
namespace Window {

// The window's pixel density: back-buffer pixels per SDL point (display_sdl.cpp). The ONE
// place src/platform/sdl asks for it. 1.0 on every host today: no window is created with
// SDL_WINDOW_HIGH_PIXEL_DENSITY, so SDL points are pixels. A Darwin build that opts a
// window into high density fills the body in; every coordinate crossing the seam is then
// scaled by it.
float PixelDensity(SDL_Window* window);

namespace Sdl {

// The one game window window_sdl.cpp's Create made; nullptr before / after.
SDL_Window* GameWindow();

// pump_sdl.cpp: install the platform-specific event sources the pump needs (the
// Windows message hook that reports the end of a user move / size loop). Create calls
// it once the window exists.
void InstallPumpHooks();
void RemovePumpHooks();
// Queue the end of a user move / size loop (Win32: WM_EXITSIZEMOVE) as an event, in
// order with the rest of the queue; the pump turns it into the EXITSIZEMOVE tuple.
void QueueMoveSizeEnded();

// Queue a window message exactly as the window received it, in order with the rest of
// the queue; the pump dispatches it unchanged (window_messages.h, RawMessage). Main
// thread only (it is called from the window procedure).
void QueueRawMessage(uint32_t message, uint64_t wParam, int64_t lParam);

// src/platform/win/window_hook_win.cpp (Windows only):
//  * SDL_SetWindowsMessageHook with an observer that calls QueueMoveSizeEnded on
//    WM_EXITSIZEMOVE;
//  * a window-procedure subclass ahead of SDL's WIN_WindowProc that calls
//    QueueRawMessage for WM_ACTIVATE, WM_SETFOCUS and WM_KILLFOCUS.
// `window` is the native handle (the HWND).
void InstallWindowsMessageHook(void* window);
void RemoveWindowsMessageHook(void* window);

}  // namespace Sdl
}  // namespace Window
}  // namespace Platform

#pragma once
// ============================================================================
// src/platform/common/platform_input_hooks.h -- per-platform input hooks
// ============================================================================
// NOT RETAIL. The steps of the SDL input backend (src/platform/sdl/input_sdl.cpp,
// pump_sdl.cpp) that a platform may need to do its own way. Every hook has an identity
// (or "nothing to do") body on Windows and Linux, so those hosts keep retail's behaviour:
//   win/input_win.cpp, posix/input_posix.cpp   the identity bodies;
//   darwin/input_darwin.cpp                    replaces input_posix.cpp on Apple
//                                              (src/platform/darwin/README.md): the Mac key
//                                              layer, the Option-click remap and the window
//                                              chord of src/platform/common/mac_keymap.h.
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here; the SDL types
// are only forward-declared.
// ============================================================================

#include <cstdint>

union SDL_Event;

namespace Platform {
namespace Input {

// The capacity of the scratch array ApplyKeyLayer fills: one flag per SDL scan code.
inline constexpr int kKeyLayerCapacity = 512;

// SDL's per-scan-code key state, as the DIK table is to be read from it. `raw` and `*count`
// are SDL_GetKeyboardState's result. `scratch` holds kKeyLayerCapacity flags and receives the
// result when the platform changes the state; `*count` is lowered to what the result holds.
// Identity elsewhere: returns `raw` untouched.
const bool* ApplyKeyLayer(const bool* raw, int* count, bool* scratch);

// The LOGICAL mouse buttons (SDL_MouseButtonFlags) the pump's decisions leave down, given
// the physical ones SDL_GetGlobalMouseState reports. Identity elsewhere.
uint32_t PolledMouseButtons(uint32_t physical);

// The pump calls this for every event before anything else sees it. The platform may
// observe it and rewrite it in place (the Option-click button remap). Nothing elsewhere.
void PreprocessPumpEvent(SDL_Event* event);

// The pump calls this after the debug overlay and before the event is translated; true
// when the platform consumed the event (the game sees nothing). Always false elsewhere.
bool ConsumePumpEvent(const SDL_Event& event);

}  // namespace Input
}  // namespace Platform

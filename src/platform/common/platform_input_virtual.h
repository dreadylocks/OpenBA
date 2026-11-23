#pragma once
// ============================================================================
// src/platform/common/platform_input_virtual.h -- a scripted input source (harness only)
// ============================================================================
// Not retail. The game polls the devices themselves (the mouse buttons as they are on the
// desk, the keyboard only while its window is in front: src/platform/sdl/input_sdl.cpp), so
// input sent to a window in the background never reaches it. BANISHED_DRIVE
// (src/diag/drive_harness.cpp) plays a script instead: while a source is installed and says
// it is active, every Platform::Input read answers from the frame it returned, the window
// counts as in front, and the devices are drained and ignored. When it says inactive, or no
// source is installed (every normal run), nothing changes.
//
// One implementation for every host: src/platform/sdl/input_sdl.cpp.
// ============================================================================

#include <cstdint>

namespace Platform {
namespace Input {
namespace Virtual {

// The input of one frame: the cursor in client-area pixels, the buttons (bit 0 left, 1 right,
// 2 middle), the wheel this frame (+1 up, -1 down, 0) and the keys held, by DirectInput scan code
// (bit 7 = down), the bytes ReadKeyboard fills.
struct Frame {
    int32_t x = 0;
    int32_t y = 0;
    uint8_t buttons = 0;
    int32_t wheel = 0;
    uint8_t keys[256] = {};
};

// The game window's client area, for the source's points -> pixels.
struct Client {
    int32_t widthPx = 0;
    int32_t heightPx = 0;
    float density = 1.0f;   // pixels per point
};

// Called once per Platform::Input::Poll (once a frame, menus included). Returns true and fills
// `out` while it is driving; false to leave the devices to the player.
using Source = bool (*)(const Client& client, Frame* out);

// Installs the source (nullptr removes it). Boot only, before the first frame.
void SetSource(Source source);

}  // namespace Virtual
}  // namespace Input
}  // namespace Platform

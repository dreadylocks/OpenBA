// ============================================================================
// src/diag/tweak_overlay.h -- the debug tweak overlay (Dear ImGui, Alt+F10)
// ============================================================================
// NOT RETAIL. DEBUG FLAVOUR ONLY: CMake option BANISHED_TWEAK_OVERLAY (ON for the debug
// flavour on Windows, OFF for og / perf / release / Linux). Without it this header is empty,
// tweak_overlay.cpp and third_party/imgui are not compiled, and every call site below sits
// inside `#if BANISHED_TWEAK_OVERLAY`. Block TWEAKUI; docs/reference/tweak_overlay.md.
//
// Live tuning of the graphics extensions (ext.ambient_occlusion, ext.better_trees,
// ext.max_shadow_res, ext.render_pipe, ext.eye_camera) without the Options dialog round
// trip. Every change goes through the extension's own live path (Ext::SetAndRecord,
// Ext::SetValueAndRecord, Ext::BetterTrees::Set / Sync, src/ext/tweak/ext_tweaks.h), never
// a poke at render state. Under /profile:faithful it shows "faithful profile: nothing to
// tweak" and every control is disabled.
//
// THREADS
//   main (the SDL pump)   OnSdlEvent: the toggle key; while open, copies the mouse /
//                         keyboard events into a queue and swallows them (the game sees
//                         none). MainThreadTick: applies the queued row / value changes
//                         and snapshots the registry for the UI. Creates the ImGui
//                         context on the first open.
//   presenting thread     BeforePresent: the ImGui frame (events drained from the queue,
//                         NewFrame, the panels, Render) and the DX11 draw into the back
//                         buffer, right before Present. The main thread under the direct
//                         path (ContextData::Finish), the render thread under
//                         ext.render_pipe (Ext::RenderPipe::FinishPresent). Only this thread
//                         touches the ImGui context once it exists.
//   main, render idle     OnDeviceShutdown (DeviceData::Shutdown): the DX11 backend lets go
//                         of the device; it re-inits on the next open frame (device lost,
//                         a settings rebuild).
// Closed, each hook costs one atomic load (and the pump one key-event compare).
// ============================================================================
#pragma once

#if BANISHED_TWEAK_OVERLAY

#include <atomic>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>

union SDL_Event;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

// The toggle: a compile-time key combo (no settings key, no environment flag).
#define TWEAK_OVERLAY_KEY     SDL_SCANCODE_F10
#define TWEAK_OVERLAY_KEY_MOD SDL_KMOD_ALT     // either Alt

namespace Diag {
namespace TweakOverlay {

namespace detail {
extern std::atomic<bool> g_open;
}

// The overlay is open: it owns the mouse and the keyboard (input_sdl.cpp reads nothing).
inline bool IsOpen() { return detail::g_open.load(std::memory_order_acquire); }

// Pump thread, every SDL event. True = the overlay took it (the toggle, or a mouse /
// keyboard / text event while open): the pump does not translate it for the game.
bool OnSdlEvent(const SDL_Event& ev);

// Pump thread, once per Pump: the queued changes and the registry snapshot.
void MainThreadTick();

// The presenting thread, the device locked, right before Present.
void BeforePresent(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain);

// DeviceData::Shutdown (main thread; the render thread idle).
void OnDeviceShutdown();

}  // namespace TweakOverlay
}  // namespace Diag

#endif  // BANISHED_TWEAK_OVERLAY

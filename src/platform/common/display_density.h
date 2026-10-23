#pragma once
// ============================================================================
// src/platform/common/display_density.h -- UI-scale defaults for a high-density display
// ============================================================================
// NOT RETAIL. The game's UI is laid out in pixel units (GameSettings::uiScale, percent;
// _uiScale = uiScale / 100 is LAYOUT UNITS PER DEVICE PIXEL, so a smaller value is a
// bigger UI). On a window whose back buffer has more pixels than points (a high-density
// window, SDL pixel density > 1) the same settings show the UI physically smaller. These
// are the two questions game code would ask so a platform can answer for that case:
//   * the UI scale a FIRST RUN gets (no "UIScale" saved yet);
//   * the Options > UI scale slider's range.
// Bodies: src/platform/common/display_density.cpp. Both answers are derived from the game
// window's pixel density (Platform::Window::PixelDensity, display_sdl.cpp, through
// GamePixelDensity); at density 1 they are the identity, which is every window not created
// with the high-density flag (Windows, Linux, Apple without the Retina mode). On a
// density-2 Apple window a first run gets 50 and the slider 38..75 (docs/macos/UI_SCALE.md).
// D4_RULES C1: namespace `Platform`, free functions, NO OS / SDL header here.
//
// Callers: GameSettings::Load (game_settings_persist.cpp, first run only) and
// OptionsDialog::OnOpened (the slider range).
// ============================================================================

namespace Platform {
namespace Display {

// The UI scale (percent) to use when no "UIScale" is saved. `defaultPercent` is what the
// game would use otherwise; returned unchanged at density 1.
int FirstRunUiScalePercent(int defaultPercent);

// The UI-scale slider's range (percent). In/out: pass retail's 75 / 150. Unchanged at density 1.
void UiScaleSliderRange(float* minPercent, float* maxPercent);

}  // namespace Display
}  // namespace Platform

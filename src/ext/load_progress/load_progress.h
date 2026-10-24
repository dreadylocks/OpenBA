// ============================================================================
// src/ext/load_progress/load_progress.h -- ext.load_progress (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/LOADPROGRESS.md (issue #13).
//
// Retail's boot runs the Game/GlobalResources.rsc load and every enabled mod's
// Resources.rsc behind the two outer LoaderLoops, whose frames are a black clear
// (LoaderLoop::Draw). With a large mod set that is about a minute of black. With
// the row ON and at least one mod enabled, those frames show the game's own
// loading screen instead (Dialog/Loading.rsc: the logo and the turning spinner,
// which game_main.cpp already loaded before MainLoop::Run) with one text line
// saying what the loader thread is on: "Loading game data" or "Loading mod X of Y".
// The screen goes the moment the first splash starts (BootEnd), so the boot from
// the Shining Rock splash on is retail's. With no mod enabled nothing changes.
//
// Seams: Engine::LoaderLoop::_extFrame (draw + update) and
// Engine::UpdateLoop::_extLoadProgress (the loader's counters), both null unless
// BootBegin installed them. Nothing is loaded from a package while the loader
// thread runs: the dialog, its fonts and sprites and our string table are all
// resident before the first loader kick.
//
// TextFor / SameText are standard C++ (tests/unit/ext_load_progress_test.cpp).
// ============================================================================
#pragma once

#include <cstdint>

namespace UI { class Dialog; }

namespace Ext {
namespace LoadProgress {

// game_main.cpp, after the splash preloads and before MainLoop::Run (main thread, the
// loader idle): builds the screen and installs the seams when the row is on and a mod
// is enabled. `loadingDialog` is Dialog/Loading.rsc (the Loading splash's _dialogExternal).
void BootBegin(const UI::Dialog* loadingDialog);

// GameLoadDialogLoopT::Startup, first thing (main thread, the loader idle): removes the
// seams and frees the screen. A no-op when BootBegin did not install it.
void BootEnd();

// What the text line says, from the loader's counters (UpdateLoop::ExtLoadProgress).
enum class Line : int32_t { GameData = 0, Mod = 1 };
struct Text {
    Line    line;
    int32_t mod;    // Line::Mod: 1-based
    int32_t mods;
};

// The first LoadResources at boot (InnerLoaderLoop's) walks Game/GlobalResources.rsc
// (mod 0), then each enabled mod's list (mod k of mods). Once it is done (lists >= 1)
// every later list in the boot is a re-stamp of resident data, which reads as game data.
inline Text TextFor(int32_t mod, int32_t mods, int32_t lists) {
    if (lists == 0 && mod >= 1 && mods >= 1 && mod <= mods)
        return Text{Line::Mod, mod, mods};
    return Text{Line::GameData, 0, 0};
}

inline bool SameText(const Text& a, const Text& b) {
    return a.line == b.line && a.mod == b.mod && a.mods == b.mods;
}

}  // namespace LoadProgress
}  // namespace Ext

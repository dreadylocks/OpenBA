// ============================================================================
// src/ext/toolbar/toolbar.h -- the in-game extension window: one toolbar button, a page per extension
// ============================================================================
// NOT RETAIL. docs/extensions/DESIGN.md "Surfaces" (ruled 2026-10-20),
// docs/extensions/handins/RESTRICTEDFOODS.md. An extension with an in-game control does not get a button of
// its own: the main toolbar gets ONE extension button, in its Reports group (Status Bar, Limits, Town Hall,
// Help), and it opens one window with one tab per extension that contributes a page. With no active
// contributor the button is not added and the toolbar is retail's.
//
// A contribution keeps its own element pointers (found by name in its page); this host holds only the list.
// The page's controls are live: `changed` writes the extension's state at once (no Apply). The lock (class B
// / C) is on the extension's ACTIVATION, so a page is offered only while the row is active for this game;
// what the page edits may change whenever the player likes (DESIGN.md "What the lock covers").
//
// ELEMENT IDS. The window's own title bar uses ids 0 and 1. Each open page owns the span [idBase, idBase +
// kPageIdSpan) and the host hands it that `idBase` in `open`, so two pages can never answer each other's
// clicks: a control gets `idBase + local` as its element id, and `changed` is called only for ids in the
// page's own span, with the LOCAL id (0 .. kPageIdSpan - 1).
//
// The host's arms (the one EXT SEAM line per site):
//   src/game/Dialog/toolbardialog.cpp   InsertAt / Style while the Reports group is built, Toggle on the
//                                       button's click
//   src/game/Dialog/entityui.cpp        RestoreWindow in PlayerToolManager::Startup (the window reopens
//                                       with the save, window_state.h)
//
// Standard C++ for the list and Offered (tests/unit/ext_toolbar_test.cpp links toolbar.cpp alone); the
// window, the button and the helpers every page shares are toolbar_window.cpp (exe only).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace UI {
class Element;
class String;
class UserInterface;
}
class Toolbar;   // Game/Toolbar.h: the retail toolbar item (global)

namespace Ext {
namespace Toolbar {

constexpr int32_t kPageIdSpan = 10000;  // the element ids one page owns
constexpr int kMaxPages = 8;            // pages the window shows; more are logged and left out

// The first element id of the page shown at `pageIndex` (0-based); the window's own ids sit below it.
constexpr int32_t IdBaseFor(int pageIndex) {
    return kPageIdSpan * (pageIndex + 1);
}
// Whether element id `id` belongs to the page whose span starts at `idBase`.
constexpr bool OwnsId(int32_t idBase, int32_t id) {
    return id >= idBase && id < idBase + kPageIdSpan;
}

struct Contribution {
    const char* id;        // the Entry id; the page is offered only while that row is active for the game
    const char* caption;   // the tab's text: a key in Dialog/ExtensionsStringTable.rsc
    // "Dialog/RestrictedFoodsPage.rsc:pageRestrictedFoods": our text .rsc, the page's root object
    const char* page;
    // find the elements by name, give the controls ids from `idBase`, fill them from the state
    void (*open)(UI::Element* page, int32_t idBase);
    // a control of this page (local id `id`) was used: write the state now; false = not mine
    bool (*changed)(UI::Element* page, int32_t id);
    void (*close)();       // forget the element pointers
};

// The contributions (defined beside their extensions, src/ext/<id>/toolbar_page.cpp).
const Contribution& RestrictedFoodsPage();

// The list, and the ones whose row is active for this game (into `out`; returns how many there are,
// which may exceed `capacity`).
const Contribution* const* All(int* n);
int ActivePages(const Contribution** out, int capacity);

// Whether the host adds its button: at least one active contributor, and not under the
// gate's command-line faithful profile.
bool Offered();

// ---- helpers every page shares (toolbar_window.cpp; exe only) ----
// A string of Dialog/ExtensionsStringTable.rsc; invalid when the table did not load.
UI::String OurString(const char* key);
// Sets a text element to one of our table's strings. Null-safe: anything but a text element is left alone.
void SetTableText(UI::Element* e, const char* key);
// ASCII lower case of a C string (null = empty).
std::string Lower(const char* s);

// ---- our buttons (toolbar_window.cpp; EXT SEAMS in ToolbarDialog) ----
// The toolbar gets buttons of our own: the extension window's (in the Reports group, while Offered()) and
// the tool buttons of the fork's always-on features (ToolButton below; docs/fork/COPY_MOVE.md). The toolbar
// keeps a null entry in its item array for each, so each retail reader of that array skips null.
constexpr int kMaxButtons = 4;   // our buttons one toolbar group can hold

// A fork feature's toolbar button: a click runs `click` (it starts the feature's tool) instead of opening
// the window. Defined beside its feature; the list is ToolButtons() in toolbar_window.cpp.
struct ToolButton {
    const char* name;          // the button element's name; Click finds the button by it
    const char* group;         // the toolbar group it joins, by GroupName: "" = the main bar
    int32_t     sortPriority;  // it goes before the group's first item with a higher priority (Toolbar +0x50)
    const char* tooltip;       // a key in Dialog/ExtensionsStringTable.rsc
    const char* sprite;        // a sprite in Dialog/ExtensionsSprites.rsc
    bool (*offered)();         // shown when the toolbar is built
    void (*click)();           // the button was clicked
};
const ToolButton& CopyMoveButton();   // src/ext/copy_move/copy_move.cpp

// Where in the sorted `items` (count `n`) of the toolbar group `group` (null = the main bar) our buttons
// go: up to `capacity` slots into `slots`, ascending, and how many. Each slot is an index into `items`
// before any insertion, and two buttons may share one (they keep the returned order). None: no running
// game, the gate's command-line faithful profile, or our resources do not load.
int InsertAt(const ::Toolbar* group, const ::Toolbar* const* items, int n, int* slots, int capacity);
// Name, id, tooltip and icon on the freshly cloned toolbar button `button` (a template-3 clone) at `index`:
// the buttons the last InsertAt placed, in its order.
void Style(UI::Element* button, int32_t index);
// A click on one of our buttons, told apart by the element's name: the window's button opens or closes the
// window, a tool button runs its `click`.
void Click(UI::UserInterface* ui, UI::Element* button);
// Opens the window, or closes it when it is open.
void Toggle(UI::UserInterface* ui);
// PlayerToolManager::Startup's EXT SEAM, after retail reopened its pinned utility windows: reopens the
// window when the town's save had it open (window_state.h) and some page is offered.
void RestoreWindow(UI::UserInterface* ui);

}  // namespace Toolbar
}  // namespace Ext

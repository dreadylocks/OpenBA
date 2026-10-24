// src/ext/enhanced_tab/enhanced_tab.h -- the Options dialog's "Enhanced" tab (blocks AOMENU, FAA-MENU)
//
// NOT RETAIL. A fifth tab added at run time to retail's OptionsDialog by UI::TabElement::AddTab
// (docs/extensions/AO.md §6.3, handins/AOSPIKE.md S6 (a)). The tab's button and page are NEW
// objects in Dialog/EnhancedOptions.rsc (loose .crs, tools/ext_resources.sh). The page is a table
// of rows; each extension contributes its own rows through a Contribution:
//   ambient_occlusion/enhanced_entry.cpp   combo "Ambient occlusion" + slider "AO strength"
//   better_trees/enhanced_entry.cpp        combo "Better trees"
//   max_shadow_res/enhanced_entry.cpp      combo "Max shadow resolution"
//   seamless_loops/enhanced_entry.cpp      combo "Seamless ambient loops"
//   derived_cache/enhanced_entry.cpp       the cache's size + button "Clear cache" (COMPRESSAO)
// A contribution keeps its own element pointers (found by name in the new page); this file holds
// only the dialog key, never OptionsDialog fields (its sizeof 0x1C0 is retail's, asserted). One
// Options dialog is open at a time (it is modal), so the side state is one slot.
//
// The dialog's arms (src/game/Dialog/optionsdialog.cpp):
//   OnOpened  Open(this, tabOptions)   before the last-tab restore, so tab 4 can be restored
//   OnApply   Apply(this)              every contribution's apply (retail's ApplyVideoSettings
//                                      does not know the tab); OK calls Apply too
//   OnClosed  Close(this)              every contribution forgets its pointers
//   OnElementEvent  OnButton(this, id) a click on an id the dialog does not own (a row's button)
// Cancel needs no arm: nothing is written before Apply, and the next Open re-reads the values.
#pragma once

#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "ui/Element.h"
#include "ext/ui/find_element.h"   // Ext::FindElement<T>: the contributions find their elements by name

namespace UI {
class TabElement;
}

namespace Ext {
namespace EnhancedTab {

struct Contribution {
    const char* name;                    // for diagnostics
    void (*open)(UI::Element* page);     // find the elements by name, fill them from the live values
    void (*apply)();                     // read the elements, write the values (the caller flushes)
    void (*close)();                     // forget the element pointers
    // A button of the row was clicked (its element id, set by `open`): true when it was the row's.
    // Null for a row without buttons. Acts at once (Cancel does not undo it).
    bool (*button)(int32_t elementId);
};

// The contributions (defined beside their extensions).
const Contribution& AmbientOcclusionEntry();
const Contribution& BetterTreesEntry();
const Contribution& MaxShadowResEntry();
const Contribution& SeamlessLoopsEntry();
const Contribution& DerivedCacheEntry();   // src/ext/derived_cache/enhanced_entry.cpp: size + "Clear cache"

// Whether this build offers the tab: always, except under the gate (`/profile:faithful` from the
// command line), whose menus stay exactly retail's (Ext::GateProfileForced). Deliberately NOT a
// row's value: the player turns the extensions ON from this tab.
bool Offered();

// Adds the tab to `tabs` and opens every contribution on the new page. Returns false (and adds
// nothing) when not Offered() or the resources did not load (the loose .crs files are not
// deployed); the dialog then has its four retail tabs.
bool Open(const void* dialog, UI::TabElement* tabs);

// No-op unless `dialog` is the one Open filled. The caller flushes settings.cfg (OnApply's
// WriteSettings).
void Apply(const void* dialog);

// Forgets the element pointers (the dialog is closing). No-op for any other dialog.
void Close(const void* dialog);

// A button click the dialog does not own (OptionsDialog::OnElementEvent, event 0x12): true when a
// contribution's button took it. False for any other dialog.
bool OnButton(const void* dialog, int32_t elementId);

}  // namespace EnhancedTab
}  // namespace Ext

// src/ext/seamless_loops/enhanced_entry.cpp -- ext.seamless_loops's row of the Enhanced tab (block SEAMLESS)
//
// NOT RETAIL. The combo "Seamless ambient loops": Off / On (next start) = the bool row
// `ext.seamless_loops`. SetAndRecord records the key; the decode seam was set at boot from the value the
// game started with (registry_boot.cpp), so the change is heard after the next start. The combo shows the
// REQUESTED value: what the next start will use.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboSeamlessLoops");
    const Entry* row = FindEntry("seamless_loops");
    const int item = row != nullptr && Requested(*row) ? 1 : 0;
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[SeamlessLoops] combo=%p item=%d\n", static_cast<void*>(g_combo), item);
}

void Apply() {
    if (g_combo == nullptr) return;
    const Entry* row = FindEntry("seamless_loops");
    if (row == nullptr) return;
    const bool on = g_combo->_currentItem == 1;
    if (on != Requested(*row)) SetAndRecord(*row, on);
    std::fprintf(stderr, "[SeamlessLoops] Apply: item=%d requested=%d (takes effect at the next start)\n",
                 g_combo->_currentItem, Requested(*row) ? 1 : 0);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& SeamlessLoopsEntry() {
    static const Contribution c = { "seamless_loops", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext

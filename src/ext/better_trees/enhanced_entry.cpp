// src/ext/better_trees/enhanced_entry.cpp -- Better trees' row of the Enhanced tab
//
// NOT RETAIL. The combo "Better trees": Off / On / On + distance fill =
// BetterTrees::Current().mode 0 / 1 / 2. It is the master of the bool row `ext.better_trees`: Off turns
// the row off (the stored `ext.foliage_aa_mode` is kept), the others set it on and the mode key.
// BetterTrees::Set records the keys and Sync()s the render side, so the next frame follows Apply / OK.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/better_trees/better_trees.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboBetterTrees");
    const int mode = BetterTrees::Current().mode;
    if (g_combo != nullptr) g_combo->SetCurrentItem(mode);
    std::fprintf(stderr, "[BetterTrees] combo=%p mode=%d\n", static_cast<void*>(g_combo), mode);
}

void Apply() {
    if (g_combo == nullptr) return;
    BetterTrees::Params p;
    p.mode = g_combo->_currentItem;
    BetterTrees::Set(p);
    std::fprintf(stderr, "[BetterTrees] Apply: mode=%d\n", BetterTrees::Current().mode);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& BetterTreesEntry() {
    static const Contribution c = { "better_trees", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext

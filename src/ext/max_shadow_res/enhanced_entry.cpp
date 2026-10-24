// src/ext/max_shadow_res/enhanced_entry.cpp -- ext.max_shadow_res's row of the Enhanced tab (blocks FAA-MENU, FAA-SHADOW2)
//
// NOT RETAIL. The combo "Max shadow resolution" Off / 4096 / 8192: Off = the bool row
// `ext.max_shadow_res` off (the stored `ext.max_shadow_size` is kept), 4096 / 8192 = the row on and
// the valued key `ext.max_shadow_size` (the sun map's size while the retail Shadow Resolution combo
// is at High; Medium / Low stay 1024 / 512). This directory holds only the tab row: the seam is
// Graphics::SunLight::_extMaxShadowRes / _extMaxShadowSize (docs/extensions/handins/FAA-SHADOW.md,
// FAA-SHADOW2.md). SetAndRecord / SetValueAndRecord record the keys and request the map's rebuild on
// this (the main) thread, so the next frame follows Apply / OK.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/registry.h"

#include "ui/ComboElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement* g_combo = nullptr;

// Combo item <-> the choice (registry.h SetMaxShadowResAndRecord): 0 Off, 1 = 4096, 2 = 8192.
constexpr int32_t kItemSize[] = { 0, 4096, 8192 };

void Open(UI::Element* page) {
    g_combo = FindElement<UI::ComboElement>(page, "comboMaxShadowRes");
    const int32_t choice = MaxShadowResChoice();
    const int item = choice == 8192 ? 2 : (choice == 4096 ? 1 : 0);
    if (g_combo != nullptr) g_combo->SetCurrentItem(item);
    std::fprintf(stderr, "[MaxShadowRes] combo=%p item=%d (choice %d)\n", static_cast<void*>(g_combo), item,
                 choice);
}

void Apply() {
    if (g_combo == nullptr) return;
    const int item = g_combo->_currentItem;
    const int32_t choice = SetMaxShadowResAndRecord(item >= 0 && item <= 2 ? kItemSize[item] : 0);
    std::fprintf(stderr, "[MaxShadowRes] Apply: item=%d choice=%d\n", item, choice);
}

void Close() { g_combo = nullptr; }

}  // namespace

const Contribution& MaxShadowResEntry() {
    static const Contribution c = { "max_shadow_res", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext

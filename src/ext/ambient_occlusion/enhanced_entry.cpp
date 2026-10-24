// src/ext/ambient_occlusion/enhanced_entry.cpp -- AO's rows of the Enhanced tab (block AOMENU)
//
// NOT RETAIL. The combo "Ambient occlusion" (Off / Low / Medium / High = Params::quality 0..3) and the
// slider "AO strength" (0..30 = Params::opacity); enhanced_tab.h has the contract.

#include "ext/ambient_occlusion/params.h"
#include "ext/enhanced_tab/enhanced_tab.h"

#include "ui/ComboElement.h"
#include "ui/SliderElement.h"

#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

UI::ComboElement*  g_quality = nullptr;
UI::SliderElement* g_opacity = nullptr;

void Open(UI::Element* page) {
    g_quality = FindElement<UI::ComboElement>(page, "comboAoQuality");
    g_opacity = FindElement<UI::SliderElement>(page, "sliderAoOpacity");
    const AmbientOcclusion::Params& now = AmbientOcclusion::Current();
    if (g_quality != nullptr) g_quality->SetCurrentItem(now.quality);
    if (g_opacity != nullptr) {
        g_opacity->_minimum = 0.0f;      // like the UI scale slider (OptionsDialog::OnOpened)
        g_opacity->_maximum = 30.0f;     // ext.ao_opacity's range (registry.cpp kValueKeys; the user's ruling 2026-10-14)
        g_opacity->_value = static_cast<float>(now.opacity);
    }
    std::fprintf(stderr, "[AmbientOcclusion] combo=%p slider=%p quality=%d opacity=%d\n",
                 static_cast<void*>(g_quality), static_cast<void*>(g_opacity), now.quality, now.opacity);
}

void Apply() {
    AmbientOcclusion::Params p = AmbientOcclusion::Current();
    if (g_quality != nullptr) p.quality = g_quality->_currentItem;
    if (g_opacity != nullptr) p.opacity = static_cast<int>(g_opacity->_value + 0.5f);
    AmbientOcclusion::Set(p);
    const AmbientOcclusion::Params& now = AmbientOcclusion::Current();
    std::fprintf(stderr, "[AmbientOcclusion] Apply: quality=%d opacity=%d\n", now.quality, now.opacity);
}

void Close() { g_quality = nullptr; g_opacity = nullptr; }

}  // namespace

const Contribution& AmbientOcclusionEntry() {
    static const Contribution c = { "ambient_occlusion", Open, Apply, Close, nullptr };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext

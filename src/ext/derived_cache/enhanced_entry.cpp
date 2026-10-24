// src/ext/derived_cache/enhanced_entry.cpp -- the derived-data cache's row of the Enhanced tab (block COMPRESSAO)
//
// NOT RETAIL (docs/extensions/handins/COMPRESSAO.md). The text "Data cache on disk: <size>" and the
// button "Clear cache". The size is the shared store's (or the files', when this run did not open
// the cache: the row off, the faithful profile chosen in settings.cfg, another instance holding it).
// The button acts at once and cannot be undone by Cancel: the shared store stops reading and writing
// for the rest of the run, and the files are deleted at the next start (the running store holds them
// open). Element ids: kClearButtonId, far above the Options dialog's own (10..12, input binds from 13).
//
// THE WIDTH. The Options dialog sizes itself to its content, and the text is in the page's first
// column: a size that changed the text's length ("4096 MB" -> "0 MB (cleared at the next start)")
// changed the column's width and so the dialog's. Open measures the text with every value it can
// show (the widest size the cap allows, and the cleared one) and makes the widest its minimum width,
// so the dialog keeps one size while it is open.

#include "ext/enhanced_tab/enhanced_tab.h"
#include "ext/derived_cache/derived_cache.h"

#include "ui/ButtonElement.h"
#include "ui/TextElement.h"

#include <cstdint>
#include <cstdio>

namespace Ext {
namespace EnhancedTab {

namespace {

constexpr int32_t kClearButtonId = 0x0DC00001;

UI::TextElement*   g_text   = nullptr;
UI::ButtonElement* g_button = nullptr;

void FormatSize(char (&text)[64], bool cleared, unsigned long long mb) {
    if (cleared)
        std::snprintf(text, sizeof(text), "0 MB (cleared at the next start)");
    else
        std::snprintf(text, sizeof(text), "%llu MB", mb);
}

void ShowSize() {
    if (g_text == nullptr) return;
    char text[64];
    FormatSize(text, DerivedCache::SharedClearPending(), (DerivedCache::SharedBytes() + (1u << 19)) >> 20);
    g_text->SetParameter(0, System::String(text));
}

// The text's width with every value ShowSize can give it; the widest becomes its minimum width.
void FixWidth() {
    if (g_text == nullptr) return;
    // 88888 MB: as many digits as the largest cap (ext.derived_cache_mb 65536), in the widest digit.
    const bool cleared[2]             = { false, true };
    const unsigned long long mb[2]    = { 88888ull, 0ull };
    int32_t widest = g_text->_minWidth;
    for (int i = 0; i < 2; ++i) {
        char text[64];
        FormatSize(text, cleared[i], mb[i]);
        g_text->SetParameter(0, System::String(text));
        g_text->ComputeSize();
        if (g_text->GetLayoutWidth() > widest) widest = g_text->GetLayoutWidth();
    }
    g_text->_minWidth = widest;
}

void Open(UI::Element* page) {
    g_text   = FindElement<UI::TextElement>(page, "textDerivedCache");
    g_button = FindElement<UI::ButtonElement>(page, "buttonDerivedCacheClear");
    if (g_button != nullptr) g_button->SetElementId(kClearButtonId);
    FixWidth();
    ShowSize();
    std::fprintf(stderr, "[DerivedCache] Enhanced tab: text=%p button=%p\n", static_cast<void*>(g_text),
                 static_cast<void*>(g_button));
}

void Apply() {
}

void Close() {
    g_text   = nullptr;
    g_button = nullptr;
}

bool Button(int32_t elementId) {
    if (elementId != kClearButtonId) return false;
    DerivedCache::ClearShared();
    ShowSize();
    return true;
}

}  // namespace

const Contribution& DerivedCacheEntry() {
    static const Contribution c = { "derived_cache", Open, Apply, Close, Button };
    return c;
}

}  // namespace EnhancedTab
}  // namespace Ext

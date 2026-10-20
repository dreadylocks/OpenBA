// ============================================================================
// tests/unit/ext_rsc_text_test.cpp -- our .rsc text dialect (KITFREE, RSCTEXT)
// ============================================================================
// src/ext/resources/text_parser.{h,cpp} over the three embedded files
// (gen/ext_rsc_text.h, the exact text the exe carries) and ResolveReference.
// The object construction needs the live factory and the game's packages, so
// it is checked in the exe (src/diag/rsc_text_check.cpp + tools/ext_rsc_check.py).
// ============================================================================
#include "unit.h"
#include "ext/load_notice.h"
#include "ext/resources/text_parser.h"
#include "ext_rsc_text.h"

#include <cstring>
#include <initializer_list>
#include <string>

using namespace Ext::RscText;

namespace {

const EmbeddedFile* Embedded(const char* path) {
    for (int i = 0; i < kEmbeddedCount; ++i)
        if (std::strcmp(kEmbedded[i].path, path) == 0) return &kEmbedded[i];
    return nullptr;
}

bool ParseEmbedded(const char* path, File& f) {
    const EmbeddedFile* e = Embedded(path);
    if (e == nullptr) return false;
    std::string err;
    const bool ok = Parse(e->text, e->size, f, err);
    if (!ok) std::printf("  %s: %s\n", path, err.c_str());
    return ok;
}

}  // namespace

UNIT_TEST(ext_rsc_text_embedded_files) {
    CHECK(kEmbeddedCount == 5);
    CHECK(Embedded("Dialog/Extensions.rsc") != nullptr);
    CHECK(Embedded("Dialog/EnhancedOptions.rsc") != nullptr);
    CHECK(Embedded("Dialog/ExtensionsStringTable.rsc") != nullptr);
    CHECK(Embedded("Dialog/ExtensionsNeighbour.rsc") != nullptr);   // ext.neighbour's panel (this fork)
    CHECK(Embedded("Dialog/ExtensionsLobby.rsc") != nullptr);       // ext.neighbour's lobby (M2)
}

// ext.neighbour's panel (this fork): it parses, its dialog is modal without pausing the game, and
// the chat box is an EditDescription (text_reader.cpp has its schema).
UNIT_TEST(ext_rsc_text_parse_neighbour_panel) {
    File f;
    CHECK(ParseEmbedded("Dialog/ExtensionsNeighbour.rsc", f));
    const Object* dlg = f.Find("resource");
    CHECK(dlg != nullptr && dlg->type == "Dialog");
    if (dlg) {
        const Field* flags = dlg->body.Find("_flags");
        CHECK(flags && flags->tokens.size() == 2 && flags->tokens[0].text == "Modal");
    }
    const Object* edit = f.Find("editChat");
    CHECK(edit && edit->type == "EditDescription" && edit->base == "NewGame.rsc:editName");
}

UNIT_TEST(ext_rsc_text_parse_extensions) {
    File f;
    CHECK(ParseEmbedded("Dialog/Extensions.rsc", f));
    const Object* dlg = f.Find("resource");
    CHECK(dlg != nullptr && dlg->type == "Dialog" && dlg->base.empty());
    if (dlg) {
        const Field* flags = dlg->body.Find("_flags");
        CHECK(flags && !flags->isArray && flags->tokens.size() == 3 && flags->tokens[1].text == "PauseGame");
        const Field* refs = dlg->body.Find("_references");
        CHECK(refs && refs->isArray && refs->tokens.size() == 3 && refs->tokens[0].quoted);
        const Field* tpl = dlg->body.Find("_templates");
        CHECK(tpl && tpl->isArray && tpl->tokens.size() == 1 && tpl->tokens[0].text == "entryRow");
    }
    const Object* row = f.Find("entryRow");
    CHECK(row && row->type == "LayoutDescription");
    if (row) {
        const Field* items = row->body.Find("_elements");
        CHECK(items && items->isArray && items->blocks.size() == 4 && items->tokens.empty());
        if (items && items->blocks.size() == 4) {
            const Field* off = items->blocks[1].Find("_xOffset");
            CHECK(off && off->tokens.size() == 1 && off->tokens[0].text == "4" && !off->tokens[0].quoted);
            const Field* anchor = items->blocks[3].Find("_xAnchorTo");
            CHECK(anchor && anchor->tokens[0].quoted && anchor->tokens[0].text == "blurb");
        }
    }
    const Object* title = f.Find("labelTitle");
    CHECK(title && title->base == "Mod.rsc:labelTitle" && title->type == "LabelDescription");
    const Object* notice = f.Find("extensionNotInBuild");
    CHECK(notice && notice->base == "SaveLoad.rsc:modsMissing");
    if (notice) {
        const Field* ov = notice->body.Find("_override");
        CHECK(ov && ov->blocks.size() == 3 && ov->blocks[1].fields.size() == 1);
    }
    CHECK(f.objects.size() == 36);
}

UNIT_TEST(ext_rsc_text_parse_enhanced_and_strings) {
    File e;
    CHECK(ParseEmbedded("Dialog/EnhancedOptions.rsc", e));
    const Object* label = e.Find("labelEnhanced");
    CHECK(label && label->type == "LabelDescription");
    if (label) {
        const Field* font = label->body.Find("_font");
        CHECK(font && font->tokens[0].text == "Font\\FontSmall.rsc");   // a backslash is not an escape
        const Field* pos = label->body.Find("_labelPosition");
        CHECK(pos && pos->tokens[0].text == "TextBottom");
    }
    const Object* table = e.Find("enhancedOptions");
    CHECK(table && table->body.Find("_items") && table->body.Find("_items")->blocks.size() == 10);
    const Object* opt = e.Find("textAoOption");
    CHECK(opt && opt->body.Find("_color") && opt->body.Find("_color")->tokens[0].text == "0xFFC0C0C0");

    File s;
    CHECK(ParseEmbedded("Dialog/ExtensionsStringTable.rsc", s));
    CHECK(s.objects.size() == 1 && s.objects[0].type == "StringTable" && s.objects[0].name == "resource");
    const Field* strings = s.objects[0].body.Find("_strings");
    CHECK(strings && strings->blocks.size() == 41);
    if (strings && strings->blocks.size() == 41) {
        const Field* t = strings->blocks[40].Find("_text");
        CHECK(t && t->tokens[0].text.find("~n~n@0 [@1]") != std::string::npos);
    }
}

UNIT_TEST(ext_rsc_text_syntax_errors) {
    File f;
    std::string err;
    CHECK(!Parse("Dialog x { int _a = 1 }", 22, f, err) && err.find("line 1") == 0);   // no ';'
    CHECK(!Parse("Dialog x : base { }", 19, f, err));                                  // base not quoted
    CHECK(!Parse("Dialog x { int _a [ 1 { } ] }", 29, f, err));                        // mixed array
    CHECK(!Parse("Dialog x { String _s = \"open; }", 31, f, err));                     // unterminated
    const char* ok = "// c\nT n : \"B.rsc\" { bool _b = true; Flags _f = A | B; X _x [ ]\n}";
    CHECK(Parse(ok, std::strlen(ok), f, err) && f.objects.size() == 1 && f.objects[0].body.fields.size() == 3);
}

UNIT_TEST(ext_rsc_text_resolve_reference) {
    CHECK(ResolveReference("SharedElements.rsc:dialogBorder", "Dialog/", "Extensions.rsc") == "Dialog/SharedElements.rsc:dialogBorder");
    CHECK(ResolveReference("dialogRoot", "Dialog/", "Extensions.rsc") == "Dialog/Extensions.rsc:dialogRoot");
    CHECK(ResolveReference("Font/FontSmall.rsc", "Dialog/", "Extensions.rsc") == "Font/FontSmall.rsc:resource");
    CHECK(ResolveReference("Font\\FontMedium.rsc", "Dialog/", "EnhancedOptions.rsc") == "Font/FontMedium.rsc:resource");
    CHECK(ResolveReference("MessageBox.rsc", "Dialog/", "Extensions.rsc") == "Dialog/MessageBox.rsc:resource");
    CHECK(ResolveReference("EnhancedOptions.rsc:aoStrings", "Dialog/", "EnhancedOptions.rsc") == "Dialog/EnhancedOptions.rsc:aoStrings");
    CHECK(ResolveReference("ExtensionsStringTable.rsc", "Dialog/", "Extensions.rsc") == "Dialog/ExtensionsStringTable.rsc:resource");
}

// Every `ext:` load notice DoLoad can pick (src/ext/load_notice.h) names a Dialog
// object of the embedded Dialog/Extensions.rsc, under a prefix InitText registers
// ("Dialog/Extensions"). DoLoad registers that provider on demand before the notice
// is loaded, under any profile (known_port_issues row 1330: under /profile:faithful
// the provider was never registered and the null Dialog crashed MessageDialog).
UNIT_TEST(ext_rsc_text_load_notices_are_embedded) {
    using N = Ext::PseudoModNotice;
    File f;
    CHECK(ParseEmbedded("Dialog/Extensions.rsc", f));
    for (N n : {N::NotInBuild, N::NewerVersion, N::TurnedOff}) {
        const char* path = Ext::PseudoModNoticeDialog(n);
        CHECK(path != nullptr);
        if (path == nullptr) continue;
        const std::string p(path);
        const size_t colon = p.find(':');
        CHECK(colon != std::string::npos);
        if (colon == std::string::npos) continue;
        CHECK(p.substr(0, colon) == "Dialog/Extensions.rsc");
        CHECK(p.compare(0, std::strlen("Dialog/Extensions"), "Dialog/Extensions") == 0);
        const Object* o = f.Find(p.substr(colon + 1).c_str());
        CHECK(o != nullptr && o->type == "Dialog" && o->base == "SaveLoad.rsc:modsMissing");
    }
}

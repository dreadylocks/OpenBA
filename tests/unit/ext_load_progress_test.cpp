// ============================================================================
// tests/unit/ext_load_progress_test.cpp -- ext.load_progress (block LOADPROGRESS)
// ============================================================================
// The registry row (class A, default ON, off under /profile:faithful, never a
// pseudo-mod) and the text line of src/ext/load_progress/load_progress.h: the
// loop's own list and everything after the first list read "Loading game data",
// the k-th enabled mod's list of the first one reads "Loading mod k of n".
// docs/extensions/handins/LOADPROGRESS.md.
// ============================================================================
#include "unit.h"
#include "ext/registry.h"
#include "ext/load_progress/load_progress.h"
#include "io/settings_file.h"

#include <string>

namespace {

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const IO::SettingsFile::Entries* entries = static_cast<const IO::SettingsFile::Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

bool Resolved(const char* text, const char* cmd = nullptr) {
    IO::SettingsFile::Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, cmd, nullptr).ext.load_progress;
}

}  // namespace

UNIT_TEST(ext_load_progress_row) {
    const Ext::Entry* e = Ext::FindEntry("load_progress");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(e->defaultOn);   // class A ships ON (DESIGN.md section 2)
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.load_progress");

    CHECK(Resolved(""));
    CHECK(Resolved("ext.load_progress=1\n"));
    CHECK(!Resolved("ext.load_progress=0\n"));
    CHECK(!Resolved("ext.load_progress=1\nprofile=faithful\n"));
    CHECK(!Resolved("ext.load_progress=1\n", "faithful"));     // the gate's /profile:faithful

    Ext::State on{};
    on.ext.load_progress = true;
    Ext::Install(on, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().load_progress);
    CHECK(Ext::PseudoModsForSave(nullptr, 0) == 0);
    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_load_progress_text_follows_the_first_list) {
    using Ext::LoadProgress::Line;
    using Ext::LoadProgress::TextFor;

    // Game/GlobalResources.rsc, before the mod walk.
    CHECK(TextFor(0, 0, 0).line == Line::GameData);
    CHECK(TextFor(0, 7, 0).line == Line::GameData);

    // The k-th enabled mod's list.
    const Ext::LoadProgress::Text t = TextFor(3, 7, 0);
    CHECK(t.line == Line::Mod);
    CHECK(t.mod == 3);
    CHECK(t.mods == 7);
    CHECK(TextFor(7, 7, 0).line == Line::Mod);

    // Out of range never prints a wrong number.
    CHECK(TextFor(8, 7, 0).line == Line::GameData);
    CHECK(TextFor(1, 0, 0).line == Line::GameData);

    // After the first list every later walk (the splash loops' re-stamps) is game data.
    CHECK(TextFor(2, 7, 1).line == Line::GameData);
    CHECK(TextFor(0, 7, 2).line == Line::GameData);

    CHECK(Ext::LoadProgress::SameText(TextFor(3, 7, 0), TextFor(3, 7, 0)));
    CHECK(!Ext::LoadProgress::SameText(TextFor(3, 7, 0), TextFor(4, 7, 0)));
}

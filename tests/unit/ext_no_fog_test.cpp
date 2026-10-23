// ============================================================================
// tests/unit/ext_no_fog_test.cpp -- ext.no_fog
// ============================================================================
// src/ext/no_fog/no_fog.h without the game: the registry row (class A, OFF by default, no lock, the profile
// keys), a live toggle that is not held by the game's lock, and the fog maximum the seam writes (retail's own
// value to the last bit with the row off, zero with it on). The seam itself is
// WeatherSimulation::BlendLighting (weather_sun.cpp), which needs the running game.
// ============================================================================
#include "unit.h"
#include "ext/no_fog/no_fog.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

uint32_t Bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

}  // namespace

UNIT_TEST(ext_no_fog_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("no_fog");
    CHECK(e != nullptr);
    if (e == nullptr) {
        return;
    }
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);              // a picture-only change: no sim effect, nothing saved
    CHECK(!e->defaultOn);                        // a class A exception like eye_camera: the player's choice
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.no_fog");
    CHECK(std::strstr(e->description, "fog") != nullptr);
    CHECK(!ResolveText("").ext.no_fog);                         // absent key = the default (off)
    CHECK(!ResolveText("ext.no_fog=0\n").ext.no_fog);
    CHECK(ResolveText("ext.no_fog=1\n").ext.no_fog);
    CHECK(!ResolveText("profile=faithful\n").ext.no_fog);       // the faithful profile turns it off
}

UNIT_TEST(ext_no_fog_toggle_is_live_under_the_game_lock) {
    const Ext::Entry* e = Ext::FindEntry("no_fog");
    if (e == nullptr) {
        CHECK(false);
        return;
    }
    int game = 0;
    Ext::Install(ResolveText("ext.no_fog=1\n"), Ext::Profile::PerEntry, false);
    Ext::LockForGame(&game);
    CHECK(Ext::Active(*e));
    CHECK(!Ext::EntryLocked(*e));                // only class B / C rows are held by the lock
    CHECK(Ext::SetRequested(*e, false));         // takes effect at once, in the running game
    CHECK(!Ext::Active(*e));
    CHECK(Ext::SetRequested(*e, true));
    CHECK(Ext::Active(*e));
    Ext::UnlockForGame(&game);
    Ext::Install(ResolveText(""), Ext::Profile::PerEntry, false);
}

UNIT_TEST(ext_no_fog_fog_max) {
    // row off: the very same bits come back, for the default record and for any weather's value
    const float values[] = {0.95f, 0.4f, 0.0f, 1.0f, 0.123456789f};
    for (float v : values) {
        CHECK(Bits(Ext::NoFog::FogMax(v, false)) == Bits(v));
    }
    // row on: zero whatever the record held
    for (float v : values) {
        CHECK(Bits(Ext::NoFog::FogMax(v, true)) == Bits(0.0f));
    }
    CHECK(Ext::NoFog::kFogMax == 0.0f);
}

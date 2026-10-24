// ============================================================================
// tests/unit/persistence_settings_test.cpp — settings.cfg on disk (Phase D.4 SETTINGS E.3)
// ============================================================================
// IO::SettingsStore::Load / Store over a fresh temp directory, through the
// real Win32 file bodies (src/platform/win/settings_win.cpp): file read,
// the read-only legacy import when the file is absent (with an INJECTED
// reader: no test reads the real registry), the merge on write (disk-only
// keys survive, the map wins), the parent directory created on first write,
// no tmp file left behind.
//
// IO::Persistence::ReadSettings / WriteSettings themselves are NOT driven
// here: a Persistence object embeds its save/load worker (a System::Thread,
// whose ctor needs the engine), and the map insert is rtavl::StringMapInsert
// (avl_trees.cpp, the registry-era Set cores), neither of which links in the
// unit harness. The two members are thin: Clear + Load + insert, and walk +
// Store. Their run-time check is the coordinator's registry-vs-file compare.
// ============================================================================
#include "unit.h"
#include "io/settings_file.h"
#include "io/settings_store.h"
#include "platform_settings.h"   // Platform::SetUserDirOverride (the BANISHED_USER_DIR test)

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using IO::SettingsFile::Entries;
using IO::SettingsFile::Entry;
using IO::SettingsStore::Load;
using IO::SettingsStore::Source;
using IO::SettingsStore::Store;

#define CHECK_SSTREQ(a, b) do { const std::string _sa = (a); const std::string _sb = (b);     CHECK_STREQ(_sa.c_str(), _sb.c_str()); } while (0)

namespace {

// A fresh, empty directory under the OS temp folder; removed by the dtor.
struct TempDir {
    fs::path dir;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() /
              ("banished_settings_unit_" + std::to_string(static_cast<long long>(stamp)));
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string File(const char* rel) const { return (dir / rel).u8string(); }
};

// The host spelling of an engine-style path. The store takes the engine's '\'
// (DefaultPath is "<Documents>\Banished\settings.cfg" on every OS) and the POSIX
// file layer turns it into '/'; the standard library used to check the result does
// not, so a test that reads back through it converts first (Phase D.4 POSIX).
std::string HostPath(std::string path) {
#if !defined(_WIN32)
    for (char& c : path) if (c == '\\') c = '/';
#endif
    return path;
}

std::string ReadText(const std::string& path) {
    // The raw bytes, through the standard library rather than the store's own
    // reader (which is under test). Temp paths here are ASCII.
    std::string text;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return text;
}

void WriteText(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

int g_legacyCalls = 0;

bool FakeLegacy(void (*emit)(void*, const char*, const char*), void* ctx) {
    ++g_legacyCalls;
    emit(ctx, "VideoWidth", "1280");
    emit(ctx, "ModEnabled_Some Mod", "true");
    emit(ctx, "Ach7", "true");
    return true;
}

bool NoLegacy(void (*)(void*, const char*, const char*), void*) {
    ++g_legacyCalls;
    return false;
}

}  // namespace

UNIT_TEST(settings_store_load_reads_the_file_not_the_legacy_store) {
    TempDir t;
    const std::string path = t.File("settings.cfg");
    WriteText(path, "VideoWidth=1920\nAudioMusicVolume=50\n");

    g_legacyCalls = 0;
    Entries e{Entry{"stale", "x"}};
    CHECK(Load(path, e, &FakeLegacy) == Source::File);
    CHECK_EQ(g_legacyCalls, 0);
    CHECK_EQ(e.size(), static_cast<size_t>(2));
    CHECK_SSTREQ(e[0].key, "VideoWidth");
    CHECK_SSTREQ(e[0].value, "1920");
    CHECK_SSTREQ(e[1].key, "AudioMusicVolume");
}

UNIT_TEST(settings_store_absent_file_imports_read_only) {
    TempDir t;
    const std::string path = t.File("settings.cfg");

    g_legacyCalls = 0;
    Entries e;
    CHECK(Load(path, e, &FakeLegacy) == Source::Legacy);
    CHECK_EQ(g_legacyCalls, 1);
    CHECK_EQ(e.size(), static_cast<size_t>(3));
    CHECK_SSTREQ(e[1].key, "ModEnabled_Some Mod");
    CHECK_SSTREQ(e[1].value, "true");
    // The import writes nothing: the first Store materialises the file.
    CHECK(!fs::exists(fs::u8path(path)));
}

UNIT_TEST(settings_store_nothing_anywhere_is_an_empty_map) {
    TempDir t;
    const std::string path = t.File("settings.cfg");
    Entries e{Entry{"stale", "x"}};
    CHECK(Load(path, e, &NoLegacy) == Source::None);
    CHECK(e.empty());
    Entries e2{Entry{"stale", "x"}};
    CHECK(Load(path, e2, nullptr) == Source::None);
    CHECK(e2.empty());
}

UNIT_TEST(settings_store_first_write_creates_the_directory_sorted) {
    TempDir t;
    const std::string path = t.File("Banished\\settings.cfg");
    CHECK(Store(path, Entries{Entry{"VideoWidth", "1280"}, Entry{"Ach7", "true"}}));
    CHECK_SSTREQ(ReadText(HostPath(path)), "Ach7=true\nVideoWidth=1280\n");
    CHECK(!fs::exists(fs::u8path(HostPath(path + ".tmp"))));
}

UNIT_TEST(settings_store_write_merges_disk_only_keys_survive) {
    TempDir t;
    const std::string path = t.File("settings.cfg");
    // What retail's never-deleting WriteSettings leaves after `-reset`: the
    // map lost Ach7 and the mod entry, the store keeps them.
    WriteText(path, "Ach7=true\nModEnabled_Foo=true\nVideoWidth=1920\n");
    CHECK(Store(path, Entries{Entry{"VideoWidth", "-1"}, Entry{"VideoHeight", "-1"}}));
    CHECK_SSTREQ(ReadText(path),
                 "Ach7=true\nModEnabled_Foo=true\nVideoHeight=-1\nVideoWidth=-1\n");
}

UNIT_TEST(settings_store_round_trip_through_load) {
    TempDir t;
    const std::string path = t.File("settings.cfg");
    const Entries in{Entry{"ModEnabled_A = B", " spaced "},
                     Entry{"ModIndex_\xC3\xA9t\xC3\xA9", "3"}};
    CHECK(Store(path, in));
    Entries out;
    CHECK(Load(path, out, &FakeLegacy) == Source::File);
    CHECK_EQ(out.size(), static_cast<size_t>(2));
    // Sorted by strcmp: "ModE..." < "ModI...".
    CHECK_SSTREQ(out[0].key, "ModEnabled_A = B");
    CHECK_SSTREQ(out[0].value, " spaced ");
    CHECK_SSTREQ(out[1].key, "ModIndex_\xC3\xA9t\xC3\xA9");
}

UNIT_TEST(settings_store_utf8_path) {
    TempDir t;
    const std::string path = t.File("dossier \xC3\xA9t\xC3\xA9\\settings.cfg");
    CHECK(Store(path, Entries{Entry{"k", "v"}}));
    Entries out;
    CHECK(Load(path, out, &NoLegacy) == Source::File);
    CHECK_EQ(out.size(), static_cast<size_t>(1));
}

UNIT_TEST(settings_store_default_path_names_the_save_folder) {
    // A read-only query of the documents folder; nothing is opened.
    std::string path;
    CHECK(IO::SettingsStore::DefaultPath(path));
    const std::string tail = "\\Banished\\settings.cfg";
    CHECK(path.size() > tail.size());
    CHECK(path.compare(path.size() - tail.size(), tail.size(), tail) == 0);
}

UNIT_TEST(settings_store_user_dir_override_is_the_whole_folder) {
    // BANISHED_USER_DIR (Platform::SetUserDirOverride, src/platform/common/user_dir_override.cpp)
    // replaces "<Documents>\Banished" as a whole: settings.cfg sits directly inside it, a
    // trailing separator is dropped, and clearing it restores the documents-folder path.
    std::string before;
    CHECK(IO::SettingsStore::DefaultPath(before));

    Platform::SetUserDirOverride("D:\\pool\\_userdata\\");
    CHECK(Platform::UserDirOverride() != nullptr);
    CHECK(std::string(Platform::UserDirOverride()) == "D:\\pool\\_userdata");
    std::string path;
    CHECK(IO::SettingsStore::DefaultPath(path));
    CHECK(path == "D:\\pool\\_userdata\\settings.cfg");

    Platform::SetUserDirOverride("");
    CHECK(Platform::UserDirOverride() == nullptr);
    Platform::SetUserDirOverride(nullptr);
    CHECK(Platform::UserDirOverride() == nullptr);
    std::string after;
    CHECK(IO::SettingsStore::DefaultPath(after));
    CHECK(after == before);
}

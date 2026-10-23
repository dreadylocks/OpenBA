// ============================================================================
// tests/unit/platform_mac_data_test.cpp -- saves, settings and read paths on macOS
// ============================================================================
// docs/macos/DATA_AUDIO.md. The macOS data layer is the shared POSIX one
// (src/platform/darwin/{file,settings}_darwin.cpp, the Darwin twins of the POSIX pair); this file pins what a Mac user's folders
// need and the other POSIX tests do not:
//   * DocumentsDir is <home>/Documents, so saves land in ~/Documents/Banished/Save;
//   * a path with spaces and non-ASCII characters (a home folder named "José García")
//     works through the settings writer, the temp-file -> FileMove save sequence the engine
//     uses (FileSystem::CreateFile), FindFirst("*.sav") and FileOpenRead;
//   * a save written that way reads back byte for byte;
//   * the READ primitives find a file spelled in another case (APFS defaults to
//     case-insensitive and passes natively; on a case-SENSITIVE volume the fallback in
//     file_darwin.cpp resolves it: run this binary with TMPDIR on such a volume to hit it);
//   * a temp file that becomes a save is 0644, not mkstemp's 0600.
// Everything lives under the OS temp directory (TMPDIR); the user's Documents is never touched.
// ============================================================================
#if defined(__APPLE__)

#include "unit.h"

#include "platform_file.h"
#include "platform_settings.h"
#include "platform/posix/posix_path.h"
#include "app/options.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(__APPLE__)
// Test seam for settings_darwin.cpp's DocumentsDir, which reads Opt().userProfile. The unit
// binary does not link src/app/options.cpp, so this supplies Opt() with every option at
// its default (no profile redirect): DocumentsDir answers the password database's home.
const Options& Opt() {
    static const Options options;
    return options;
}
#endif

namespace {

using Platform::PathChar;

struct MacTempDir {
    fs::path dir;
    MacTempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        // Spaces and non-ASCII on purpose: "Jos\xC3\xA9 Garc\xC3\xADa" is a real Mac short name.
        dir = fs::temp_directory_path() / ("banished mac data \xC3\xA9 " + std::to_string(static_cast<long long>(stamp)));
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    ~MacTempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

// The engine's spelling of a host path (System::Filename: '/' -> '\') as UTF-16.
std::u16string Engine(const fs::path& p) {
    std::string s = p.string();
    for (char& c : s)
        if (c == '/') c = '\\';
    return Platform::Posix::Utf8ToUtf16(s.data(), s.size());
}

std::string ReadAll(const fs::path& p) {
    std::string out;
    if (FILE* f = std::fopen(p.string().c_str(), "rb")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

// A deterministic 3 MB "save": the size class of a small town, incompressible enough that a
// dropped or reordered chunk shows.
std::string FakeSave() {
    std::string s(3 * 1024 * 1024 + 17, '\0');
    uint32_t x = 0x1234567u;
    for (char& c : s) {
        x = x * 1664525u + 1013904223u;
        c = static_cast<char>(x >> 24);
    }
    return s;
}

uint64_t Fnv(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

UNIT_TEST(mac_data_documents_dir_is_home_documents) {
    std::string docs;
    CHECK(Platform::DocumentsDir(docs));
    CHECK(!docs.empty() && docs[0] == '/');
#if defined(__APPLE__)
    CHECK(docs.size() > 10 && docs.compare(docs.size() - 10, 10, "/Documents") == 0);
    // GetSaveDirectory(): Format("%s\\Banished\\", docs) then ".../Save/": the original game's folder.
    const std::string saves = docs + "/Banished/Save";
    CHECK(saves.find("/Documents/Banished/Save") != std::string::npos);
#endif
}

UNIT_TEST(mac_data_settings_survive_a_path_with_spaces_and_accents) {
    MacTempDir t;
    const std::string path = (t.dir / "Documents" / "Banished" / "settings.cfg").string();
    CHECK(!fs::exists(t.dir / "Documents"));
    const std::string body = "AudioMusicVolume=50\nGameAutosaveInterval=1\n";
    CHECK(Platform::WriteFileAtomic(path.c_str(), body.data(), body.size()));   // makes Documents/Banished
    std::string back;
    CHECK(Platform::ReadWholeFile(path.c_str(), back));
    CHECK(back == body);
    CHECK(!fs::exists(path + ".tmp"));
    const std::string body2 = "AudioMusicVolume=75\n";
    CHECK(Platform::WriteFileAtomic(path.c_str(), body2.data(), body2.size()));  // replaces in place
    CHECK(Platform::ReadWholeFile(path.c_str(), back));
    CHECK(back == body2);
    // The engine hands the settings layer '\' spellings of the same path.
    std::string backslashed = path;
    for (char& c : backslashed)
        if (c == '/') c = '\\';
    CHECK(Platform::ReadWholeFile(backslashed.c_str(), back));
    CHECK(back == body2);
}

UNIT_TEST(mac_data_save_write_move_find_read_round_trip) {
    MacTempDir t;
    // FileSystem::CreateFile: write a temp file, then move it to <Documents>\Banished\Save\Auto.sav
    // (the engine creates each missing directory with DirCreate, one level at a time).
    const fs::path saveDir = t.dir / "Documents" / "Banished" / "Save";
    for (const fs::path& d : { t.dir / "Documents", t.dir / "Documents" / "Banished", saveDir }) {
        const std::u16string w = Engine(d);
        CHECK(Platform::DirCreate(w.c_str()) == 1);
    }
    const std::string save = FakeSave();
    PathChar tmpName[Platform::kMaxPath];
    const std::u16string prefix = u"Auto";
    Platform::FileHandle h = Platform::FileCreateTemp(prefix.c_str(), tmpName);
    CHECK(h != Platform::kInvalidFileHandle);
    uint32_t wrote = 0;
    CHECK(Platform::FileWrite(h, save.data(), static_cast<uint32_t>(save.size()), &wrote) == 1);
    CHECK_EQ(wrote, save.size());
    CHECK(Platform::FileFlush(h) == 1);
    CHECK(Platform::FileClose(h) == 1);

    const std::u16string dest = Engine(saveDir / "Auto.sav");
    CHECK(Platform::FileMove(tmpName, dest.c_str()) == 1);
    CHECK(Platform::FileMove(tmpName, dest.c_str()) == 0);   // the source is gone / the target exists

    struct stat st;
    CHECK(::stat((saveDir / "Auto.sav").string().c_str(), &st) == 0);
    CHECK_EQ(static_cast<size_t>(st.st_size), save.size());
    CHECK_EQ(static_cast<unsigned>(st.st_mode & 0777), 0644u);   // not mkstemp's 0600

    // FileSystem::FindFiles(<save dir>\*.sav): case-insensitive pattern, the saves list.
    Platform::FindData fd;
    const std::u16string pat = Engine(saveDir) + u"\\*.SAV";
    Platform::FindHandle fh = Platform::FindFirst(pat.c_str(), &fd);
    CHECK(fh != Platform::kInvalidFindHandle);
    if (fh != Platform::kInvalidFindHandle) {
        CHECK(std::u16string(fd.name) == u"Auto.sav");
        CHECK_EQ(fd.sizeLow, save.size());
        CHECK(Platform::FindNext(fh, &fd) == 0);
        Platform::FindClose(fh);
    }

    // SaveLoad reads it back through FileOpenRead: byte for byte.
    Platform::FileHandle r = Platform::FileOpenRead(dest.c_str());
    CHECK(r != Platform::kInvalidFileHandle);
    std::string back(save.size(), '\0');
    uint32_t got = 0;
    CHECK(Platform::FileRead(r, &back[0], static_cast<uint32_t>(back.size()), &got) == 1);
    CHECK_EQ(got, save.size());
    Platform::FileClose(r);
    CHECK(Fnv(back) == Fnv(save));
    CHECK(back == save);
}

UNIT_TEST(mac_data_read_primitives_find_a_file_spelled_in_another_case) {
    MacTempDir t;
    fs::create_directories(t.dir / "WinData" / "Mods");
    {
        FILE* f = std::fopen((t.dir / "WinData" / "Mods" / "ColonialCharterJ.pkm").string().c_str(), "wb");
        CHECK(f != nullptr);
        if (f != nullptr) {
            std::fputs("PKM", f);
            std::fclose(f);
        }
    }
    // The case the Windows-spelled code uses vs the case on disk. A case-sensitive volume needs
    // the fallback; APFS/HFS+ default volumes match natively. Either way these must open.
    const fs::path odd = t.dir / "windata" / "MODS" / "colonialcharterj.PKM";
    const std::u16string w = Engine(odd);
    Platform::FileHandle h = Platform::FileOpenRead(w.c_str());
    CHECK(h != Platform::kInvalidFileHandle);
    if (h != Platform::kInvalidFileHandle) {
        char buf[8] = {};
        uint32_t got = 0;
        CHECK(Platform::FileRead(h, buf, 8, &got) == 1);
        CHECK_EQ(got, 3u);
        CHECK(std::string(buf, 3) == "PKM");
        Platform::FileClose(h);
    }
    Platform::FileHandle s = Platform::FileOpenReadShared(w.c_str());
    CHECK(s != Platform::kInvalidFileHandle);
    if (s != Platform::kInvalidFileHandle) Platform::FileClose(s);
    CHECK(Platform::PathAttributes(Engine(t.dir / "WINDATA" / "mods").c_str()) != Platform::kInvalidPathAttributes);
    // A name that is not there stays not there.
    const std::u16string missing = Engine(t.dir / "WinData" / "Mods" / "nothere.pkm");
    CHECK(Platform::FileOpenRead(missing.c_str()) == Platform::kInvalidFileHandle);
    CHECK(Platform::PathAttributes(missing.c_str()) == Platform::kInvalidPathAttributes);
}

#endif  // __APPLE__

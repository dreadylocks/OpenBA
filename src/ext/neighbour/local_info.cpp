// ============================================================================
// src/ext/neighbour/local_info.cpp -- NOT RETAIL. ext.neighbour: this game's layer-1 facts
// ============================================================================
// handshake.h's LocalInfo, from the running game. Worked out once per process (the first lobby
// conversation or the first neighbour game) and kept: the retail packages are read whole for their
// hash, which takes about a second the first time and must not happen every connect.
//   simBuild   xxHash64 of "openba|<version>|<git commit>|<compiler>|<target>": two builds that
//              share it run the same simulation code. A Windows and a Mac build differ (warn only).
//   dataHash   xxHash64 over WinData's retail packages in a fixed order, each as its name, its size
//              (8 bytes, little-endian) and its bytes; a missing one as its name and "missing".
//              Two players with the same Banished 1.0.7 files (Steam, GOG, or a copy) get the same.
//              "" when none could be read (the relay then cannot refuse on it).
//   mods       IO::Modification's enabled mods in its order (the order the game loads them), each
//              its .pkm file name, size and xxHash64.
//   exts       the registry's active class B / C extensions with their format versions.
// ============================================================================

#include "ext/neighbour/handshake.h"

#include <cstdio>
#include <mutex>
#include <vector>

#include "ext/registry.h"
#include "io/FileSystem.h"
#include "io/Modification.h"
#include "platform/common/platform_file.h"
#include "system/String.h"
#include "system/WideString.h"
#include "openba_build_info.h"   // OPENBA_GIT_HASH (generated into <build>/gen)

namespace Neighbour {

namespace {

// The packages Banished 1.0.7 ships in WinData (FileSystem::LoadPackages loads every *.pkg there;
// a stock install holds exactly these four).
const char* const kRetailPackages[] = { "data0.pkg", "data1.pkg", "patch0.pkg", "patch1.pkg" };

const char* TargetName() {
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
    return "windows-x86_64";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "macos-x86_64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-arm64";
#else
    return "unknown";
#endif
}

// Feeds the file at `path` (UTF-8) into `x`; false when it cannot be opened. `size` gets its size.
bool HashFile(const std::string& path, Xxh64& x, uint64_t& size) {
    const System::String s(path.c_str());
    Platform::FileHandle h;
    {
        const System::WideString ws(s);
        h = Platform::FileOpenRead(ws._data);
    }
    if (h == nullptr || h == Platform::kInvalidFileHandle) return false;
    size = Platform::FileSize(h);
    std::vector<uint8_t> buf(1u << 20);
    uint64_t done = 0;
    for (;;) {
        uint32_t got = 0;
        if (Platform::FileRead(h, buf.data(), static_cast<uint32_t>(buf.size()), &got) == 0 || got == 0) break;
        x.Update(buf.data(), got);
        done += got;
    }
    Platform::FileClose(h);
    if (done != size) size = done;   // read what was there
    return true;
}

std::string Join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::string PackDirectory() {
    const ::IO::FileSystem* fs = ::IO::FileSystem::_singleton;
    if (fs == nullptr) return std::string();
    const System::Filename dir = fs->GetPackDirectory();
    return dir._data ? std::string(dir._data) : std::string();
}

std::string DataHash() {
    const std::string dir = PackDirectory();
    if (dir.empty()) return std::string();
    Xxh64 all(0);
    int read = 0;
    for (const char* name : kRetailPackages) {
        all.Update(name, std::char_traits<char>::length(name));
        Xxh64 file(0);
        uint64_t size = 0;
        if (HashFile(Join(dir, name), file, size)) {
            uint8_t le[8];
            for (int i = 0; i < 8; ++i) le[i] = static_cast<uint8_t>(size >> (8 * i));
            all.Update(le, sizeof(le));
            const uint64_t fh = file.Digest();
            uint8_t fle[8];
            for (int i = 0; i < 8; ++i) fle[i] = static_cast<uint8_t>(fh >> (8 * i));
            all.Update(fle, sizeof(fle));
            ++read;
        } else {
            all.Update("missing", 7);
        }
    }
    if (read == 0) return std::string();
    return Hex64(all.Digest());
}

std::vector<ModEntry> EnabledMods() {
    std::vector<ModEntry> out;
    ::IO::Modification* mods = ::IO::Modification::_singleton;
    if (mods == nullptr) return out;
    for (int32_t i = 0; i < mods->_mods._size && out.size() < kMaxMods; ++i) {
        const RtIO::Mod& m = mods->_mods._data[i];
        if (m.enabled == 0 || m.file._data == nullptr) continue;
        const std::string path = m.file._data;
        const size_t slash = path.find_last_of("/\\");
        ModEntry e;
        e.name = slash == std::string::npos ? path : path.substr(slash + 1);
        Xxh64 x(0);
        uint64_t size = 0;
        if (HashFile(path, x, size)) {
            e.size = size;
            e.hash = x.Digest();
            e.hasHash = true;
        }
        out.push_back(e);
    }
    return out;
}

std::vector<ExtEntry> ActiveExtensions() {
    std::vector<ExtEntry> out;
    for (int i = 0; i < Ext::EntryCount(); ++i) {
        const Ext::Entry& e = Ext::EntryAt(i);
        if (!Ext::IsLockedClass(e.cls) || !Ext::Active(e)) continue;
        out.push_back(ExtEntry{e.id, e.formatVersion});
    }
    return out;
}

}  // namespace

const LocalInfo& LocalFacts() {
    static std::once_flag once;
    static LocalInfo info;
    std::call_once(once, [] {
        info.engineVersion = OPENBA_VERSION;
        const std::string build = std::string(kEngineId) + "|" + OPENBA_VERSION + "|" + OPENBA_GIT_HASH + "|" +
#if defined(__VERSION__)
                                  __VERSION__ +
#endif
                                  "|" + TargetName();
        info.simBuild = Hex64(Xxh64Of(build.data(), build.size()));
        info.dataHash = DataHash();
        info.mods = EnabledMods();
        fprintf(stderr, "[Neighbour] this game: %s %s, sim build %s, data %s, %u mod(s)\n", kEngineId,
                OPENBA_VERSION, info.simBuild.c_str(), info.dataHash.empty() ? "(unread)" : info.dataHash.c_str(),
                static_cast<unsigned>(info.mods.size()));
    });
    return info;
}

LocalInfo LocalInfoNow() {
    LocalInfo info = LocalFacts();
    info.exts = ActiveExtensions();   // read each time: the lock set changes per game
    return info;
}

}  // namespace Neighbour

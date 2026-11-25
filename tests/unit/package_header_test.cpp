// ============================================================================
// tests/unit/package_header_test.cpp -- a package's header, read by the real
//   IO::PackageFile::ReadPackfile (src/engine/io/package_file_read.cpp)
// ============================================================================
// Why this exists: a save made with RKEditorChoiceEdition.pkm installed asks, on
// every load, "The following mod is out of date ... Saved with version 0 (0.0.0)"
// (SaveLoad::CheckMod status 2). The save copies the mod's version from
// IO::Modification::RefreshList, which copies PackageFile::_version, which
// ReadPackfile reads from the package's directory blob. This file reads that
// blob through the port's own body, so what a save records can be read here
// rather than guessed at.
//
// Two tests:
//  * package_header_empty_directory needs no game data. It writes a 12-byte
//    package whose header word has the metadata bit set and points at an empty
//    directory blob, and checks what ReadPackfile makes of it: no name, version 0,
//    no entries, the file kept (platform 0). That is the shape an incomplete
//    package has (docs/retail_bugs.md section 10).
//  * package_header_installed_pkm reads every WinData/*.pkm in the user's install
//    ($BANISHED_DIR, C:/GOG Games/Banished or /mnt/c/GOG Games/Banished) and prints
//    each one's name, version, user version, format version and entry count. It
//    SKIPS without an install. It asserts only structure (no value read from the
//    install is stored here): the entry count matches the directory's own, and a
//    package whose directory blob is empty reads as the synthetic one does.
// ============================================================================
#include "unit.h"
#include "pkm_names.h"

#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "platform_file.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// ReadPackfile opens through IO::FileSystem::_singleton (OpenFile, then ReadFile).
// An empty FileSystem (no data directory, no packages mounted) sends every open
// to PlatformOpenFile, the loose-file arm, which is the arm RefreshList's open of
// a .pkm takes. FileSystem has no constructor (0x1800ac7c0 is unported), so the
// value-initialised object is the zeroed one; it is never destroyed.
struct ScopedFileSystem {
    alignas(IO::FileSystem) unsigned char storage[sizeof(IO::FileSystem)];
    IO::FileSystem* saved = nullptr;
    ScopedFileSystem() {
        std::memset(storage, 0, sizeof(storage));
        saved = IO::FileSystem::_singleton;
        IO::FileSystem::_singleton = ::new (static_cast<void*>(storage)) IO::FileSystem();
    }
    ~ScopedFileSystem() { IO::FileSystem::_singleton = saved; }
};

// The host has no seated MemoryAllocator: ReadPackfile's blob, the stream's
// windows and every String allocate through the instrument seat, armed with the
// CRT heap (ext_package_cache_test.cpp does the same). Declared first in
// ReadHeader, so it outlives every String the read makes.
void* SeatAlloc(uint64_t size, int) {
    return std::malloc(static_cast<size_t>(size));
}
void SeatFree(void* p) {
    std::free(p);
}
struct HeapSeat {
    HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = &SeatAlloc;
        System::g_memoryAllocatorInstrument.memFree = &SeatFree;
    }
    ~HeapSeat() {
        System::g_memoryAllocatorInstrument.memAlloc = nullptr;
        System::g_memoryAllocatorInstrument.memFree = nullptr;
    }
};

struct Header {
    bool ok = false;
    std::string name;
    int32_t version = 0;
    int32_t userVersion = 0;
    int32_t platform = 0;
    int32_t entries = 0;
};

// One ReadPackfile on a fresh PackageFile, as RefreshList does (modification_refresh.cpp).
Header ReadHeader(const std::string& path) {
    Header h;
    HeapSeat seat;
    ScopedFileSystem fsys;
    alignas(16) unsigned char mem[sizeof(IO::PackageFile)];
    IO::PackageFile* const pkg = ::new (static_cast<void*>(mem)) IO::PackageFile();
    const System::Filename name(path.c_str());
    h.ok = pkg->IO::PackageFile::ReadPackfile(name);
    const char* const n = pkg->_name._data;
    h.name = (n != nullptr) ? n : "";
    h.version = pkg->_version;
    h.userVersion = pkg->_userVersion;
    h.platform = pkg->_platform;
    h.entries = pkg->_entries._size;
    // The loose open's OS handle (FileSystem::CloseFile's loose arm is this one call).
    if (h.ok && pkg->_file.winHandle != Platform::kInvalidFileHandle && pkg->_file.winHandle != nullptr)
        Platform::FileClose(pkg->_file.winHandle);
    pkg->IO::PackageFile::~PackageFile();
    return h;
}

void PutI32(std::vector<uint8_t>& out, int32_t v) {
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

// The directory blob a package's header word points at: its offset, its length,
// and its first two ints (file count, format version) when it has them.
struct Directory {
    bool read = false;
    int64_t at = 0;
    int32_t length = 0;
    bool metadata = false;
    int32_t formatVersion = -1;   // -1: the blob has no format word
    int32_t fileCount = -1;
};

Directory PeekDirectory(const std::string& path) {
    HeapSeat seat;
    Directory d;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr)
        return d;
    uint8_t word[4] = {};
    if (std::fread(word, 1, 4, f) == 4) {
        const int32_t hdr = pkm_names::ReadI32(word);
        d.metadata = (hdr & int32_t(0x80000000)) != 0;
        d.at = hdr & 0x7fffffff;
        if (std::fseek(f, static_cast<long>(d.at), SEEK_SET) == 0 && std::fread(word, 1, 4, f) == 4) {
            d.read = true;
            d.length = pkm_names::ReadI32(word);
            if (d.length > 0 && d.length < (1 << 30)) {
                std::vector<uint8_t> blob(static_cast<size_t>(d.length));
                if (std::fread(blob.data(), 1, blob.size(), f) == blob.size()) {
                    pkm_names::BlobReader r(blob.data(), d.length);
                    if (r.cs != nullptr) {
                        int32_t v = 0;
                        if (r.cs->IO::CompressedStream::Read(4, &v))
                            d.fileCount = v;
                        if (d.metadata && r.cs->IO::CompressedStream::Read(4, &v))
                            d.formatVersion = v;
                    }
                }
            }
        }
    }
    std::fclose(f);
    return d;
}

// Where a file's last non-zero byte is (-1: the file is all zeros). Read from the
// end in 1 MiB blocks; only asked of a package whose directory is empty. A stream,
// not fseek: a package can be past 2 GiB, and `long` is 32-bit on Windows.
int64_t LastNonZeroByte(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return -1;
    in.seekg(0, std::ios::end);
    int64_t pos = static_cast<int64_t>(in.tellg());
    std::vector<char> buf(1u << 20);
    int64_t last = -1;
    while (pos > 0 && last < 0) {
        const int64_t start = std::max<int64_t>(0, pos - static_cast<int64_t>(buf.size()));
        const std::streamsize n = static_cast<std::streamsize>(pos - start);
        in.seekg(static_cast<std::streamoff>(start), std::ios::beg);
        if (!in.read(buf.data(), n))
            break;
        for (std::streamsize i = n; i > 0; --i) {
            if (buf[static_cast<size_t>(i - 1)] != 0) {
                last = start + static_cast<int64_t>(i - 1);
                break;
            }
        }
        pos = start;
    }
    return last;
}

std::string GameDir() {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("BANISHED_DIR"); env && *env)
        dirs.push_back(env);
    dirs.push_back("C:/GOG Games/Banished");
    dirs.push_back("/mnt/c/GOG Games/Banished");
    for (const std::string& d : dirs) {
        std::error_code ec;
        if (fs::is_directory(fs::path(d) / "WinData", ec))
            return d;
    }
    return std::string();
}

}  // namespace

// The damaged package's shape, built here: header word 0x80000004 (the metadata
// bit, directory at offset 4), then a directory blob of length 0. ReadPackfile
// reads the length, allocates and reads nothing, opens a CompressedStream over
// zero bytes (Open fails, unchecked, as retail leaves it), and every later Read
// fails without writing: fileCount, the format version and every header field
// keep the constructor's values, and the three strings come out empty.
UNIT_TEST(package_header_empty_directory) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string leaf = "banished_pkg_header_unit_" + std::to_string(static_cast<long long>(stamp));
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec);
    CHECK(!ec);
    if (ec)
        return;
    const fs::path dir = tmp / leaf;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "EmptyDirectory.pkm";

    std::vector<uint8_t> bytes;
    PutI32(bytes, static_cast<int32_t>(0x80000004u));   // metadata bit | directory at 4
    PutI32(bytes, 0);                                   // directory blob length 0
    PutI32(bytes, 0);                                   // padding, never read
    FILE* f = std::fopen(file.string().c_str(), "wb");
    CHECK(f != nullptr);
    if (f == nullptr) {
        fs::remove_all(dir, ec);
        return;
    }
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);

    const Header h = ReadHeader(file.string());
    CHECK(h.ok);                       // the file opened: RefreshList keeps it
    CHECK_EQ(h.platform, 0);           // ... and does not discard it as another platform's
    CHECK_STREQ(h.name.c_str(), "");   // the Mods list and the save show no name
    CHECK_EQ(h.version, 0);            // the save records version 0: CheckMod status 2
    CHECK_EQ(h.userVersion, 0);
    CHECK_EQ(h.entries, 0);            // and the package mounts no files

    fs::remove_all(dir, ec);
}

UNIT_TEST(package_header_installed_pkm) {
    const std::string game = GameDir();
    if (game.empty())
        UNIT_SKIP("no game install (WinData not found)");

    std::vector<std::string> pkms;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(fs::path(game) / "WinData", ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".pkm")
            pkms.push_back(e.path().string());
    }
    std::sort(pkms.begin(), pkms.end());
    if (pkms.empty())
        UNIT_SKIP("no .pkm in WinData");

    for (const std::string& path : pkms) {
        const std::string file = fs::path(path).filename().string();
        const Header h = ReadHeader(path);
        const Directory d = PeekDirectory(path);
        std::printf("    [pkm] %s: read=%d name=\"%s\" version=0x%08x userVersion=%d format=%d entries=%d"
                    " (directory %d bytes at %lld, metadata bit %d)\n",
                    file.c_str(), h.ok ? 1 : 0, h.name.c_str(), static_cast<uint32_t>(h.version),
                    h.userVersion, d.formatVersion, h.entries, d.length, static_cast<long long>(d.at),
                    d.metadata ? 1 : 0);
        CHECK(h.ok);
        CHECK(d.read);
        if (d.length > 0) {
            CHECK_EQ(h.entries, d.fileCount);
        } else {
            // No directory: the reader gets what the synthetic test gets.
            CHECK_STREQ(h.name.c_str(), "");
            CHECK_EQ(h.version, 0);
            CHECK_EQ(h.entries, 0);
            // Why it is empty: a file that stops being data before its directory is an
            // incomplete download or copy (docs/retail_bugs.md section 10).
            const int64_t last = LastNonZeroByte(path);
            const int64_t size = static_cast<int64_t>(fs::file_size(path, ec));
            std::printf("    [pkm] %s: EMPTY DIRECTORY. %lld bytes, last non-zero byte at %lld,"
                        " %lld zero bytes after it: %s\n",
                        file.c_str(), static_cast<long long>(size), static_cast<long long>(last),
                        static_cast<long long>(size - 1 - last),
                        last < d.at ? "the directory lies in the zeros, the file is incomplete"
                                    : "the directory is inside the data, the package was written empty");
        }
    }
}

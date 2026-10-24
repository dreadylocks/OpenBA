// ============================================================================
// tests/unit/file_map_read_test.cpp -- Platform::FileMapOpen / FileMapRead (MAPREAD)
// ============================================================================
// FileSystem::ReadFile's packed arm (filesystem_openfile.cpp) serves a package read
// from a read-only mapping instead of FileSeek32 + FileRead. These tests pin that the
// two give the same bytes and the same counts on every platform's twin
// (file_map_win.cpp, file_map_posix.cpp): inside the file, straddling end of file, at
// and past end of file, zero bytes. They also pin the 32-bit seek's failure values
// the arm reproduces: a negative position returns 0xFFFFFFFF, so retail's
// `seek == abs` test fails for abs < -1 and PASSES for abs == -1.
// Over one temp file (FileCreateTemp), deleted again whatever the checks say.
// ============================================================================
#include "unit.h"

#include "platform_file.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Platform::PathChar;

struct TempFile {
    PathChar name[Platform::kMaxPath] = {};
    bool made = false;
    explicit TempFile(const std::vector<uint8_t>& bytes) {
        const std::u16string prefix = u"bmr";
        Platform::FileHandle h = Platform::FileCreateTemp(prefix.c_str(), name);
        if (h == Platform::kInvalidFileHandle || h == nullptr) return;
        uint32_t written = 0;
        Platform::FileWrite(h, bytes.data(), static_cast<uint32_t>(bytes.size()), &written);
        Platform::FileClose(h);
        made = written == bytes.size();
    }
    ~TempFile() {
        if (name[0] != 0) Platform::FileDelete(name);
    }
};

std::vector<uint8_t> Pattern(size_t n) {
    std::vector<uint8_t> v(n);
    uint32_t x = 0x12345678u;
    for (size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        v[i] = static_cast<uint8_t>(x >> 24);
    }
    return v;
}

}  // namespace

UNIT_TEST(file_map_read_matches_seek_and_read) {
    const size_t kSize = 3 * 65536 + 123;   // not a page multiple: the tail page is partial
    const std::vector<uint8_t> bytes = Pattern(kSize);
    TempFile tf(bytes);
    if (!tf.made) UNIT_SKIP("no temp file could be created");

    Platform::FileHandle h = Platform::FileOpenRead(tf.name);
    CHECK(h != Platform::kInvalidFileHandle && h != nullptr);
    if (h == Platform::kInvalidFileHandle || h == nullptr) return;

    Platform::FileMapping m;
    CHECK(Platform::FileMapOpen(h, &m));
    CHECK(m.data != nullptr);
    CHECK_EQ(m.size, static_cast<uint64_t>(kSize));

    struct Case { int32_t pos; uint32_t n; };
    const Case cases[] = {
        {0, 4}, {0, 32768}, {4096, 16}, {65530, 20}, {static_cast<int32_t>(kSize) - 10, 10},
        {static_cast<int32_t>(kSize) - 10, 32768},   // straddles end of file: a short read
        {static_cast<int32_t>(kSize), 8},            // at end of file: 0 bytes
        {static_cast<int32_t>(kSize) + 4096, 8},     // past end of file: the seek succeeds, 0 bytes
        {100, 0},                                    // zero bytes
    };
    std::vector<uint8_t> a(65536), b(65536);
    for (const Case& c : cases) {
        std::memset(a.data(), 0xAA, a.size());
        std::memset(b.data(), 0xAA, b.size());
        CHECK_EQ(Platform::FileSeek32(h, c.pos), static_cast<uint32_t>(c.pos));
        uint32_t gotHandle = 0xDEADu;
        Platform::FileRead(h, a.data(), c.n, &gotHandle);
        const uint32_t gotMap = Platform::FileMapRead(m, static_cast<uint64_t>(c.pos), c.n, b.data());
        CHECK_EQ(gotMap, gotHandle);
        CHECK(std::memcmp(a.data(), b.data(), a.size()) == 0);   // same bytes, nothing past `got` written
    }

    // The failure values PackedMapRead reproduces (filesystem_openfile.cpp's banner).
    CHECK_EQ(Platform::FileSeek32(h, -5), 0xFFFFFFFFu);
    CHECK_EQ(static_cast<int32_t>(Platform::FileSeek32(h, -5)) == -5, false);   // abs < -1: the read fails
    CHECK_EQ(static_cast<int32_t>(Platform::FileSeek32(h, -1)) == -1, true);    // abs == -1: it passes

    Platform::FileMapClose(&m);
    CHECK(m.data == nullptr);
    CHECK_EQ(m.size, 0u);
    Platform::FileClose(h);
}

UNIT_TEST(file_map_open_refuses_an_empty_file) {
    TempFile tf(std::vector<uint8_t>{});
    if (tf.name[0] == 0) UNIT_SKIP("no temp file could be created");
    Platform::FileHandle h = Platform::FileOpenRead(tf.name);
    CHECK(h != Platform::kInvalidFileHandle && h != nullptr);
    if (h == Platform::kInvalidFileHandle || h == nullptr) return;
    Platform::FileMapping m;
    CHECK(!Platform::FileMapOpen(h, &m));   // the arm keeps the handle path
    CHECK(m.data == nullptr);
    Platform::FileMapClose(&m);             // closing an unmapped mapping is a no-op
    Platform::FileClose(h);
}

// ============================================================================
// tests/unit/metal_shader_cache_test.cpp -- src/render/metal/shader_cache.h: the cache key,
// the cache folder, the converter search order and the OS version rule (pure logic)
// ============================================================================
#include "unit.h"
#include "render/metal/shader_cache.h"

#include <string>

using namespace VideoMetal::ShaderCache;

namespace {

ConverterIdentity Dylib() {
    ConverterIdentity id;
    id.path = "/App/Contents/Frameworks/libairconv.dylib";
    id.size = 1234567;
    id.mtimeNs = 1700000000123456789ll;
    return id;
}

}  // namespace

UNIT_TEST(metal_cache_fingerprint_is_stable_and_16_hex) {
    std::string a = Fingerprint(Dylib(), "14.5.0", "metal320");
    CHECK_EQ(a, Fingerprint(Dylib(), "14.5.0", "metal320"));
    CHECK_EQ(a.size(), (size_t)16);
    for (char c : a)
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}

UNIT_TEST(metal_cache_fingerprint_changes_with_converter_os_and_metal) {
    std::string base = Fingerprint(Dylib(), "14.5.0", "metal320");
    ConverterIdentity swapped = Dylib();
    swapped.path = "/tmp/other/libairconv.dylib";
    CHECK(Fingerprint(swapped, "14.5.0", "metal320") != base);
    ConverterIdentity resized = Dylib();
    resized.size += 1;
    CHECK(Fingerprint(resized, "14.5.0", "metal320") != base);
    ConverterIdentity touched = Dylib();
    touched.mtimeNs += 1;
    CHECK(Fingerprint(touched, "14.5.0", "metal320") != base);
    CHECK(Fingerprint(Dylib(), "15.0.0", "metal320") != base);
    CHECK(Fingerprint(Dylib(), "14.5.0", "metal310") != base);
}

UNIT_TEST(metal_cache_entry_paths) {
    std::string dir = EntryDirectory("/Users/someone", "0123456789abcdef");
    CHECK_EQ(dir, std::string("/Users/someone/Library/Caches/OpenBA/shaders-0123456789abcdef"));
    CHECK_EQ(EntryFileName("abc"), std::string("abc.metallib"));
}

UNIT_TEST(metal_cache_home_resolution) {
    CHECK_EQ(ResolveHome("", "/Users/real"), std::string("/Users/real"));
    CHECK_EQ(ResolveHome("/tmp/run", "/Users/real"), std::string("/tmp/run"));
    CHECK_EQ(ResolveHome("/tmp/run//", "/Users/real"), std::string("/tmp/run"));
    CHECK_EQ(ResolveHome("", ""), std::string(""));
    CHECK_EQ(EntryDirectory(ResolveHome("/tmp/run", "/Users/real"), "ab"),
             std::string("/tmp/run/Library/Caches/OpenBA/shaders-ab"));
}

UNIT_TEST(metal_converter_search_order) {
    auto plain = ConverterSearchOrder("", "/App/Contents/MacOS");
    CHECK_EQ(plain.size(), (size_t)2);
    CHECK_EQ(plain[0], std::string("/App/Contents/MacOS/../Frameworks/libairconv.dylib"));
    CHECK_EQ(plain[1], std::string("/App/Contents/MacOS/libairconv.dylib"));
    auto over = ConverterSearchOrder("/x/libairconv.dylib", "/App/Contents/MacOS");
    CHECK_EQ(over.size(), (size_t)3);
    CHECK_EQ(over[0], std::string("/x/libairconv.dylib"));
}

UNIT_TEST(metal_os_version_rule) {
    int v[3];
    CHECK(ParseVersion("14.5.1", v));
    CHECK_EQ(v[0], 14);
    CHECK_EQ(v[1], 5);
    CHECK_EQ(v[2], 1);
    CHECK(ParseVersion("15", v));
    CHECK_EQ(v[1], 0);
    CHECK(!ParseVersion("x", v));
    CHECK(OsAtLeast(14, 0, kMinimumMacOSMajor, kMinimumMacOSMinor));
    CHECK(OsAtLeast(15, 2, kMinimumMacOSMajor, kMinimumMacOSMinor));
    CHECK(!OsAtLeast(13, 6, kMinimumMacOSMajor, kMinimumMacOSMinor));
}

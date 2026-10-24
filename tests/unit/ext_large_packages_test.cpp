// ============================================================================
// tests/unit/ext_large_packages_test.cpp -- fix.large_packages' unwrap rule
// ============================================================================
// src/ext/large_packages/large_packages_unwrap.{h,cpp} on synthetic indexes:
// no wrap, one wrap, two wraps (the single-file MegaMod's shape: index at the
// front, data contiguous after it), the index at the back (MegaMod1..7's
// shape), and every refusal (out of the file, over the index, overlapping /
// out of order, sparse), each of which must leave the output empty so the
// caller keeps retail's 32-bit reads.
// ============================================================================
#include "unit.h"
#include "ext/large_packages/large_packages_unwrap.h"
#include "ext/registry.h"

#include <cstdint>
#include <vector>

namespace {

using namespace Ext::LargePackages;

constexpr int64_t kGiB = int64_t{1} << 30;
constexpr int64_t k4G = int64_t{1} << 32;

// A contiguous layout: `n` entries of `size` bytes from `first`, the stored
// offsets the true ones mod 2^32 (what the packer writes).
struct Layout {
    std::vector<uint32_t> offs;
    std::vector<uint32_t> sizes;
    std::vector<int64_t>  truth;
    int64_t end = 0;
};

Layout Contiguous(int64_t first, int32_t n, uint32_t size) {
    Layout l;
    int64_t at = first;
    for (int32_t i = 0; i < n; ++i) {
        l.truth.push_back(at);
        l.offs.push_back(static_cast<uint32_t>(at & 0xFFFFFFFF));
        l.sizes.push_back(size);
        at += size;
    }
    l.end = at;
    return l;
}

Result Run(const Layout& l, int64_t fileSize, int64_t ib, int64_t ie, std::vector<int64_t>& out) {
    return Unwrap(l.offs.data(), l.sizes.data(), static_cast<int32_t>(l.offs.size()), fileSize, ib, ie, out);
}

}  // namespace

UNIT_TEST(ext_large_packages_no_wrap) {
    // 3 GiB of data after a 1 MB index at the front: no 4 GiB boundary, but every
    // entry past 2 GiB is one retail's int32 seek refuses.
    const Layout l = Contiguous(4 + (1 << 20), 3 * 64, static_cast<uint32_t>(16 << 20));
    std::vector<int64_t> out;
    const Result r = Run(l, l.end, 4, 4 + (1 << 20), out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.wraps, 0);
    CHECK_EQ(r.holes, int64_t{0});
    CHECK(out == l.truth);
}

UNIT_TEST(ext_large_packages_one_wrap) {
    const Layout l = Contiguous(4 + 100, 5 * 4, static_cast<uint32_t>(256 << 20));   // 5 GiB
    CHECK(l.end > k4G);
    std::vector<int64_t> out;
    const Result r = Run(l, l.end + 1000, 4, 104, out);   // a tail after the data is allowed
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.wraps, 1);
    CHECK(out == l.truth);
}

UNIT_TEST(ext_large_packages_two_wraps_megamod_shape) {
    // The single-file MegaMod: header word 0x80000004, index [4, 5,151,918), a 25 KB
    // pad, data contiguous from 5,177,344, ~10 GiB, a tail after it.
    const int64_t first = 5177344;
    const Layout l = Contiguous(first, 10 * 8, static_cast<uint32_t>(128 << 20));
    CHECK(l.end > 2 * k4G);
    std::vector<int64_t> out;
    const Result r = Run(l, l.end + 5946716, 4, 5151918, out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.wraps, 2);
    CHECK_EQ(r.holes, int64_t{0});
    CHECK(out == l.truth);
}

UNIT_TEST(ext_large_packages_wrap_straddling_entry) {
    // An entry that itself crosses the 4 GiB line: the NEXT entry's stored offset
    // is the first to decrease.
    Layout l;
    const int64_t a = k4G - 100;
    l.offs = { 4, static_cast<uint32_t>(a), 50 };
    l.sizes = { static_cast<uint32_t>(a - 4), 150, 10 };
    std::vector<int64_t> out;
    const Result r = Run(l, k4G + 60, k4G + 60, k4G + 60, out);   // index at the end
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.wraps, 1);
    CHECK(out.size() == 3u);
    CHECK_EQ(out[2], k4G + 50);
}

UNIT_TEST(ext_large_packages_index_at_back) {
    // MegaMod1..7's shape: data from byte 4, the index after the data.
    const Layout l = Contiguous(4, 40, static_cast<uint32_t>(64 << 20));   // 2.5 GiB
    const int64_t ib = l.end;
    const int64_t ie = l.end + 4096;
    std::vector<int64_t> out;
    const Result r = Run(l, ie, ib, ie, out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK(out == l.truth);
}

UNIT_TEST(ext_large_packages_index_between_entries_is_not_a_hole) {
    Layout l;
    l.offs = { 4, 1004 + 500 };
    l.sizes = { 1000, 10 };
    std::vector<int64_t> out;
    const Result r = Run(l, 3 * kGiB, 1004, 1504, out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.holes, int64_t{0});
}

UNIT_TEST(ext_large_packages_refuses_past_file_end) {
    const Layout l = Contiguous(4 + 100, 5 * 4, static_cast<uint32_t>(256 << 20));
    std::vector<int64_t> out = { 1, 2, 3 };
    const Result r = Run(l, l.end - 1, 4, 104, out);   // the last entry runs one byte past
    CHECK(r.verdict == Verdict::OutOfFile);
    CHECK_EQ(r.badEntry, 19);
    CHECK(out.empty());
}

UNIT_TEST(ext_large_packages_refuses_header_and_index_overlap) {
    Layout l;
    l.offs = { 0 };
    l.sizes = { 10 };
    std::vector<int64_t> out;
    CHECK(Run(l, 3 * kGiB, 100, 200, out).verdict == Verdict::OutOfFile);   // over the header word

    l.offs = { 150 };
    const Result r = Run(l, 3 * kGiB, 100, 200, out);
    CHECK(r.verdict == Verdict::IndexOverlap);
    CHECK_EQ(r.badEntry, 0);
    CHECK(out.empty());
}

UNIT_TEST(ext_large_packages_refuses_non_monotonic) {
    // An index written out of order (no wrap at all): the decrease would be read
    // as a wrap, and the entry shifted 4 GiB forward runs past this 3 GiB file.
    Layout l;
    l.offs = { 4, static_cast<uint32_t>(2 * kGiB), 1000 };
    l.sizes = { 996, 1000, 1000 };
    std::vector<int64_t> out;
    const Result r = Run(l, 3 * kGiB, 3 * kGiB - 10, 3 * kGiB - 10, out);
    CHECK(r.verdict == Verdict::OutOfFile);
    CHECK_EQ(r.badEntry, 2);
    CHECK(out.empty());

    // Out of order with no decrease: an entry starting inside the previous one.
    l.offs = { 4, 500, 900 };
    l.sizes = { 996, 100, 100 };
    const Result o = Run(l, 3 * kGiB, 3 * kGiB - 10, 3 * kGiB - 10, out);
    CHECK(o.verdict == Verdict::Overlap);
    CHECK_EQ(o.badEntry, 1);
    CHECK(out.empty());
}

UNIT_TEST(ext_large_packages_refuses_spurious_wrap_in_big_file) {
    // A 9 GiB file whose index is out of order: entry 2 really sits at 1 GiB, but
    // the decrease shifts it to 5 GiB. Inside the file and overlapping nothing, so
    // only the density check catches it.
    Layout l;
    l.offs = { 4, static_cast<uint32_t>(3 * kGiB), static_cast<uint32_t>(kGiB) };
    l.sizes = { 1000, static_cast<uint32_t>(kGiB), static_cast<uint32_t>(kGiB) };
    std::vector<int64_t> out;
    const Result r = Run(l, 9 * kGiB, 9 * kGiB - 10, 9 * kGiB - 10, out);
    CHECK(r.verdict == Verdict::Sparse);
    CHECK(r.holes > kMaxHoleBytes);
    CHECK(out.empty());
}

UNIT_TEST(ext_large_packages_small_holes_accepted) {
    Layout l;
    l.offs = { 4, 2000, 5000 };
    l.sizes = { 1000, 1000, 1000 };
    std::vector<int64_t> out;
    const Result r = Run(l, 3 * kGiB, 3 * kGiB - 10, 3 * kGiB - 10, out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK_EQ(r.holes, int64_t{(2000 - 1004) + (5000 - 3000)});
}

UNIT_TEST(ext_large_packages_empty_index) {
    std::vector<int64_t> out = { 7 };
    const Result r = Unwrap(nullptr, nullptr, 0, 3 * kGiB, 4, 100, out);
    CHECK(r.verdict == Verdict::Ok);
    CHECK(out.empty());
}

UNIT_TEST(ext_large_packages_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("large_packages");
    CHECK(e != nullptr);
    CHECK(e->tier == Ext::Tier::Fix);
    CHECK(e->cls == Ext::Class::None);
    CHECK(e->defaultOn);
    CHECK(Ext::Fixes{}.large_packages);
    CHECK(kRetailLimit == 0x7FFFFFFF);
}

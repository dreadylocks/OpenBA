#pragma once
// ============================================================================
// src/ext/large_packages/large_packages_unwrap.h -- fix.large_packages' pure rule
// ============================================================================
// NOT RETAIL (docs/extensions/handins/LARGEPKM.md, docs/retail_bugs.md section 9).
//
// A .pkg / .pkm index stores each entry's offset as a uint32 (ReadPackfile
// 0x1800b5de3), and retail reads a packed entry with a 32-bit signed seek
// (ReadFile 0x1800ace61: SetFilePointer, no high word). A package larger than
// 2 GiB therefore fails (offsets in [2, 4) GiB seek negative) or reads the
// wrong bytes (offsets past 4 GiB wrap). The packer that writes such a file
// (the single-file MegaMod.pkm, 10.76 GB) lays the data out contiguously in
// index order, so the stored offset is the true one mod 2^32 and, in index
// order, it DECREASES exactly where the true offset crosses a 4 GiB boundary.
//
// Unwrap rebuilds the true offsets from that rule, then refuses the result
// unless it describes a sane file:
//   * every entry lies inside the file, after the 4-byte header word;
//   * no entry overlaps the index blob [indexBegin, indexEnd);
//   * in index order every entry starts at or after the previous one's end
//     (monotonic, no overlap);
//   * the data is dense: the holes between consecutive entries (the index
//     blob not counted) add up to at most kMaxHoleBytes. A decrease that is
//     NOT a 4 GiB wrap (an index written out of order) shifts an entry 4 GiB
//     forward and leaves a hole of gigabytes, or runs past the file end.
// On a refusal the caller keeps retail's 32-bit reads (and its crash).
//
// Standard C++ only: tests/unit/ext_large_packages_test.cpp links this file alone.
// ============================================================================

#include <cstdint>
#include <vector>

namespace Ext {
namespace LargePackages {

// A package up to this size is read exactly as retail reads it; the fix never
// looks at its index.
constexpr int64_t kRetailLimit = 0x7FFFFFFF;

// The most hole bytes Unwrap accepts between entries (see the banner). The
// packers seen write none; the single-file MegaMod has a 25 KB pad between the
// index and the first entry, which is outside this count (the first entry's
// gap is not a hole between entries).
constexpr int64_t kMaxHoleBytes = int64_t{64} << 20;

enum class Verdict : uint8_t {
    Ok,
    OutOfFile,      // an entry starts before byte 4 or ends past the file
    IndexOverlap,   // an entry overlaps the index blob
    Overlap,        // an entry starts before the previous one ends
    Sparse,         // more than kMaxHoleBytes of holes between entries
};

const char* VerdictText(Verdict v);

struct Result {
    Verdict verdict = Verdict::Ok;
    int32_t badEntry = -1;   // the entry that failed the check (-1 for Ok / Sparse)
    int32_t wraps = 0;       // the 4 GiB boundaries crossed
    int64_t holes = 0;       // hole bytes between entries
};

// `offsets` / `sizes`: the index's two uint32 per entry, in index order.
// `out` gets one absolute offset per entry on Ok; it is cleared otherwise.
Result Unwrap(const uint32_t* offsets, const uint32_t* sizes, int32_t count, int64_t fileSize,
              int64_t indexBegin, int64_t indexEnd, std::vector<int64_t>& out);

}  // namespace LargePackages
}  // namespace Ext

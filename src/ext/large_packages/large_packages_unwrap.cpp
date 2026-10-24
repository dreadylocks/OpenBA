// ============================================================================
// src/ext/large_packages/large_packages_unwrap.cpp -- fix.large_packages' pure rule
// ============================================================================
// NOT RETAIL. See the header (docs/extensions/handins/LARGEPKM.md).
// ============================================================================
#include "ext/large_packages/large_packages_unwrap.h"

namespace Ext {
namespace LargePackages {

const char* VerdictText(Verdict v) {
    switch (v) {
    case Verdict::Ok:           return "ok";
    case Verdict::OutOfFile:    return "an entry lies outside the file";
    case Verdict::IndexOverlap: return "an entry overlaps the index";
    case Verdict::Overlap:      return "entries overlap or are out of order";
    case Verdict::Sparse:       return "the data has large holes";
    }
    return "?";
}

Result Unwrap(const uint32_t* offsets, const uint32_t* sizes, int32_t count, int64_t fileSize,
              int64_t indexBegin, int64_t indexEnd, std::vector<int64_t>& out) {
    constexpr int64_t kWrap = int64_t{1} << 32;
    constexpr int64_t kHeaderBytes = 4;   // the header word at offset 0

    Result r;
    out.clear();
    out.reserve(static_cast<size_t>(count > 0 ? count : 0));

    int64_t base = 0;
    int64_t prevEnd = -1;   // -1: no entry yet
    for (int32_t i = 0; i < count; ++i) {
        if (i > 0 && offsets[i] < offsets[i - 1]) {
            base += kWrap;
            ++r.wraps;
        }
        const int64_t begin = base + offsets[i];
        const int64_t end = begin + sizes[i];

        if (begin < kHeaderBytes || end > fileSize) {
            r.verdict = Verdict::OutOfFile;
        } else if (begin < indexEnd && end > indexBegin && end > begin) {
            r.verdict = Verdict::IndexOverlap;
        } else if (prevEnd >= 0 && begin < prevEnd) {
            r.verdict = Verdict::Overlap;
        }
        if (r.verdict != Verdict::Ok) {
            r.badEntry = i;
            out.clear();
            return r;
        }

        if (prevEnd >= 0) {
            int64_t gap = begin - prevEnd;
            // The index blob between two entries is not a hole.
            if (indexBegin >= prevEnd && indexEnd <= begin) gap -= indexEnd - indexBegin;
            r.holes += gap;
        }
        out.push_back(begin);
        prevEnd = end;
    }

    if (r.holes > kMaxHoleBytes) {
        r.verdict = Verdict::Sparse;
        out.clear();
    }
    return r;
}

}  // namespace LargePackages
}  // namespace Ext

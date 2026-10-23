// ============================================================================
// src/ext/draw_batch/batch_fill.cpp -- batch_fill.h
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWBATCH.md.
// ============================================================================
#include "ext/draw_batch/batch_fill.h"

#include <cstring>

namespace Ext {
namespace DrawBatch {

bool RetailAccepts(int32_t vertexCapacityBytes, int64_t vertexCount, int32_t stride, int64_t indexCount) {
    const int64_t capacity = vertexCapacityBytes;
    if (capacity <= vertexCount * stride) return false;
    if (indexCount * 2 >= capacity) return false;   // sic: the vertex buffer's capacity, as retail
    return true;
}

bool RunFits(int32_t vertexCapacityBytes, int32_t stride, int64_t runVertices, int64_t runIndices,
             int64_t vertices, int64_t indices) {
    const int64_t v = runVertices + vertices;
    const int64_t i = runIndices + indices;
    if (v > kMaxRunVertices) return false;
    return RetailAccepts(vertexCapacityBytes, v, stride, i);
}

int64_t FillRun(const Span* spans, int32_t count, int32_t stride, uint8_t* vb, uint16_t* ib) {
    uint32_t base    = 0;
    int64_t  written = 0;
    for (int32_t s = 0; s < count; ++s) {
        const Span& span = spans[s];
        const size_t bytes = static_cast<size_t>(span.vertexCount) * static_cast<size_t>(stride);
        std::memcpy(vb, span.vertices, bytes);
        vb += bytes;
        for (int32_t k = 0; k < span.indexCount; ++k) {
            ib[k] = static_cast<uint16_t>(span.indices[k] + base);
        }
        ib += span.indexCount;
        written += span.indexCount;
        base += static_cast<uint32_t>(span.vertexCount);
    }
    return written;
}

}  // namespace DrawBatch
}  // namespace Ext

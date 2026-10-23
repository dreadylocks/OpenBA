// ============================================================================
// src/ext/draw_batch/batch_fill.h -- ext.draw_batch's buffer fill and run limits (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/DRAWBATCH.md. Standard C++ only: the unit tests
// (tests/unit/ext_draw_batch_test.cpp) link batch_fill.cpp alone.
//
// A merged run is a sequence of consecutive draw items whose faithful draws would each map
// the Context's dynamic vertex and index buffers, copy one item's vertices and indices, and
// issue one DrawIndexed. The merged draw maps once, writes every item's vertices in item
// order and every item's indices offset by the vertices written before it, and draws once.
// The vertices the GPU fetches, in the order it rasterises the triangles, are the same.
//
// The run limits keep every merged draw one that retail's own path would accept and that
// 16-bit indices can address:
//   * retail's two capacity tests of ContextData::BeginInstancedIndexedVertices
//     (src/render/dx11/contextdata.cpp), INCLUDING the sic test of the index byte count
//     against the VERTEX buffer's capacity;
//   * at most 65536 vertices, so the largest rebased index is 65535.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>

namespace Ext {
namespace DrawBatch {

constexpr int64_t kMaxRunVertices = 65536;   // 16-bit indices address 0..65535

// Retail's accept test for one Begin(Instanced)IndexedVertices request, the two compares as
// written there: reject `capacity <= vertices * stride`, reject `indices * 2 >= capacity` (the
// VERTEX buffer's capacity, sic). Widened to 64 bits: every request it is asked about here is
// one retail computes in 32 bits without overflow (at most kMaxRunVertices * 0x34 bytes).
bool RetailAccepts(int32_t vertexCapacityBytes, int64_t vertexCount, int32_t stride, int64_t indexCount);

// True when a run already holding (runVertices, runIndices) can take one more item of
// (vertices, indices) and stay a request retail accepts with at most kMaxRunVertices.
bool RunFits(int32_t vertexCapacityBytes, int32_t stride, int64_t runVertices, int64_t runIndices,
             int64_t vertices, int64_t indices);

// One item's CPU geometry: `vertexCount` records of the run's stride and its 16-bit indices,
// each relative to the item's own first vertex.
struct Span {
    const void*     vertices    = nullptr;
    int32_t         vertexCount = 0;
    const uint16_t* indices     = nullptr;
    int32_t         indexCount  = 0;
};

// Writes the spans in order: the vertex bytes concatenated into `vb`, and each span's indices
// plus the number of vertices written before that span into `ib`. The caller has checked the
// run with RunFits, so every rebased index fits 16 bits. Returns the indices written.
int64_t FillRun(const Span* spans, int32_t count, int32_t stride, uint8_t* vb, uint16_t* ib);

// ---------------------------------------------------------------------------
// Idea B: the particle vertices of one emitter instance in a merged smoke run.
// ---------------------------------------------------------------------------
// ParticleInstance::Draw's 0x34-byte vertex (wave27_lane_gfxpart.cpp, part C), with the dword at
// +0x0C, which retail writes as the int 0, holding the emitter's transform slot as a float (the
// rewritten particle VS reads it as v0.w, smoke_vs_rewrite.h). Slot 0 writes retail's bytes.
struct ParticleVertex {
    float pos[3];     // +0x00
    float slot;       // +0x0c  retail: int 0 (== 0.0f's bits)
    float color[3];   // +0x10
    float alpha;      // +0x1c
    float u, v;       // +0x20
    float cx, cy;     // +0x28
    float rot;        // +0x30  radians
};
static_assert(sizeof(ParticleVertex) == 0x34, "the particle vertex stride is 0x34");

constexpr uint32_t kParticleOneBits      = 0x3F800000u;   // 1.0f, ParticleInstance::Draw's One()
constexpr uint32_t kParticleDegToRadBits = 0x3C8EFA35u;   // pi/180 as retail's .rdata holds it (DegToRad())

inline float FloatBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// Writes `count` particles of `records` exactly as ParticleInstance::Draw's part C does -- walked
// BACKWARD, four corners per particle, six 16-bit indices (b-2, b, b-1, b-2, b+1, b) -- with the
// index base starting at `firstVertex + 2` instead of 2 (the merged draw's vertex offset) and
// `slot` at +0x0C. `Record` is RtGraphics::ParticleRecord (or a test's struct with its field names).
// The caller keeps firstVertex + 4 * count <= 65536, so the 16-bit base never wraps where retail's
// would not. Returns the vertices written.
template <class Record>
int32_t WriteParticles(const Record* records, int32_t count, float alpha, float slot, uint32_t firstVertex,
                       uint8_t* vb, uint16_t* ib) {
    static const float kCornerUV[8]     = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    static const float kCornerOffset[8] = {-1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f};
    const float one      = FloatBits(kParticleOneBits);
    const float oneMinus = one - alpha;
    const float degToRad = FloatBits(kParticleDegToRadBits);
    uint16_t base = static_cast<uint16_t>(firstVertex + 2);
    for (int32_t i = 0; i < count; ++i) {
        const int32_t slotIndex = count - i - 1;
        const Record& rec = records[static_cast<int64_t>(slotIndex)];

        const float size = (oneMinus * rec._sizePrev) + (alpha * rec._size);
        const float fade = (oneMinus * rec._alphaPrev) + (alpha * rec._alpha);
        const float rot  = ((oneMinus * rec._rotPrev) * degToRad) + ((alpha * rec._rot) * degToRad);

        ParticleVertex vertex;
        vertex.pos[0]   = (rec._pos.x - rec._posPrev.x) * alpha + rec._posPrev.x;
        vertex.color[0] = (rec._color.x - rec._colorPrev.x) * alpha + rec._colorPrev.x;
        vertex.pos[1]   = (rec._pos.y - rec._posPrev.y) * alpha + rec._posPrev.y;
        vertex.color[1] = (rec._color.y - rec._colorPrev.y) * alpha + rec._colorPrev.y;
        vertex.pos[2]   = (rec._pos.z - rec._posPrev.z) * alpha + rec._posPrev.z;
        vertex.color[2] = (rec._color.z - rec._colorPrev.z) * alpha + rec._colorPrev.z;
        vertex.slot     = slot;
        vertex.alpha    = fade;
        vertex.rot      = rot;
        for (int c = 0; c < 4; ++c) {
            vertex.u  = kCornerUV[c * 2 + 0];
            vertex.v  = kCornerUV[c * 2 + 1];
            vertex.cx = size * kCornerOffset[c * 2 + 0];
            vertex.cy = size * kCornerOffset[c * 2 + 1];
            std::memcpy(vb, &vertex, sizeof vertex);
            vb += sizeof vertex;
        }
        const uint16_t b = base;
        ib[0] = static_cast<uint16_t>(b - 2);
        ib[1] = b;
        ib[2] = static_cast<uint16_t>(b - 1);
        ib[3] = static_cast<uint16_t>(b - 2);
        ib[4] = static_cast<uint16_t>(b + 1);
        ib[5] = b;
        ib += 6;
        base = static_cast<uint16_t>(base + 4);
    }
    return 4 * count;
}

}  // namespace DrawBatch
}  // namespace Ext

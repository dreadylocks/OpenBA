#pragma once
// ============================================================================
// src/render/common/draw_batch/smoke_vs_rewrite.h -- ext.draw_batch's particle vertex-shader rewriter
// (docs/extensions/handins/DRAWBATCH.md, Idea B)
// ============================================================================
// NOT RETAIL. Pure: no D3D, no retail bytes (the unit tests build their own vs_4_0 blob). It reads the
// blob it is handed -- the user's installed vertex shaders, at run time -- and emits a copy in memory.
//
// THE PROBLEM. Every smoke emitter is its own draw: ParticleInstance::Draw writes the emitter's quads
// (emitter-local positions) and the VS places them with the transform it reads from the banked
// transform constants at transforms[SV_InstanceID] (slot 0 for one emitter). Two emitters cannot share
// a draw because the slot comes from the instance id only.
//
// THE MATCH (`Analyse`): a vs_4_x program whose input signature is exactly the particle vertex
// (ParticleInstance::Draw's 0x34-byte record) plus the instance id:
//     v0 float4 (position xyz, +0x0C), v1 float4, v2 float4, v3 float1, vI SV_InstanceID (uint)
// with v0.w declared unread (`dcl_input v0.xyz`, ISGN read mask without w), a dynamically indexed
// constant buffer of 208 float4 (52 matrices), and the instance id read by exactly one instruction,
// `ishl rT.c, vI.c, l(2)` (the matrix's first row).
//
// THE REWRITE (`Transform`): that one instruction becomes three,
//     ftou rT.c, v0.wwww         the vertex's emitter slot k, written as a float at +0x0C
//     iadd rT.c, rT.cccc, vI.c   k + SV_InstanceID
//     ishl rT.c, rT.cccc, l(2)   retail's instruction on the sum
// and v0 is declared xyzw (the dcl's mask and the ISGN read mask). Every other token is the user's own.
// Retail's particle writer stores the int 0 at +0x0C (ParticleInstance::Draw, `vertex.zero = 0`), whose
// bits are 0.0f: ftou gives 0, the index is SV_InstanceID, and every value the program computes is the
// retail program's. With k > 0 (only the merged draws write one) the vertex reads transforms[k + iid].
// ============================================================================
#include "better_trees/dxbc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SmokeVs {

constexpr uint32_t kTransformCbFloat4 = 208;   // 52 matrices of 4 float4

struct Match {
    bool             container = false;   // the blob parsed as DXBC
    bool             program   = false;   // its SHDR / SHEX parsed
    bool             matched   = false;
    std::string      why;                 // the first test that failed (when !matched)
    Dxbc::Container  cont;
    Dxbc::Program    prog;
    uint32_t         iidReg      = 0;     // the SV_InstanceID input register
    int              shiftIdx    = -1;    // instrs[] index of the `ishl rT, vI, l(2)`
    int              dclInputIdx = -1;    // instrs[] index of `dcl_input v0.xyz`
    size_t           isgnV0      = 0;     // byte offset of v0's element in the ISGN chunk data
};

// Reads the blob; `m->matched` says whether Transform applies.
void Analyse(const uint8_t* blob, size_t size, Match* m);

// The rewritten copy (THE REWRITE above), self-checked: it parses, its checksum verifies and it has
// exactly two instructions more. False (and `out` untouched) when the match did not hold or the check
// failed.
bool Transform(const Match& m, std::vector<uint8_t>* out);

}  // namespace SmokeVs

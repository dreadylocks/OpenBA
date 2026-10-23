// ============================================================================
// src/ext/terrain_batch/terrain_batch.h -- ext.terrain_batch (class A)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/TERRAINBATCH.md.
//
// Two changes to the terrain chunk draws of WorldGeometry (src/game/Game/geometry_draw.cpp),
// one row:
//
//   row merge     AddDrawItems emits one draw item per run of consecutive visible chunks
//                 in a grid row instead of one per chunk; Draw draws the run with ONE
//                 DrawIndexed over a copy of retail's chunk index list repeated per chunk
//                 (the run index buffer, built from retail's own list when the mesh is
//                 built). Same vertices, same indices per chunk, same order, same state.
//   shadow cull   in the shadow pass (renderer shader 1) a chunk is also tested against
//                 the VIEW frustum, grown along the sun direction as far as its shadow
//                 can reach (terrain_batch_rules.h CasterSweepBox): the rule retail gives
//                 objects, made height-aware.
//
// The seam is WorldGeometry::_extTerrainBatch, a null-by-default pointer to the row's live
// bool (DESIGN.md hook rule 5), set at boot unless the profile is faithful. Null: nothing
// here runs, the run buffer is never built, retail's loop and draw run unchanged. Set:
// the mesh build keeps a run buffer per WorldGeometry, and AddDrawItems takes this file's
// loop while the bool is on and the buffer is ready.
//
// Threads: the run buffers are built and released on the thread that builds the mesh or
// handles the device loss (under a mutex); DrawRun runs on whichever thread draws
// (ext.render_pipe's render thread) and reads a slot only after its `ready` flag.
// ============================================================================
#pragma once

#include <cstdint>

#include "ext/terrain_batch/terrain_batch_rules.h"

class WorldGeometry;
struct Frustum;
struct FrustumCullPlanes;
namespace Graphics { class Renderer; }
namespace Math { class Transform3; }
namespace Video { class Context; }

namespace Ext {
namespace TerrainBatch {

// AddDrawItems' step 4 with the row on: the same chunk boxes, the same cull-camera test
// (`localPlanes`, `cullVerify` as the faithful loop has them), the shadow pass's view test,
// and one item per run. `localCullFrustum` is the cull camera's frustum in the geometry's
// space (its plane 0 is the sun direction in the shadow pass), `selfInverse` the
// geometry's inverse transform. Only called while Ready(geo).
void AddChunkItems(const WorldGeometry& geo, Graphics::Renderer& renderer,
                   const FrustumCullPlanes& localPlanes, bool cullVerify,
                   const Frustum& localCullFrustum, const Math::Transform3& selfInverse,
                   uint16_t materialKey, float uvScale);

// The geometry's run buffer exists on the current device and matches its chunk grid.
bool Ready(const WorldGeometry& geo);

// WorldGeometry::Draw's run arm: the chunk draw for every chunk of the run in `key`, as
// one DrawIndexed on the run buffer, or (no buffer for this geometry on this device)
// one retail chunk DrawIndexed per chunk.
void DrawRun(const WorldGeometry& geo, Video::Context& context, uint32_t key);

// BuildTerrainMesh's seam: retail's chunk index list (`count` 16-bit indices, the bytes it
// just uploaded) becomes this geometry's run buffer.
void OnTerrainIndices(const WorldGeometry& geo, const int16_t* indices, int32_t count);

// WorldGeometry::OnVideoError's seam: the run buffer's device half is released (the
// restart's BuildTerrainMesh builds it again).
void OnVideoError(const WorldGeometry& geo);

// ~WorldGeometry's seam: the geometry's slot and buffer are freed.
void Forget(const WorldGeometry& geo);

}  // namespace TerrainBatch
}  // namespace Ext

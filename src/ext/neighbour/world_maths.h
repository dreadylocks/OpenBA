#pragma once
// ============================================================================
// src/ext/neighbour/world_maths.h -- NOT RETAIL. ext.neighbour: the same land on every platform
// ============================================================================
// EXT SEAM for worldgen (worldgen.cpp, worldgen_cells.cpp, worldgen_f390_erosion.cpp,
// worldgen_f390_rivers.cpp) and the start spiral (starttown.cpp StartTown_FindSpawnTile). While a lobby
// room's shared map is being built, those take the engine's ported MSVC maths (src/engine/crt/
// msvc_libm.h: Powf, Sinf, Cosf) instead of the platform's libm. The ported bodies give the same bits on
// x86-64 and arm64 (their scalar arms are exact twins of the SSE2 ones, and the build passes
// -ffp-contract=off), so a Windows PC and a Mac build the same land from one seed; the platform libms
// may round some powf results a last bit apart. Off (the platform's, as retail) for every other game.
// Neighbour::ResetForGame sets it at the start of each Load, before the world is generated.
//
// Standard C++ only, no game header: the unit tests link worldgen_f390_erosion.cpp with this file.
// ============================================================================

namespace Neighbour {

bool PortableWorldMaths();
void SetPortableWorldMaths(bool on);

}  // namespace Neighbour

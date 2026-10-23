#pragma once
// ============================================================================
// src/ext/walk_batch_grid/walk_batch_grid.h -- NOT RETAIL. ext.walk_batch_grid (class B, locked
// for the game, pseudo-mod ext:walk_batch_grid): the walk batch without CharacterGrid::Place.
// docs/extensions/handins/WALKGRID.md (the class B row), WALKBATCH.md (the design and the proof).
//
// The row's body is ext.walk_batch's: one template (src/ext/walk_batch/walk_batch.cpp, TickImpl /
// TickCharacterImpl with kPlace = false) serves both rows, and walk_batch.cpp's BeginFrame reads
// both toggles once per frame (kWalkNoPlace when this row is on). This header is the row's own
// view of that shared body: the seam helpers EntityList::UpdateWalkBatchGrid calls.
// ============================================================================
#include "ext/walk_batch/walk_batch.h"

namespace Ext {
namespace WalkBatchGrid {

// The batched tick without Place (Ext::WalkBatch::TickGrid): true when the tick was taken here.
inline bool Tick(CharacterComponent* self, const System::Time* time) { return Ext::WalkBatch::TickGrid(self, time); }

// The grid walk's slot-8 tick of a CharacterComponent (Ext::WalkBatch::TickCharacterGrid).
inline void TickCharacter(CharacterComponent* self, const System::Time* time) {
    Ext::WalkBatch::TickCharacterGrid(self, time);
}

}  // namespace WalkBatchGrid
}  // namespace Ext

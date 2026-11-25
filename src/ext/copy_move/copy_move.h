// ============================================================================
// src/ext/copy_move/copy_move.h -- copy, paste and move buildings (docs/fork/COPY_MOVE.md)
// ============================================================================
// NOT RETAIL. A fork feature, always on (the user, 2026-11-24): no Extensions row, no switch. It does
// nothing until the player uses it, and what it makes is what the retail tools make, so a save stays a
// retail save.
//
// THE TOOL (copy_move.cpp). A player tool like PlacementTool, made current by PlayerToolManager:
//   select   click a building, field, pasture, orchard or road to add it to the selection or take it out;
//            drag a box to select everything it touches (a field is taken whole, roads by the cell);
//            Shift keeps the old selection. Right-click clears it, a second right-click leaves the tool.
//   Ctrl+C   copy: the selection follows the cursor as a ghost; R / T turn it; a click places a copy (new
//            construction, built and paid for as usual) and the copy stays in hand; right-click puts it down.
//   Ctrl+X   move: the same, but a click moves the selection there at once (teleport.h); right-click cancels.
//   Ctrl+V   paste the last copy again.
// Reached by its toolbar button (Ext::Toolbar::CopyMoveButton) or the keys from anywhere in the game: with
// an entity's info panel open, Ctrl+C / Ctrl+X take that entity straight into hand.
//
// THE SEAMS (one line each, `// EXT SEAM` at the site):
//   src/game/Game/camera_input.cpp     PollHotkeys at the top of PlayerToolManager::UpdateHotkeys
//   src/game/Dialog/toolbardialog.cpp  the toolbar host's buttons (src/ext/toolbar), which this tool's joins
// ============================================================================
#pragma once

class PlayerToolManager;

namespace Ext {
namespace CopyMove {

// PlayerToolManager::UpdateHotkeys's EXT SEAM, before retail's keys: Ctrl+C / Ctrl+X / Ctrl+V start the
// tool when another tool is current. True = the keys were ours this frame and the caller skips its own: C,
// X and V are also retail's camera views 3, 2 and 4 (Camera2, Camera1, Camera3).
bool PollHotkeys(PlayerToolManager* manager, bool inputFlag);

}  // namespace CopyMove
}  // namespace Ext

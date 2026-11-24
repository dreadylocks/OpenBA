# Copy, paste and move buildings (this fork)

A player tool that copies buildings, roads, fields, pastures, orchards and stockpiles and pastes them as
new construction, and moves buildings and roads instantly. Code: `src/ext/copy_move/`. Asked for by the
user on 2026-11-24.

## 1. Rulings (the user, 2026-11-24)

- **Always on.** "I just want all my changes baked into the base game." No Extensions row, no switch. The
  tool does nothing until the player uses it, and what it makes is what the retail tools make, so a save
  stays a retail save. The gate's command-line faithful profile (`/profile:faithful`) hides the toolbar
  button, so the gate's toolbar stays retail's.
- **A toolbar button and a hotkey** start the tool. The button sits at the end of the main bar (the
  toolbar host's second kind of button, `Ext::Toolbar::ToolButton`, beside the extension window's).
- **Move is instant**: the building keeps its workers, residents and stored goods.
- **Areas are taken whole**: a box that touches a field, pasture or orchard selects all of it; roads are
  selected by the cell.

## 2. Using it

| Where | Input | What happens |
|---|---|---|
| anywhere in game | toolbar button (two houses), or Ctrl+C / Ctrl+X / Ctrl+V | the tool starts. With an info panel open, Ctrl+C / Ctrl+X take that building straight into hand |
| select | click | adds the building / field / pasture / orchard / road cell under the cursor, or takes it out |
| select | drag a box | selects everything it touches (Shift keeps the old selection) |
| select | Ctrl+C | copy: the selection follows the cursor as a ghost |
| select | Ctrl+X | move: the same, for the selection's movable things |
| select | Ctrl+V | the last copy again |
| copy / move | R / T | turn the group a quarter (the keys that turn one building) |
| copy | click | places a copy of everything that fits (green); red is skipped. The copy stays in hand |
| move | click | moves everything at once, or nothing when one part does not fit (the selection stays as it was) |
| any | right-click | back one step: out of copy / move, then the selection clears, then the tool closes |

Ctrl+C, X and V are taken from retail's camera views (plain C, X and V still jump to them): a frame whose
keys were the tool's skips retail's hotkeys. On the Mac it is the Control key (Cmd has no slot in the
game's key table).

## 3. How it works

- **Paste = the retail tool.** A building goes through `PlacementTool::CreateAt` with the copied template,
  rotation and mesh group; a field, stockpile or orchard through `DragPlacementTool::CalcPath`; a pasture
  through `FencedTool::CalcPath` with its RotationSetting set to the copied shelter side; a road through
  `Map::PlaceRoadCell` with the road tool's template for its road type. Builders put copies up and they cost
  what the retail tool charges. The preview's red / green is the same per-cell test
  (`PlayerTileRender_BuildRecords_1380`'s loop, the area tools' `CalcCost`, the road tool's end-cell test).
- **Move = lift, test, set down** (`teleport.cpp`). A whole-entity OnRemove / OnAdded is NOT safe: those
  slots are spawn / destroy hooks (WorkPlace releases its workers, Residence evicts, Storage empties,
  MultiObject removes its children). Only the components that put a building on the map are cycled, each
  with its own pair: Map (OnRemove ... AddToMap when it had claimed its cells), Zone, Decal, Picking
  (RefreshMesh after the model moved), Model (SetTransformFromYaw + OnAdded), Floor (PlaceFloor), and the
  running particle emitters restarted where the model now is. Every ModelComponent and every MultiObject
  child moves rigidly with the building's centre and turn. Citizens' actions that name the building are
  dropped first (`TownMgr_AbortCitizenActionsFor`, as retail does when a storage burns). A site that
  ClearArea flattened has its region heights refreshed at both places. Roads are map-cell state: a
  finished road is laid finished at the new cell, a planned one is planned again. A road over a road as
  good is a no-op, as for the road tool. A planned road in a game loaded from a save is missing from the
  map's pending-road set (`docs/known_port_issues.md` row 1334, suspected port gap); the move puts it back
  in the set before `Map::RemovePendingRoad`, which otherwise leaves the old cell planned.
- **ext.neighbour**: pastes go through the same seams as the retail tools (`NoteStructure`, `NoteRoadCell`);
  a move sends `NoteRemoved` then `NoteStructure`.
- **Pastures and other multi-part entities**: a pasture's shelter and fence pieces are entities of their own
  made from placeable templates; selection always takes the pasture and never a part alone
  (`EntitiesIn`, `MasterOf`).

## 4. Not yet (follow-ups)

- **Moving fields, pastures, orchards and stockpiles, and unfinished sites.** They are copied but not
  moved: in a move they stay put, drawn yellow. Their state holds absolute cells: crop and orchard slots
  (`CropSlot::cell`, `OrchardSlot::tile`) and the planted crops / trees as entities, the queued
  PlantArea / HarvestArea / TendArea / ClearArea jobs, the pasture's animals and the cells it stamps every
  frame. A move would shift the slots and children for a straight move, clear and replant for a turned
  one, and repost the jobs.
- **Bridges and tunnels** are not selected.
- **Copied settings**: a copy is a new building with the game's defaults (a field's crop, a workplace's
  worker count are not copied).

## 5. Tested

On the Mac (2026-11-24, `BANISHED_DRIVE` scripts on a new game, then the same town saved and loaded):
box and click selection with outlines; a four-house copy pasted where it fit and skipped where it hit a
house; a turned paste; copies built by builders; a house moved with its three residents; the storage barn
moved and turned with its whole inventory (19 coats, 38 tools, 2,400 potatoes); a planned road moved and
turned, then laid by builders at its new cells; planned roads moved again after a save and a load (the
row 1334 case: the old cells are clear); a stockpile and a turned pasture pasted through the retail area
tools; the town saved and loaded with everything where it was moved to. Unit: `tests/unit/ext_copy_move_test.cpp` (the turn agrees with
`Map::FootprintValidity`'s cell by cell). Not run here: Windows and Linux (CI builds both), the faithful
gate on Windows, a finished road moved, a turned pasture's shelter side once built, and an orchard paste
(it goes through the same tool as the stockpile).

// src/diag/cell_trace.h -- map-cell diagnostics for the "red square" reports.
//
// A cell turns red in the placement overlay when Map::TestPlacement (0x14009f5d0) fails one
// of four tests: terrain type, blocker, the shared footprintCount refcount (MapCell +0x28:
// roads and '.' footprint cells), or height. These hooks make the first three visible.
// docs/retail_bugs.md §8 has the findings they are meant to confirm.
//
//   BANISHED_CELL_DUMP=<dir>  Map::Serialize's load arm writes <dir>/cells_raw.txt (the cells
//                             as the save holds them, BEFORE the load fix-up pass that turns a
//                             0xFF count back into 0) and <dir>/cells_fixed.txt (after it).
//                             tools/cell_dump/cell_diff.py compares two dumps.
//   BANISHED_CELL_TRACE=1     one stderr line per footprintCount change that wraps below 0 or
//                             climbs above 2, and per re-sort that turns a buildable cell
//                             (terrain type 2) or a resource cell (4) into a non-buildable type.
//
// Read only: no hook changes a cell. Off (one Opt() test) unless the flag is set.
#pragma once
#include <cstdint>

class Map;
struct MapCell;

namespace CellTrace {

bool Enabled();

// A footprintCount write: `before` is the count the writer read, `delta` what it added.
void NoteCount(const Map* map, const MapCell* cell, uint8_t before, int delta, const char* site);

// The removal re-sort (0x14009dee0's ClassifyTerrain + SetTerrainType arm; the live body is
// StartTown_UnmarkResourceCellGrid, Game/starttown.cpp).
void NoteResort(const Map* map, int cellIndex, uint8_t before, uint8_t after);

// Map::Serialize's load arm: `stage` is "raw" (before the fix-up pass) or "fixed".
void DumpCells(const Map* map, const char* stage, int version);

}  // namespace CellTrace

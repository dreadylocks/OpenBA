#pragma once
// ============================================================================
// src/ext/ferries/test_docks.h -- ext.ferries' test aid (docs/fork/FERRIES.md)
// ============================================================================
// NOT RETAIL, a test aid like neighbour.autolobby: `ferries.testdocks=1` in
// settings.cfg, read on a NEW game with ext.ferries on. A little after the
// start it places, through the game's own placement test and CreateAt (the
// player's path), a Ferry Dock on the town's shore and one across the water,
// both finished at once (the start town's instant-build bracket), and a house
// beside the far one as a construction site: the builders row over to build it
// and carry its materials across. The `[Ferries]` lines say where (for a camera
// pin) and count the crossings.
// ============================================================================

namespace Ext {
namespace Ferries {
namespace TestDocks {

void SelectForGame(bool townHasDocks);   // Ferries::SelectForGame: arms it when the key is 1, on a new
                                         // game whose town has no Ferry Dock yet
void BeginFrame();      // Ferries::BeginFrame: places the sites once

}  // namespace TestDocks
}  // namespace Ferries
}  // namespace Ext

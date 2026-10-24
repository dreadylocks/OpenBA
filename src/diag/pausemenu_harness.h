// src/diag/pausemenu_harness.h -- the unattended in-game pause menu driver (pausemenu_harness.cpp)
//
// BANISHED_PAUSEMENU / _QUIT / _BUTTON / _CLICKS open the Pause.rsc dialog on a frame and click one
// of its buttons (the banner in the .cpp). The two entry points other code calls:
#pragma once

class PlayerToolManager;

extern "C" {

// Once per GameUpdateLoop::Update, after the world ticked (gameupdateloop.cpp).
void PauseMenuHarness_Tick(PlayerToolManager* entityUI, int frameNum);

// Arm the Quit Game click on the current world as if BANISHED_PAUSEMENU had named the next frame:
// the menu opens on the next Tick and the click follows BANISHED_PAUSEMENU_QUIT frames later (its
// default). A harness whose sequence is over calls this so the run ends on its own instead of
// sitting on a live world until its script kills the exe (the showroom, src/diag/showroom.cpp).
// Nothing happens if the menu is already open or was already clicked on this world.
void PauseMenuHarness_QuitGame();

}

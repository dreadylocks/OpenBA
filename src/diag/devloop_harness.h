#pragma once
// src/diag/devloop_harness.h -- BANISHED_DEVLOOP=<dir>, the in-game half of the dev loop
// (devloop_harness.cpp; docs/extensions/DEVLOOP.md).

class GameUpdateLoop;

// Once per GameUpdateLoop::Update (gameupdateloop.cpp): the request / save / quit handshake.
extern "C" void DevLoopHarness_Tick(GameUpdateLoop* gul);

// True once a dev-loop run has reached its game: from then on a player is in it, so the modals
// an unattended run answers by itself (SaveLoadDialog's) are the player's to answer again. The
// relaunch's own autoload, before that, is still unattended (a modal there would stall the loop).
bool DevLoopHarness_Attended();

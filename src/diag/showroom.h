// src/diag/showroom.h
// BANISHED_SHOWROOM=<list file> — a building showroom for visual A/B checks (issue #13).
//
// The list file names one building template per line ("Template\X.rsc"; '/' works too,
// '#' starts a comment). A new game with the flag set:
//   1. places every listed template, COMPLETED, through the stress town's siting and
//      placement path (StartTown_StressSpawnStructures in src/game/Game/starttown.cpp),
//      a quarter map east of the town with a 4-tile free ring each, logging one
//      "[Showroom] placed" line per building with its tile and world position;
//   2. keeps the world-gen scatter (trees, rocks, bushes) out of the rectangle around them;
//   3. pauses the game, waits for the world to settle, then pins the camera
//      (the BANISHED_CAMERA_PIN machinery in camera_input.cpp) on each placed building's
//      model bounds, from two opposite headings, and takes one screenshot per heading
//      through the retail TakeScreenshot route;
//   4. moves each shot out of the Documents Screenshots folder to
//      showroom/NN_<name>_yNNN.bmp in the working directory (the game dir) and logs
//      "[Showroom] DONE" at the end.
//
// Same seed + same list = the same layout and framing, so two runs (e.g. two texture
// encodings) give image pairs to compare by eye. Pixel diffs between runs measure
// nothing (CLAUDE.md), so nothing here diffs.
#pragma once

extern "C" {

bool        Showroom_Enabled();          // the flag is set and the list has entries
int         Showroom_Count();
const char* Showroom_Path(int index);    // "Template\X.rsc"
const char* Showroom_Name(int index);    // "X", for logs and file names

// starttown.cpp: one call per list entry after its placement attempt. placed=false
// leaves the entry out of the camera sequence; `why` says what stopped it. worldBounds
// points at the model's world box (min xyz, max xyz), which must outlive the run (the
// ModelComponent's own Geometry::_worldBounds); null frames the footprint instead.
void Showroom_NotePlaced(int index, bool placed, int tileX, int tileY, int w, int h, int mapDim,
                         const float* worldBounds, const char* why);

// starttown.cpp's resource scatter: true for a cell inside the showroom rectangle (every
// placed footprint plus its free ring), which the scatter leaves bare.
bool Showroom_InArea(int x, int y);

// GameUpdateLoop::Update, once per frame: drives the sequence. Returns true on the frame
// a screenshot should be requested (the caller raises the TakeScreenshot request bit).
bool Showroom_Tick();
bool Showroom_WantsPause();

// camera_input.cpp, inside the camera pin: the current shot's camera focus, or false
// when no showroom shot is being framed.
bool Showroom_CameraFocus(float* x, float* y, float* z, float* dist, float* yaw);

}

#pragma once
// src/diag/drive_harness.h -- BANISHED_DRIVE=<dir>: a script in a file plays as the mouse and
// keyboard (drive_harness.cpp has the mailbox, drive_script.h the language).

// Boot, after OptionsInit (src/app/main.cpp): installs the scripted input source when
// BANISHED_DRIVE names a folder; nothing otherwise.
void DriveHarness_Install();

// MenuDialogLoopT::Draw, after its UI: takes a `shot` asked for on the menus (the main menu, the
// lobby), where there is no game loop to take it. Nothing when none was asked for.
void DriveHarness_AfterFrontEndDraw();

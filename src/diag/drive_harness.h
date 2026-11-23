#pragma once
// src/diag/drive_harness.h -- BANISHED_DRIVE=<dir>: a script in a file plays as the mouse and
// keyboard (drive_harness.cpp has the mailbox, drive_script.h the language).

// Boot, after OptionsInit (src/app/main.cpp): installs the scripted input source when
// BANISHED_DRIVE names a folder; nothing otherwise.
void DriveHarness_Install();

#pragma once
// ============================================================================
// src/ext/auto_update/auto_update_state.h -- NOT RETAIL. What auto_update.cpp (launch, check,
// exit) and auto_update_ui.cpp (menu, game) share. Internal to src/ext/auto_update/.
// ============================================================================

#include <atomic>
#include <filesystem>
#include <string>

namespace AutoUpdate {
namespace Internal {

// The staging folder, "" until AtLaunch or Start has found it.
std::filesystem::path StagingDir();
// The build staged and ready to install ("" none), and a one-line notice for the menu ("" none).
std::string ReadyVersion();
std::string Notice();
void        ClearNotice();
// The check thread posted a ready build the game has not announced yet (GameTick takes it).
bool TakeReadyAnnouncement(std::string& version);

// Restart now: AtExit restarts the program when this is set.
void RequestRestart();
// A line to standalone_error.log and update.log, "[Update] ...".
void Log(const std::string& line);

}  // namespace Internal
}  // namespace AutoUpdate

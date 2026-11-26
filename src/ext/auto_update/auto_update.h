#pragma once
// ============================================================================
// src/ext/auto_update/auto_update.h -- NOT RETAIL. ext.auto_update: OpenBA updates itself
// ============================================================================
// docs/fork/AUTO_UPDATE.md. A release build checks the feed (the GitHub release's
// openba-update-<platform>.txt) at start and every two hours, downloads a newer build in the
// background, checks its SHA-256 and the manifest's Ed25519 signature, unpacks it into the staging
// folder (staging.h), and installs it at the next start (AtLaunch). A build that never reaches its
// menu is rolled back. Restart now (a main-menu / pause-menu button while a build is ready) saves
// the game, restarts and loads the save again.
//
// Only a build made with a feed (CMake OPENBA_UPDATE_FEED, set by tools/release/publish_update.sh)
// checks or installs anything: a dev build, tools/build.sh deploy and the dev loop never replace
// themselves. settings.cfg: ext.auto_update (the Extensions row, on by default; 0 stops checks,
// downloads and installs) and update.feed (another feed address, for a test server).
//
// Class A: nothing in the game or the save. The launch half runs before the settings exist, so it
// reads ext.auto_update from settings.cfg itself.
// ============================================================================

#include <cstdint>
#include <string>

class GameUpdateLoop;
class MainMenuDialog;
class MenuDialogLoopT;

namespace AutoUpdate {

constexpr int32_t kRestartMenuButtonId = 15;   // after Extensions (13) and Multiplayer (14)

// main / WinMain, the first line (before CrashReport::KeepLastCrash and the logs): installs a staged
// build, hands over to the installed one (macOS: execv, does not return), or rolls back a build
// that failed to start. -1: carry on booting. Otherwise main returns that code at once (the Windows
// supervisor's end: the new build is running, or the old one was started again).
int AtLaunch(int argc, char** argv);

// Ext::InitFromSettings, after the registry is installed. `settingsFeed`: settings.cfg's
// update.feed ("" when absent). Starts the background check when ext.auto_update is on and the
// build has a feed; with the row off, a staged build is thrown away.
void Start(const std::string& settingsFeed);

// Check now instead of at the next two-hour mark (at most once per five minutes): ext.neighbour,
// when the relay says the neighbour plays another version.
void CheckSoon(const char* why);

// EXT SEAM, MainMenuDialog::OnOpened: the Restart to update button (front end and pause menu)
// while a build is ready.
void AttachMenuButton(MainMenuDialog* menu);
// Its click (MainMenuDialog::OnCommand, id kRestartMenuButtonId).
void OnRestartButton(MainMenuDialog* menu);
// EXT SEAM, MainMenuDialog::OnOpened: appended to the build line ("" for nothing), for example
// "  -  update 1.2.1 ready: restart to install".
std::string BuildLineNote();

// Once per menu frame (MenuDialogLoopT::Update, beside AutoStart::TickMenu): the first one records
// that this build started; after a Restart now it loads the save back.
void MenuTick(MenuDialogLoopT* menuLoop);
// Once per GameUpdateLoop::Update (beside DevLoopHarness_Tick): the event-log row when a build is
// ready, and Restart now's save.
void GameTick(GameUpdateLoop* gul);

// main / WinMain, just before it returns: the restart Restart now asked for (POSIX: execv, does not
// return on success).
void AtExit();

}  // namespace AutoUpdate

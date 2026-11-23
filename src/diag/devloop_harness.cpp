// src/diag/devloop_harness.cpp -- BANISHED_DEVLOOP=<dir>: the in-game half of the dev loop
// (docs/extensions/DEVLOOP.md; tools/devloop.sh is the other half and the one that sets the flag).
//
// The watcher asks a running game to make way for a new build by writing <dir>/request (one line:
// the new build's commit and subject). About once a second, in game, this harness looks for it.
// When it is there the harness deletes it, saves to the autosave slot (Auto.sav, the save
// BANISHED_AUTOLOAD=1 loads back; the save holds the camera, so the game comes back looking at the
// same spot), waits until the asynchronous write is over, and quits through Application::Quit, as
// the pause menu's Quit Game does (exit 0, settings.cfg written as on any quit).
//
// The save is the retail autosave's own sequence (AutoSaveDialog::Autosave, autosavedialog.cpp)
// without its first test, "not while the UI pauses the game": a player saves from the pause menu,
// which pauses the game, so a dialog that pauses it is no reason to wait. The other two tests stay:
// not while the world's lock bit is set, not while a save or load is already running. While either
// holds, the harness asks again a second later. A game sitting in a menu never runs this tick; the
// watcher's timeout covers that.
//
// Progress goes to <dir>/state, one line the watcher reads (each write atomic):
//   running <commit>   this exe is in a game (the first in-game frame; <commit> = OPENBA_GIT_HASH)
//   saving             a request was taken; the save is being written
//   quitting           the save is on disk; the game is closing
// Every step is also an event-log row, so the player sees the restart coming and sees which build
// came back, and a [DevLoop] line in standalone_error.log.
#include "devloop_harness.h"
#include "options.h"
#include "openba_build_info.h"      // OPENBA_GIT_HASH / OPENBA_BUILD_DATE (generated into <build>/gen)
#include "platform/common/platform_settings.h"   // Platform::ReadWholeFile / WriteFileAtomic
#include "save/SaveLoad.h"
#include "Game/GameUpdateLoop.h"
#include "Game/EventLog.h"          // EventLogEntry
#include "Game/game.h"             // g_gameContainer (the save sprite, the "Autosave" name's table)
#include "ui/String.h"
#include "system/String.h"
#include "system/Value.h"           // System::Time
#include "application/Application.h"   // Application::Quit, the pause menu's Quit Game
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// The row's look: the neighbour extension's event type (0x7E, no retail writer uses it) and the
// general event icon; violet so it never reads as a game event.
constexpr int32_t  kEventType = 0x7E;
constexpr int32_t  kEventIcon = 0;
constexpr uint32_t kColour    = 0xFFD0B0FFu;

enum class Phase {
    Idle,       // watching for a request
    WantSave,   // a request was taken; the save has not started yet (the world is locked, or busy)
    Saving,     // the save is running
    Quitting,   // Application::Quit has been called
};

Phase       s_phase = Phase::Idle;
const void* s_world = nullptr;   // the GameUpdateLoop whose first frame wrote "running"
std::chrono::steady_clock::time_point s_nextCheck;
std::string s_lastRefusal;       // logged once per reason, not every second

const char* Dir() { return Opt().devloop; }

std::string PathOf(const char* name) {
    std::string p = Dir();
    p += '/';
    p += name;
    return p;
}

void Log(const std::string& text) {
    std::fprintf(stderr, "[DevLoop] %s\n", text.c_str());
    std::fflush(stderr);
}

void Row(const std::string& text) {
    Log(text);
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_eventNotifier == nullptr) return;
    EventLog* log = g->m_eventNotifier;
    const System::String str(text.c_str());
    const EventLogEntry entry(kColour, str, kEventType, /*severity*/ 1, /*tile*/ -1, kEventIcon, /*popup*/ -1);
    const int count = log->entries.GetSize();
    log->entries.SetSize(count + 1, entry);
}

void WriteState(const std::string& line) {
    const std::string body = line + "\n";
    if (!Platform::WriteFileAtomic(PathOf("state").c_str(), body.data(), body.size())) {
        Log("could not write " + PathOf("state"));
    }
}

// The request's text with line ends and control characters dropped (it goes into an event-log row,
// whose font has printable ASCII only), at most 120 characters.
std::string Clean(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c == '\n' || c == '\r') break;
        if (c >= 0x20 && c < 0x7F) out += c;
        if (out.size() >= 120) break;
    }
    return out;
}

// AutoSaveDialog::Autosave's sequence (0x14005dec0) minus its UI-pause test (see the banner): a 1 s
// minimum display time, the container's SaveLoadEmpty sprite, the localized "Autosave" name in slot
// 2 (Auto.sav), then SetOperation(4, -4). Returns why it did not start, or "" once it has.
std::string StartSave(GameUpdateLoop* gul, Save::SaveLoad* saveLoad) {
    if ((gul->m_gameFlags >> 10) & 1) return "the world is locked (gameFlags bit 10)";
    const int op = saveLoad->GetOperation();
    if (op != 0) return "a save or load is already running (operation " + std::to_string(op) + ")";
    System::Time minTime;
    RtSystem_Time_Ctor(&minTime, 1.0f);
    saveLoad->SetMinOperationTime(minTime);
    UI::Sprite sprite = g_gameContainer->spriteSaveLoadEmpty;
    saveLoad->SetSaveSprite(&sprite);
    System::String name = UI::String(g_gameContainer->globalNames, System::String("Autosave"));
    saveLoad->SetSaveName(name, 2);
    saveLoad->SetOperation(Save::SaveLoad::kOpSave, -4);
    if (saveLoad->GetOperation() == 0) return "the save did not start";
    return "";
}

}  // namespace

bool DevLoopHarness_Attended() {
    return Opt().devloop != nullptr && s_world != nullptr;
}

// Once per GameUpdateLoop::Update, beside the save harness (gameupdateloop.cpp).
extern "C" void DevLoopHarness_Tick(GameUpdateLoop* gul) {
    if (Dir() == nullptr) return;
    const auto now = std::chrono::steady_clock::now();

    if (s_world != gul) {
        s_world = gul;
        s_phase = Phase::Idle;
        s_nextCheck = now;
        WriteState(std::string("running ") + OPENBA_GIT_HASH);
        Row(std::string("Dev loop: running build ") + OPENBA_GIT_HASH + " (" + OPENBA_BUILD_DATE + ").");
    }

    if (s_phase == Phase::Quitting) return;
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;

    if (s_phase == Phase::Saving) {
        if (saveLoad != nullptr && saveLoad->GetOperation() != 0) return;   // still writing
        Log("the save is written; quitting");
        WriteState("quitting");
        s_phase = Phase::Quitting;
        ::Application::Application::_singleton->Quit();
        return;
    }

    if (now < s_nextCheck) return;
    s_nextCheck = now + std::chrono::seconds(1);

    if (s_phase == Phase::Idle) {
        std::string request;
        if (!Platform::ReadWholeFile(PathOf("request").c_str(), request)) return;
        std::remove(PathOf("request").c_str());
        const std::string what = Clean(request);
        Row("Dev loop: new build " + (what.empty() ? std::string("(no name)") : what) + ": saving and restarting.");
        WriteState("saving");
        s_phase = Phase::WantSave;
    }

    // WantSave: start the save, or wait a second while the world is locked or a save runs.
    if (saveLoad == nullptr) return;
    const std::string why = StartSave(gul, saveLoad);
    if (!why.empty()) {
        if (why != s_lastRefusal) Log("waiting to save: " + why);
        s_lastRefusal = why;
        return;
    }
    s_lastRefusal.clear();
    s_phase = Phase::Saving;
    Log("save started");
}

// src/ext/auto_update/auto_update_ui.cpp -- NOT RETAIL. ext.auto_update: the menu and the game
// (auto_update.h; docs/fork/AUTO_UPDATE.md §7).
//
// - The main menu (front end and pause menu) gets a Restart to update button while a build is
//   ready, and the build line says so (or says what a rollback did).
// - In game, the event log says when a build has downloaded.
// - Restart now: in the front end it quits and AtExit starts the game again. In game it first saves
//   with the retail autosave's sequence (the dev loop harness's, src/diag/devloop_harness.cpp: slot 2,
//   Auto.sav), waits for the asynchronous write, writes `resume`, then quits the same way.
// - The first menu frame records that this build started (`started`, which ends a hand-over's or the
//   supervisor's wait) and, when `resume` names a save, loads it as the player would: the main menu's
//   Load, then that save in the list, then Load. The load's own questions (mods changed and so on) are
//   the player's to answer: this is not BANISHED_AUTOLOAD, which answers them unattended.
#include "ext/auto_update/auto_update.h"
#include "ext/auto_update/auto_update_state.h"
#include "ext/auto_update/staging.h"

#include "application/Application.h"   // Application::Quit, the menu's Quit
#include "Dialog/MainMenuDialog.h"
#include "Dialog/SaveLoadDialog.h"     // the save list's fields
#include "Game/EventLog.h"             // EventLogEntry
#include "Game/GameUpdateLoop.h"
#include "Game/MenuDialogLoopT.h"
#include "Game/game.h"                 // g_gameContainer (the save sprite, the "Autosave" name's table)
#include "ext/registry.h"              // Ext::GateProfileForced
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "save/SaveLoad.h"
#include "system/Filename.h"
#include "system/String.h"
#include "system/Value.h"              // System::Time
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/ElementDescription.h"
#include "ui/String.h"
#include "ui/UserInterface.h"

#include <cstdio>
#include <cstring>
#include <string>

extern SaveLoadDialog* g_StandaloneSaveLoadDialog;   // src/diag/autostart.cpp: the open SaveLoadDialog (mainmenudialog.cpp sets it)

namespace AutoUpdate {

namespace {

constexpr const char* kButtonRsc = "Dialog/ExtensionsLobby.rsc:buttonUpdate";
constexpr int32_t     kEventType = 0x7E;           // the neighbour extension's event type (no retail writer)
constexpr int32_t     kEventIcon = 0;
constexpr uint32_t    kColour    = 0xFFB0E0FFu;
constexpr int         kMsgButtonClick = 0x12;      // the UI dispatcher's button click
constexpr int         kMenuLoadButton = 2;         // MainMenuDialog "buttonLoad"
constexpr int         kSaveLoadLoadButton = 2;     // SaveLoadDialog "buttonLoad"
constexpr int         kSettleFrames = 30;          // menu frames before the resume's first click
constexpr int         kResumeTimeoutFrames = 600;

enum class RestartPhase {
    Idle,
    WantSave,   // asked in game; the save has not started (the world is locked, or busy)
    Saving,     // the save is running
    Quitting,   // Application::Quit has been called
};

enum class ResumePhase {
    Off,
    WaitMenu,   // counting menu frames before clicking Load
    WaitList,   // the Load dialog is opening and scanning the saves
    Done,
};

bool         s_started = false;
RestartPhase s_restart = RestartPhase::Idle;
std::string  s_restartWhy;
ResumePhase  s_resume = ResumePhase::Off;
std::string  s_resumeName;
int          s_resumeFrames = 0;
std::string  s_lastRefusal;

UI::Element* Find(UI::DialogInstance* d, const char* name) {
    return d->FindElement(System::String(name)).Get();
}

// One violet row in the event log, as the dev loop harness writes them.
void Row(GameUpdateLoop* g, const std::string& text) {
    Internal::Log(text);
    if (g == nullptr || g->m_eventNotifier == nullptr) return;
    EventLog*            log = g->m_eventNotifier;
    const System::String str(text.c_str());
    const EventLogEntry  entry(kColour, str, kEventType, /*severity*/ 1, /*tile*/ -1, kEventIcon, /*popup*/ -1);
    const int            count = log->entries.GetSize();
    log->entries.SetSize(count + 1, entry);
}

// AutoSaveDialog::Autosave's sequence minus its UI-pause test, as DevLoopHarness's StartSave: a 1 s
// minimum display time, the container's SaveLoadEmpty sprite, the localized "Autosave" name in slot 2
// (Auto.sav), then SetOperation(4, -4). Returns why it did not start, or "" once it has.
std::string StartSave(GameUpdateLoop* gul, Save::SaveLoad* saveLoad) {
    if ((gul->m_gameFlags >> 10) & 1) return "the world is locked";
    const int op = saveLoad->GetOperation();
    if (op != 0) return "a save or load is already running";
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

void QuitForRestart() {
    Internal::RequestRestart();
    s_restart = RestartPhase::Quitting;
    ::Application::Application::_singleton->Quit();
}

void MarkStarted() {
    s_started = true;
    const std::filesystem::path dir = Internal::StagingDir();
    if (dir.empty()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    Staging::WriteLine(dir, "started", OPENBA_VERSION);
    if (Staging::ReadLine(dir, "pending") == OPENBA_VERSION) Staging::Remove(dir, "pending");
    // The program an install replaced, a failed one, and (macOS) installed builds that are neither
    // current nor previous are not needed once this build has started.
    std::filesystem::remove_all(dir / "failed-program", ec);
    const std::string current = Staging::ReadLine(dir, "current");
    const std::string previous = Staging::ReadLine(dir, "previous");
    if (!current.empty()) {
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = Staging::Utf8(it->path().filename());
            if (!it->is_directory(ec) || name == current || name == previous) continue;
            if (name == "staged" || name == "download" || name == "unpack" || name == "previous-program") continue;
            if (name.empty() || name[0] < '0' || name[0] > '9') continue;   // installed builds are named by version
            std::error_code rm;
            std::filesystem::remove_all(it->path(), rm);
        }
    }
    const std::string resume = Staging::ReadLine(dir, "resume");
    if (!resume.empty()) {
        Staging::Remove(dir, "resume");   // first: a load that crashes must not loop
        s_resumeName = resume;
        s_resume = ResumePhase::WaitMenu;
        s_resumeFrames = 0;
        Internal::Log("loading \"" + resume + "\" again after the restart");
    }
}

void TickResume(MenuDialogLoopT* menuLoop) {
    UI::DialogInstance* mainMenu = menuLoop->_dialogInstance;
    switch (s_resume) {
    case ResumePhase::WaitMenu:
        if (mainMenu == nullptr) {
            s_resumeFrames = 0;
            return;
        }
        if (++s_resumeFrames < kSettleFrames) return;
        g_StandaloneSaveLoadDialog = nullptr;
        mainMenu->OnElementEvent(nullptr, kMsgButtonClick, kMenuLoadButton, 0, 0, 0, nullptr);
        s_resume = ResumePhase::WaitList;
        s_resumeFrames = 0;
        return;
    case ResumePhase::WaitList: {
        if (++s_resumeFrames > kResumeTimeoutFrames) {
            Internal::Log("the save list did not open: the save is not loaded");
            s_resume = ResumePhase::Done;
            return;
        }
        SaveLoadDialog* dlg = g_StandaloneSaveLoadDialog;
        if (dlg == nullptr || dlg->refreshFlag != 0 || dlg->saveCount <= 0) return;
        int pick = -1;
        if (::Save::SaveLoad::_singleton != nullptr) {
            for (int i = 0; i < dlg->saveCount; ++i) {
                const ::Save::SaveLoad::SaveInfo* info = &::Save::SaveLoad::_singleton->GetSaveInfo(i);
                const char*                       nm = info != nullptr ? info->name._data : nullptr;
                if (nm != nullptr && std::strcmp(nm, s_resumeName.c_str()) == 0) {
                    pick = i;
                    break;
                }
            }
        }
        s_resume = ResumePhase::Done;
        if (pick < 0) {
            Internal::Log("the save \"" + s_resumeName + "\" is not in the list: it is left for the player");
            return;
        }
        dlg->selectedIndex = pick;
        dlg->OnElementEvent(nullptr, kMsgButtonClick, kSaveLoadLoadButton, 0, 0, 0, nullptr);
        Internal::Log("the load of \"" + s_resumeName + "\" was asked for");
        return;
    }
    default:
        return;
    }
}

}  // namespace

std::string BuildLineNote() {
    const std::string notice = Internal::Notice();
    if (!notice.empty()) {
        Internal::ClearNotice();   // shown once
        return "  -  " + notice;
    }
    const std::string ready = Internal::ReadyVersion();
    if (ready.empty()) return std::string();
    return "  -  OpenBA " + ready + " is ready: restart to install it";
}

void AttachMenuButton(MainMenuDialog* menu) {
    if (menu == nullptr || menu->_ui == nullptr || menu->_dialog == nullptr) return;
    if (Ext::GateProfileForced()) return;   // the gate's faithful run: the menu as retail's
    if (Internal::ReadyVersion().empty()) return;
    UI::Element* mod = Find(menu, "buttonMod");
    UI::Element* quit = Find(menu, "buttonQuit");
    UI::Element* anchor = mod != nullptr ? mod : quit;
    if (anchor == nullptr || anchor->_parent == nullptr || ::Object::ExternalFactory::_singleton == nullptr) return;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kButtonRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        std::fprintf(stderr, "[Update] %s did not load: no Restart to update button\n", kButtonRsc);
        return;
    }
    Object::InternalPtr<UI::Element> button;
    static_cast<const UI::ElementDescription*>(ext)->CreateInstance(&button, menu->_dialog);
    if (button.Get() == nullptr) return;
    UI::Element* list = anchor->_parent;
    button->SetName(System::String("buttonUpdate"));
    button->SetElementId(kRestartMenuButtonId);
    list->AddChild(&button);
    // Just before Quit when Quit is in the same list: move the appended child up.
    UI::Element** kids = list->_children.GetData();
    const int     n = list->_children.GetSize();
    int           at = -1;
    for (int i = 0; i < n; ++i) {
        if (kids[i] == quit) {
            at = i;
            break;
        }
    }
    if (at >= 0 && n >= 2 && kids[n - 1] == button.Get()) {
        UI::Element* added = kids[n - 1];
        for (int i = n - 1; i > at; --i) kids[i] = kids[i - 1];
        kids[at] = added;
    }
    std::fprintf(stderr, "[Update] Restart to update button added (id %d, %s menu)\n", kRestartMenuButtonId,
                 menu->game != nullptr ? "pause" : "main");
}

void OnRestartButton(MainMenuDialog* menu) {
    if (menu == nullptr || s_restart != RestartPhase::Idle) return;
    const std::string ready = Internal::ReadyVersion();
    if (menu->game == nullptr) {
        Internal::Log("Restart to update (" + ready + ") from the main menu");
        QuitForRestart();
        return;
    }
    // In game: close the pause menu (its Resume), then GameTick saves and quits.
    s_restart = RestartPhase::WantSave;
    s_restartWhy = ready;
    Object::InternalPtr<UI::DialogInstance> self(menu);
    menu->_ui->CloseDialog(&self);
}

void MenuTick(MenuDialogLoopT* menuLoop) {
    if (!s_started) MarkStarted();
    if (menuLoop == nullptr) return;
    if (s_resume == ResumePhase::WaitMenu || s_resume == ResumePhase::WaitList) TickResume(menuLoop);
}

void GameTick(GameUpdateLoop* gul) {
    if (gul == nullptr) return;
    if (!s_started) MarkStarted();
    std::string announced;
    if (Internal::TakeReadyAnnouncement(announced)) {
        Row(gul, "OpenBA " + announced + " has downloaded. It installs when you restart the game; "
                 "Esc > Restart to update keeps your town.");
    }
    if (s_restart == RestartPhase::Idle || s_restart == RestartPhase::Quitting) return;
    Save::SaveLoad* saveLoad = ::Save::SaveLoad::_singleton;
    if (saveLoad == nullptr) return;
    if (s_restart == RestartPhase::Saving) {
        if (saveLoad->GetOperation() != 0) return;   // still writing
        const std::filesystem::path dir = Internal::StagingDir();
        if (!dir.empty()) Staging::WriteLine(dir, "resume", "Auto");
        Internal::Log("the town is saved; restarting to install " + s_restartWhy);
        QuitForRestart();
        return;
    }
    const std::string why = StartSave(gul, saveLoad);
    if (!why.empty()) {
        if (why != s_lastRefusal) Internal::Log("waiting to save before the restart: " + why);
        s_lastRefusal = why;
        return;
    }
    s_lastRefusal.clear();
    s_restart = RestartPhase::Saving;
    Row(gul, "Saving your town and restarting to install OpenBA " + s_restartWhy + ".");
}

}  // namespace AutoUpdate

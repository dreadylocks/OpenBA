// ============================================================================
// src/ext/neighbour/lobby_panel.cpp -- NOT RETAIL. ext.neighbour M2: the Multiplayer lobby (lobby_panel.h)
// ============================================================================
// Standalone code over the game's UI (the guards are ours to place). The relay is asked when the
// lobby opens and on Refresh; Create and Join each ask it once more. Those waits (a few seconds at
// most) happen on the menu, where nothing else runs.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/EditElement.h"
#include "ui/ElementDescription.h"
#include "ui/Element.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"
#include "ui/Dialog.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/String.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "Dialog/MainMenuDialog.h"
#include "Game/ClimateGenerator.h"
#include "Game/MapGenerator.h"
#include "Game/TerrainGenerator.h"
#include "io/Persistence.h"
#include "ext/registry.h"

#include "ext/neighbour/lobby_panel.h"
#include "ext/neighbour/lobby.h"
#include "ext/neighbour/neighbour.h"
#include "ext/neighbour/protocol.h"
#include "ext/neighbour/seat_store.h"   // LoadSeatToken / RememberSeat

#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Neighbour {

namespace {

constexpr const char* kLobbyRsc  = "Dialog/ExtensionsLobby.rsc";
constexpr const char* kButtonRsc = "Dialog/ExtensionsLobby.rsc:buttonMultiplayer";
constexpr int kMaxRows = 12;

enum : int32_t { kIdCancel = 100, kIdRow0 = 3000, kIdRefresh = 4001, kIdCreate, kIdJoin };
constexpr int32_t kMsgClick = 0x12;

UI::Element* Find(UI::DialogInstance* d, const char* name) { return d->FindElement(System::String(name)).Get(); }

void SetText(UI::Element* e, const std::string& text) {
    if (::Object::IsDerivedFromNullSafe(e, UI::TextElement::TypeId()))
        static_cast<UI::TextElement*>(e)->SetParameter(0, System::String(text.c_str()));
}

UI::EditElement* AsEdit(UI::Element* e) {
    return ::Object::IsDerivedFromNullSafe(e, UI::EditElement::TypeId()) ? static_cast<UI::EditElement*>(e) : nullptr;
}

std::string EditText(UI::Element* e) {
    UI::EditElement* edit = AsEdit(e);
    if (edit == nullptr || edit->GetText()._data == nullptr) return std::string();
    return edit->GetText()._data;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// settings.cfg neighbour.server: the relay box's starting text. There is no built-in relay
// (interop spec layer 0: no default address tied to one person's account).
std::string SettingsServer() {
    const ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p != nullptr) {
        System::String k("neighbour.server");
        const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
        if (node != nullptr && node->element.value._data != nullptr && node->element.value._data[0] != '\0')
            return node->element.value._data;
    }
    return std::string();
}

// What the player last typed in the relay box this session (the box starts from it next time).
std::string s_typedRelay;

template <class T>
T* LoadAs(const std::string& path) {
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr || path.empty()) return nullptr;
    T* x = f->FindOrLoadAs<T>(System::Filename(path.c_str()));
    return ::Object::IsDerivedFromNullSafe(x, T::TypeId()) ? x : nullptr;
}

class LobbyDialog final : public UI::DialogInstance {
public:
    LobbyDialog(UI::UserInterface* ui, const UI::Dialog* dialog, MainMenuDialog* menu, std::string note)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0),
          _menu(menu), _note(std::move(note)) {}

    void OnOpened() override {
        if (UI::Element* c = Find(this, "buttonCancel")) {
            c->SetElementId(kIdCancel);
            static_cast<UI::ButtonElement*>(c)->_keyIndex = 8;   // Esc
        }
        if (UI::Element* e = Find(this, "buttonRefresh")) e->SetElementId(kIdRefresh);
        if (UI::Element* e = Find(this, "buttonCreate")) e->SetElementId(kIdCreate);
        if (UI::Element* e = Find(this, "buttonJoin")) e->SetElementId(kIdJoin);
        _list = FindElement(System::String("roomList"));
        // The relay: what was typed earlier this session, else settings.cfg's.
        const std::string relay = s_typedRelay.empty() ? SettingsServer() : s_typedRelay;
        if (UI::EditElement* r = AsEdit(Find(this, "editRelay"))) r->SetText(System::String(relay.c_str()));
        // Your town: what settings.cfg names, as a starting point.
        const ::IO::Persistence* p = ::IO::Persistence::_singleton;
        if (p != nullptr) {
            System::String k("neighbour.name");
            const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
            if (node != nullptr && node->element.value._data != nullptr)
                if (UI::EditElement* t = AsEdit(Find(this, "editTown"))) t->SetText(System::String(node->element.value._data));
        }
        SetText(Find(this, "textHint"), "Pick a room, then type its password and your town's name and press Join. "
                                        "Or type a new room name and password and press Create room. Room names are "
                                        "public; the password keeps others out.");
        Refresh();
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c, int32_t d,
                        int32_t e, void* userData) override {
        if (msg == kMsgClick) {
            if (id == kIdCancel) { Close(); return; }
            if (id == kIdRefresh) Refresh();
            else if (id == kIdCreate) { if (Create()) return; }
            else if (id == kIdJoin) { if (Join()) return; }
            else if (id >= kIdRow0 && id < kIdRow0 + static_cast<int32_t>(_shown)) Pick(id - kIdRow0);
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

private:
    void Close() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }

    void Status(const std::string& s) { SetText(Find(this, "textStatus"), s); }

    // The relay box (wss://... or ws://...), remembered for this session; "" with a hint when empty.
    std::string Server() {
        const std::string relay = Trim(EditText(Find(this, "editRelay")));
        s_typedRelay = relay;
        if (relay.empty())
            Status("Type the relay's address in the Relay box (wss://... for an internet relay, "
                   "ws://<address>:8787 for a test relay), then Refresh.");
        return relay;
    }

    void Refresh() {
        std::string why;
        std::vector<RoomInfo> rooms;
        const std::string server = Server();
        if (server.empty()) return;
        const bool ok = ListRooms(&MakePlatformTransport, server, rooms, why);
        if (ok) _rooms = rooms;
        Rebuild();
        std::string line = ok ? (_rooms.empty() ? std::string("No rooms yet: create one.")
                                                : std::to_string(_rooms.size()) + " room(s) on the server.")
                              : "Could not get the room list: " + why;
        if (!_note.empty()) { line = _note + "  " + line; _note.clear(); }
        Status(line);
    }

    // One button per room (the first kMaxRows), built once and re-labelled after.
    void Rebuild() {
        const UI::Dialog* dialog = _dialog;
        if (_list.Get() == nullptr || dialog == nullptr) return;
        const size_t want = _rooms.size() < static_cast<size_t>(kMaxRows) ? _rooms.size() : kMaxRows;
        while (_rows.size() < want) {
            Object::ExternalPtr<UI::ElementDescription> tpl = dialog->GetTemplate(0);
            if (tpl.Get() == nullptr) break;
            Object::InternalPtr<UI::Element> row;
            tpl->CreateInstance(&row, dialog);
            if (row.Get() == nullptr) break;
            row->SetElementId(kIdRow0 + static_cast<int32_t>(_rows.size()));
            _list->AddChild(&row);
            _rows.push_back(row);
        }
        _shown = want;
        for (size_t i = 0; i < _rows.size(); ++i) {
            UI::Element* row = _rows[i].Get();
            if (row == nullptr) continue;
            if (i >= want) { row->Hide(true); continue; }
            row->Hide(false);
            const RoomInfo& r = _rooms[i];
            std::string label = r.name + "  -  by " + (r.creator.empty() ? std::string("?") : r.creator) + "  -  " +
                                std::to_string(r.seats) + "/2 towns  -  " + DescribeSettings(r.settings);
            if (r.seats >= 2) label += "  (full)";
            SetText(row->FindElement(System::String("roomCaption")).Get(), Sanitize(label, 120));
        }
    }

    void Pick(int i) {
        if (i < 0 || static_cast<size_t>(i) >= _rooms.size()) return;
        if (UI::EditElement* r = AsEdit(Find(this, "editRoom"))) r->SetText(System::String(_rooms[i].name.c_str()));
        Status("Room \"" + _rooms[i].name + "\" by " + _rooms[i].creator + ": type its password and your town's name, then Join.");
    }

    bool Fields(std::string& room, std::string& pass, std::string& town) {
        room = Trim(EditText(Find(this, "editRoom")));
        pass = EditText(Find(this, "editPass"));
        town = Sanitize(EditText(Find(this, "editTown")), kMaxNameChars);
        if (room.empty()) { Status("Type a room name (or pick one from the list)."); return false; }
        if (pass.empty()) { Status("Type the room's password."); return false; }
        if (town.empty()) { Status("Type your town's name."); return false; }
        return true;
    }

    // Create: remember the room for the next new game, then the game's own New Game dialog.
    bool Create() {
        std::string room, pass, town;
        if (!Fields(room, pass, town)) return false;
        for (const RoomInfo& r : _rooms)
            if (r.name == room) { Status("A room called \"" + room + "\" already exists: pick another name, or Join it."); return false; }
        const std::string server = Server();
        if (server.empty()) return false;
        LobbyStart s;
        s.create = true;
        s.server = server;
        s.room = room;
        s.pass = pass;
        s.town = town;
        SetLobbyStart(s);
        fprintf(stderr, "[Neighbour] lobby: creating room \"%s\" as \"%s\" -> New Game\n", room.c_str(), town.c_str());
        MainMenuDialog* menu = _menu;
        Close();
        menu->OpenNewGame();
        return true;
    }

    // Join: ask the room for its settings, then start the new game with them.
    bool Join() {
        std::string room, pass, town;
        if (!Fields(room, pass, town)) return false;
        const std::string server = Server();
        if (server.empty()) return false;
        Status("Joining \"" + room + "\"...");
        RoomJoin j;
        std::string why;
        const std::string kept = LoadSeatToken(server, room, town);   // a town coming back to its seat
        if (!JoinRoom(&MakePlatformTransport, server, room, pass, town, kept, LocalInfoNow(), j, why)) {
            Status("Could not join \"" + room + "\": " + why);
            return false;
        }
        MapGenerator* mg = LoadAs<MapGenerator>(j.settings.start);
        TerrainGenerator* tg = LoadAs<TerrainGenerator>(j.settings.terrain);
        ClimateGenerator* cg = LoadAs<ClimateGenerator>(j.settings.climate);
        if (mg == nullptr || tg == nullptr || cg == nullptr) {
            Status("This game does not have the room's map settings (" + DescribeSettings(j.settings) +
                   "): the two games need the same version and mods.");
            return false;
        }
        LobbyStart s;
        s.create = false;
        s.server = server;
        s.room = room;
        s.pass = pass;
        s.town = town;
        s.seat = j.seat;
        s.token = j.token.empty() ? kept : j.token;
        if (!j.token.empty()) RememberSeat(server, room, town, j.token);   // before the game can crash
        s.settings = j.settings;
        SetLobbyStart(s);
        fprintf(stderr, "[Neighbour] lobby: joining room \"%s\" as \"%s\" (seat %d, seed %lld, %s)\n", room.c_str(),
                town.c_str(), j.seat, static_cast<long long>(j.settings.seed), DescribeSettings(j.settings).c_str());
        MainMenuDialog* menu = _menu;
        Close();
        menu->StartNewGame(System::String(town.c_str()), static_cast<uint32_t>(j.settings.seed),
                           static_cast<uint32_t>(j.settings.size), /*disastersOff=*/!j.settings.disasters, mg, tg, cg,
                           /*scenarioResIdx=*/static_cast<uint32_t>(-1), /*savedStateIBP=*/nullptr);
        return true;
    }

    MainMenuDialog*                               _menu;
    std::string                                   _note;
    Object::InternalPtr<UI::Element>              _list;
    std::vector<Object::InternalPtr<UI::Element>> _rows;
    size_t                                        _shown = 0;
    std::vector<RoomInfo>                         _rooms;
};

}  // namespace

void Lobby_Open(MainMenuDialog* menu, const std::string& note) {
    if (menu == nullptr || menu->_ui == nullptr || ::Object::ExternalFactory::_singleton == nullptr) return;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kLobbyRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        fprintf(stderr, "[Neighbour] %s did not load as a dialog\n", kLobbyRsc);
        return;
    }
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(LobbyDialog), 8);
    if (mem == nullptr) return;
    LobbyDialog* dlg = ::new (mem) LobbyDialog(menu->_ui, static_cast<const UI::Dialog*>(ext), menu, note);
    Object::InternalPtr<UI::DialogInstance> ref(dlg);
    menu->_ui->AddDialog(&ref);   // dispatches OnOpened
}

void Lobby_AttachMenuButton(MainMenuDialog* menu) {
    if (menu == nullptr || menu->_ui == nullptr || menu->_dialog == nullptr) return;
    if (menu->game != nullptr) return;            // the pause menu: no lobby in a running game
    if (Ext::GateProfileForced()) return;         // the gate's faithful run: the menu as retail's
    UI::Element* mod = Find(menu, "buttonMod");
    if (mod == nullptr || mod->_parent == nullptr || ::Object::ExternalFactory::_singleton == nullptr) return;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kButtonRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::ElementDescription::TypeId())) {
        fprintf(stderr, "[Neighbour] %s did not load: no Multiplayer button\n", kButtonRsc);
        return;
    }
    Object::InternalPtr<UI::Element> button;
    static_cast<const UI::ElementDescription*>(ext)->CreateInstance(&button, menu->_dialog);
    if (button.Get() == nullptr) return;
    UI::Element* list = mod->_parent;
    button->SetName(System::String("buttonMultiplayer"));
    button->SetElementId(kMultiplayerMenuButtonId);
    list->AddChild(&button);
    // Right after New (the first button): move the appended child up.
    UI::Element** kids = list->_children.GetData();
    const int n = list->_children.GetSize();
    UI::Element* first = Find(menu, "buttonNew");
    int at = -1;
    for (int i = 0; i < n; ++i) if (kids[i] == first) { at = i; break; }
    if (at >= 0 && n >= 2 && kids[n - 1] == button.Get()) {
        UI::Element* added = kids[n - 1];
        for (int i = n - 1; i > at + 1; --i) kids[i] = kids[i - 1];
        kids[at + 1] = added;
    }
    fprintf(stderr, "[Neighbour] Multiplayer button added (id %d)\n", kMultiplayerMenuButtonId);
}

}  // namespace Neighbour

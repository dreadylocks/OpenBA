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
#include "collection/avl_trees.h"   // rtavl::StringMapInsert: the settings map's insert
#include "ext/registry.h"

#include "ext/neighbour/lobby_panel.h"
#include "ext/neighbour/lobby.h"
#include "ext/neighbour/neighbour.h"
#include "ext/neighbour/protocol.h"
#include "ext/neighbour/seat_store.h"   // LoadSeatToken / RememberSeat
#include "ext/neighbour/continents.h"   // the Continents map type (§12)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Neighbour {

namespace {

constexpr const char* kLobbyRsc  = "Dialog/ExtensionsLobby.rsc";
constexpr const char* kButtonRsc = "Dialog/ExtensionsLobby.rsc:buttonMultiplayer";
constexpr int kMaxRows = 200;   // rows shown at most; the search narrows the rest

enum : int32_t {
    kIdBack = 100,
    kIdRow0 = 3000,
    kIdRefresh = 4001,
    kIdPrimary,
    kIdNew,
    kIdRelay,
    kIdConnect,
    kIdLoad,
    kIdMapOne,
    kIdMapContinents
};
constexpr int32_t kMsgClick = 0x12;

// Text colours (ARGB): ordinary, dimmed details, a full room, the picked room, a good and a bad state.
constexpr uint32_t kColourText    = 0xFFFFFFFF;
constexpr uint32_t kColourMuted   = 0xFFB4AB9C;
constexpr uint32_t kColourDim     = 0xFF7E776C;
constexpr uint32_t kColourPicked  = 0xFFE8C77D;
constexpr uint32_t kColourGood    = 0xFFA5CF7A;
constexpr uint32_t kColourProblem = 0xFFE8937F;

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

// A relay that answered becomes settings.cfg's neighbour.server, so the box starts with it next time.
// Persistence::Set's insert-or-replace (persistence.cpp), with a text value; written at once.
void RememberRelay(const std::string& relay) {
    ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr || relay.empty() || relay == SettingsServer()) return;
    System::String key("neighbour.server");
    System::String value(relay.c_str());
    RtIo::SettingsNode* node = nullptr;
    rtavl::StringMapInsert(&p->_settingsMap, &node, &key._data, &value._data);
    if (node != nullptr && node->element.value._data != value._data) node->element.value = value;
    p->WriteSettings();
    fprintf(stderr, "[Neighbour] lobby: relay %s kept in settings.cfg\n", relay.c_str());
}

std::string Setting(const char* key);   // below: a settings.cfg value, "" when unset

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
        if (UI::Element* c = Find(this, "buttonBack")) {
            c->SetElementId(kIdBack);
            static_cast<UI::ButtonElement*>(c)->_keyIndex = 8;   // Esc
        }
        if (UI::Element* e = Find(this, "buttonRefresh")) e->SetElementId(kIdRefresh);
        if (UI::Element* e = Find(this, "buttonPrimary")) e->SetElementId(kIdPrimary);
        if (UI::Element* e = Find(this, "buttonNew")) e->SetElementId(kIdNew);
        if (UI::Element* e = Find(this, "buttonRelay")) e->SetElementId(kIdRelay);
        if (UI::Element* e = Find(this, "buttonConnect")) e->SetElementId(kIdConnect);
        if (UI::Element* e = Find(this, "buttonLoad")) e->SetElementId(kIdLoad);
        if (UI::Element* e = Find(this, "buttonMapOne")) e->SetElementId(kIdMapOne);
        if (UI::Element* e = Find(this, "buttonMapContinents")) e->SetElementId(kIdMapContinents);
        _table = FindElement(System::String("roomTable"));
        // The relay: what was typed earlier this session, else settings.cfg's.
        std::string relay = s_typedRelay.empty() ? SettingsServer() : s_typedRelay;
        // TEST AID (settings.cfg neighbour.lobbyrelay=<url>): the box starts with this, as if the player
        // had typed it, so an unattended run can check that a relay that answers is kept (RememberRelay).
        const std::string typed = Setting("neighbour.lobbyrelay");
        if (!typed.empty()) relay = typed;
        if (UI::EditElement* r = AsEdit(Find(this, "editRelay"))) r->SetText(System::String(relay.c_str()));
        // Your town: what settings.cfg names, as a starting point.
        const ::IO::Persistence* p = ::IO::Persistence::_singleton;
        if (p != nullptr) {
            System::String k("neighbour.name");
            const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
            if (node != nullptr && node->element.value._data != nullptr)
                if (UI::EditElement* t = AsEdit(Find(this, "editTown"))) t->SetText(System::String(node->element.value._data));
        }
        ShowRelayRow(relay.empty());
        ShowPick();
        Refresh();
        // TEST AID (settings.cfg neighbour.lobbyview=join|new): open on the first room's Join panel, or
        // on New room (with neighbour.map=continents picked), so an unattended run can show each state
        // (the menu driver clicks only the menu).
        const std::string view = Setting("neighbour.lobbyview");
        if (view == "join" && !_visible.empty()) Pick(_visible[0]);
        if (view == "new") ShowCreate();
        if (view == "new" && Setting("neighbour.map") == Continents::kMapContinents) PickMap(Continents::kMapContinents);
    }

    // The search box has no change event (an edit box reports only when it loses focus), so it is
    // read once a frame and the list is filtered when its text changes.
    void OnUpdate(const System::Time& time) override {
        const std::string search = Trim(EditText(Find(this, "editSearch")));
        if (search != _search) {
            _search = search;
            Rebuild();
        }
        UI::DialogInstance::OnUpdate(time);
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c, int32_t d,
                        int32_t e, void* userData) override {
        if (msg == kMsgClick) {
            if (id == kIdBack) {
                Close();
                return;
            }
            if (id == kIdRefresh || id == kIdConnect) Refresh();
            else if (id == kIdRelay) ShowRelayRow(!_relayShown);
            else if (id == kIdNew) ShowCreate();
            else if (id == kIdMapOne) PickMap("");
            else if (id == kIdMapContinents) PickMap(Continents::kMapContinents);
            else if (id == kIdLoad) {
                MainMenuDialog* menu = _menu;
                Close();
                menu->OpenLoad();   // a saved town carries its room and seat with it
                return;
            }
            else if (id == kIdPrimary) {
                if (_mode == Mode::Create && Create()) return;
                if (_mode == Mode::Join && Join()) return;
            } else if (id >= kIdRow0 && id < kIdRow0 + static_cast<int32_t>(_visible.size())) {
                Pick(_visible[static_cast<size_t>(id - kIdRow0)]);
            }
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

private:
    enum class Mode { Pick, Join, Create };

    void Close() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
    }

    void Show(const char* name, bool show) {
        if (UI::Element* e = Find(this, name)) e->Hide(!show);
    }

    void Paint(UI::Element* e, const std::string& text, uint32_t colour) {
        SetText(e, text);
        if (e != nullptr) e->SetColor(colour);
    }

    void Status(const std::string& s, uint32_t colour = kColourText) { Paint(Find(this, "textStatus"), s, colour); }
    void Problem(const std::string& s) { Status(s, kColourProblem); }

    void ShowRelayRow(bool show) {
        Show("rowRelay", show);
        _relayShown = show;
    }

    // The relay box (wss://... or ws://...), remembered for this session; "" with a hint when empty.
    std::string Server() {
        const std::string relay = Trim(EditText(Find(this, "editRelay")));
        s_typedRelay = relay;
        return relay;
    }

    void Refresh() {
        UI::Element* connection = Find(this, "textConnection");
        const std::string server = Server();
        if (server.empty()) {
            Paint(connection, "No relay set", kColourProblem);
            ShowRelayRow(true);
            Problem("Type the relay's address above (wss://...), then Connect.");
            _rooms.clear();
            Rebuild();
            return;
        }
        std::string why;
        std::vector<RoomInfo> rooms;
        _mySeats = LoadSeats();
        _server = server;
        if (ListRooms(&MakePlatformTransport, server, rooms, why)) {
            // Rooms this machine holds a seat in first, then open rooms, then full ones; the most
            // recently used first in each.
            auto rank = [this](const RoomInfo& r) { return Yours(r) ? 0 : (r.seats >= 2 ? 2 : 1); };
            std::stable_sort(rooms.begin(), rooms.end(), [&rank](const RoomInfo& a, const RoomInfo& b) {
                if (rank(a) != rank(b)) return rank(a) < rank(b);
                return a.lastActive > b.lastActive;
            });
            _rooms = rooms;
            _listed = true;
            RememberRelay(server);
            Paint(connection, "Connected to your relay", kColourGood);
            Status("");
        } else {
            _listed = false;
            Paint(connection, "Can't reach the relay", kColourProblem);
            ShowRelayRow(true);
            Problem("Couldn't get the room list: " + Sanitize(why, 80));
        }
        if (!_note.empty()) {
            Status(_note);
            _note.clear();
        }
        if (_mode == Mode::Join && FindRoom(_selected) == nullptr) ShowPick();
        Rebuild();
    }

    // Our seat in a room (seat_store.h), as of the last Refresh; an empty token when none.
    SeatKey MySeat(const RoomInfo& r) const { return FindSeatInRoom(_mySeats, _server, r.name); }
    bool Yours(const RoomInfo& r) const { return !MySeat(r).token.empty(); }

    const RoomInfo* FindRoom(const std::string& name) const {
        for (const RoomInfo& r : _rooms)
            if (r.name == name) return &r;
        return nullptr;
    }

    static std::string Lower(std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    }

    // The rooms the search matches (by room or town name, any case), at most kMaxRows of them, one
    // row each. Rows are made as they are first needed and re-labelled after.
    void Rebuild() {
        const UI::Dialog* dialog = _dialog;
        if (_table.Get() == nullptr || dialog == nullptr) return;
        const std::string needle = Lower(_search);
        size_t matches = 0;
        _visible.clear();
        for (size_t i = 0; i < _rooms.size(); ++i) {
            if (!needle.empty() && Lower(_rooms[i].name).find(needle) == std::string::npos &&
                Lower(_rooms[i].creator).find(needle) == std::string::npos)
                continue;
            ++matches;
            if (_visible.size() < static_cast<size_t>(kMaxRows)) _visible.push_back(i);
        }
        while (_rows.size() < _visible.size()) {
            Object::ExternalPtr<UI::ElementDescription> tpl = dialog->GetTemplate(0);
            if (tpl.Get() == nullptr) break;
            Object::InternalPtr<UI::Element> row;
            tpl->CreateInstance(&row, dialog);
            if (row.Get() == nullptr) break;
            row->SetElementId(kIdRow0 + static_cast<int32_t>(_rows.size()));
            _table->AddChild(&row);
            _rows.push_back(row);
        }
        for (size_t slot = 0; slot < _rows.size(); ++slot) {
            UI::Element* row = _rows[slot].Get();
            if (row == nullptr) continue;
            if (slot >= _visible.size()) {
                row->Hide(true);
                continue;
            }
            row->Hide(false);
            const RoomInfo& r = _rooms[_visible[slot]];
            const bool yours = Yours(r);
            const bool full = r.seats >= 2 && !yours;   // a full room of ours is not closed to us
            const bool picked = _mode == Mode::Join && r.name == _selected;
            const std::string seats = yours ? "yours" : (full ? "full" : (r.seats == 1 ? "1 of 2" : "empty"));
            const std::string by = r.creator.empty() ? std::string() : "by " + r.creator + " - ";
            Paint(row->FindElement(System::String("roomName")).Get(), Sanitize(r.name, 64),
                  full ? kColourDim : (picked ? kColourPicked : kColourText));
            Paint(row->FindElement(System::String("roomSeats")).Get(), seats,
                  full ? kColourDim : (picked ? kColourPicked : kColourText));
            Paint(row->FindElement(System::String("roomDetails")).Get(), Sanitize(by + DescribeSettings(r.settings), 72),
                  full ? kColourDim : kColourMuted);
        }
        std::string foot;
        if (!_listed) foot = "";
        else if (_rooms.empty()) foot = "No rooms yet. Start one with New room.";
        else if (matches == 0) foot = "No rooms match \"" + Sanitize(_search, 30) + "\".";
        else if (matches > _visible.size())
            foot = "Showing " + std::to_string(_visible.size()) + " of " + std::to_string(matches) + ". Search to narrow it.";
        else {
            size_t open = 0;
            for (size_t i : _visible)
                if (_rooms[i].seats < 2) ++open;
            foot = std::to_string(matches) + (matches == 1 ? " room, " : " rooms, ") + std::to_string(open) + " open";
            if (!_mySeats.empty()) {
                size_t yours = 0;
                for (size_t i : _visible)
                    if (Yours(_rooms[i])) ++yours;
                if (yours > 0) foot += ", " + std::to_string(yours) + " yours";
            }
        }
        SetText(Find(this, "textListNote"), foot);
    }

    void Panel(const std::string& title, const std::string& note) {
        SetText(Find(this, "textPanelTitle"), title);
        SetText(Find(this, "textPanelNote"), note);
    }

    void ShowPick() {
        _mode = Mode::Pick;
        _selected.clear();
        _ownTown.clear();
        Panel("Pick a room", "Choose one on the left, or start a new one.");
        Show("fieldRoom", false);
        Show("fieldPass", false);
        Show("fieldTown", false);
        Show("fieldMap", false);
        Show("buttonLoad", false);
        Show("buttonPrimary", false);
    }

    void Pick(size_t i) {
        if (i >= _rooms.size()) return;
        const RoomInfo& r = _rooms[i];
        _mode = Mode::Join;
        _selected = r.name;
        // A room this machine holds a seat in (seat_store.h): yours, as that town, never a second seat.
        const SeatKey seat = MySeat(r);
        _ownTown = seat.token.empty() ? std::string() : seat.town;
        Show("fieldRoom", false);
        Show("fieldPass", true);
        Show("fieldMap", false);
        Show("buttonPrimary", true);
        if (!_ownTown.empty()) {
            Panel("Your room: " + Sanitize(r.name, 36), "You play here as " + Sanitize(_ownTown, kMaxNameChars) + ".");
            Show("fieldTown", false);
            Show("buttonLoad", true);
            SetText(Find(this, "captionPrimary"), "Start over as " + Sanitize(_ownTown, kMaxNameChars));
            Status("Starting over clears your old town here.", kColourMuted);
        } else {
            const std::string who = r.creator.empty() ? std::string("A town") : r.creator;
            Panel("Join " + Sanitize(r.name, 40),
                  r.seats >= 2 ? "Full: only its own two towns can come back."
                               : (r.seats == 1 ? who + " is waiting for a neighbor." : "No town is in this room yet."));
            Show("fieldTown", true);
            Show("buttonLoad", false);
            SetText(Find(this, "captionPrimary"), "Join room");
            Status("");
        }
        Rebuild();
    }

    void ShowCreate() {
        _mode = Mode::Create;
        _selected.clear();
        _ownTown.clear();
        Panel("New room", "Name it and set a password. The map comes next.");
        Show("fieldRoom", true);
        Show("fieldPass", true);
        Show("fieldTown", true);
        Show("fieldMap", true);
        Show("buttonLoad", false);
        Show("buttonPrimary", true);
        SetText(Find(this, "captionPrimary"), "Create room");
        PickMap(_map);
        Status("");
        Rebuild();
    }

    // New room's map: one land the two towns share, or Continents, a land each with sea between.
    void PickMap(const std::string& map) {
        _map = map;
        const bool continents = _map == Continents::kMapContinents;
        if (UI::Element* c = Find(this, "captionMapOne")) c->SetColor(continents ? kColourMuted : kColourPicked);
        if (UI::Element* c = Find(this, "captionMapContinents")) c->SetColor(continents ? kColourPicked : kColourMuted);
        SetText(Find(this, "textMapNote"), continents ? "Each town gets its own land, with sea between."
                                                      : "Both towns build on one land.");
    }

    bool Fields(const std::string& room, std::string& pass, std::string& town) {
        pass = EditText(Find(this, "editPass"));
        town = _ownTown.empty() ? Sanitize(EditText(Find(this, "editTown")), kMaxNameChars) : _ownTown;
        if (room.empty()) {
            Problem("Type a name for the room.");
            return false;
        }
        if (pass.empty()) {
            Problem("Type the room's password.");
            return false;
        }
        if (town.empty()) {
            Problem("Type your town's name.");
            return false;
        }
        return true;
    }

    // Create: remember the room for the next new game, then the game's own New Game dialog.
    bool Create() {
        const std::string room = Trim(EditText(Find(this, "editRoom")));
        std::string pass, town;
        if (!Fields(room, pass, town)) return false;
        if (FindRoom(room) != nullptr) {
            Problem("There's already a room called \"" + Sanitize(room, 40) + "\". Pick another name.");
            return false;
        }
        const std::string server = Server();
        if (server.empty()) {
            ShowRelayRow(true);
            Problem("Type the relay's address above first.");
            return false;
        }
        LobbyStart s;
        s.create = true;
        s.server = server;
        s.room = room;
        s.pass = pass;
        s.town = town;
        s.settings.map = _map;   // the rest of the map comes from New Game
        SetLobbyStart(s);
        fprintf(stderr, "[Neighbour] lobby: creating room \"%s\" as \"%s\" (%s) -> New Game\n", room.c_str(),
                town.c_str(), _map.empty() ? "one land" : _map.c_str());
        MainMenuDialog* menu = _menu;
        Close();
        menu->OpenNewGame();
        return true;
    }

    // Join: ask the room for its settings, then start the new game with them.
    bool Join() {
        const std::string room = _selected;
        std::string pass, town;
        if (!Fields(room, pass, town)) return false;
        const std::string server = Server();
        if (server.empty()) return false;
        RoomJoin j;
        std::string why;
        const std::string kept = LoadSeatToken(server, room, town);   // a town coming back to its seat
        if (!JoinRoom(&MakePlatformTransport, server, room, pass, town, kept, LocalInfoNow(), j, why)) {
            Problem("Couldn't join: " + Sanitize(why, 120));
            return false;
        }
        MapGenerator* mg = LoadAs<MapGenerator>(j.settings.start);
        TerrainGenerator* tg = LoadAs<TerrainGenerator>(j.settings.terrain);
        ClimateGenerator* cg = LoadAs<ClimateGenerator>(j.settings.climate);
        if (mg == nullptr || tg == nullptr || cg == nullptr) {
            Problem("This game doesn't have the room's map (" + DescribeSettings(j.settings) +
                    "). Both games need the same version and mods.");
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
    Object::InternalPtr<UI::Element>              _table;
    std::vector<Object::InternalPtr<UI::Element>> _rows;
    std::vector<size_t>                           _visible;    // row slot -> index into _rooms
    std::vector<RoomInfo>                         _rooms;
    std::string                                   _search;
    std::string                                   _selected;   // the room picked to join
    std::string                                   _ownTown;    // our town in it, when this machine holds a seat
    std::vector<SeatKey>                          _mySeats;    // the seat file, read at each Refresh
    std::string                                   _server;     // the relay the list came from
    std::string                                   _map;        // New room's map type ("" = one land)
    Mode                                          _mode = Mode::Pick;
    bool                                          _listed = false;
    bool                                          _relayShown = false;
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

namespace {

std::string Setting(const char* key) {
    const ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr) return std::string();
    System::String k(key);
    const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
    if (node == nullptr || node->element.value._data == nullptr) return std::string();
    return node->element.value._data;
}

// TEST AID (NOT RETAIL; settings.cfg neighbour.autolobby=create|join, with BANISHED_AUTOSTART): a game
// creates or joins the settings' lobby room by itself (neighbour.server / room / password / name; a
// create takes neighbour.map=continents for a Continents room), so a
// shared-map game between two games -- two on one machine, or a Mac and a PC -- runs unattended.
// create: the autostarted New Game makes the room (BeforeNewGame), exactly as the lobby's Create.
// join: the room is joined here, on the menu, as the lobby's Join; StartNewGame then takes the room's
// settings (Lobby_RoomSettingsForJoin). Once per process; absent or anything else: nothing.
void AutoLobby() {
    static bool done = false;
    if (done) return;
    done = true;
    const std::string mode = Setting("neighbour.autolobby");
    if (mode != "create" && mode != "join") return;
    const std::string server = SettingsServer();
    const std::string room = Trim(Setting("neighbour.room"));
    const std::string pass = Setting("neighbour.password");
    const std::string town = Sanitize(Setting("neighbour.name"), kMaxNameChars);
    if (server.empty() || room.empty() || pass.empty() || town.empty()) {
        fprintf(stderr, "[Neighbour] autolobby=%s needs neighbour.server, room, password and name\n", mode.c_str());
        return;
    }
    LobbyStart s;
    s.server = server;
    s.room = room;
    s.pass = pass;
    s.town = town;
    if (mode == "create") {
        s.create = true;
        if (Setting("neighbour.map") == Continents::kMapContinents) s.settings.map = Continents::kMapContinents;
        SetLobbyStart(s);
        fprintf(stderr, "[Neighbour] autolobby: the next new game creates room \"%s\" as \"%s\" (%s)\n", room.c_str(),
                town.c_str(), s.settings.map.empty() ? "one land" : s.settings.map.c_str());
        return;
    }
    const std::string kept = LoadSeatToken(server, room, town);
    RoomJoin j;
    std::string why;
    if (!JoinRoom(&MakePlatformTransport, server, room, pass, town, kept, LocalInfoNow(), j, why)) {
        fprintf(stderr, "[Neighbour] autolobby: could not join room \"%s\": %s\n", room.c_str(), why.c_str());
        return;
    }
    s.create = false;
    s.seat = j.seat;
    s.token = j.token.empty() ? kept : j.token;
    s.settings = j.settings;
    if (!j.token.empty()) RememberSeat(server, room, town, j.token);
    SetLobbyStart(s);
    fprintf(stderr, "[Neighbour] autolobby: joined room \"%s\" as \"%s\" (seat %d, seed %lld, %s)\n", room.c_str(),
            town.c_str(), j.seat, static_cast<long long>(j.settings.seed), DescribeSettings(j.settings).c_str());
}

}  // namespace

void Lobby_RoomSettingsForJoin(uint32_t* seed, uint32_t* terrainSize, bool* disastersOff, MapGenerator** mapGen,
                               TerrainGenerator** terrainGen, ClimateGenerator** climateGen) {
    RoomSettings rs;
    if (!PendingJoinSettings(rs)) return;
    MapGenerator* mg = LoadAs<MapGenerator>(rs.start);
    TerrainGenerator* tg = LoadAs<TerrainGenerator>(rs.terrain);
    ClimateGenerator* cg = LoadAs<ClimateGenerator>(rs.climate);
    if (mg == nullptr || tg == nullptr || cg == nullptr) {
        fprintf(stderr, "[Neighbour] this game does not have the room's map settings (%s)\n",
                DescribeSettings(rs).c_str());
        return;
    }
    *seed = static_cast<uint32_t>(rs.seed);
    *terrainSize = static_cast<uint32_t>(rs.size);
    *disastersOff = !rs.disasters;
    *mapGen = mg;
    *terrainGen = tg;
    *climateGen = cg;
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
    AutoLobby();
}

}  // namespace Neighbour

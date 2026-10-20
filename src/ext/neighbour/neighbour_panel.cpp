// ============================================================================
// src/ext/neighbour/neighbour_panel.cpp -- NOT RETAIL. ext.neighbour: the in-game Neighbour panel
// ============================================================================
// Dialog/ExtensionsNeighbour.rsc (our text resource, embedded in the exe). N opens and closes it
// while a game with ext.neighbour runs (not while a text box has the keyboard). The panel shows
// the room's state and has three rows: a chat line, goods to send (pick a material with < >,
// an amount with - +), and the family that would leave. Every action goes through neighbour.h;
// the event log says what happened. Standalone code: the guards are ours to place.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "ui/TextElement.h"
#include "ui/ButtonElement.h"
#include "ui/EditElement.h"
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
#include "input/Device.h"
#include "input/Input.h"
#include "Game/GameUpdateLoop.h"

#include "ext/neighbour/neighbour.h"
#include "ext/neighbour/protocol.h"
#include "ext/neighbour/town.h"

#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace Neighbour {

namespace {

constexpr const char* kPanelRsc = "Dialog/ExtensionsNeighbour.rsc";
constexpr int kKeyN      = 36;   // Input::KeyTables index of 'N' (kIndexToVirtualKey[36] = 0x4E)
constexpr int kKeyEscape = 60;   // editelement.cpp's
constexpr int kRefreshFrames = 30;

enum : int32_t {
    kIdClose = 100, kIdSend = 2001, kIdPrev, kIdNext, kIdLess, kIdMore, kIdSendGoods, kIdSendFamily,
    kIdChat = 2010,
};
constexpr int32_t kMsgClick        = 0x12;
constexpr int32_t kMsgEditFinished = 0x15;   // editelement.cpp kEventEditFinished

const int kAmounts[] = {1, 5, 10, 25, 50, 100, 250, 500};
constexpr int kAmountCount = static_cast<int>(sizeof(kAmounts) / sizeof(kAmounts[0]));

const UI::Dialog* LoadPanel() {
    if (::Object::ExternalFactory::_singleton == nullptr) return nullptr;
    const ::Object::External* ext = ::Object::ExternalFactory::_singleton->FindOrLoad(System::Filename(kPanelRsc));
    if (!::Object::IsDerivedFromNullSafe(ext, UI::Dialog::TypeId())) {
        fprintf(stderr, "[Neighbour] %s did not load as a dialog\n", kPanelRsc);
        return nullptr;
    }
    return static_cast<const UI::Dialog*>(ext);
}

class NeighbourPanel;
NeighbourPanel* s_open = nullptr;
int s_framesToRefresh = 0;

class NeighbourPanel final : public UI::DialogInstance {
public:
    NeighbourPanel(UI::UserInterface* ui, const UI::Dialog* dialog)
        : UI::DialogInstance(ui, dialog, Object::InternalPtr<UI::DialogInstance>(), nullptr, nullptr, 0) {}
    ~NeighbourPanel() override {
        if (s_open == this) s_open = nullptr;
    }

    void OnOpened() override {
        Id("buttonCancel", kIdClose);
        if (UI::Element* c = Find("buttonCancel")) static_cast<UI::ButtonElement*>(c)->_keyIndex = 8;   // Esc
        Id("buttonSend", kIdSend);
        Id("buttonPrev", kIdPrev);
        Id("buttonNext", kIdNext);
        Id("buttonLess", kIdLess);
        Id("buttonMore", kIdMore);
        Id("buttonSendGoods", kIdSendGoods);
        Id("buttonSendFamily", kIdSendFamily);
        Id("editChat", kIdChat);
        ReloadGoods();
        Refresh();
        fprintf(stderr, "[Neighbour] panel opened (%u material(s) in storage)\n", static_cast<unsigned>(_goods.size()));
        for (const Town::Stock& g : _goods)
            fprintf(stderr, "[Neighbour]   %s: %d (%s)\n", g.path.c_str(), g.available, Town::DisplayName(g.resource).c_str());
    }

    void OnElementEvent(Object::InternalPtr<UI::Element>* element, int32_t msg, int32_t id, int32_t c,
                        int32_t d, int32_t e, void* userData) override {
        if (msg == kMsgClick) {
            switch (id) {
            case kIdClose: Close(); return;
            case kIdSend: SendTyped(); break;
            case kIdPrev: Step(-1); break;
            case kIdNext: Step(+1); break;
            case kIdLess: if (_amount > 0) --_amount; break;
            case kIdMore: if (_amount + 1 < kAmountCount) ++_amount; break;
            case kIdSendGoods: SendSelectedGoods(); break;
            case kIdSendFamily: Report(Neighbour::SendFamily()); break;
            default: break;
            }
            Refresh();
        } else if (msg == kMsgEditFinished && id == kIdChat) {
            // Enter or a click elsewhere sends; Esc keeps the text unsent.
            const ::Input::Device* dev = ::Input::Device::_singleton;
            if (dev == nullptr || !dev->_keyboard[kKeyEscape].pressed) SendTyped();
        }
        Object::InternalPtr<UI::Element> copy = *element;
        UI::DialogInstance::OnElementEvent(&copy, msg, id, c, d, e, userData);
    }

    void Close() {
        Object::InternalPtr<UI::DialogInstance> self(this);
        _ui->CloseDialog(&self);
        if (s_open == this) s_open = nullptr;
    }

    void Refresh() {
        const Status st = GetStatus();
        std::string line;
        if (!st.serverSet)        line = "No server set (neighbour.server in settings.cfg).";
        else if (!st.connected)   line = "Connecting to the neighbour server...";
        else if (!st.joined)      line = "Joining the room \"" + st.room + "\"...";
        else if (st.peer.empty()) line = "In the room \"" + st.room + "\" as " + st.name + ". No neighbour has joined yet.";
        else line = "In the room \"" + st.room + "\" as " + st.name + ". " + st.peer +
                    (st.peerOnline ? " is here." : " is away (parcels wait for them).");
        if (st.unsent > 0) line += " " + std::to_string(st.unsent) + " parcel(s) not yet sent.";
        if (st.waiting > 0) line += " " + std::to_string(st.waiting) + " parcel(s) waiting for room.";
        if (!_note.empty()) line += "  " + _note;
        Text("textStatus", line);

        if (_goods.empty()) {
            Text("textGood", "(nothing in storage)");
            Text("textStock", "");
            Text("textAmount", "-");
        } else {
            const Town::Stock& g = _goods[static_cast<size_t>(_pick)];
            Text("textGood", Sanitize(Town::DisplayName(g.resource), 32));
            Text("textStock", std::to_string(g.available) + " in storage");
            Text("textAmount", std::to_string(Amount()));
        }
        int families = 0;
        const std::vector<Town::Member> fam = Town::PreviewFamily(&families);
        if (fam.empty()) {
            Text("textFamily", families < 2 ? "A family can move once two or more families have homes."
                                            : "No family can move right now.");
        } else {
            int adults = 0, children = 0;
            for (const Town::Member& m : fam) (m.child ? children : adults) += 1;
            std::string t = "A family of " + std::to_string(fam.size()) + " (" + std::to_string(adults) +
                            (adults == 1 ? " adult" : " adults");
            if (children > 0) t += ", " + std::to_string(children) + (children == 1 ? " child" : " children");
            t += ") could move to " + (st.peer.empty() ? std::string("your neighbour") : st.peer) + ".";
            Text("textFamily", t);
        }
    }

private:
    UI::Element* Find(const char* name) { return FindElement(System::String(name)).Get(); }
    void Id(const char* name, int32_t id) {
        if (UI::Element* el = Find(name)) el->SetElementId(id);
        else fprintf(stderr, "[Neighbour] panel: no element %s\n", name);
    }
    void Text(const char* name, const std::string& text) {
        UI::Element* el = Find(name);
        if (::Object::IsDerivedFromNullSafe(el, UI::TextElement::TypeId()))
            static_cast<UI::TextElement*>(el)->SetParameter(0, System::String(text.c_str()));
    }
    UI::EditElement* Edit() {
        UI::Element* el = Find("editChat");
        return ::Object::IsDerivedFromNullSafe(el, UI::EditElement::TypeId()) ? static_cast<UI::EditElement*>(el) : nullptr;
    }

    void ReloadGoods() {
        std::string keep = _goods.empty() ? std::string() : _goods[static_cast<size_t>(_pick)].path;
        _goods = Town::ListGoods();
        _pick = 0;
        for (size_t i = 0; i < _goods.size(); ++i)
            if (_goods[i].path == keep) _pick = static_cast<int>(i);
    }
    void Step(int dir) {
        if (_goods.empty()) return;
        const int n = static_cast<int>(_goods.size());
        _pick = (_pick + dir + n) % n;
    }
    int Amount() const {
        if (_goods.empty()) return 0;
        const int want = kAmounts[_amount];
        const int have = _goods[static_cast<size_t>(_pick)].available;
        return want < have ? want : have;
    }

    void Report(const std::string& why) { _note = why; }

    void SendTyped() {
        UI::EditElement* edit = Edit();
        if (edit == nullptr || edit->GetText()._data == nullptr) return;
        const std::string text = Sanitize(edit->GetText()._data, kMaxChatChars);
        if (text.empty()) return;
        SendChat(text);
        edit->SetText(System::String(""));
        _note.clear();
    }
    void SendSelectedGoods() {
        if (_goods.empty()) { Report("Nothing in storage to send."); return; }
        const Town::Stock g = _goods[static_cast<size_t>(_pick)];
        Report(SendGoods(g.resource, Amount()));
        ReloadGoods();
    }

    std::vector<Town::Stock> _goods;
    int         _pick = 0;
    int         _amount = 3;   // kAmounts[3] = 25
    std::string _note;          // the last action's problem, if any
};

void Open() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_userInterface == nullptr || s_open != nullptr) return;
    const UI::Dialog* rsc = LoadPanel();
    if (rsc == nullptr) return;
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(NeighbourPanel), 8);
    if (mem == nullptr) return;
    NeighbourPanel* panel = ::new (mem) NeighbourPanel(g->m_userInterface, rsc);
    s_open = panel;
    s_framesToRefresh = kRefreshFrames;
    Object::InternalPtr<UI::DialogInstance> ref(panel);
    g->m_userInterface->AddDialog(&ref);   // dispatches OnOpened
}

}  // namespace

void PanelPollHotkey() {
    const ::Input::Device* dev = ::Input::Device::_singleton;
    const ::Input::Configuration* cfg = ::Input::Configuration::_singleton;
    if (dev == nullptr) return;
    const bool typing = cfg != nullptr && cfg->_keyMap != nullptr && cfg->_keyMap->_suspendCount > 0;
    if (!typing && dev->_keyboard[kKeyN].pressed) {
        if (s_open != nullptr) s_open->Close();
        else Open();
        return;
    }
    if (s_open != nullptr && --s_framesToRefresh <= 0) {
        s_framesToRefresh = kRefreshFrames;
        s_open->Refresh();
    }
}

// The game is ending (or a new one starting): the game's UserInterface owns the panel and goes
// with the game, so only forget it here (calling into a UI being torn down is not safe).
void PanelClose() {
    s_open = nullptr;
}

}  // namespace Neighbour

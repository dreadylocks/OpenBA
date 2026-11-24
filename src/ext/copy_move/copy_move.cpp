// ============================================================================
// src/ext/copy_move/copy_move.cpp -- the copy / move tool (copy_move.h)
// ============================================================================
// NOT RETAIL. A Tool subclass of our own (not a reflected type: it answers as Tool, so PlayerToolManager's
// IsA(EditTool) tests see "another tool", which it is). It is installed the way
// PlayerToolManager::SelectTool installs a tool (toolbardialog.cpp, steps 3 and 8) and leaves the same way:
// SelectTool(null) from the manager's right-click arm, or any toolbar button.
// ============================================================================
#include "collection/Collection.h"
#include "input/Device.h"
#include "input/Input.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "system/Value.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Player/EditTool.h"
#include "Player/Tool.h"
#include "Player/exe_bodies.h"
#include "ext/toolbar/toolbar.h"
#include "ext/copy_move/copy_move.h"
#include "ext/copy_move/layout.h"
#include "ext/copy_move/place.h"
#include "ext/copy_move/teleport.h"
#include "ext/copy_move/world.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
#include <set>
#include <vector>

namespace Ext {
namespace CopyMove {

namespace {

// Input::KeyTables indices (kIndexToVirtualKey, input_key_tables.h): what Input::Device::_keyboard holds.
constexpr int kKeyC = 25;        // 'C'
constexpr int kKeyV = 44;        // 'V'
constexpr int kKeyX = 46;        // 'X'
constexpr int kKeyShift = 61;    // VK_SHIFT
constexpr int kKeyLShift = 62;
constexpr int kKeyRShift = 66;
constexpr int kKeyCtrl = 67;     // VK_CONTROL
constexpr int kKeyLCtrl = 69;
constexpr int kKeyRCtrl = 73;

// KeyMap ids (Game/inputbindings.cpp): the player's own bindings.
constexpr int kActionLeft = 5;         // MouseLeft
constexpr int kStructureLeft = 0x13;   // R: PlacementTool turns the building one way...
constexpr int kStructureRight = 0x14;  // T: ...and the other

constexpr int kCursorSelect = 1;   // EditTool's (Tool slot 12)
constexpr int kCursorPlace = 5;    // PlacementTool's

// Overlay colours (ARGB; PlayerTileRender_DrawOverlay_08c0 tints its tile with them).
constexpr uint32_t kColourOk = 0xff00ff00u;        // retail's green
constexpr uint32_t kColourBad = 0xffff0000u;       // retail's red
constexpr uint32_t kColourStays = 0xffffff00u;     // retail's yellow: in the selection, not moved
constexpr uint32_t kColourSelected = 0xff40a0ffu;  // the selection
constexpr uint32_t kColourBox = 0xffffffffu;       // the drag box's edge

enum class Mode { Select,
                  Paste,
                  Move };

// The last copy, kept after the tool closes (Ctrl+V); forgotten with its game.
Layout g_clipboard;
const GameUpdateLoop* g_clipboardGame = nullptr;

bool KeyDown(int index) {
    const ::Input::Device* dev = ::Input::Device::_singleton;
    return dev != nullptr && dev->_keyboard[index].value != 0;
}
bool KeyPressed(int index) {
    const ::Input::Device* dev = ::Input::Device::_singleton;
    return dev != nullptr && dev->_keyboard[index].pressed;
}
bool CtrlDown() {
    return KeyDown(kKeyCtrl) || KeyDown(kKeyLCtrl) || KeyDown(kKeyRCtrl);
}
bool ShiftDown() {
    return KeyDown(kKeyShift) || KeyDown(kKeyLShift) || KeyDown(kKeyRShift);
}
// A text field has the keyboard (the neighbour panel's test, neighbour_panel.cpp).
bool Typing() {
    const ::Input::Configuration* cfg = ::Input::Configuration::_singleton;
    return cfg != nullptr && cfg->_keyMap != nullptr && cfg->_keyMap->_suspendCount > 0;
}
const ::Input::KeyMap* Keys() {
    const ::Input::Configuration* cfg = ::Input::Configuration::_singleton;
    return cfg != nullptr ? cfg->_keyMap : nullptr;
}

Layout& Clipboard() {
    if (g_clipboardGame != GameUpdateLoop::_singleton) {
        g_clipboard.Clear();
        g_clipboardGame = GameUpdateLoop::_singleton;
    }
    return g_clipboard;
}

// What a copy of `e` is: its template, map rect, turn and mesh group.
bool ItemOf(ComponentEntity* e, Item* out) {
    Kind kind;
    Rect rect;
    int degrees = 0;
    if (!KindOf(e->_description, &kind) || !RectOf(e, &rect, &degrees)) {
        return false;
    }
    out->kind = kind;
    out->tmpl = e->_description;
    out->rect = rect;
    out->turn = kind == Kind::Field ? 0 : TurnOfDegrees(degrees);
    ModelComponent* model = e->FindComponent<ModelComponent>();
    out->variant = model != nullptr ? model->meshGroup : 0;
    out->source = e;
    return true;
}

class CopyMoveTool;
CopyMoveTool* g_live = nullptr;   // the tool while it is current (not a reflected type: no IsA for it)

class CopyMoveTool final : public Tool {
public:
    CopyMoveTool() {
        g_live = this;
    }
    ~CopyMoveTool() override {
        if (g_live == this) {
            g_live = nullptr;
        }
    }

    void Startup() override {
        std::fprintf(stderr, "[CopyMove] tool on\n");
    }

    void Shutdown() override {
        EndPlacing();
        ClearSelection();
        std::fprintf(stderr, "[CopyMove] tool off\n");
    }

    char OnKey(char active) override;
    void Update(const System::Time* time, char active) override;
    void Draw(Video::Context* context, char flag) override;

    // Right-click (PlayerToolManager::UpdateHotkeys): out of a paste or a move, then the selection goes,
    // then the tool (0 = the manager drops back to the EditTool).
    char OnCancel() override {
        if (_mode != Mode::Select) {
            EndPlacing();
            return 1;
        }
        if (!_selected.empty() || !_roads.empty()) {
            ClearSelection();
            return 1;
        }
        return 0;
    }

    // PlayerToolManager::OnEntityRemoved asks the current tool about every entity on its way out: ours
    // forgets it (selection, move) and answers no, so the tool stays.
    bool IsSelected(ComponentEntity* const* entityRef) override {
        ComponentEntity* gone = entityRef != nullptr ? *entityRef : nullptr;
        if (gone == nullptr) {
            return false;
        }
        _selected.erase(std::remove_if(_selected.begin(), _selected.end(),
                                       [gone](const EntityRef& r) { return r.entity == gone; }),
                        _selected.end());
        auto isGone = [gone](const EntityRef& r) { return r.entity == gone; };
        _boxPreview.erase(std::remove_if(_boxPreview.begin(), _boxPreview.end(), isGone), _boxPreview.end());
        _stays.erase(std::remove_if(_stays.begin(), _stays.end(), isGone), _stays.end());
        if (_mode == Mode::Move && _moving.entities.count(gone) != 0) {
            std::fprintf(stderr, "[CopyMove] a building being moved was removed: the move is cancelled\n");
            EndPlacing();
        }
        return false;
    }

    int GetCursor() const override {
        return _mode == Mode::Select ? kCursorSelect : kCursorPlace;
    }

    void SelectOnly(ComponentEntity* e) {
        ClearSelection();
        AddEntity(e);
    }

    // Ctrl+C / Ctrl+X: the selection into hand; Ctrl+V: the clipboard.
    void Copy();
    void Cut();
    void PasteClipboard();

private:
    void AddEntity(ComponentEntity* e);
    void RemoveEntity(ComponentEntity* e);
    bool Holds(ComponentEntity* e) const;
    void ClearSelection();
    void ClearBoxPreview();
    void DropDead();
    void ClickAt(int cell);
    void BoxSelect(const Rect& box, bool additive);
    void BeginPlacing(Mode mode, const Layout& layout);
    void EndPlacing();
    void Commit();

    Mode _mode = Mode::Select;
    std::vector<EntityRef> _selected;
    std::set<int> _roads;

    bool _dragging = false;
    Cell _dragStart;
    Cell _dragEnd;
    // Every entity list below is of EntityRefs taken while the entity was alive: a list is read through
    // Alive (world.h), which never touches an entity it cannot find in the master array.
    std::vector<EntityRef> _boxPreview;           // outlined while the box is dragged

    Layout _hand;                                 // what follows the cursor (a copy, or what moves)
    std::vector<std::unique_ptr<Placer>> _placers;
    std::vector<EntityRef> _stays;                // a move's selected things that do not move
    MoveSet _moving;
    bool _onMap = false;
    Cell _origin;
    Cell _lastOrigin;
    std::vector<bool> _fits;
    std::vector<Tile> _tiles;                     // this frame's footprint cells, in item order
};

void CopyMoveTool::AddEntity(ComponentEntity* e) {
    if (e == nullptr || Holds(e)) {
        return;
    }
    _selected.push_back(Remember(e));
    SetOutline(e, true);
}

void CopyMoveTool::RemoveEntity(ComponentEntity* e) {
    for (size_t i = 0; i < _selected.size(); ++i) {
        if (_selected[i].entity == e) {
            if (Alive(_selected[i])) {
                SetOutline(e, false);
            }
            _selected.erase(_selected.begin() + static_cast<long>(i));
            return;
        }
    }
}

bool CopyMoveTool::Holds(ComponentEntity* e) const {
    for (const EntityRef& r : _selected) {
        if (r.entity == e) {
            return true;
        }
    }
    return false;
}

void CopyMoveTool::ClearSelection() {
    for (EntityRef& r : _selected) {
        if (Alive(r)) {
            SetOutline(r.entity, false);
        }
    }
    _selected.clear();
    _roads.clear();
    ClearBoxPreview();
}

void CopyMoveTool::ClearBoxPreview() {
    for (EntityRef& r : _boxPreview) {
        if (Alive(r) && !Holds(r.entity)) {
            SetOutline(r.entity, false);
        }
    }
    _boxPreview.clear();
}

void CopyMoveTool::DropDead() {
    _selected.erase(std::remove_if(_selected.begin(), _selected.end(), [](EntityRef& r) { return !Alive(r); }),
                    _selected.end());
    for (auto it = _roads.begin(); it != _roads.end();) {
        if (!HasRoad(*it)) {
            it = _roads.erase(it);
        } else {
            ++it;
        }
    }
}

void CopyMoveTool::ClickAt(int cell) {
    ComponentEntity* e = EntityUnderCursor(cell);
    if (e != nullptr) {
        if (Holds(e)) {
            RemoveEntity(e);
        } else {
            AddEntity(e);
        }
        return;
    }
    if (cell >= 0 && HasRoad(cell)) {
        if (_roads.erase(cell) == 0) {
            _roads.insert(cell);
        }
    }
}

void CopyMoveTool::BoxSelect(const Rect& box, bool additive) {
    if (!additive) {
        ClearSelection();
    }
    for (ComponentEntity* e : EntitiesIn(box)) {
        AddEntity(e);
    }
    Map* map = TheMap();
    for (int row = box.row; row < box.row + box.h; ++row) {
        for (int col = box.col; col < box.col + box.w; ++col) {
            const int cell = row * map->size + col;
            if (HasRoad(cell)) {
                _roads.insert(cell);
            }
        }
    }
    std::fprintf(stderr, "[CopyMove] box %dx%d at (%d,%d): %zu thing(s), %zu road cell(s) selected\n", box.w,
                 box.h,
                 box.col, box.row, _selected.size(), _roads.size());
}

void CopyMoveTool::Copy() {
    DropDead();
    Layout l;
    for (EntityRef& r : _selected) {
        Item it;
        if (ItemOf(r.entity, &it)) {
            it.source = nullptr;   // a copy keeps no link to what it was copied from
            l.Add(it);
        }
    }
    Map* map = TheMap();
    for (int cell : _roads) {
        Item it;
        it.kind = Kind::Road;
        it.rect = { cell % map->size, cell / map->size, 1, 1 };
        it.variant = RoadType(cell);
        l.Add(it);
    }
    l.End();
    if (l.Empty()) {
        DragTool_PlayErrorSound_100140(this);
        return;
    }
    Clipboard() = l;
    std::fprintf(stderr, "[CopyMove] copied %zu thing(s) (%d x %d cells)\n", l.Items().size(), l.BoxW(), l.BoxH());
    BeginPlacing(Mode::Paste, l);
}

void CopyMoveTool::Cut() {
    DropDead();
    Layout l;
    std::vector<EntityRef> stays;
    for (EntityRef& r : _selected) {
        Item it;
        if (CanTeleport(r.entity) && ItemOf(r.entity, &it)) {
            l.Add(it);
        } else {
            stays.push_back(r);
        }
    }
    Map* map = TheMap();
    for (int cell : _roads) {
        Item it;
        it.kind = Kind::Road;
        it.rect = { cell % map->size, cell / map->size, 1, 1 };
        it.variant = RoadType(cell);
        it.sourceCell = cell;
        l.Add(it);
    }
    l.End();
    if (l.Empty()) {
        std::fprintf(stderr, "[CopyMove] nothing selected can be moved (%zu thing(s) stay)\n", stays.size());
        DragTool_PlayErrorSound_100140(this);
        return;
    }
    if (!stays.empty()) {
        std::fprintf(stderr,
                     "[CopyMove] %zu selected thing(s) cannot move yet (unfinished, or a field, pasture "
                     "or orchard) and stay\n",
                     stays.size());
    }
    BeginPlacing(Mode::Move, l);
    _stays = stays;   // after BeginPlacing, whose EndPlacing empties the list
}

void CopyMoveTool::PasteClipboard() {
    const Layout& l = Clipboard();
    if (l.Empty()) {
        DragTool_PlayErrorSound_100140(this);
        return;
    }
    BeginPlacing(Mode::Paste, l);
}

void CopyMoveTool::BeginPlacing(Mode mode, const Layout& layout) {
    EndPlacing();
    _mode = mode;
    _hand = layout;
    _moving = MoveSet();
    for (const Item& it : _hand.Items()) {
        _placers.push_back(std::make_unique<Placer>(it, dialogPtr));
        if (mode == Mode::Move) {
            if (it.source != nullptr) {
                _moving.entities.insert(static_cast<const ComponentEntity*>(it.source));
            }
            if (it.sourceCell >= 0) {
                _moving.roadCells.insert(it.sourceCell);
            }
        }
    }
    _fits.assign(_hand.Items().size(), false);
    _onMap = false;
    _lastOrigin = Cell{ -1, -1 };
}

void CopyMoveTool::EndPlacing() {
    for (const std::unique_ptr<Placer>& p : _placers) {
        p->Ghost(Item(), Rect(), false);
    }
    _placers.clear();
    _hand.Clear();
    _moving = MoveSet();
    _stays.clear();
    _fits.clear();
    _tiles.clear();
    _onMap = false;
    _mode = Mode::Select;
}

void CopyMoveTool::Commit() {
    if (!_onMap) {
        return;
    }
    if (_mode == Mode::Paste) {
        int placed = 0;
        const std::vector<Item>& items = _hand.Items();
        // Buildings and areas first, then roads (a road never goes under a copied site's walls).
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < items.size(); ++i) {
                const bool road = items[i].kind == Kind::Road;
                if ((pass == 0) == road) {
                    continue;
                }
                const Rect r = Layout::Placed(items[i], _origin);
                if (!_fits[i]) {
                    std::fprintf(stderr, "[CopyMove]   item %zu (kind %d) at (%d,%d) %dx%d: blocked, skipped\n", i,
                                 static_cast<int>(items[i].kind), r.col, r.row, r.w, r.h);
                } else if (_placers[i]->Paste(items[i], r)) {
                    ++placed;
                } else {
                    std::fprintf(stderr,
                                 "[CopyMove]   item %zu (kind %d) at (%d,%d) %dx%d: the retail tool refused it\n",
                                 i, static_cast<int>(items[i].kind), r.col, r.row, r.w, r.h);
                }
            }
        }
        std::fprintf(stderr, "[CopyMove] pasted %d of %zu at (%d,%d)\n", placed, items.size(), _origin.col,
                     _origin.row);
        if (placed > 0) {
            DragTool_PlayPlaceSound_1000f0(this);
        } else {
            DragTool_PlayErrorSound_100140(this);
        }
        return;
    }
    // A move: all or nothing.
    for (size_t i = 0; i < _fits.size(); ++i) {
        if (!_fits[i]) {
            std::fprintf(stderr, "[CopyMove] move refused: item %zu does not fit at (%d,%d)\n", i, _origin.col,
                         _origin.row);
            DragTool_PlayErrorSound_100140(this);
            return;
        }
    }
    std::vector<ComponentEntity*> moved;
    const Layout hand = _hand;
    const Cell origin = _origin;
    // The selection's outlines come off first; the moved things are selected again where they now are, or,
    // when the move was refused (Teleport put everything back), the selection is what it was.
    std::vector<EntityRef> before = _selected;
    const std::set<int> roadsBefore = _roads;
    ClearSelection();
    const bool ok = Teleport(hand, origin, _placers, &moved);
    std::vector<int> roads;
    if (ok) {
        Map* map = TheMap();
        for (const Item& it : hand.Items()) {
            if (it.kind == Kind::Road) {
                const Rect r = Layout::Placed(it, origin);
                roads.push_back(r.row * map->size + r.col);
            }
        }
    }
    EndPlacing();
    if (ok) {
        for (ComponentEntity* e : moved) {
            AddEntity(e);
        }
        for (int cell : roads) {
            _roads.insert(cell);
        }
    } else {
        for (EntityRef& r : before) {
            if (Alive(r)) {
                AddEntity(r.entity);
            }
        }
        _roads = roadsBefore;
    }
    if (ok) {
        DragTool_PlayPlaceSound_1000f0(this);
    } else {
        DragTool_PlayErrorSound_100140(this);
    }
}

char CopyMoveTool::OnKey(char active) {
    const ::Input::KeyMap* keys = Keys();
    if (keys == nullptr) {
        return 1;
    }
    // Ctrl + C / X / V are ours: the manager skips its camera views this frame (return 0).
    if (!Typing() && CtrlDown()) {
        if (KeyPressed(kKeyC)) {
            if (_mode == Mode::Select) {
                Copy();
            }
            return 0;
        }
        if (KeyPressed(kKeyX)) {
            if (_mode == Mode::Select) {
                Cut();
            }
            return 0;
        }
        if (KeyPressed(kKeyV)) {
            if (_mode != Mode::Move) {
                PasteClipboard();
            }
            return 0;
        }
    }
    if (_mode == Mode::Select) {
        if (_dragging && keys->Released(0, kActionLeft)) {
            _dragging = false;
            ClearBoxPreview();
            if (_dragStart.col == _dragEnd.col && _dragStart.row == _dragEnd.row) {
                Map* map = TheMap();
                ClickAt(_dragStart.row * map->size + _dragStart.col);
            } else {
                BoxSelect(DragRect(_dragStart, _dragEnd), ShiftDown());
            }
        } else if (active && !_dragging && keys->Pressed(0, kActionLeft)) {
            const int cell = CursorCell();
            Map* map = TheMap();
            if (cell >= 0 && map != nullptr) {
                _dragging = true;
                _dragStart = CellOf(cell, map->size);
                _dragEnd = _dragStart;
            }
        }
        return 1;
    }
    // Pasting or moving.
    if (keys->Pressed(0, kStructureLeft)) {
        _hand.Turn(-1);
    } else if (keys->Pressed(0, kStructureRight)) {
        _hand.Turn(+1);
    }
    if (active && keys->Pressed(0, kActionLeft)) {
        Commit();
    }
    return 1;
}

void CopyMoveTool::Update(const System::Time* /*time*/, char active) {
    Map* map = TheMap();
    if (map == nullptr) {
        return;
    }
    if (_mode == Mode::Select) {
        if (_dragging) {
            const int cell = CursorCell();
            if (cell >= 0) {
                const Cell end = CellOf(cell, map->size);
                if (end.col != _dragEnd.col || end.row != _dragEnd.row) {
                    _dragEnd = end;
                    dialogPtr->PlayTickSound();
                    // What the box would take, outlined as it is dragged.
                    std::vector<ComponentEntity*> now = EntitiesIn(DragRect(_dragStart, _dragEnd));
                    for (EntityRef& r : _boxPreview) {
                        if (std::find(now.begin(), now.end(), r.entity) == now.end() && Alive(r) &&
                            !Holds(r.entity)) {
                            SetOutline(r.entity, false);
                        }
                    }
                    _boxPreview.clear();
                    for (ComponentEntity* e : now) {
                        SetOutline(e, true);
                        _boxPreview.push_back(Remember(e));
                    }
                }
            }
        }
        return;
    }
    const int cell = active ? CursorCell() : -1;
    _onMap = cell >= 0;
    _tiles.clear();
    const std::vector<Item>& items = _hand.Items();
    if (!_onMap) {
        for (const std::unique_ptr<Placer>& p : _placers) {
            p->Ghost(Item(), Rect(), false);
        }
        return;
    }
    _origin = _hand.OriginFor(CellOf(cell, map->size));
    if (_origin.col != _lastOrigin.col || _origin.row != _lastOrigin.row) {
        _lastOrigin = _origin;
        dialogPtr->PlayTickSound();
    }
    const MoveSet* moving = _mode == Mode::Move ? &_moving : nullptr;
    for (size_t i = 0; i < items.size(); ++i) {
        const Rect r = Layout::Placed(items[i], _origin);
        _fits[i] = _placers[i]->Check(items[i], r, moving, &_tiles);
        _placers[i]->Ghost(items[i], r, true);
    }
}

void CopyMoveTool::Draw(Video::Context* context, char flag) {
    if (flag != 0) {
        return;   // the UI has the mouse, or pauses the game
    }
    Map* map = TheMap();
    if (map == nullptr) {
        return;
    }
    Collection::Array<ToolTileRecord> records;
    auto add = [&records](int col, int row, uint32_t colour) {
        ToolTileRecord rec;
        rec.col = col;
        rec.row = row;
        rec.variant = 0;
        rec.color = colour;
        rec.angle = 0;
        records.SetSize(records._size + 1, rec);
    };
    auto addRect = [&add](const Rect& r, uint32_t colour) {
        for (int row = r.row; row < r.row + r.h; ++row) {
            for (int col = r.col; col < r.col + r.w; ++col) {
                add(col, row, colour);
            }
        }
    };
    if (_mode == Mode::Select) {
        for (EntityRef& r : _selected) {
            Rect rect;
            if (Alive(r) && RectOf(r.entity, &rect, nullptr)) {
                addRect(rect, kColourSelected);
            }
        }
        for (int cell : _roads) {
            add(cell % map->size, cell / map->size, kColourSelected);
        }
        if (_dragging) {
            // The box's edge only: a field-sized box would be thousands of tiles.
            const Rect box = DragRect(_dragStart, _dragEnd);
            for (int col = box.col; col < box.col + box.w; ++col) {
                add(col, box.row, kColourBox);
                if (box.h > 1) {
                    add(col, box.row + box.h - 1, kColourBox);
                }
            }
            for (int row = box.row + 1; row < box.row + box.h - 1; ++row) {
                add(box.col, row, kColourBox);
                if (box.w > 1) {
                    add(box.col + box.w - 1, row, kColourBox);
                }
            }
        }
    } else {
        for (EntityRef& r : _stays) {
            Rect rect;
            if (Alive(r) && RectOf(r.entity, &rect, nullptr)) {
                addRect(rect, kColourStays);
            }
        }
        if (_onMap) {
            for (const Tile& t : _tiles) {
                add(t.col, t.row, t.ok ? kColourOk : kColourBad);
            }
        }
    }
    if (records._size == 0) {
        return;
    }
    Collection::Array<ToolSegmentRecord> none;
    PlayerTileRender_DrawOverlay_08c0(nullptr, context, &records, &none);
}

// The tool, made current the way PlayerToolManager::SelectTool does it (toolbardialog.cpp steps 3 and 8).
// Null when the manager is locked by a tutorial step (+0x80), as SelectTool would refuse another tool.
CopyMoveTool* Start(PlayerToolManager* manager) {
    if (manager == nullptr || manager->field_0x80) {
        return nullptr;
    }
    if (manager->currentTool != nullptr) {
        manager->currentTool->Shutdown();
        delete manager->currentTool.Get();
        manager->currentTool = nullptr;
    }
    manager->field_0x20 = nullptr;   // no toolbar group is active
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(CopyMoveTool), 8);   // Base's operator delete frees it
    if (mem == nullptr) {
        return nullptr;
    }
    CopyMoveTool* tool = ::new (mem) CopyMoveTool();
    tool->dialogPtr = manager;
    tool->descriptionPtr = nullptr;
    tool->toolSetting = nullptr;
    manager->currentTool = tool;
    tool->Startup();
    return tool;
}

CopyMoveTool* Current(PlayerToolManager* manager) {
    if (manager == nullptr || manager->currentTool == nullptr) {
        return nullptr;
    }
    return manager->currentTool.Get() == g_live ? g_live : nullptr;
}

PlayerToolManager* Manager() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_toolManager : nullptr;
}

bool ButtonOffered() {
    return GameUpdateLoop::_singleton != nullptr;
}

void ButtonClicked() {
    PlayerToolManager* manager = Manager();
    if (manager == nullptr) {
        return;
    }
    if (Current(manager) != nullptr) {
        manager->SelectTool(nullptr);   // a second click puts the tool away
        return;
    }
    Start(manager);
}

}  // namespace

bool PollHotkeys(PlayerToolManager* manager, bool inputFlag) {
    if (!inputFlag || manager == nullptr || Typing() || !CtrlDown() || Current(manager) != nullptr) {
        return false;
    }
    const bool c = KeyPressed(kKeyC);
    const bool x = KeyPressed(kKeyX);
    const bool v = KeyPressed(kKeyV);
    if (!c && !x && !v) {
        return false;
    }
    // The entity whose info panel is open goes straight into hand.
    ComponentEntity* shown = nullptr;
    if (manager->currentTool != nullptr && manager->currentTool->IsA(EditTool::TypeId())) {
        shown = static_cast<EditTool*>(manager->currentTool.Get())->lastEntityRef;
        Kind kind;
        if (shown != nullptr && !KindOf(shown->_description, &kind)) {
            shown = nullptr;
        }
        shown = shown != nullptr ? MasterOf(shown) : nullptr;   // a fence piece stands for its pasture
    }
    CopyMoveTool* tool = Start(manager);
    if (tool == nullptr) {
        return false;
    }
    if (v) {
        tool->PasteClipboard();
    } else if (shown != nullptr) {
        tool->SelectOnly(shown);
        if (c) {
            tool->Copy();
        } else {
            tool->Cut();
        }
    }
    return true;
}

}  // namespace CopyMove

namespace Toolbar {

const ToolButton& CopyMoveButton() {
    // The main bar, at its end (after every retail item: none has a higher priority).
    static const ToolButton kButton = {
        "copyMove",
        "",
        0x7fffffff,
        "CopyMoveTip",
        "ToolbarCopyMove",
        &CopyMove::ButtonOffered,
        &CopyMove::ButtonClicked,
    };
    return kButton;
}

}  // namespace Toolbar
}  // namespace Ext

// ============================================================================
// src/ext/ferries/test_docks.cpp -- ext.ferries' test aid (test_docks.h)
// ============================================================================
// NOT RETAIL. Sites are tried with the game's own footprint test
// (Map::FootprintValidity, as PlacementTool::CreateAt calls it) and placed with
// CreateAt, so what it builds a player could build.
// ============================================================================
#include "ext/ferries/test_docks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "Component/CitizenComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/MapDescription.h"
#include "Component/ModelComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Player/EditTool.h"
#include "Player/PlacementTool.h"
#include "Player/edittool_funcs.h"
#include "ext/ferries/ferries.h"
#include "ext/ferries/lanes.h"
#include "io/Persistence.h"
#include "math/Math.h"
#include "object/External.h"
#include "system/Filename.h"

namespace Ext {
namespace Ferries {
namespace TestDocks {

namespace {

constexpr int kPlaceFrame  = 90;   // a little after the start town settles
constexpr int kPanelDelay  = 120;  // then the near dock's panel is opened
constexpr int kSearch      = 70;   // dock sites within this many tiles of the town
constexpr int kHouseSearch = 30;   // the house within this many tiles of the far dock
constexpr const char* kHouse = "Template\\WoodHouse.rsc";

bool s_armed = false;
int s_frames = 0;
int s_panelAt = -1;   // the frame the near dock's panel is opened (a player's click on it)

std::string Setting(const char* key) {
    const ::IO::Persistence* p = ::IO::Persistence::_singleton;
    if (p == nullptr) return std::string();
    System::String k(key);
    const RtIo::SettingsNode* node = p->_settingsMap.Find(k._data);
    if (node == nullptr || node->element.value._data == nullptr) return std::string();
    return node->element.value._data;
}

// A tool manager with no placement constraint (+0x80 clear), as the tool harness has.
PlayerToolManager* Owner() {
    alignas(PlayerToolManager) static uint8_t s_storage[sizeof(PlayerToolManager)];
    static PlayerToolManager* const s_owner = ::new (static_cast<void*>(s_storage)) PlayerToolManager();
    return s_owner;
}

PlacementTool* ToolFor(ComponentDescription* tmpl) {
    PlacementTool* tool = PlacementTool_Factory_SA();
    if (tool == nullptr) return nullptr;
    tool->descriptionPtr = tmpl;
    tool->dialogPtr = Owner();
    tool->Startup();
    if (tool->descComponent == nullptr || tool->mapComponent == nullptr) return nullptr;
    return tool;
}

struct Site {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the footprint, both corners included
    int orient = 0;
    uint16_t zone = 0;
    Math::Vector3 pos;
};

// The footprint centred on (col, row) at an orientation, as GetPlacement lays it out (the
// width and height swap at 90 / 270), and the world centre CreateAt is given.
bool Layout(Map* map, const CreatePlacedDescription* cpd, int col, int row, int orient, Site* s) {
    int w = cpd->_i44, h = cpd->_i48;
    if (orient == 90 || orient == 270) std::swap(w, h);
    const int n = map->size;
    s->x0 = (w & 1) == 0 ? col - w / 2 + 1 : col - (w - 1) / 2;
    s->y0 = (h & 1) == 0 ? row - h / 2 + 1 : row - (h - 1) / 2;
    s->x1 = s->x0 + w - 1;
    s->y1 = s->y0 + h - 1;
    s->orient = orient;
    if (s->x0 < 1 || s->y0 < 1 || s->x1 >= n - 1 || s->y1 >= n - 1) return false;
    const float off = static_cast<float>(n >> 1) - 0.5f;
    const int a = s->y0 * n + s->x0, b = s->y1 * n + s->x1;
    float inA[3] = { static_cast<float>(a % n) - off, static_cast<float>(a / n) - off, 0.0f };
    float inB[3] = { static_cast<float>(b % n) - off, static_cast<float>(b / n) - off, 0.0f };
    float wa[3] = { 0, 0, 0 }, wb[3] = { 0, 0, 0 };
    map->GetTerrainPos(wa, inA, '\0', nullptr);
    map->GetTerrainPos(wb, inB, '\0', nullptr);
    s->pos = Math::Vector3((wa[0] + wb[0]) * 0.5f, (wa[1] + wb[1]) * 0.5f, 0.0f);
    return true;
}

bool Valid(Map* map, PlacementTool* tool, const Site& s) {
    const CreatePlacedDescription* cpd = tool->descComponent;
    const uint64_t ok = map->FootprintValidity(s.x0, s.y0, s.x1 - s.x0 + 1, s.y1 - s.y0 + 1, cpd->_category, cpd,
                                               &cpd->_name, 0.0f, 1000.0f, 0.0f, '\0',
                                               &tool->mapComponent->_footprint, s.orient, cpd->_created);
    return static_cast<char>(ok) != '\0';
}

// The land zone a site stands in: the most common zone of the walkable cells in and round it.
uint16_t ZoneOf(Map* map, const Site& s) {
    std::map<uint16_t, int> count;
    const int n = map->size;
    const MapCell* c = map->cells.GetData();
    for (int y = s.y0 - 1; y <= s.y1 + 1; ++y)
        for (int x = s.x0 - 1; x <= s.x1 + 1; ++x) {
            if (x < 0 || y < 0 || x >= n || y >= n) continue;
            const MapCell& cell = c[y * n + x];
            if (cell.zone != 0 && IsWalkable(cell.terrainType)) ++count[cell.zone];
        }
    uint16_t best = 0;
    int most = 0;
    for (const auto& kv : count)
        if (kv.second > most) {
            most = kv.second;
            best = kv.first;
        }
    return best;
}

DockSite AsDock(Map* map, const Site& s, bool built) {
    DockSite d;
    d.built = built;
    for (int y = s.y0; y <= s.y1; ++y)
        for (int x = s.x0; x <= s.x1; ++x) d.cells.push_back(y * map->size + x);
    return d;
}

// `finished`: inside retail's instant-build bracket (GameUpdateLoop +0xd0 bit 0 around the
// RegisterEntity CreateAt makes, as the start town's founding buildings are made), so the
// building completes at once instead of waiting for the builders.
bool Create(PlacementTool* tool, const Site& s, bool finished) {
    uint32_t* const flags = GameUpdateLoop::_singleton != nullptr ? &GameUpdateLoop::_singleton->m_gameFlags : nullptr;
    if (finished && flags != nullptr) *flags |= 1u;
    const uint64_t placed = tool->CreateAt(s.x0, s.y0, s.x1, s.y1, &s.pos, static_cast<float>(s.orient));
    if (finished && flags != nullptr) *flags &= ~1u;
    return static_cast<char>(placed) != '\0';
}

// The town: where its citizens are on average, and the zone most of them stand in.
bool TownCell(GameUpdateLoop* g, int* col, int* row, uint16_t* zone) {
    CitizenBehavior* town = g->m_citizenList;
    Map* map = g->m_game;
    if (town == nullptr || map == nullptr || town->citizens._size <= 0) return false;
    const int n = map->size, half = n >> 1;
    double sx = 0, sy = 0;
    int k = 0;
    std::map<uint16_t, int> zones;
    for (int i = 0; i < town->citizens._size; ++i) {
        CitizenComponent* c = town->citizens._data[i].Get();
        ComponentEntity* e = c != nullptr ? c->_entity.Get() : nullptr;
        const ModelComponent* m = e != nullptr ? e->FindComponent<ModelComponent>() : nullptr;
        if (m == nullptr) continue;
        sx += m->transform.translation.x;
        sy += m->transform.translation.y;
        ++k;
        const int cx = static_cast<int>(m->transform.translation.x + static_cast<float>(half));
        const int cy = static_cast<int>(m->transform.translation.y + static_cast<float>(half));
        if (cx >= 0 && cy >= 0 && cx < n && cy < n) ++zones[map->cells.GetData()[cy * n + cx].zone];
    }
    if (k == 0) return false;
    *col = static_cast<int>(std::floor(sx / k)) + half;
    *row = static_cast<int>(std::floor(sy / k)) + half;
    *zone = 0;
    int most = 0;
    for (const auto& kv : zones)
        if (kv.first != 0 && kv.second > most) {
            most = kv.second;
            *zone = kv.first;
        }
    return true;
}

void Place() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = g != nullptr ? g->m_game : nullptr;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (map == nullptr || f == nullptr) return;
    ComponentDescription* dockT = f->FindOrLoadAs<ComponentDescription>(System::Filename(kDockTemplate));
    ComponentDescription* houseT = f->FindOrLoadAs<ComponentDescription>(System::Filename(kHouse));
    PlacementTool* dockTool = dockT != nullptr ? ToolFor(dockT) : nullptr;
    PlacementTool* houseTool = houseT != nullptr ? ToolFor(houseT) : nullptr;
    map->FillZones();   // the zones as a path request would see them
    int tc = 0, tr = 0;
    uint16_t townZone = 0;
    if (dockTool == nullptr || houseTool == nullptr || !TownCell(g, &tc, &tr, &townZone)) {
        std::fprintf(stderr, "[Ferries] test docks: no tools or no town, nothing placed\n");
        return;
    }
    const int n = map->size;

    // Every valid dock site near the town, nearest first.
    std::vector<Site> sites;
    for (int row = tr - kSearch; row <= tr + kSearch; ++row)
        for (int col = tc - kSearch; col <= tc + kSearch; ++col)
            for (int orient = 0; orient < 360; orient += 90) {
                Site s;
                if (!Layout(map, dockTool->descComponent, col, row, orient, &s) || !Valid(map, dockTool, s)) continue;
                s.zone = ZoneOf(map, s);
                if (s.zone != 0) sites.push_back(s);
            }
    auto dist2 = [](const Site& s, int col, int row) {
        const int dx = (s.x0 + s.x1) / 2 - col, dy = (s.y0 + s.y1) / 2 - row;
        return dx * dx + dy * dy;
    };
    std::stable_sort(sites.begin(), sites.end(),
                     [&](const Site& a, const Site& b) { return dist2(a, tc, tr) < dist2(b, tc, tr); });

    std::vector<uint8_t> type(static_cast<size_t>(n) * n);
    std::vector<uint16_t> zone(type.size());
    for (size_t i = 0; i < type.size(); ++i) {
        type[i] = map->cells.GetData()[i].terrainType;
        zone[i] = map->cells.GetData()[i].zone;
    }
    const MapView view{ n, type.data(), zone.data() };

    // The near dock: the first site in the town's zone that links to a site in another zone.
    int tries = 0;
    for (const Site& a : sites) {
        if (a.zone != townZone) continue;
        if (++tries > 40) break;
        std::vector<DockSite> docks{ AsDock(map, a, true) };
        std::vector<const Site*> far;
        std::map<int, bool> bucket;   // one far site per 4x4 tiles keeps the search small
        for (const Site& b : sites) {
            if (b.zone == a.zone) continue;
            const int key = ((b.y0 / 4) * 1024) + b.x0 / 4;
            if (bucket.count(key) != 0) continue;
            bucket[key] = true;
            docks.push_back(AsDock(map, b, false));
            far.push_back(&b);
        }
        Link link;
        if (far.empty() || !LinkFrom(view, docks, 0, &link)) continue;
        const Site& b = *far[static_cast<size_t>(link.dockB - 1)];
        const bool madeA = Create(dockTool, a, true);
        const bool madeB = madeA && Create(dockTool, b, true);
        std::fprintf(stderr,
                     "[Ferries] test docks: near dock at (%d,%d)-(%d,%d) rot %d %s, far dock at (%d,%d)-(%d,%d) rot %d "
                     "%s, lane %zu cells; town at (%d,%d)\n",
                     a.x0, a.y0, a.x1, a.y1, a.orient, madeA ? "placed" : "REFUSED", b.x0, b.y0, b.x1, b.y1, b.orient,
                     madeB ? "placed" : "REFUSED", link.lane.size(), tc, tr);
        if (!madeB) return;
        // A house beside the far dock, in its zone: the builders have it to build too.
        const int bc = (b.x0 + b.x1) / 2, br = (b.y0 + b.y1) / 2;
        for (int r = 6; r <= kHouseSearch; ++r)
            for (int row = br - r; row <= br + r; ++row)
                for (int col = bc - r; col <= bc + r; ++col) {
                    if (row != br - r && row != br + r && col != bc - r && col != bc + r) continue;
                    Site h;
                    if (!Layout(map, houseTool->descComponent, col, row, 0, &h) || !Valid(map, houseTool, h)) continue;
                    if (ZoneOf(map, h) != b.zone) continue;
                    if (Create(houseTool, h, false)) {
                        std::fprintf(stderr, "[Ferries] test docks: a house across the water at (%d,%d)-(%d,%d)\n",
                                     h.x0, h.y0, h.x1, h.y1);
                        return;
                    }
                }
        std::fprintf(stderr, "[Ferries] test docks: no house site across the water\n");
        return;
    }
    std::fprintf(stderr, "[Ferries] test docks: %zu dock site(s) near the town, no pair across water found\n",
                 sites.size());
}

// A click on the dock: the edit tool selects it and opens its panel, as the tool harness does.
void OpenPanel() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    ComponentEntity* dock = FirstDock();
    EditTool* tool = g != nullptr && g->m_toolManager != nullptr ? EditTool_HarnessAcquire(g->m_toolManager) : nullptr;
    const int opened = tool != nullptr && dock != nullptr ? EditTool_HarnessSelectEntity(tool, dock) : 0;
    std::fprintf(stderr, "[Ferries] test docks: the Ferry Dock's panel %s\n", opened ? "opened" : "did NOT open");
}

}  // namespace

void SelectForGame(bool townHasDocks) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    s_armed = g != nullptr && g->m_savedState == nullptr && !townHasDocks && Setting("ferries.testdocks") == "1";
    s_frames = 0;
    s_panelAt = -1;
    if (s_armed) std::fprintf(stderr, "[Ferries] test docks: armed (ferries.testdocks=1)\n");
}

void BeginFrame() {
    ++s_frames;
    if (s_armed && s_frames >= kPlaceFrame) {
        s_armed = false;
        Place();
        s_panelAt = s_frames + kPanelDelay;
    }
    if (s_panelAt >= 0 && s_frames >= s_panelAt) {
        s_panelAt = -1;
        OpenPanel();
    }
}

}  // namespace TestDocks
}  // namespace Ferries
}  // namespace Ext

// ============================================================================
// src/ext/ferries/ferries.cpp -- ext.ferries' game glue (docs/fork/FERRIES.md)
// ============================================================================
// NOT RETAIL. See ferries.h. The pure part (which docks are linked, the lanes)
// is lanes.cpp; this file reads the game's map and docks, writes the lanes into
// the path worker's copy, joins the zones, links the regions and draws the
// riders' boats.
// ============================================================================
#include "ext/ferries/ferries.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "Action/PathSearch.h"
#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/Description.h"
#include "Component/FishingDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ModelDescription.h"
#include "Component/RadiusDescription.h"
#include "Component/ToolbarDescription.h"
#include "Component/WorkDescription.h"
#include "Component/WorkPlaceDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PathfinderThread.h"
#include "Game/Toolbar.h"
#include "ext/ferries/lanes.h"
#include "ext/ferries/test_docks.h"
#include "ext/registry.h"
#include "ext/resources/Resources.h"
#include "graphics/Mesh.h"
#include "graphics/Scene.h"
#include "math/Math.h"
#include "object/External.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "ui/SpriteSheet.h"
#include "ui/StringTable.h"

namespace Ext {
namespace Ferries {

bool g_riding = false;

namespace {

constexpr const char* kFishermansDock = "Template\\FishermansDock.rsc";
constexpr const char* kWell           = "Template\\Well.rsc";
constexpr const char* kTransportGroup = "Game\\Toolbar.rsc:transport";
constexpr const char* kStringTable    = "Dialog\\ExtensionsStringTable.rsc";
constexpr const char* kTraderBoat     = "Template\\TraderBoat.rsc";
constexpr const char* kBoatMesh       = "Models\\Buildings\\TradingPost\\BoatMesh.rsc";
constexpr int32_t     kButtonPriority = 500;    // after the Bridge (300) and the Tunnel (400)
constexpr int         kScanFrames     = 30;     // the docks are looked at twice a second
constexpr int         kBoatLinger     = 12;     // frames a boat stays after its rider's last note
constexpr float       kBoatScale      = 0.32f;  // the trader's boat, rowboat size
constexpr float       kBoatYaw        = 90.0f;  // the boat model's bow against a citizen's forward
constexpr float       kWaterZ         = -0.75f; // the water's drawn height (FERRIES.md §8) if the boat says none

// ---- per game ----------------------------------------------------------------------------

struct Dock {
    ComponentEntity* entity = nullptr;   // a key and a reader during the scan only
    int x = 0, y = 0, w = 0, h = 0;      // the footprint (MapComponent)
    bool built = false;
};

struct Boat {
    Graphics::MeshInstance* mesh = nullptr;
    int32_t sceneId = -1;
    int lastFrame = 0;
};

struct RideNote {
    const void* key = nullptr;   // the rider's CharacterComponent, never dereferenced here
    float x = 0, y = 0, z = 0, yaw = 0;
};

const void* s_owner = nullptr;        // the GameUpdateLoop this game's state belongs to
bool s_on = false;
ComponentDescription* s_template = nullptr;
std::vector<Dock> s_docks;
uint64_t s_dockSig = 0;
std::vector<Link> s_links;
std::vector<uint8_t> s_laneType;      // per cell: the worker type we wrote, 0 = no lane
std::vector<int> s_laneCells;         // the cells s_laneType marks
float s_waterZ = kWaterZ;
int s_frame = 0;
std::vector<RideNote> s_notes;
std::map<const void*, Boat> s_boats;
Graphics::GraphicsMesh* s_boatMesh = nullptr;
long long s_boarded = 0, s_landed = 0, s_reported = -1;

Map* TheMap() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_game : nullptr;
}

// ---- the template and its button (the provider) ------------------------------------------

// The Fisherman's Dock's parts a Ferry Dock does not take: it catches nothing. Its workers are the
// Well's (builders while it is built, none after): Swap. Its storage stays (a construction site keeps
// the delivered materials there, GatherRawMaterials), and its build-menu text (ToolbarDescription)
// is replaced by ours. Every part keeps its place in the list: the state machine chains the parts'
// controllers in that order.
bool DropPart(const Description* d) {
    const uint32_t t = d->Type();
    return t == RadiusDescription::TypeId() || t == FishingDescription::TypeId();
}

Description* Swap(Description* d, const ComponentDescription* well) {
    const uint32_t t = d->Type();
    if (t != WorkDescription::TypeId() && t != WorkPlaceDescription::TypeId()) return d;
    for (int i = 0; i < well->_subDescCount; ++i)
        if (well->_subDescs[i] != nullptr && well->_subDescs[i]->Type() == t) return well->_subDescs[i];
    return nullptr;
}

template <class T>
T* NewObject() {
    void* mem = System::MemoryAllocator::MemAlloc(sizeof(T), 8);
    return mem != nullptr ? ::new (mem) T() : nullptr;
}

// The dock's build-menu text, from our string table; its icon as the Fisherman's Dock has it.
ToolbarDescription* NewToolbarText(const ToolbarDescription* dock) {
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    UI::StringTable* table = f->FindOrLoadAs<UI::StringTable>(System::Filename(kStringTable));
    if (!::Object::IsDerivedFromNullSafe(table, UI::StringTable::TypeId())) return nullptr;
    ToolbarDescription* t = NewObject<ToolbarDescription>();
    if (t == nullptr) return nullptr;
    t->_spriteSheet = dock->_spriteSheet;
    t->_name = dock->_name;   // the Fisherman's Dock's icon
    t->_stringTable = table;
    t->_str1 = System::String("FerryDock");
    t->_str2 = System::String("FerryDockLwr");
    t->_str3 = System::String("FerryDockTip");
    t->_entries.SetSize(2);
    t->_entries[0] = System::String("FerryCreateOk");
    t->_entries[1] = System::String("FerryCreateBlocked");
    t->_val0 = dock->_val0;
    t->_val1 = dock->_val1;
    return t;
}

// Template\ExtFerryDock.rsc: the Fisherman's Dock's parts (placement, model, construction,
// picking, its panel...) by reference, less the ones DropPart names, and our build-menu text.
::Object::External* BuildDockTemplate() {
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    ComponentDescription* dock = f->FindOrLoadAs<ComponentDescription>(System::Filename(kFishermansDock));
    if (!::Object::IsDerivedFromNullSafe(dock, ComponentDescription::TypeId())) {
        std::fprintf(stderr, "[Ferries] no %s in the game data: no Ferry Dock\n", kFishermansDock);
        return nullptr;
    }
    ComponentDescription* well = f->FindOrLoadAs<ComponentDescription>(System::Filename(kWell));
    if (!::Object::IsDerivedFromNullSafe(well, ComponentDescription::TypeId())) {
        std::fprintf(stderr, "[Ferries] no %s in the game data: no Ferry Dock\n", kWell);
        return nullptr;
    }
    ComponentDescription* t = NewObject<ComponentDescription>();
    if (t == nullptr) return nullptr;
    t->_category = dock->_category;
    t->_subDescs = static_cast<Description**>(
        System::MemoryAllocator::MemAlloc(sizeof(Description*) * static_cast<uint64_t>(dock->_subDescCount), 8));
    t->_subDescCap = dock->_subDescCount;
    t->_subDescCount = 0;
    for (int i = 0; i < dock->_subDescCount; ++i) {
        Description* d = dock->_subDescs[i];
        if (d == nullptr || DropPart(d)) continue;
        d = Swap(d, well);
        if (d == nullptr) {
            std::fprintf(stderr, "[Ferries] the Well has no %s: no Ferry Dock\n", dock->_subDescs[i]->TypeName());
            return nullptr;
        }
        if (d->Type() == ToolbarDescription::TypeId()) {
            ToolbarDescription* text = NewToolbarText(static_cast<const ToolbarDescription*>(d));
            if (text == nullptr) {
                std::fprintf(stderr, "[Ferries] %s did not load: no Ferry Dock\n", kStringTable);
                return nullptr;
            }
            text->_path = System::Filename("Template\\ExtFerryDock.rsc:toolbar");
            d = text;
        }
        t->_subDescs[t->_subDescCount++] = d;
    }
    std::fprintf(stderr, "[Ferries] the Ferry Dock: %d parts (the Fisherman's Dock's, its workers the Well's)\n",
                 t->_subDescCount);
    return t;
}

// Template\ExtFerryDock.rsc:ferrydock: the Ferry Dock's button in the Transport group, after the
// Tunnel. Shown only in a game with ext.ferries on (SelectForGame).
::Object::External* BuildToolbarItem() {
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    Toolbar* group = f->FindOrLoadAs<Toolbar>(System::Filename(kTransportGroup));
    ComponentDescription* tmpl = f->FindOrLoadAs<ComponentDescription>(System::Filename(kDockTemplate));
    if (!::Object::IsDerivedFromNullSafe(group, Toolbar::TypeId()) ||
        !::Object::IsDerivedFromNullSafe(tmpl, ComponentDescription::TypeId())) {
        std::fprintf(stderr, "[Ferries] no Transport group or no Ferry Dock template: no button\n");
        return nullptr;
    }
    Toolbar* item = NewObject<Toolbar>();
    if (item == nullptr) return nullptr;
    item->_field44 = true;    // a shortcut key from the group's free ones
    item->_field45 = false;   // hidden until a game with ext.ferries on
    item->_field48 = 1;       // a building button (its text and icon from the template)
    item->_field50 = kButtonPriority;
    item->_field58 = tmpl;
    item->_field60 = group;
    return item;
}

::Object::External* Provide(const char* path, void*) {
    // `path` has forward slashes and always an identifier (Resources.h).
    if (std::strcmp(path, "Template/ExtFerryDock.rsc:resource") == 0) return BuildDockTemplate();
    if (std::strcmp(path, "Template/ExtFerryDock.rsc:ferrydock") == 0) return BuildToolbarItem();
    return nullptr;
}

// ---- the docks ---------------------------------------------------------------------------

// The town's Ferry Docks (built or being built), from the town manager's per-template list
// (CitizenBehavior +0x160, filled by ZoneComponent as a building is added).
std::vector<Dock> ScanDocks() {
    std::vector<Dock> out;
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_citizenList == nullptr || s_template == nullptr) return out;
    const TemplateBucket* node = g->m_citizenList->field_0x160._root;
    while (node != nullptr && node->element.key != s_template)
        node = s_template < node->element.key ? node->left : node->right;
    if (node == nullptr) return out;
    const auto& list = node->element.entities;
    for (int i = 0; i < list._size; ++i) {
        ComponentEntity* e = list._data[i].Get();
        if (e == nullptr) continue;
        const MapComponent* mc = e->FindComponent<MapComponent>();
        if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) continue;
        Dock d;
        d.entity = e;
        d.x = mc->tileX0;
        d.y = mc->tileY0;
        d.w = mc->tileX1;
        d.h = mc->tileY1;
        // Finished: the model shows its group's last mesh (the construction stages come first).
        if (const ModelComponent* m = e->FindComponent<ModelComponent>()) {
            const ModelDescription* md = static_cast<const ModelDescription*>(m->_description);
            if (md != nullptr && m->meshGroup >= 0 && m->meshGroup < md->_meshGroupCount && m->meshIndex >= 0)
                d.built = m->meshIndex == md->_meshGroups[m->meshGroup].meshCount - 1;
        }
        out.push_back(d);
    }
    return out;
}

uint64_t Signature(const std::vector<Dock>& docks) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    for (const Dock& d : docks) {
        mix(reinterpret_cast<uintptr_t>(d.entity));
        mix(static_cast<uint64_t>(d.x) << 32 | static_cast<uint32_t>(d.y));
        mix(static_cast<uint64_t>(d.w) << 32 | static_cast<uint32_t>(d.h));
        mix(d.built ? 1 : 2);
    }
    return h;
}

// ---- the lanes in the path worker's copy -------------------------------------------------

void WriteLanes(Map* map) {
    const size_t cells = static_cast<size_t>(map->size) * map->size;
    if (s_laneType.size() != cells) s_laneType.assign(cells, 0);
    std::map<int, uint8_t> want;
    for (const Link& l : s_links)
        for (const auto& ct : LaneCellTypes(l)) {
            auto it = want.find(ct.first);
            // Where two lanes cross, the cheaper (the lane, not the wait) wins.
            if (it == want.end() || ct.second == kLaneType) want[ct.first] = ct.second;
        }
    // Cells no lane holds any more get their own type back.
    for (int c : s_laneCells) {
        if (want.count(c) != 0) continue;
        map->pathfinder->SetCellTerrain(c, map->cells.GetData()[c].terrainType);
        s_laneType[static_cast<size_t>(c)] = 0;
    }
    s_laneCells.clear();
    for (const auto& kv : want) {
        if (s_laneType[static_cast<size_t>(kv.first)] != kv.second)
            map->pathfinder->SetCellTerrain(kv.first, kv.second);
        s_laneType[static_cast<size_t>(kv.first)] = kv.second;
        s_laneCells.push_back(kv.first);
    }
    g_riding = !s_links.empty();
}

void MarkLandingsDirty(Map* map, const std::vector<Link>& links) {
    if (map->regionGraph == nullptr) return;
    for (const Link& l : links) {
        map->regionGraph->MarkCellDirty(l.landA % map->size, l.landA / map->size);
        map->regionGraph->MarkCellDirty(l.landB % map->size, l.landB / map->size);
    }
}

bool SameLinks(const std::vector<Link>& a, const std::vector<Link>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].landA != b[i].landA || a[i].landB != b[i].landB || a[i].lane != b[i].lane) return false;
    return true;
}

// ---- the riders' boats -------------------------------------------------------------------

void FreeBoat(Boat& b, bool sceneAlive) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (sceneAlive && b.sceneId >= 0 && g != nullptr && g->m_graphicsScene != nullptr)
        g->m_graphicsScene->RemoveObject(b.sceneId);
    b.sceneId = -1;
    if (b.mesh != nullptr) {
        b.mesh->~MeshInstance();
        System::MemoryAllocator::MemFree(b.mesh);
        b.mesh = nullptr;
    }
}

void FreeBoats(bool sceneAlive) {
    for (auto& kv : s_boats) FreeBoat(kv.second, sceneAlive);
    s_boats.clear();
}

Graphics::GraphicsMesh* BoatMesh() {
    if (s_boatMesh != nullptr) return s_boatMesh;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    Graphics::GraphicsMesh* m = f->FindOrLoadAs<Graphics::GraphicsMesh>(System::Filename(kBoatMesh));
    if (::Object::IsDerivedFromNullSafe(m, Graphics::GraphicsMesh::TypeId()))
        s_boatMesh = m;
    else
        std::fprintf(stderr, "[Ferries] no %s: riders cross without a boat\n", kBoatMesh);
    return s_boatMesh;
}

void PlaceBoats() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_graphicsScene == nullptr) return;
    for (const RideNote& n : s_notes) {
        auto it = s_boats.find(n.key);
        if (it == s_boats.end()) {
            ++s_boarded;
            Boat b;
            Graphics::GraphicsMesh* gm = BoatMesh();
            void* mem = gm != nullptr ? System::MemoryAllocator::MemAlloc(0x120, 8) : nullptr;
            if (mem != nullptr) {
                b.mesh = ::new (mem) Graphics::MeshInstance();
                b.mesh->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(gm));
                b.mesh->_color[0] = kBoatScale;
                b.mesh->_color[1] = kBoatScale;
                b.mesh->_color[2] = kBoatScale;
            }
            it = s_boats.emplace(n.key, b).first;
        }
        Boat& b = it->second;
        b.lastFrame = s_frame;
        if (b.mesh == nullptr) continue;
        const Math::Vector3 pos(n.x, n.y, n.z);
        b.mesh->SetTransform(Math::Transform::FromYawPitchOffset(n.yaw + kBoatYaw, 0.0f, pos));
        b.mesh->UpdateHierarchy();
        if (b.sceneId < 0) b.sceneId = g->m_graphicsScene->AddObject(b.mesh);
    }
    s_notes.clear();
    for (auto it = s_boats.begin(); it != s_boats.end();) {
        if (s_frame - it->second.lastFrame > kBoatLinger) {
            ++s_landed;
            FreeBoat(it->second, true);
            it = s_boats.erase(it);
        } else {
            ++it;
        }
    }
}

void ReadWaterHeight() {
    s_waterZ = kWaterZ;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    ComponentDescription* boat = f->FindOrLoadAs<ComponentDescription>(System::Filename(kTraderBoat));
    if (!::Object::IsDerivedFromNullSafe(boat, ComponentDescription::TypeId())) return;
    if (const CharacterDescription* cd = boat->FindDescription<CharacterDescription>())
        if (cd->_b3c) s_waterZ = cd->_f40;   // the trader's boat sails at this fixed height
}

void Reset() {
    s_docks.clear();
    s_dockSig = 0;
    s_links.clear();
    s_laneType.clear();
    s_laneCells.clear();
    s_notes.clear();
    s_frame = 0;
    s_boarded = s_landed = 0;
    s_reported = -1;
    g_riding = false;
}

}  // namespace

void InitProviders() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    if (!Ext::Resources::Register("Template/ExtFerryDock.rsc", &Provide, nullptr))
        std::fprintf(stderr, "[Ferries] the resource provider table is full: no Ferry Dock\n");
}

void SelectForGame() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (s_owner != g) {
        FreeBoats(false);   // the last game's scene went with it
        Reset();
        s_owner = g;
    }
    s_on = Ext::Ext().ferries;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr) return;
    // The button exists once loaded (the provider pins it for the process); a game with
    // ext.ferries off never loads it, or hides it again.
    Toolbar* item = s_on ? f->FindOrLoadAs<Toolbar>(System::Filename(kToolbarItem))
                         : f->FindAs<Toolbar>(System::Filename(kToolbarItem));
    const bool button = ::Object::IsDerivedFromNullSafe(item, Toolbar::TypeId());
    if (button) item->_field45 = s_on;
    if (!s_on) return;
    s_template = f->FindOrLoadAs<ComponentDescription>(System::Filename(kDockTemplate));
    ReadWaterHeight();
    std::fprintf(stderr, "[Ferries] on for this game: %s; riders at height %.3f\n",
                 button ? "the Ferry Dock is in the Transport menu" : "NO Ferry Dock button (see above)",
                 static_cast<double>(s_waterZ));
    // A loaded town's docks: the zones are worked out again (and joined) before the first path.
    s_docks = ScanDocks();
    s_dockSig = Signature(s_docks);
    if (!s_docks.empty()) {
        if (Map* map = TheMap()) map->field_0xA2 = 1;
        std::fprintf(stderr, "[Ferries] %zu Ferry Dock(s) in this town\n", s_docks.size());
    }
    TestDocks::SelectForGame(!s_docks.empty());
}

void BeginFrame() {
    if (!s_on) return;
    ++s_frame;
    if (s_frame % kScanFrames == 0) {
        std::vector<Dock> docks = ScanDocks();
        const uint64_t sig = Signature(docks);
        if (sig != s_dockSig) {
            int built = 0;
            for (const Dock& d : docks) built += d.built ? 1 : 0;
            std::fprintf(stderr, "[Ferries] docks: %zu, %d finished\n", docks.size(), built);
            s_docks.swap(docks);
            s_dockSig = sig;
            // The links are worked out again with the zones, before the next path.
            if (Map* map = TheMap()) map->field_0xA2 = 1;
        }
    }
    PlaceBoats();
    if (s_frame % 600 == 0 && s_boarded != s_reported) {
        s_reported = s_boarded;
        std::fprintf(stderr, "[Ferries] crossings: %lld boarded, %lld landed, %zu on the water, %zu link(s)\n",
                     s_boarded, s_landed, s_boats.size(), s_links.size());
    }
    TestDocks::BeginFrame();
}

ComponentEntity* FirstDock() { return s_docks.empty() ? nullptr : s_docks.front().entity; }

void EndGame(const void* owner) {
    if (owner == nullptr || owner != s_owner) return;
    FreeBoats(false);
    Reset();
    s_on = false;
    s_owner = nullptr;
}

void AfterFillZones(Map* map) {
    if (!s_on || map == nullptr || map->pathfinder == nullptr) return;
    if (s_docks.empty() && s_links.empty()) return;
    const int n = map->size;
    const size_t cells = static_cast<size_t>(n) * n;
    std::vector<uint8_t> type(cells);
    std::vector<uint16_t> zone(cells);
    const MapCell* c = map->cells.GetData();
    uint16_t maxZone = 0;
    for (size_t i = 0; i < cells; ++i) {
        type[i] = c[i].terrainType;
        zone[i] = c[i].zone;
        if (zone[i] > maxZone) maxZone = zone[i];
    }
    std::vector<DockSite> sites;
    for (const Dock& d : s_docks) {
        DockSite s;
        s.built = d.built;
        for (int yy = d.y; yy < d.y + d.h; ++yy)
            for (int xx = d.x; xx < d.x + d.w; ++xx)
                if (xx >= 0 && yy >= 0 && xx < n && yy < n) s.cells.push_back(yy * n + xx);
        sites.push_back(s);
    }
    const MapView view{ n, type.data(), zone.data() };
    std::vector<Link> links = FindLinks(view, sites);
    if (!SameLinks(links, s_links)) {
        MarkLandingsDirty(map, s_links);   // the old landings lose their link
        s_links.swap(links);
        MarkLandingsDirty(map, s_links);
        for (const Link& l : s_links)
            std::fprintf(stderr, "[Ferries] link: dock (%d,%d) to dock (%d,%d), %zu cells of water, zones %u and %u\n",
                         s_docks[l.dockA].x, s_docks[l.dockA].y, s_docks[l.dockB].x, s_docks[l.dockB].y,
                         l.lane.size(), l.zoneA, l.zoneB);
        if (s_links.empty()) std::fprintf(stderr, "[Ferries] no links between the %zu dock(s)\n", s_docks.size());
    }
    WriteLanes(map);
    // The shores are one zone for every zone test in the game.
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
    for (const Link& l : s_links) pairs.emplace_back(l.zoneA, l.zoneB);
    if (pairs.empty()) return;
    const std::vector<uint16_t> remap = JoinZones(pairs, maxZone);
    MapCell* w = map->cells.GetData();
    for (size_t i = 0; i < cells; ++i) w[i].zone = remap[w[i].zone];
}

void AfterRegionFlush(RegionGraph* graph) {
    if (!s_on || s_links.empty() || graph == nullptr || graph->map == nullptr) return;
    Map* map = graph->map;
    const int n = map->size;
    auto blockOf = [&](int cell) {
        return (cell / n) / graph->blockHeight * graph->blocksWide + (cell % n) / graph->blockWidth;
    };
    // The landing blocks' lists afresh (a rebuild elsewhere may have reused a region id), then
    // the links on top.
    for (const Link& l : s_links) {
        graph->RebuildAdjacency(blockOf(l.landA));
        graph->RebuildAdjacency(blockOf(l.landB));
    }
    auto add = [](RegionNode& r, int other) {
        for (int i = 0; i < r.neighbours.GetSize(); ++i)
            if (r.neighbours[i] == other) return;
        const int size = r.neighbours.GetSize();
        r.neighbours.SetSize(size + 1);
        r.neighbours[size] = other;
    };
    const MapCell* c = map->cells.GetData();
    for (const Link& l : s_links) {
        const int ra = c[l.landA].regionId, rb = c[l.landB].regionId;
        if (ra < 0 || rb < 0 || ra >= graph->regions.size || rb >= graph->regions.size || ra == rb) continue;
        add(graph->regions.data[ra], rb);
        add(graph->regions.data[rb], ra);
    }
}

void AfterSetTerrainType(Map* map, int cell) {
    if (s_laneCells.empty() || map == nullptr || cell < 0 || static_cast<size_t>(cell) >= s_laneType.size()) return;
    const uint8_t t = s_laneType[static_cast<size_t>(cell)];
    if (t != 0) map->pathfinder->SetCellTerrain(cell, t);   // the lane stays in the worker's copy
}

void Ride(const CharacterComponent* rider, float* pos, float yaw) {
    Map* map = TheMap();
    if (map == nullptr || rider == nullptr) return;
    // A trader boat holds its own fixed height (CharacterDescription +0x3c): it is not a rider.
    if (rider->characterDescription != nullptr && rider->characterDescription->_b3c) return;
    const int n = map->size;
    const int ix = static_cast<int>(pos[0] + static_cast<float>(n >> 1));
    const int iy = static_cast<int>(pos[1] + static_cast<float>(n >> 1));
    if (ix < 0 || iy < 0 || ix >= n || iy >= n) return;
    if (!IsWater(map->cells.GetData()[iy * n + ix].terrainType)) return;
    pos[2] = s_waterZ;
    RideNote note;
    note.key = rider;
    note.x = pos[0];
    note.y = pos[1];
    note.z = pos[2];
    note.yaw = yaw;
    s_notes.push_back(note);
}

}  // namespace Ferries
}  // namespace Ext

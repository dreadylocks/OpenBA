// ============================================================================
// src/ext/neighbour/shared_map.cpp -- NOT RETAIL. ext.neighbour M2: claims and mirrors (shared_map.h)
// ============================================================================
// Our own code over the game's objects (the guards are ours to place).
// ============================================================================

#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "math/Math.h"

#include "ext/neighbour/shared_map.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <new>
#include <set>
#include <vector>

#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ModelDescription.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/DecalDescription.h"
#include "Component/GrowthComponent.h"
#include "Component/MapDescription.h"
#include "Action/PathSearch.h"
#include "Game/PathfinderThread.h"
#include "Game/WorldGeometry.h"
#include "Entity/EntityList.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "object/External.h"
#include "object/Internal.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"

#include "ext/neighbour/link.h"
#include "ext/neighbour/protocol.h"

namespace Neighbour {
namespace SharedMap {

namespace {

constexpr uint64_t kCheckFrames = 300;      // ~5 s: look for structures that are gone
constexpr float    kMirrorOpacity = 0.55f;  // see-through: "the neighbour's", not ours
constexpr int      kBlockedType = 31;       // a terrain type no walk mask has (0xe1f, 0x1e0)
constexpr size_t   kMaxPending = 20000;     // road / resource messages waiting for the relay
// Pacing: the relay drops a town that floods it (600 messages in 10 s), so the shared map sends
// at most kSendRate a second, kSendBurst at once; what does not fit waits for the next frame.
constexpr double   kSendRate = 40.0;
constexpr double   kSendBurst = 120.0;

bool s_on = false;
int  s_dim = 0;
std::vector<uint8_t> s_peer;                // 1 = the neighbour holds the tile

// This town's structures, by id ("s<x>_<y>": a footprint origin holds one structure).
struct Mine {
    Object::InternalPtr<ComponentEntity> entity;
    Message                              build;   // the "build" message
    TileRect                             rect;
    int                                  built = -1;   // the stage last reported (percent)
};

// How far along a structure's model is, in percent: its mesh index in the group's stages
// (foundation first, the finished building last). No model or one stage: 100.
int BuiltPercent(ComponentEntity* entity) {
    ModelComponent* model = entity->FindComponent<ModelComponent>();
    if (model == nullptr) return 100;
    const ModelDescription* md = ::Object::IsDerivedFromNullSafe(model->_description, ModelDescription::TypeId())
                                     ? static_cast<const ModelDescription*>(model->_description) : nullptr;
    if (md == nullptr || md->_meshGroups == nullptr || md->_meshGroupCount <= 0) return 100;
    const int g = (model->meshGroup >= 0 && model->meshGroup < md->_meshGroupCount) ? model->meshGroup : 0;
    const int count = md->_meshGroups[g].meshCount;
    if (count <= 1) return 100;
    const int idx = model->meshIndex < 0 ? 0 : (model->meshIndex >= count ? count - 1 : model->meshIndex);
    return ((idx + 1) * 100) / count;
}
std::map<std::string, Mine> s_mine;
std::vector<TileRect> s_claimQueue;
std::vector<TileRect> s_releaseQueue;
std::vector<std::string> s_unbuildQueue;
std::vector<std::string> s_buildQueue;      // ids whose build message is to be sent
uint64_t s_claimSerial = 0;

// The neighbour's structures, drawn.
struct Mirror {
    Graphics::MeshInstance* mesh = nullptr;
    uint32_t                sceneId = 0;
    bool                    inScene = false;
    std::vector<uint32_t>   solid;   // the cells marked solid for the path worker
};
std::map<std::string, Mirror> s_mirrors;
std::vector<uint8_t> s_solidRef;            // per cell: how many mirrors hold it solid

// M3.
bool s_live = false;                        // the world is built (the first Tick): changes are news
bool s_applying = false;                    // applying the neighbour's change: not news
std::string s_roadTmpl[2];                  // the road tool's template per road type
std::set<const ComponentEntity*> s_quiet;   // removed here for the neighbour: not news
std::vector<Message> s_pending;             // road / resource messages for the relay
// What the neighbour's changes did here (logged when it changes): roads laid / taken up,
// resources removed / planted, tiles marked solid.
struct Applied { int roads = 0, unroads = 0, removed = 0, planted = 0, solid = 0, refused = 0; } s_applied, s_appliedLogged;
// Why the last change was not applied (logged with the counts).
const char* s_lastRefusal = "";

Map* TheMap() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_game : nullptr;
}

bool EnsureGrid() {
    if (!s_on) return false;
    Map* map = TheMap();
    if (map == nullptr || map->size <= 0 || map->size > kMaxMapDim) return false;
    if (s_dim != map->size) {
        s_dim = map->size;
        s_peer.assign(static_cast<size_t>(s_dim) * s_dim, 0);
    }
    return true;
}

void MarkPeer(const std::vector<TileRect>& rects, uint8_t v) {
    if (!EnsureGrid()) return;
    for (const TileRect& r : rects)
        for (int y = r.y; y < r.y + r.h && y < s_dim; ++y)
            for (int x = r.x; x < r.x + r.w && x < s_dim; ++x)
                s_peer[static_cast<size_t>(y) * s_dim + x] = v;
}

std::string PathOf(const ::Object::External* x) {
    if (x == nullptr) return std::string();
    const System::Filename p = x->GetPath();
    return p.AsString()._data != nullptr ? std::string(p.AsString()._data) : std::string();
}

PathfinderThread* ThePathfinder() {
    Map* map = TheMap();
    return map != nullptr ? map->pathfinder : nullptr;
}

// Solid for the path worker only: its terrain byte becomes a type no walk mask accepts; Map::cells
// (the saved truth) keeps the real type, which comes back when the last mirror on the cell goes.
void MarkSolid(uint32_t cell, bool on) {
    Map* map = TheMap();
    PathfinderThread* pf = ThePathfinder();
    if (map == nullptr || pf == nullptr || s_solidRef.size() != static_cast<size_t>(map->size) * map->size ||
        cell >= s_solidRef.size())
        return;
    uint8_t& ref = s_solidRef[cell];
    if (on) {
        if (ref == 255) return;
        if (ref++ == 0) pf->SetCellTerrain(static_cast<int>(cell), kBlockedType);
    } else {
        if (ref == 0) return;
        if (--ref == 0) pf->SetCellTerrain(static_cast<int>(cell), map->cells.GetData()[cell].terrainType);
    }
}

void FreeMirror(Mirror& m, bool sceneAlive) {
    if (sceneAlive)
        for (uint32_t c : m.solid) MarkSolid(c, false);
    m.solid.clear();
    if (m.inScene && sceneAlive) {
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        if (g != nullptr && g->m_graphicsScene != nullptr) g->m_graphicsScene->RemoveObject(m.sceneId);
    }
    m.inScene = false;
    if (m.mesh != nullptr) {
        m.mesh->~MeshInstance();
        System::MemoryAllocator::MemFree(m.mesh);
        m.mesh = nullptr;
    }
}

// Remove the natural resources (trees, rocks, deposits: NaturalResourceComponent) standing on a
// footprint, the way the game removes an entity (EntityList::QueueDestroy); nothing else.
void ClearNatureUnder(const TileRect& r) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    if (g == nullptr || map == nullptr || g->m_entityList == nullptr) return;
    std::vector<ComponentEntity*> doomed;
    for (int y = r.y; y < r.y + r.h && y < map->size; ++y) {
        for (int x = r.x; x < r.x + r.w && x < map->size; ++x) {
            const MapCell& cell = map->cells.GetData()[static_cast<size_t>(y) * map->size + x];
            for (ComponentEntity* e : {cell.occupant.Get(), cell.blocker.Get()}) {
                if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
                bool seen = false;
                for (ComponentEntity* d : doomed) seen |= d == e;
                if (!seen) doomed.push_back(e);
            }
        }
    }
    for (ComponentEntity* e : doomed) {
        s_quiet.insert(e);   // the neighbour cleared it in theirs: not news for them
        g->m_entityList->QueueDestroy(&e);
    }
}

// Draw one of the neighbour's structures: its template's finished mesh, see-through.
void AddMirror(const Message& m) {
    std::string id, tmpl;
    int64_t x = 0, y = 0, z = 0, rot = 0, grp = 0;
    if (!m.Get("id", id) || !m.Get("tmpl", tmpl) || !m.GetInt("x", x) || !m.GetInt("y", y) || !m.GetInt("z", z) ||
        !m.GetInt("rot", rot))
        return;
    m.GetInt("grp", grp);
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (g == nullptr || g->m_graphicsScene == nullptr || f == nullptr) return;
    ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
    if (!::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())) return;
    const ModelDescription* model = desc->FindDescription<ModelDescription>();
    if (model == nullptr || model->_meshGroups == nullptr || model->_meshGroupCount <= 0) return;
    const int group = (grp >= 0 && grp < model->_meshGroupCount) ? static_cast<int>(grp) : 0;
    const ModelMeshGroup& mg = model->_meshGroups[group];
    if (mg.meshes == nullptr || mg.meshCount <= 0) return;
    // The stage: `built` percent of the group's meshes (foundation first, finished last).
    int64_t built = 100;
    m.GetInt("built", built);
    int stage = static_cast<int>((built * mg.meshCount + 99) / 100) - 1;
    if (stage < 0) stage = 0;
    if (stage >= mg.meshCount) stage = mg.meshCount - 1;
    Graphics::GraphicsMesh* gm = mg.meshes[stage];
    if (gm == nullptr) return;

    auto it = s_mirrors.find(id);
    if (it != s_mirrors.end()) FreeMirror(it->second, true);   // an update: draw it afresh
    Mirror mir;
    void* mem = System::MemoryAllocator::MemAlloc(0x120, 8);
    if (mem == nullptr) return;
    mir.mesh = ::new (mem) Graphics::MeshInstance();
    mir.mesh->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(gm));
    const Math::Vector3 pos(static_cast<float>(x) / 100.0f, static_cast<float>(y) / 100.0f, static_cast<float>(z) / 100.0f);
    const float yaw = static_cast<float>(rot) / 100.0f, pitch = 0.0f;
    mir.mesh->SetTransform(Math::Transform::FromYawPitchOffset(yaw, pitch, pos));
    mir.mesh->UpdateHierarchy();
    mir.mesh->_opacity = kMirrorOpacity;
    mir.mesh->_opacityPrev = kMirrorOpacity;
    mir.sceneId = g->m_graphicsScene->AddObject(mir.mesh);
    mir.inScene = true;
    s_mirrors[id] = mir;

    // Its footprint is the neighbour's land (the claim normally said so already), and what grows
    // there in this copy of the map -- trees, rocks, the neighbour cleared them in theirs -- goes,
    // so the see-through building does not stand in a forest.
    int64_t tx = 0, ty = 0, tw = 0, th = 0;
    if (m.GetInt("tx", tx) && m.GetInt("ty", ty) && m.GetInt("tw", tw) && m.GetInt("th", th) && tw > 0 && th > 0) {
        const TileRect r{static_cast<int>(tx), static_cast<int>(ty), static_cast<int>(tw), static_cast<int>(th)};
        MarkPeer({r}, 1);
        ClearNatureUnder(r);
        // Its solid tiles ("bm", row by row): citizens walk round the neighbour's building.
        const std::string bm = m.GetOr("bm", "");
        Map* map = TheMap();
        if (map != nullptr && bm.size() == static_cast<size_t>(tw * th)) {
            if (s_solidRef.size() != static_cast<size_t>(map->size) * map->size)
                s_solidRef.assign(static_cast<size_t>(map->size) * map->size, 0);
            Mirror& placed = s_mirrors[id];
            for (int64_t j = 0; j < th; ++j)
                for (int64_t i = 0; i < tw; ++i) {
                    if (bm[static_cast<size_t>(j * tw + i)] != '1') continue;
                    const int64_t cx = tx + i, cy = ty + j;
                    if (cx < 0 || cy < 0 || cx >= map->size || cy >= map->size) continue;
                    const uint32_t c = static_cast<uint32_t>(cy * map->size + cx);
                    MarkSolid(c, true);
                    placed.solid.push_back(c);
                    ++s_applied.solid;
                }
        }
    }
}

void Queue(const Message& m) {
    if (s_pending.size() >= kMaxPending) s_pending.erase(s_pending.begin());
    s_pending.push_back(m);
}

// ---- M3: roads ----------------------------------------------------------------------------------

Graphics::MaterialInstance* RoadDecal(const std::string& tmpl) {
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (tmpl.empty() || f == nullptr) return nullptr;
    ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
    if (!::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())) return nullptr;
    const DecalDescription* decal = desc->FindDescription<DecalDescription>();
    if (decal == nullptr || decal->_materials == nullptr || decal->_materialCount <= 0) return nullptr;
    return decal->_materials[0];
}

// The neighbour finished (or removed) a road tile: the same here, at once -- the tile's road
// stamp, then the game's own completion (Map::FinishRoadCell: terrain, heights, the road mesh).
void ApplyRoad(const Message& m) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    int64_t cell = -1;
    if (g == nullptr || map == nullptr || g->m_worldGeometry == nullptr || !m.GetInt("cell", cell) || cell < 0 ||
        cell >= static_cast<int64_t>(map->size) * map->size)
        return;
    MapCell& c = map->cells.GetData()[cell];
    bool off = false;
    m.GetBool("off", off);
    s_applying = true;
    if (off) {
        if (c.field_0x2A != 0xFF && c.field_0x2B != 0x80) {
            map->FinishRoadCell(static_cast<int>(cell), 0, 2);
            ++s_applied.unroads;
        }
        s_applying = false;
        return;
    }
    int64_t type = -1, tt = -1;
    std::string tmpl;
    m.GetInt("type", type);
    m.GetInt("tt", tt);
    m.Get("tmpl", tmpl);
    // The road's own template says what ground it makes (its MapDescription), as RoadTool reads it.
    if (::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton; f != nullptr && !tmpl.empty()) {
        ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
        if (::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId()))
            if (const MapDescription* md = desc->FindDescription<MapDescription>()) tt = md->_cellType;
    }
    ComponentEntity* standing = c.blocker.Get();
    const bool ours = standing != nullptr && standing->FindComponent<NaturalResourceComponent>() == nullptr;
    // Not where something of ours stands, not over a road as good, not over one being built.
    if (type < 0 || type > 254 || tt < 0 || tt > 31 || ours || !(static_cast<uint8_t>(type) < c.field_0x2A) ||
        c.field_0x2B == 0x80) {
        ++s_applied.refused;
        s_lastRefusal = ours ? "a road on a tile this town built on" : (c.field_0x2B == 0x80 ? "a road where this town is building one"
                                                                                          : "a road over as good a road");
        s_applying = false;
        return;
    }
    // The road's look: the batch material for its type (this town may never have laid one).
    WorldGeometry* wg = g->m_worldGeometry;
    if (wg->_batchMaterials.size <= type || wg->_batchMaterials.data == nullptr ||
        wg->_batchMaterials.data[type] == nullptr) {
        Graphics::MaterialInstance* decal = RoadDecal(tmpl);
        if (decal == nullptr) {
            ++s_applied.refused;
            s_lastRefusal = "a road of a kind this game does not know";
            s_applying = false;
            return;
        }
        wg->StoreBatchMaterial(static_cast<int>(type), decal);
    }
    const int x = static_cast<int>(cell % map->size), y = static_cast<int>(cell / map->size);
    ClearNatureUnder(TileRect{x, y, 1, 1});
    // As Map::PlaceRoadCell stamps it (plain, roadless ground counts the road), then finished.
    const uint32_t flag = 1u << (c.terrainType & 0x1f);
    if ((flag & 3) == 0 && c.field_0x2A == 0xFF) c.footprintCount = static_cast<uint8_t>(c.footprintCount + 1);
    c.field_0x2A = static_cast<uint8_t>(type);
    map->FinishRoadCell(static_cast<int>(cell), 0xff, static_cast<uint32_t>(tt));
    ++s_applied.roads;
    s_applying = false;
}

// ---- M3: natural resources ---------------------------------------------------------------------

int CellOf(ComponentEntity* e) {
    Map* map = TheMap();
    MapComponent* mc = e != nullptr ? e->FindComponent<MapComponent>() : nullptr;
    if (map == nullptr || mc == nullptr || mc->tileX0 < 0 || mc->tileY0 < 0 || mc->tileX0 >= map->size ||
        mc->tileY0 >= map->size)
        return -1;
    return mc->tileY0 * map->size + mc->tileX0;
}

void ApplyNat(const Message& m) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    int64_t cell = -1;
    std::string op, tmpl;
    if (g == nullptr || map == nullptr || g->m_entityList == nullptr || !m.GetInt("cell", cell) || cell < 0 ||
        cell >= static_cast<int64_t>(map->size) * map->size || !m.Get("op", op) || !m.Get("tmpl", tmpl))
        return;
    const MapCell& c = map->cells.GetData()[cell];
    if (op == "rm") {
        // The same resource here: a natural resource of that template whose origin is the cell.
        for (ComponentEntity* e : {c.occupant.Get(), c.blocker.Get()}) {
            if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
            if (CellOf(e) != cell || PathOf(e->_description) != tmpl) continue;
            if ((e->_entityFlags >> 10) & 1) return;   // already going
            s_quiet.insert(e);
            g->m_entityList->QueueDestroy(&e);
            ++s_applied.removed;
            return;
        }
        return;
    }
    if (op != "add") return;
    // A sapling, as the game's spreading plants one: on a free cell only.
    if (c.occupant.Get() != nullptr || c.blocker.Get() != nullptr || c.footprintCount != 0 || c.field_0x2A != 0xFF) {
        ++s_applied.refused;
        s_lastRefusal = "a sapling on a tile that is taken";
        return;
    }
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr) return;
    ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
    if (!::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())) return;
    void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
    if (mem == nullptr) return;
    ComponentEntity* ent = ::new (mem) ComponentEntity(desc);
    Object::InternalPtr<ComponentEntity> ibp(ent);
    NaturalResourceComponent* nr = ent->FindComponent<NaturalResourceComponent>();
    GrowthComponent* growth = ent->FindComponent<GrowthComponent>();
    if (nr == nullptr || growth == nullptr) {   // not a plant: the pointer frees it
        ++s_applied.refused;
        s_lastRefusal = "a sapling of something that does not grow";
        return;
    }
    s_applying = true;
    nr->PlaceAtCell(static_cast<int>(cell));
    growth->SetProgress(0.0f);
    g->m_entityList->RegisterEntity(&ibp, static_cast<int>(desc->_category));
    ++s_applied.planted;
    s_applying = false;
}

void RemoveMirror(const std::string& id) {
    auto it = s_mirrors.find(id);
    if (it == s_mirrors.end()) return;
    FreeMirror(it->second, true);
    s_mirrors.erase(it);
}

double s_tokens = kSendBurst;
std::chrono::steady_clock::time_point s_tokensAt;

bool Spend() {
    if (s_tokens < 1.0) return false;
    s_tokens -= 1.0;
    return true;
}

void SendClaims(Link* link) {
    // At most kMaxRects rectangles a message.
    while (!s_claimQueue.empty() && Spend()) {
        const size_t n = s_claimQueue.size() < static_cast<size_t>(kMaxRects) ? s_claimQueue.size() : kMaxRects;
        std::vector<TileRect> part(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        s_claimQueue.erase(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        Message c("claim");
        c.Set("id", "c" + std::to_string(++s_claimSerial)).Set("rects", EncodeRects(part));
        link->Send(c);
    }
    while (!s_releaseQueue.empty() && Spend()) {
        const size_t n = s_releaseQueue.size() < static_cast<size_t>(kMaxRects) ? s_releaseQueue.size() : kMaxRects;
        std::vector<TileRect> part(s_releaseQueue.begin(), s_releaseQueue.begin() + static_cast<long>(n));
        s_releaseQueue.erase(s_releaseQueue.begin(), s_releaseQueue.begin() + static_cast<long>(n));
        Message r("release");
        r.Set("rects", EncodeRects(part));
        link->Send(r);
    }
}

}  // namespace

void Begin(bool on) {
    End();
    s_on = on;
    s_live = false;
}

void End() {
    // The scene goes with the game: forget the mirrors without touching it (EndGame runs in the
    // loop's destructor), but free our MeshInstances.
    for (auto& kv : s_mirrors) FreeMirror(kv.second, false);
    s_mirrors.clear();
    s_mine.clear();
    s_claimQueue.clear();
    s_releaseQueue.clear();
    s_unbuildQueue.clear();
    s_buildQueue.clear();
    s_peer.clear();
    s_dim = 0;
    s_on = false;
    s_live = false;
    s_applying = false;
    s_solidRef.clear();
    s_quiet.clear();
    s_pending.clear();
    s_roadTmpl[0].clear();
    s_roadTmpl[1].clear();
    s_applied = Applied();
    s_appliedLogged = Applied();
    s_tokens = kSendBurst;
}

bool PeerHolds(int x, int y) {
    if (!s_on || s_dim == 0 || x < 0 || y < 0 || x >= s_dim || y >= s_dim) return false;
    return s_peer[static_cast<size_t>(y) * s_dim + x] != 0;
}

bool PeerHoldsCell(uint32_t cellIdx) {
    if (!s_on || s_dim == 0) return false;
    return PeerHolds(static_cast<int>(cellIdx % static_cast<uint32_t>(s_dim)),
                     static_cast<int>(cellIdx / static_cast<uint32_t>(s_dim)));
}

void NoteStructure(ComponentEntity* entity) {
    if (!s_on || entity == nullptr || entity->_description == nullptr) return;
    MapComponent* mc = entity->FindComponent<MapComponent>();
    if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) return;
    TileRect r{mc->tileX0, mc->tileY0, mc->tileX1, mc->tileY1};
    if (r.x < 0 || r.y < 0) return;
    // The footprint's axes swap at 90 / 270 degrees (as the placement and start-town code do).
    const int rot = ((mc->extraField % 360) + 360) % 360;
    if (rot == 90 || rot == 270) std::swap(r.w, r.h);
    const std::string tmpl = PathOf(entity->_description);
    if (tmpl.empty()) return;
    // The id: the footprint origin and the template (a fence's corner post and the fence itself
    // share an origin), so each structure keeps its own record.
    uint32_t th = 2166136261u;
    for (unsigned char c : tmpl) th = (th ^ c) * 16777619u;
    const std::string id = "s" + std::to_string(r.x) + "_" + std::to_string(r.y) + "_" + std::to_string(th % 100000u);

    Message b("build");
    b.Set("id", id).Set("tmpl", tmpl);
    ModelComponent* model = entity->FindComponent<ModelComponent>();
    Math::Vector3 pos = model != nullptr ? model->transform.translation : Math::Vector3(0.0f, 0.0f, 0.0f);
    // A structure without a placed model (a stockpile's ground marking) has no position of its
    // own: the middle of its footprint, in world units (tile - dim/2), on the ground.
    if (model == nullptr || (pos.x == 0.0f && pos.y == 0.0f)) {
        Map* map = TheMap();
        const float half = map != nullptr ? static_cast<float>(map->size >> 1) : 0.0f;
        pos = Math::Vector3(static_cast<float>(r.x) + static_cast<float>(r.w) * 0.5f - half,
                            static_cast<float>(r.y) + static_cast<float>(r.h) * 0.5f - half, 0.0f);
    }
    b.SetInt("x", static_cast<int64_t>(std::lround(pos.x * 100.0f)))
     .SetInt("y", static_cast<int64_t>(std::lround(pos.y * 100.0f)))
     .SetInt("z", static_cast<int64_t>(std::lround(pos.z * 100.0f)))
     .SetInt("rot", static_cast<int64_t>(std::lround((model != nullptr ? model->yaw : static_cast<float>(rot)) * 100.0f)))
     .SetInt("grp", model != nullptr ? model->meshGroup : 0)
     .SetInt("tx", r.x).SetInt("ty", r.y).SetInt("tw", r.w).SetInt("th", r.h);
    const int built = BuiltPercent(entity);
    b.SetInt("built", built);
    // Which footprint tiles are solid (the structure blocks them in this map: Map::cells' blocker).
    if (Map* map = TheMap()) {
        std::string bm;
        bm.reserve(static_cast<size_t>(r.w) * r.h);
        for (int j = 0; j < r.h; ++j)
            for (int i = 0; i < r.w; ++i) {
                const int x = r.x + i, y = r.y + j;
                const bool in = x < map->size && y < map->size;
                bm.push_back(in && map->cells.GetData()[static_cast<size_t>(y) * map->size + x].blocker.Get() == entity ? '1' : '0');
            }
        if (bm.size() <= 4096) b.Set("bm", bm);
    }
    Mine mine;
    mine.built = built;
    mine.entity = Object::InternalPtr<ComponentEntity>(entity);
    mine.build = b;
    mine.rect = r;
    s_mine[id] = mine;
    s_claimQueue.push_back(r);
    s_buildQueue.push_back(id);
}

void ScanExisting() {
    if (!s_on) return;
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_entityList == nullptr) return;
    const auto& all = g->m_entityList->masterArray;
    int n = 0;
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr) continue;
        MapComponent* mc = e->FindComponent<MapComponent>();
        if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) continue;
        if (e->FindComponent<NaturalResourceComponent>() != nullptr || e->FindComponent<CitizenComponent>() != nullptr) continue;
        NoteStructure(e);
        ++n;
    }
    fprintf(stderr, "[Neighbour] shared map: %d structure(s) of the loaded town reported\n", n);
}

void NoteRoadCell(uint32_t cellIdx) {
    if (!s_on || !EnsureGrid()) return;
    s_claimQueue.push_back(TileRect{static_cast<int>(cellIdx % static_cast<uint32_t>(s_dim)),
                                    static_cast<int>(cellIdx / static_cast<uint32_t>(s_dim)), 1, 1});
}

void NoteRoadTemplate(uint8_t roadType, const ComponentDescription* tool) {
    if (!s_on || roadType > 1) return;
    s_roadTmpl[roadType] = PathOf(tool);
}

void NoteRoadFinished(int cellIdx, uint32_t terrainType) {
    if (!s_on || !s_live || s_applying || !EnsureGrid() || cellIdx < 0 || cellIdx >= s_dim * s_dim) return;
    Map* map = TheMap();
    const MapCell& c = map->cells.GetData()[cellIdx];
    Message r("road");
    r.SetInt("cell", cellIdx);
    if (terrainType == 2) {
        r.SetBool("off", true);
    } else {
        if (c.field_0x2A > 1) return;   // not a road type this side knows
        r.SetInt("type", c.field_0x2A).SetInt("tt", static_cast<int64_t>(terrainType & 0x1f)).Set("tmpl", s_roadTmpl[c.field_0x2A]);
    }
    Queue(r);
}

void NoteRemoved(ComponentEntity* entity) {
    if (!s_on || entity == nullptr) return;
    auto q = s_quiet.find(entity);
    if (q != s_quiet.end()) { s_quiet.erase(q); return; }
    if (!s_live || entity->FindComponent<NaturalResourceComponent>() == nullptr) return;
    const int cell = CellOf(entity);
    const std::string tmpl = PathOf(entity->_description);
    if (cell < 0 || tmpl.empty()) return;
    Message n("nat");
    n.Set("op", "rm").SetInt("cell", cell).Set("tmpl", tmpl);
    Queue(n);
}

void NotePlanted(ComponentEntity* entity) {
    if (!s_on || !s_live || s_applying || entity == nullptr) return;
    const int cell = CellOf(entity);
    const std::string tmpl = PathOf(entity->_description);
    if (cell < 0 || tmpl.empty()) return;
    Message n("nat");
    n.Set("op", "add").SetInt("cell", cell).Set("tmpl", tmpl);
    Queue(n);
}

void Tick(Link* link, uint64_t frame) {
    if (!s_on || link == nullptr) return;
    EnsureGrid();
    s_live = true;   // from the first frame of play on, a change is news
    // Structures that are gone: unbuild, release their land.
    if (frame % kCheckFrames == 0) {
        const Applied& a = s_applied;
        const Applied& l = s_appliedLogged;
        if (a.roads != l.roads || a.unroads != l.unroads || a.removed != l.removed || a.planted != l.planted ||
            a.solid != l.solid || a.refused != l.refused) {
            fprintf(stderr, "[Neighbour] shared map: from the neighbor so far: %d road tile(s) laid, %d taken up, "
                            "%d resource(s) gone, %d planted, %d tile(s) solid; %d not applied%s%s\n",
                    a.roads, a.unroads, a.removed, a.planted, a.solid, a.refused, a.refused > 0 ? " (last: " : "",
                    a.refused > 0 ? (std::string(s_lastRefusal) + ")").c_str() : "");
            s_appliedLogged = s_applied;
        }
        for (auto it = s_mine.begin(); it != s_mine.end();) {
            ComponentEntity* e = it->second.entity.Get();
            if (e == nullptr) {
                s_unbuildQueue.push_back(it->first);
                s_releaseQueue.push_back(it->second.rect);
                it = s_mine.erase(it);
                continue;
            }
            const int built = BuiltPercent(e);   // construction went on: the neighbour sees it
            if (built != it->second.built) {
                it->second.built = built;
                it->second.build.SetInt("built", built);
                s_buildQueue.push_back(it->first);
            }
            ++it;
        }
    }
    if (!link->Connected()) return;   // the queues wait; OnJoined sends the structures again
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - s_tokensAt).count();
    s_tokensAt = now;
    s_tokens = std::min(kSendBurst, s_tokens + (dt > 0.0 && dt < 60.0 ? dt : 0.0) * kSendRate);
    // Claims first (they keep the neighbour off the land), then buildings, then the rest.
    SendClaims(link);
    size_t i = 0;
    for (; i < s_buildQueue.size() && Spend(); ++i) {
        auto it = s_mine.find(s_buildQueue[i]);
        if (it != s_mine.end()) link->Send(it->second.build);
    }
    s_buildQueue.erase(s_buildQueue.begin(), s_buildQueue.begin() + static_cast<long>(i));
    for (i = 0; i < s_unbuildQueue.size() && Spend(); ++i) {
        Message u("unbuild");
        u.Set("id", s_unbuildQueue[i]);
        link->Send(u);
    }
    s_unbuildQueue.erase(s_unbuildQueue.begin(), s_unbuildQueue.begin() + static_cast<long>(i));
    for (i = 0; i < s_pending.size() && Spend(); ++i) link->Send(s_pending[i]);
    s_pending.erase(s_pending.begin(), s_pending.begin() + static_cast<long>(i));
}

void OnJoined(Link* link) {
    if (!s_on || link == nullptr) return;
    // Claims are idempotent at the relay; builds replace the record by id.
    for (const auto& kv : s_mine) {
        s_claimQueue.push_back(kv.second.rect);
        s_buildQueue.push_back(kv.first);
    }
}

bool OnMessage(const Message& m, std::string* notice) {
    if (!s_on) return false;
    if (m.type == "claims" || m.type == "released") {
        std::vector<TileRect> rects;
        if (DecodeRects(m.GetOr("rects", ""), rects)) MarkPeer(rects, m.type == "claims" ? 1 : 0);
        return true;
    }
    if (m.type == "claimBlock") {
        int64_t bx = 0, by = 0, size = 0;
        std::string owner;
        if (!m.GetInt("bx", bx) || !m.GetInt("by", by) || !m.GetInt("size", size) || !m.Get("owner", owner)) return true;
        if (size <= 0 || size > 256 || owner.size() != static_cast<size_t>(size * size) || !EnsureGrid()) return true;
        for (int64_t j = 0; j < size; ++j)
            for (int64_t i = 0; i < size; ++i) {
                const int64_t x = bx * size + i, y = by * size + j;
                if (x < 0 || y < 0 || x >= s_dim || y >= s_dim) continue;
                const char c = owner[static_cast<size_t>(j * size + i)];
                s_peer[static_cast<size_t>(y) * s_dim + static_cast<size_t>(x)] = (c != '.') ? 1 : 0;
            }
        return true;
    }
    if (m.type == "claimed") {
        bool ok = true;
        m.GetBool("ok", ok);
        if (!ok && notice != nullptr)
            *notice = "Some land you built on was already your neighbor's; the two towns overlap there.";
        return true;
    }
    if (m.type == "build") { AddMirror(m); return true; }
    if (m.type == "unbuild") { RemoveMirror(m.GetOr("id", "")); return true; }
    if (m.type == "mirrorEnd") return true;
    if (m.type == "road") { ApplyRoad(m); return true; }
    if (m.type == "nat") { ApplyNat(m); return true; }
    return false;
}

}  // namespace SharedMap
}  // namespace Neighbour

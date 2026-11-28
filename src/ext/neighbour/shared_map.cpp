// ============================================================================
// src/ext/neighbour/shared_map.cpp -- NOT RETAIL. ext.neighbour M2: claims and mirrors (shared_map.h)
// ============================================================================
// Our own code over the game's objects (the guards are ours to place).
// ============================================================================

#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "graphics/Material.h"   // Graphics::MaterialInstance: a road's material by path
#include "math/Math.h"

#include "ext/neighbour/shared_map.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <new>
#include <set>
#include <unordered_map>
#include <vector>

#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ModelDescription.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/DecalComponent.h"
#include "Component/DecalDescription.h"
#include "Component/GrowthComponent.h"
#include "Component/MapDescription.h"
#include "Component/ClearResourceComponent.h"   // ScanExisting: not a structure (the removal tool's marks)
#include "Component/ClearAreaComponent.h"
#include "Component/RawMaterialComponent.h"     // goods dropped on the ground
#include "Component/AnimalComponent.h"
#include "Component/BuildComponent.h"        // a structure still being built (a refused one is taken back)
#include "Component/MultiObjectComponent.h"  // a bridge's pieces (NoteStructure)
#include "Component/StorageComponent.h"      // a stockpile's goods (PilesOf)
#include "Component/StorageDisplayComponent.h"
#include "Component/LivestockComponent.h"     // this town's livestock (OwnLivestock)
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
#include "ext/neighbour/mirror_look.h"
#include "ext/neighbour/nature_sync.h"
#include "ext/neighbour/protocol.h"
#include "ext/neighbour/restate.h"

namespace Neighbour {
namespace SharedMap {

namespace {

constexpr uint64_t kCheckFrames = 300;      // ~5 s: look for structures that are gone
constexpr float    kMirrorOpacity = 0.55f;  // see-through: "the neighbour's", not ours
constexpr float    kMirrorGround = 0.75f;   // the alpha of a mirror's ground (its decal)
constexpr int      kBlockedType = 31;       // a terrain type no walk mask has (0xe1f, 0x1e0)
constexpr size_t   kMaxPending = 20000;     // road / resource messages waiting for the relay
// Pacing: the relay drops a town that floods it (600 messages in 10 s), so the shared map sends
// at most kSendRate a second, kSendBurst at once; what does not fit waits for the next frame.
constexpr double   kSendRate = 40.0;
constexpr double   kSendBurst = 120.0;
// The link's queue length under which the shared map hands it more (see Tick's backpressure).
constexpr size_t   kLinkLowWater = 8;

bool s_on = false;
int  s_dim = 0;
std::vector<uint8_t> s_peer;                // 1 = the neighbour holds the tile

// This town's structures, by id ("s<x>_<y>": a footprint origin holds one structure).
struct Mine {
    Object::InternalPtr<ComponentEntity> entity;
    Message                              build;   // the "build" message
    TileRect                             rect;
    int                                  built = -1;   // the stage last reported (percent)
    // Placed in this session's play (not a load's scan), and when: only such a site, a few seconds old,
    // is taken back when the relay refuses it (buildRefused); too young for anything to be hauled to it.
    bool                                  placedInPlay = false;
    std::chrono::steady_clock::time_point placedAt;
    bool        display = false;   // a stockpile: it draws its goods as piles (StorageDisplayComponent)
    std::string pilesSent;         // the piles last sent (goods + list), empty before the first
};
constexpr double kTakeBackSeconds = 5.0;   // a refusal comes back within a round trip; nothing hauled yet

// How far along a structure's model is, in percent: its mesh index in the group's stages
// (foundation first, the finished building last). No model or one stage: 100. A site still waiting
// for its materials has its model hidden: 0, drawn as its ground alone (mirror_look.h).
int BuiltPercent(ComponentEntity* entity) {
    ModelComponent* model = entity->FindComponent<ModelComponent>();
    if (model == nullptr) return 100;
    if (model->meshIndex < 0) return 0;
    const ModelDescription* md = ::Object::IsDerivedFromNullSafe(model->_description, ModelDescription::TypeId())
                                     ? static_cast<const ModelDescription*>(model->_description) : nullptr;
    if (md == nullptr || md->_meshGroups == nullptr || md->_meshGroupCount <= 0) return 100;
    const int g = (model->meshGroup >= 0 && model->meshGroup < md->_meshGroupCount) ? model->meshGroup : 0;
    return MirrorLook::StagePercent(model->meshIndex, md->_meshGroups[g].meshCount);
}
std::map<std::string, Mine> s_mine;
// Which id each structure of ours is under. Mine::entity is a plain pointer the free does not clear
// (EntityList::DrainRemovals: "weak refs the free does not invalidate"), so a structure that goes is
// dropped from s_mine by NoteRemoved, while it still exists; nothing reads an entity after its free.
std::unordered_map<const ComponentEntity*, std::string> s_mineIds;
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
    int                     decalId = -1;   // its ground: a WorldGeometry decal slot
    std::vector<uint32_t>   solid;   // the cells marked solid for the path worker
    uint64_t                join = 0;   // the join (s_joinCount) whose structure list last named it
    TileRect                rect{0, 0, 0, 0};   // its footprint (the build message's tx, ty, tw, th)
    // A stockpile's piles (`piles`): a mesh per tile that shows a good, and the message they came from,
    // drawn again when the structure is.
    std::vector<Graphics::MeshInstance*> piles;
    std::vector<uint32_t>                pileScene;
    std::string                          pileGoods, pileList;
};
std::map<std::string, Mirror> s_mirrors;
uint64_t s_joinCount = 0;   // joins to the relay this game (OnWelcome)
std::vector<uint8_t> s_solidRef;            // per cell: how many mirrors hold it solid

// M3.
bool s_live = false;                        // the world is built (the first Tick): changes are news
int s_seat = -1;                            // this town's seat in the room (the relay's welcome); -1 not yet
bool s_applying = false;                    // applying the neighbour's change: not news
std::map<int, std::string> s_roadTmpl;      // the road tool's template per road type (a mod adds types)
// The game's own road tools, per road type (CreateRoadDescription's bool: 0 stone, 1 dirt): sent when
// this game has not used the road tool since it was loaded.
const char* const kRoadTmplDefault[2] = {"Template\\StoneRoad.rsc:resource", "Template\\DirtRoad.rsc:resource"};
bool s_rescanRoads = false;                 // a loaded game's first join: its roads again at mirrorEnd
// The restate (restate.h): this town's whole list goes to the relay kInvFirstSeconds after every join and
// every kInvEverySeconds after that, once its own queued changes have gone (at most kInvMaxWaitSeconds late).
constexpr double kInvFirstSeconds = 15.0;
constexpr double kInvEverySeconds = 60.0;
constexpr double kInvMaxWaitSeconds = 120.0;
constexpr size_t kInvChars = 12000;         // a part's three lists, well under the relay's 16 KB frame
bool s_invScheduled = false;                // a welcome came on this line: lists are due
std::chrono::steady_clock::time_point s_invDue;
uint64_t s_invGen = 0;
uint64_t s_invLoggedJoin = 0;               // the join whose first list was logged
// Structures of ours the relay refused to mirror on this line (buildRefused: they stand on the neighbour's
// land). A `want` for one is not answered, or it would be refused, and the player told, once a minute; the
// next join sends every structure again (OnJoined), and the list always names them, so none is taken down.
std::set<std::string> s_refusedIds;
// Per cell: the join whose replay last named the neighbour's road there; and how many of its road tiles this
// join's replay brought (roadsEnd's count).
std::vector<uint32_t> s_roadSeen;
uint64_t s_roadsReplayed = 0;
std::set<const ComponentEntity*> s_quiet;   // removed here for the neighbour: not news
// This town's livestock (LivestockComponent: in a pasture or roaming free, as a start's cattle do), for the
// villager samples: found by a walk of the entity list every kCheckFrames (ScanLivestock), and one that goes
// is dropped by NoteRemoved before its free, so nothing here outlives its entity.
std::set<ComponentEntity*> s_livestock;
std::vector<Message> s_pending;             // road / resource messages for the relay
uint64_t s_waitingAdds = 0;                 // things queued for the relay (WaitingGeneration)
// What the neighbour's changes did here (logged when it changes): roads laid / taken up,
// resources removed / planted, tiles marked solid, grounds painted under its structures, its structures
// taken down (an `unbuild`; one taken down while the line was down is logged at mirrorEnd).
struct Applied {
    int roads = 0, unroads = 0, removed = 0, planted = 0, solid = 0, refused = 0, grounds = 0, unbuilt = 0;
} s_applied, s_appliedLogged;
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
        s_roadSeen.assign(static_cast<size_t>(s_dim) * s_dim, 0);
    }
    return true;
}

void MarkPeer(const std::vector<TileRect>& rects, uint8_t v) {
    if (!EnsureGrid()) return;
    for (const TileRect& r : rects)   // clipped to the map: a bad rect from the wire never writes outside
        for (int y = std::max(r.y, 0); y < r.y + r.h && y < s_dim; ++y)
            for (int x = std::max(r.x, 0); x < r.x + r.w && x < s_dim; ++x)
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
        // Count down first: PathTerrain lets the map's real type through once nothing holds the tile.
        if (--ref == 0) pf->SetCellTerrain(static_cast<int>(cell), map->cells.GetData()[cell].terrainType);
    }
}

void FreePiles(Mirror& m, bool sceneAlive) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    for (size_t i = 0; i < m.piles.size(); ++i) {
        if (sceneAlive && g != nullptr && g->m_graphicsScene != nullptr)
            g->m_graphicsScene->RemoveObject(m.pileScene[i]);
        m.piles[i]->~MeshInstance();
        System::MemoryAllocator::MemFree(m.piles[i]);
    }
    m.piles.clear();
    m.pileScene.clear();
}

void FreeMirror(Mirror& m, bool sceneAlive) {
    FreePiles(m, sceneAlive);
    if (sceneAlive)
        for (uint32_t c : m.solid) MarkSolid(c, false);
    m.solid.clear();
    if (m.decalId >= 0 && sceneAlive) {
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        if (g != nullptr && g->m_worldGeometry != nullptr) g->m_worldGeometry->RemoveDecal(m.decalId);
    }
    m.decalId = -1;
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
    for (int y = std::max(r.y, 0); y < r.y + r.h && y < map->size; ++y) {
        for (int x = std::max(r.x, 0); x < r.x + r.w && x < map->size; ++x) {
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

// The mesh a mirror shows: `built` percent of the model group's stages (foundation first, finished
// last). Null for a structure without a model (a stockpile is only its ground) and for a site still
// waiting for its materials (0: its owner sees only its ground; mirror_look.h).
Graphics::GraphicsMesh* MirrorMesh(const ComponentDescription* desc, int64_t grp, int64_t built) {
    const ModelDescription* model = desc->FindDescription<ModelDescription>();
    if (model == nullptr || model->_meshGroups == nullptr || model->_meshGroupCount <= 0) return nullptr;
    const int group = (grp >= 0 && grp < model->_meshGroupCount) ? static_cast<int>(grp) : 0;
    const ModelMeshGroup& mg = model->_meshGroups[group];
    if (mg.meshes == nullptr || mg.meshCount <= 0) return nullptr;
    const int stage = MirrorLook::StageOf(built, mg.meshCount);
    return stage >= 0 ? mg.meshes[stage] : nullptr;
}

constexpr size_t kMaxPileGoods = 64;        // goods one stockpile's `piles` names
constexpr size_t kMaxPileChars = 12000;     // its two lists, well under the relay's 16 KB frame

// Draw a stockpile mirror's piles as StorageDisplayComponent draws its owner's: a tile each, in the
// footprint's row order, each the mesh of the good's pile model (its ModelDescription's group 1) the
// owner shows, placed as StorageDisplayComponent::OnAdded places them (the tile's ground position,
// a quarter turn on every other tile), a little see-through. False when the lists are not usable.
bool DrawPiles(Mirror& mir, const std::string& goodList, const std::string& pileList) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    Map* map = TheMap();
    FreePiles(mir, true);
    if (g == nullptr || g->m_graphicsScene == nullptr || f == nullptr || map == nullptr || mir.rect.w <= 0 ||
        mir.rect.h <= 0)
        return false;
    std::vector<std::string> goods;
    std::vector<MirrorLook::Pile> piles;
    const size_t tiles = static_cast<size_t>(mir.rect.w) * static_cast<size_t>(mir.rect.h);
    if (!MirrorLook::ParseGoodList(goodList, kMaxPileGoods, 200, &goods) ||
        !MirrorLook::ParsePileList(pileList, tiles, static_cast<int>(goods.size()), &piles))
        return false;
    std::vector<const ModelMeshGroup*> groups(goods.size(), nullptr);
    std::vector<bool> looked(goods.size(), false);
    const int size = map->size;
    const float halfMap = static_cast<float>(size >> 1) - 0.5f;
    for (size_t i = 0; i < piles.size(); ++i) {
        const MirrorLook::Pile& p = piles[i];
        if (p.good < 0) continue;
        const size_t k = static_cast<size_t>(p.good);
        if (!looked[k]) {
            looked[k] = true;
            const System::Filename path(goods[k].c_str());
            ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(path);
            const ModelDescription* md = ::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())
                                             ? desc->FindDescription<ModelDescription>()
                                             : nullptr;
            if (md != nullptr && md->_meshGroups != nullptr && md->_meshGroupCount >= 2 &&
                md->_meshGroups[1].meshes != nullptr && md->_meshGroups[1].meshCount > 0)
                groups[k] = &md->_meshGroups[1];
        }
        const ModelMeshGroup* mg = groups[k];
        if (mg == nullptr) continue;   // a good this game does not know
        Graphics::GraphicsMesh* gm = mg->meshes[p.mesh < mg->meshCount ? p.mesh : mg->meshCount - 1];
        if (gm == nullptr) continue;
        const int row = static_cast<int>(i) / mir.rect.w + mir.rect.y;
        const int col = static_cast<int>(i) % mir.rect.w + mir.rect.x;
        if (row < 0 || col < 0 || row >= size || col >= size) continue;
        const float in[3] = {static_cast<float>(col) - halfMap, static_cast<float>(row) - halfMap, 0.0f};
        float world[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        map->GetTerrainPos(world, in, '\0', nullptr);
        const float yaw = ((row + col) & 1) ? 0.0f : 90.0f;
        void* mem = System::MemoryAllocator::MemAlloc(0x120, 8);
        if (mem == nullptr) break;
        Graphics::MeshInstance* mesh = ::new (mem) Graphics::MeshInstance();
        mesh->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(gm));
        const Math::Vector3 at(world[0], world[1], 0.0f);
        mesh->SetTransform(Math::Transform::FromYawPitchOffset(yaw, 0.0f, at));
        mesh->UpdateHierarchy();
        mesh->_opacity = kMirrorOpacity;
        mesh->_opacityPrev = kMirrorOpacity;
        mir.piles.push_back(mesh);
        mir.pileScene.push_back(g->m_graphicsScene->AddObject(mesh));
    }
    mir.pileGoods = goodList;
    mir.pileList = pileList;
    return true;
}

// `piles` for a structure not drawn yet (a join's replay sends the structures after): drawn with it, or
// dropped at mirrorEnd when the join's list did not name it. At most kMaxPendingPileChars between them.
struct PendingPiles {
    std::string goods;
    std::string list;
};
std::map<std::string, PendingPiles> s_pendingPiles;
size_t s_pendingPileChars = 0;
constexpr size_t kMaxPendingPileChars = 1u << 20;

void DropPendingPiles(const std::string& id) {
    auto it = s_pendingPiles.find(id);
    if (it == s_pendingPiles.end()) return;
    s_pendingPileChars -= it->second.goods.size() + it->second.list.size();
    s_pendingPiles.erase(it);
}

// A stockpile's piles (mirror_look.h) go to the neighbour as `piles`, passed on by the relay and kept
// nowhere: checked every kPilesCheckSeconds and sent when they changed, and all of them again every
// kPilesEverySeconds and when the neighbour arrives, so a town that joins later sees them too.
constexpr double kPilesCheckSeconds = 5.0;
constexpr double kPilesEverySeconds = 60.0;
std::chrono::steady_clock::time_point s_pilesCheckAt;
std::chrono::steady_clock::time_point s_pilesAllAt;
bool s_pilesAll = true;          // the next check sends every stockpile's piles
bool s_pilesTooLong = false;     // said once: a stockpile whose piles do not fit a message

// A mirror's ground, as DecalComponent::OnAdded paints the structure's: its DecalDescription's material
// for the decal state, over the footprint as MapComponent holds it ("dec": x, y, w, h, rotation, state),
// with the variant's UV rect; a little see-through. The decal slot, or -1.
int AddMirrorDecal(const ComponentDescription* desc, const Message& m, int64_t grp) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    const DecalDescription* decal = desc->FindDescription<DecalDescription>();
    std::string dec;
    if (g == nullptr || g->m_worldGeometry == nullptr || decal == nullptr || decal->_materials == nullptr ||
        !m.Get("dec", dec))
        return -1;
    long long v[6] = {0, 0, 0, 0, 0, 0};
    if (std::sscanf(dec.c_str(), "%lld,%lld,%lld,%lld,%lld,%lld", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return -1;
    const long long dim = TheMap() != nullptr ? TheMap()->size : 0;
    if (v[0] < 0 || v[1] < 0 || v[2] <= 0 || v[3] <= 0 || v[0] + v[2] > dim || v[1] + v[3] > dim || v[5] < 0 ||
        v[5] >= decal->_materialCount)
        return -1;
    Graphics::MaterialInstance* material = decal->_materials[v[5]];
    if (material == nullptr) return -1;
    DecalUVRect uv = {0.0f, 0.0f, 1.0f, 1.0f};
    if (grp >= 0 && grp < decal->_uvRectCount && decal->_uvRects != nullptr) uv = decal->_uvRects[grp];
    const int x = static_cast<int>(v[0]), y = static_cast<int>(v[1]);
    return g->m_worldGeometry->AddDecal(x, y, static_cast<int>(v[2]) - 1 + x, static_cast<int>(v[3]) - 1 + y,
                                        static_cast<uint8_t>(kMirrorGround * 255.0f), static_cast<uint32_t>(v[4]),
                                        material, decal->_tiled, decal->_sizeB, uv.u0, uv.v0, uv.u1, uv.v1, '\0', 0,
                                        0, decal->_flagB);
}

// Draw one of the neighbour's structures: its template's mesh, see-through, and its ground.
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
    int64_t built = 100;
    m.GetInt("built", built);
    Graphics::GraphicsMesh* gm = MirrorMesh(desc, grp, built);

    auto it = s_mirrors.find(id);
    PendingPiles keep;   // an update keeps the piles it showed
    if (it != s_mirrors.end()) {
        keep.goods = it->second.pileGoods;
        keep.list = it->second.pileList;
        FreeMirror(it->second, true);   // an update: draw it afresh
    }
    auto pending = s_pendingPiles.find(id);
    if (pending != s_pendingPiles.end()) {
        keep = pending->second;
        DropPendingPiles(id);
    }
    Mirror mir;
    if (gm != nullptr) {
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
    }
    mir.decalId = AddMirrorDecal(desc, m, grp);
    mir.join = s_joinCount;
    if (mir.decalId >= 0) ++s_applied.grounds;
    {
        int64_t tx = 0;
        int64_t ty = 0;
        int64_t tw = 0;
        int64_t th = 0;
        const bool have =
            m.GetInt("tx", tx) && m.GetInt("ty", ty) && m.GetInt("tw", tw) && m.GetInt("th", th);
        if (have && tw > 0 && th > 0 && tw <= kMaxMapDim && th <= kMaxMapDim) {
            mir.rect = TileRect{static_cast<int>(tx), static_cast<int>(ty), static_cast<int>(tw),
                                static_cast<int>(th)};
        }
    }
    s_mirrors[id] = mir;
    if (!keep.goods.empty() || !keep.list.empty()) DrawPiles(s_mirrors[id], keep.goods, keep.list);

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

// The natural resources by tile, for a neighbour's removal its tile's slots do not hold; built at most once
// a frame (s_frame, from Tick), on the first such removal.
uint64_t s_frame = 0;
uint64_t s_natIndexFrame = ~0ull;
std::unordered_multimap<int, ComponentEntity*> s_natIndex;

// Nature traffic, for the log (LogNature): what this town sent and what came of the neighbour's.
struct NatStats {
    uint64_t sentAdds = 0, sentRms = 0, sentRmsThere = 0, dropped = 0, rmMiss = 0, rmByScan = 0;
    std::map<std::string, int> missByTmpl;
};
NatStats s_nat;
std::string s_natLogged;

// The nature sync's (defined with it, before Tick): a change this town made on the neighbour's half,
// and the sync's state dropped with the game.
void MarkSyncDirty(int cell);
void ResetSync();

void Queue(const Message& m) {
    if (s_pending.size() >= kMaxPending) {
        s_pending.erase(s_pending.begin());
        ++s_nat.dropped;
    }
    if (m.type == "nat") {
        if (m.GetOr("op", "") == "add") {
            ++s_nat.sentAdds;
        } else {
            ++s_nat.sentRms;
            int64_t cell = -1;
            Map* map = TheMap();
            if (map != nullptr && s_seat >= 0 && m.GetInt("cell", cell) && cell >= 0 &&
                ((cell % map->size) < map->size / 2 ? 0 : 1) != s_seat)
                ++s_nat.sentRmsThere;
        }
    }
    s_pending.push_back(m);
    ++s_waitingAdds;
}

void LogNature() {
    std::string top;
    std::vector<std::pair<int, std::string>> misses;
    for (const auto& kv : s_nat.missByTmpl) misses.emplace_back(kv.second, kv.first);
    std::sort(misses.rbegin(), misses.rend());
    for (size_t i = 0; i < misses.size() && i < 4; ++i)
        top += (i ? ", " : "") + misses[i].second + " x" + std::to_string(misses[i].first);
    char line[600];
    snprintf(line, sizeof line,
             "sent %llu plant(s) and %llu removal(s) (%llu on the neighbor's half), %llu dropped (queue full); "
             "of the neighbor's removals %llu found off the tile's slots, %llu found nothing here%s%s%s",
             static_cast<unsigned long long>(s_nat.sentAdds), static_cast<unsigned long long>(s_nat.sentRms),
             static_cast<unsigned long long>(s_nat.sentRmsThere),
             static_cast<unsigned long long>(s_nat.dropped), static_cast<unsigned long long>(s_nat.rmByScan),
             static_cast<unsigned long long>(s_nat.rmMiss), top.empty() ? "" : " (", top.c_str(), top.empty() ? "" : ")");
    if (line == s_natLogged) return;
    s_natLogged = line;
    fprintf(stderr, "[Neighbour] nature: %s\n", line);
}

// ---- M3: roads ----------------------------------------------------------------------------------

// The template sent with a road of this type: the road tool's, or the game's own road of that type.
// A road type is CreateRoadDescription's byte, which retail's roads keep at 0 (stone) and 1 (dirt); a mod's
// roads use more (Colonial Charter's run past a dozen), the same on both sides with the same mods.
std::string RoadTemplate(uint8_t type) {
    auto it = s_roadTmpl.find(type);
    if (it != s_roadTmpl.end() && !it->second.empty()) return it->second;
    return type <= 1 ? std::string(kRoadTmplDefault[type]) : std::string();
}

// The material the finished road of this type is drawn with (WorldGeometry's batch material, which the
// save keeps): what the neighbour loads when it does not know the template (a loaded game whose road tool
// has not been used since).
std::string RoadMaterial(uint8_t type) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    WorldGeometry* wg = g != nullptr ? g->m_worldGeometry : nullptr;
    if (wg == nullptr || wg->_batchMaterials.data == nullptr || type >= wg->_batchMaterials.size) return std::string();
    return PathOf(wg->_batchMaterials.data[type]);
}

void SetRoadLook(Message& r, uint8_t type) {
    r.Set("tmpl", RoadTemplate(type));
    const std::string mat = RoadMaterial(type);
    if (!mat.empty()) r.Set("mat", mat);
}

// A finished road tile of this town's as a `road` message (ScanRoads, the relay's `want`).
Message OwnRoadMessage(size_t i, const MapCell& c) {
    Message r("road");
    r.SetInt("cell", static_cast<int64_t>(i))
     .SetInt("type", c.field_0x2A)
     .SetInt("tt", static_cast<int64_t>(c.terrainType & 0x1f));
    SetRoadLook(r, c.field_0x2A);
    return r;
}

// A loaded game's roads, sent again (ExpectReload): every road tile of this town's -- a road here on land
// the neighbour does not hold -- is claimed (a row's run of them in one rectangle), and a finished one is
// sent as a road. One being built goes when it is finished.
void ScanRoads() {
    Map* map = TheMap();
    if (!s_on || map == nullptr || !EnsureGrid()) return;
    const MapCell* cells = map->cells.GetData();
    int claimed = 0, sent = 0;
    for (int y = 0; y < s_dim; ++y) {
        int runStart = -1;
        for (int x = 0; x <= s_dim; ++x) {
            const size_t i = static_cast<size_t>(y) * s_dim + static_cast<size_t>(x);
            const bool road = x < s_dim && cells[i].field_0x2A != 0xFF && s_peer[i] == 0;
            if (road) {
                if (runStart < 0) runStart = x;
                ++claimed;
                const MapCell& c = cells[i];
                if (c.field_0x2B != 0x80) {
                    Queue(OwnRoadMessage(i, c));
                    ++sent;
                }
            } else if (runStart >= 0) {
                s_claimQueue.push_back(TileRect{runStart, y, x - runStart, 1});
                runStart = -1;
            }
        }
    }
    ++s_waitingAdds;
    fprintf(stderr, "[Neighbour] shared map: the loaded town's roads again: %d tile(s) claimed, %d finished sent\n",
            claimed, sent);
}

// This town's whole list to the relay (restate.h): its structures and their stages, its finished road
// tiles, and its land -- the structures' footprints and every road tile, finished or not, on land the
// neighbour does not hold.
void SendInventory(Link* link) {
    Map* map = TheMap();
    if (map == nullptr || !EnsureGrid()) return;
    std::vector<std::string> builds;
    std::vector<std::string> roads;
    std::vector<std::string> land;
    builds.reserve(s_mine.size());
    for (const auto& kv : s_mine) {
        int64_t built = 0;
        kv.second.build.GetInt("built", built);
        builds.push_back(kv.first + ":" + std::to_string(built));
        const TileRect& r = kv.second.rect;
        if (r.w > 0 && r.h > 0)
            land.push_back(std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," +
                           std::to_string(r.h));
    }
    const size_t n = static_cast<size_t>(s_dim) * s_dim;
    const MapCell* cells = map->cells.GetData();
    std::vector<uint8_t> roadLand(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (cells[i].field_0x2A == 0xFF || s_peer[i] != 0) continue;
        roadLand[i] = 1;
        if (cells[i].field_0x2B != 0x80)
            roads.push_back(std::to_string(i) + ":" + std::to_string(cells[i].field_0x2A));
    }
    const size_t structures = land.size();
    for (std::string& run : Restate::RowRuns(roadLand, s_dim)) land.push_back(std::move(run));
    const std::vector<Restate::Part> parts = Restate::Pack(builds, roads, land, kInvChars);
    ++s_invGen;
    for (size_t i = 0; i < parts.size(); ++i) {
        Message m("inv");
        m.SetInt("gen", static_cast<int64_t>(s_invGen)).SetInt("part", static_cast<int64_t>(i));
        if (i == 0) m.SetInt("dim", s_dim);   // the relay reads a road cell's tile with it
        if (!parts[i].builds.empty()) m.Set("b", parts[i].builds);
        if (!parts[i].roads.empty()) m.Set("r", parts[i].roads);
        if (!parts[i].land.empty()) m.Set("l", parts[i].land);
        if (i + 1 == parts.size()) m.SetBool("last", true);
        link->Send(m);
    }
    if (s_invLoggedJoin != s_joinCount) {   // the first list of each join: once a minute would be noise
        s_invLoggedJoin = s_joinCount;
        fprintf(stderr, "[Neighbour] shared map: this town's list to the relay: %zu structure(s), "
                        "%zu finished road tile(s), land in %zu footprint(s) and %zu road run(s), %zu part(s)\n",
                builds.size(), roads.size(), structures, land.size() - structures, parts.size());
    }
}

// The list is due (a little after the join, then once a minute) once this town's own changes have gone: a
// list sent while they still wait would only have the relay ask for them again.
void InvTick(Link* link) {
    if (!s_invScheduled) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < s_invDue) return;
    const bool waiting = !s_claimQueue.empty() || !s_releaseQueue.empty() || !s_buildQueue.empty() ||
                         !s_unbuildQueue.empty() || !s_pending.empty() || link->Queued() > kLinkLowWater;
    if (waiting && std::chrono::duration<double>(now - s_invDue).count() < kInvMaxWaitSeconds) return;
    SendInventory(link);
    s_invDue = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(kInvEverySeconds));
}

// The relay's record lacks these of ours, or holds them at another stage or road type (`want`): sent again.
void OnWant(const Message& m) {
    Map* map = TheMap();
    if (map == nullptr || !EnsureGrid()) return;
    size_t builds = 0;
    size_t roads = 0;
    for (const std::string& id : Restate::WantedIds(m.GetOr("b", ""))) {
        if (s_mine.count(id) == 0) continue;   // gone since the list: its unbuild is on the way
        if (s_refusedIds.count(id) > 0) continue;
        s_buildQueue.push_back(id);
        ++builds;
    }
    const MapCell* cells = map->cells.GetData();
    for (int cell : Restate::WantedCells(m.GetOr("r", ""), s_dim * s_dim)) {
        const MapCell& c = cells[cell];
        if (c.field_0x2A == 0xFF || c.field_0x2B == 0x80 || s_peer[static_cast<size_t>(cell)] != 0) continue;
        Queue(OwnRoadMessage(static_cast<size_t>(cell), c));
        ++roads;
    }
    if (builds + roads == 0) return;
    ++s_waitingAdds;
    fprintf(stderr, "[Neighbour] shared map: the relay's record lacked %zu structure(s) and %zu road tile(s) "
                    "of this town's: sent again\n",
            builds, roads);
}

// The join's replay of the neighbour's roads is in (`roadsEnd`): a road of theirs this copy still holds and
// the replay did not name was taken up while the line was down, and its removal never came. Only when every
// replayed tile arrived (the count says so). The neighbour's roads are the road tiles on land it holds, as
// ScanRoads tells this town's own from theirs.
void TakeUpStaleRoads(int64_t count) {
    Map* map = TheMap();
    if (map == nullptr || !EnsureGrid()) return;
    if (count < 0 || static_cast<uint64_t>(count) != s_roadsReplayed) {
        fprintf(stderr, "[Neighbour] shared map: the relay replayed %lld of the neighbor's road tile(s) "
                        "and %llu arrived: none taken up\n",
                static_cast<long long>(count), static_cast<unsigned long long>(s_roadsReplayed));
        return;
    }
    const size_t n = static_cast<size_t>(s_dim) * s_dim;
    const MapCell* cells = map->cells.GetData();
    std::vector<uint8_t> road(n, 0);
    for (size_t i = 0; i < n; ++i) road[i] = (cells[i].field_0x2A != 0xFF && cells[i].field_0x2B != 0x80) ? 1 : 0;
    const std::vector<int> stale =
        Restate::StaleRoads(s_peer, road, s_roadSeen, static_cast<uint32_t>(s_joinCount));
    if (stale.empty()) return;
    s_applying = true;
    for (int cell : stale) {
        map->FinishRoadCell(cell, 0, 2);
        ++s_applied.unroads;
    }
    s_applying = false;
    fprintf(stderr, "[Neighbour] shared map: %zu of the neighbor's road tile(s) taken up while away removed\n",
            stale.size());
}

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
    // The neighbour holds a road here (this join's roadsEnd keeps it).
    if (!off && static_cast<size_t>(cell) < s_roadSeen.size())
        s_roadSeen[static_cast<size_t>(cell)] = static_cast<uint32_t>(s_joinCount);
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
    // The same road already here (a replay after a load, or a re-send after a reconnect): nothing to do.
    if (type >= 0 && type <= 254 && c.field_0x2A == static_cast<uint8_t>(type) && c.field_0x2B != 0x80) {
        s_applying = false;
        return;
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
        if (decal == nullptr) {   // a template this game does not know: the material itself
            const std::string mat = m.GetOr("mat", "");
            ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
            if (!mat.empty() && mat.size() <= 200 && f != nullptr) {
                Graphics::MaterialInstance* mi = f->FindOrLoadAs<Graphics::MaterialInstance>(System::Filename(mat.c_str()));
                if (::Object::IsDerivedFromNullSafe(mi, Graphics::MaterialInstance::TypeId())) decal = mi;
            }
        }
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

enum class Planted { kYes, kTaken, kTakenByNature, kNoTemplate, kNotAPlant };

// A plant of `tmpl` on a free tile, as the game's spreading puts a sapling there
// (GrowthComponent::SpawnSubResource): grown to `progress` (0 a sapling, 1 full size), with the model
// variant grp / var when they are known (-1: the template's own roll).
Planted PlantAt(int cell, const std::string& tmpl, int64_t grp, int64_t var, float progress,
                ComponentEntity** made = nullptr) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    if (g == nullptr || map == nullptr || g->m_entityList == nullptr || cell < 0 ||
        cell >= map->size * map->size)
        return Planted::kNoTemplate;
    const MapCell& c = map->cells.GetData()[cell];
    if (c.occupant.Get() != nullptr || c.blocker.Get() != nullptr || c.footprintCount != 0 ||
        c.field_0x2A != 0xFF) {
        // Held by natural resources and nothing else: one may be on its way out.
        ComponentEntity* occupant = c.occupant.Get();
        ComponentEntity* blocker = c.blocker.Get();
        const bool occupantNature =
            occupant == nullptr || occupant->FindComponent<NaturalResourceComponent>() != nullptr;
        const bool blockerNature =
            blocker == nullptr || blocker->FindComponent<NaturalResourceComponent>() != nullptr;
        const bool nature = occupantNature && blockerNature && c.footprintCount == 0 && c.field_0x2A == 0xFF;
        return nature ? Planted::kTakenByNature : Planted::kTaken;
    }
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr) return Planted::kNoTemplate;
    ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
    if (!::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())) return Planted::kNoTemplate;
    void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
    if (mem == nullptr) return Planted::kNoTemplate;
    ComponentEntity* ent = ::new (mem) ComponentEntity(desc);
    Object::InternalPtr<ComponentEntity> ibp(ent);
    NaturalResourceComponent* nr = ent->FindComponent<NaturalResourceComponent>();
    GrowthComponent* growth = ent->FindComponent<GrowthComponent>();
    if (nr == nullptr || growth == nullptr) return Planted::kNotAPlant;   // the pointer frees it
    s_applying = true;
    nr->PlaceAtCell(cell);
    growth->SetProgress(progress);
    if (grp >= 0 && grp < 64 && var >= 0 && var < 64)
        if (ModelComponent* model = ent->FindComponent<ModelComponent>())
            model->SelectMesh(static_cast<uint16_t>(grp), static_cast<int16_t>(var));
    g->m_entityList->RegisterEntity(&ibp, static_cast<int>(desc->_category));
    s_applying = false;
    if (made != nullptr) *made = ibp.Get();
    return Planted::kYes;
}

// The natural resources by their tile, built once a frame when needed (a load's replay brings thousands of
// changes at once) and kept up to date with what this frame plants.
void EnsureNatIndex() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (s_natIndexFrame == s_frame || g == nullptr || g->m_entityList == nullptr) return;
    s_natIndexFrame = s_frame;
    s_natIndex.clear();
    const auto& all = g->m_entityList->masterArray;
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
        const int at = CellOf(e);
        if (at >= 0) s_natIndex.emplace(at, e);
    }
}

// A natural resource of `tmpl` whose origin is `cell`, not on its way out.
ComponentEntity* StandingNatural(int cell, const std::string& tmpl) {
    EnsureNatIndex();
    const auto range = s_natIndex.equal_range(cell);
    for (auto it = range.first; it != range.second; ++it) {
        ComponentEntity* e = it->second;
        if (!((e->_entityFlags >> 10) & 1) && PathOf(e->_description) == tmpl) return e;
    }
    return nullptr;
}

void ApplyNat(const Message& m) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    int64_t cell = -1;
    std::string op, tmpl;
    if (g == nullptr || map == nullptr || g->m_entityList == nullptr || !m.GetInt("cell", cell) || cell < 0 ||
        cell >= static_cast<int64_t>(map->size) * map->size || !m.Get("op", op) || !m.Get("tmpl", tmpl))
        return;
    // `replay`: the relay's whole record, sent again after a load (the change may be in this copy already).
    bool replay = false;
    m.GetBool("replay", replay);
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
        // Not in the tile's occupant / blocker slots (a plant that does not hold its tile, a wild herb):
        // look for it among the natural resources, indexed by tile once a frame (a load's replay brings
        // thousands of removals at once, most of them for plants this copy no longer has).
        EnsureNatIndex();
        const auto range = s_natIndex.equal_range(static_cast<int>(cell));
        for (auto it = range.first; it != range.second; ++it) {
            ComponentEntity* e = it->second;
            if (((e->_entityFlags >> 10) & 1) || PathOf(e->_description) != tmpl) continue;
            s_quiet.insert(e);
            g->m_entityList->QueueDestroy(&e);
            ++s_applied.removed;
            if (!replay) ++s_nat.rmByScan;
            return;
        }
        if (!replay) {
            ++s_nat.rmMiss;
            if (s_nat.missByTmpl.size() < 64 || s_nat.missByTmpl.count(tmpl)) ++s_nat.missByTmpl[tmpl];
        }
        return;
    }
    if (op != "add") return;
    // A replay onto a save that has the plant already: nothing to do, or a herb (which does not hold its
    // tile, so the slots cannot tell) would grow there twice. (Live, a plant is new; one that came twice
    // is the nature sync's to mend, and the index costs a walk of every entity.)
    if (replay && StandingNatural(static_cast<int>(cell), tmpl) != nullptr) return;
    int64_t grp = -1, var = -1;
    m.GetInt("grp", grp);
    m.GetInt("var", var);
    ComponentEntity* made = nullptr;
    switch (PlantAt(static_cast<int>(cell), tmpl, grp, var, 0.0f, &made)) {
    case Planted::kYes:
        ++s_applied.planted;
        if (made != nullptr && s_natIndexFrame == s_frame) s_natIndex.emplace(static_cast<int>(cell), made);
        break;
    case Planted::kTaken:
    case Planted::kTakenByNature:
        if (replay) return;   // most likely the sapling itself, grown or not
        ++s_applied.refused;
        s_lastRefusal = "a sapling on a tile that is taken";
        break;
    case Planted::kNotAPlant:
        ++s_applied.refused;
        s_lastRefusal = "a sapling of something that does not grow";
        break;
    case Planted::kNoTemplate:
        break;
    }
}

std::vector<std::string> SplitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t next = s.find(sep, at);
        out.push_back(s.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return out;
}

bool ToInt(const std::string& s, int64_t& v) {
    if (s.empty() || s.size() > 12) return false;
    char* end = nullptr;
    v = std::strtoll(s.c_str(), &end, 10);
    return end != nullptr && *end == '\0';
}

// The relay's batches (a long list in few messages; index.js sendNatBatches / sendRoads / sendRoadsOff),
// each change applied as the one-a-tile message it stands for.
void ApplyBatch(const Message& m) {
    bool replay = false;
    m.GetBool("replay", replay);
    if (m.type == "roadsOff") {
        for (const std::string& c : SplitOn(m.GetOr("cells", ""), ',')) {
            int64_t cell = 0;
            if (!ToInt(c, cell)) continue;
            Message r("road");
            r.SetInt("cell", cell).SetBool("off", true);
            if (replay) r.SetBool("replay", true);
            ApplyRoad(r);
        }
        return;
    }
    const std::vector<std::string> tmpls = SplitOn(m.GetOr("tmpls", ""), '\n');
    const std::vector<std::string> mats = SplitOn(m.GetOr("mats", ""), '\n');
    for (const std::string& entry : SplitOn(m.GetOr("list", ""), ';')) {
        const std::vector<std::string> f = SplitOn(entry, ',');
        int64_t v[5] = {0, 0, 0, -1, -1};
        bool ok = f.size() >= 3 && f.size() <= 5;
        for (size_t i = 0; ok && i < f.size(); ++i) ok = ToInt(f[i], v[i]);
        if (!ok) continue;
        if (m.type == "natBatch") {
            if (v[2] < 0 || v[2] >= static_cast<int64_t>(tmpls.size())) continue;
            Message n("nat");
            n.Set("op", v[1] == 1 ? "add" : "rm").SetInt("cell", v[0]).Set("tmpl", tmpls[static_cast<size_t>(v[2])]);
            if (f.size() == 5) n.SetInt("grp", v[3]).SetInt("var", v[4]);
            if (replay) n.SetBool("replay", true);
            ApplyNat(n);
        } else {   // roadBatch: cell, type, tt, template, material (-1 none)
            if (f.size() != 5 || v[3] < 0 || v[3] >= static_cast<int64_t>(tmpls.size())) continue;
            ++s_roadsReplayed;
            Message r("road");
            r.SetInt("cell", v[0]).SetInt("type", v[1]).SetInt("tt", v[2]).Set("tmpl", tmpls[static_cast<size_t>(v[3])]);
            if (v[4] >= 0 && v[4] < static_cast<int64_t>(mats.size())) r.Set("mat", mats[static_cast<size_t>(v[4])]);
            ApplyRoad(r);
        }
    }
}

void RemoveMirror(const std::string& id) {
    auto it = s_mirrors.find(id);
    if (it == s_mirrors.end()) return;
    FreeMirror(it->second, true);
    s_mirrors.erase(it);
    ++s_applied.unbuilt;   // counted in the "from the neighbor so far" line
}

double s_tokens = kSendBurst;
std::chrono::steady_clock::time_point s_tokensAt;

bool Spend() {
    if (s_tokens < 1.0) return false;
    s_tokens -= 1.0;
    return true;
}

// How many rects from the front of `q` fit one message: at most kMaxRects, and at most kMaxClaimTiles
// tiles between them (the relay refuses a message past either). At least one.
size_t FitOneMessage(const std::vector<TileRect>& q) {
    size_t n = 0;
    int64_t tiles = 0;
    while (n < q.size() && n < static_cast<size_t>(kMaxRects)) {
        const int64_t t = static_cast<int64_t>(q[n].w) * q[n].h;
        if (n > 0 && tiles + t > kMaxClaimTiles) break;
        tiles += t;
        ++n;
    }
    return n;
}

void SendClaims(Link* link) {
    // A reloaded town with many fields once packed 64 of them into one message: past the relay's
    // tile limit, refused whole ("bad claim"), and refused again on every join.
    while (!s_claimQueue.empty() && Spend()) {
        const size_t n = FitOneMessage(s_claimQueue);
        std::vector<TileRect> part(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        s_claimQueue.erase(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        Message c("claim");
        c.Set("id", "c" + std::to_string(++s_claimSerial)).Set("rects", EncodeRects(part));
        link->Send(c);
    }
    while (!s_releaseQueue.empty() && Spend()) {
        const size_t n = FitOneMessage(s_releaseQueue);
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

void Stop() {
    for (auto& kv : s_mirrors) FreeMirror(kv.second, true);
    s_mirrors.clear();
    End();
}

void End() {
    // The scene goes with the game: forget the mirrors without touching it (EndGame runs in the
    // loop's destructor), but free our MeshInstances.
    for (auto& kv : s_mirrors) FreeMirror(kv.second, false);
    s_mirrors.clear();
    s_mine.clear();
    s_mineIds.clear();
    s_claimQueue.clear();
    s_releaseQueue.clear();
    s_unbuildQueue.clear();
    s_buildQueue.clear();
    s_peer.clear();
    s_dim = 0;
    s_on = false;
    s_live = false;
    s_seat = -1;
    s_natIndex.clear();
    s_natIndexFrame = ~0ull;
    s_applying = false;
    s_solidRef.clear();
    s_quiet.clear();
    s_pending.clear();
    s_roadTmpl.clear();
    s_rescanRoads = false;
    s_invScheduled = false;
    s_invLoggedJoin = 0;
    s_refusedIds.clear();
    s_roadSeen.clear();
    s_roadsReplayed = 0;
    s_pendingPiles.clear();
    s_pendingPileChars = 0;
    s_livestock.clear();
    s_pilesAll = true;
    s_pilesTooLong = false;
    s_applied = Applied();
    s_appliedLogged = Applied();
    s_tokens = kSendBurst;
    s_nat = NatStats();
    s_natLogged.clear();
    ResetSync();
}

bool PeerHolds(int x, int y) {
    if (!s_on || s_dim == 0 || x < 0 || y < 0 || x >= s_dim || y >= s_dim) return false;
    return s_peer[static_cast<size_t>(y) * s_dim + x] != 0;
}

int PathTerrain(uint32_t cell, int terrainType) {
    if (!s_on || cell >= s_solidRef.size() || s_solidRef[cell] == 0) return terrainType;
    return kBlockedType;
}

bool PeerHoldsCell(uint32_t cellIdx) {
    if (!s_on || s_dim == 0) return false;
    return PeerHolds(static_cast<int>(cellIdx % static_cast<uint32_t>(s_dim)),
                     static_cast<int>(cellIdx / static_cast<uint32_t>(s_dim)));
}

namespace {
int s_pieceDepth = 0;   // NoteStructure reporting a structure's pieces (no line of their own)
}  // namespace

void NoteStructure(ComponentEntity* entity) {
    if (!s_on || entity == nullptr || entity->_description == nullptr) return;
    MapComponent* mc = entity->FindComponent<MapComponent>();
    if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) return;
    // MapComponent holds the footprint as it lies on the map: tileX1 / tileY1 are its width and height
    // after the rotation (the tile tool stores x1 - x0 + 1, the start town the swapped size, and the
    // game's own unmark walks w x h as stored). No second swap here: one turned a 90 / 270 degree
    // structure's claim, solid tiles and tree clearing on their side.
    const TileRect r{mc->tileX0, mc->tileY0, mc->tileX1, mc->tileY1};
    if (r.x < 0 || r.y < 0) return;
    const int rot = ((mc->extraField % 360) + 360) % 360;
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
    // Its ground, as DecalComponent::OnAdded paints it: MapComponent's footprint as it holds it, its
    // rotation, the decal state.
    if (DecalComponent* decal = entity->FindComponent<DecalComponent>()) {
        b.Set("dec", std::to_string(mc->tileX0) + "," + std::to_string(mc->tileY0) + "," + std::to_string(mc->tileX1) +
                         "," + std::to_string(mc->tileY1) + "," + std::to_string(mc->extraField) + "," +
                         std::to_string(decal->decalState));
    }
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
    mine.display = entity->FindComponent<StorageDisplayComponent>() != nullptr;
    auto old = s_mine.find(id);
    if (old != s_mine.end() && old->second.entity.Get() != entity) s_mineIds.erase(old->second.entity.Get());
    const bool placed = old == s_mine.end();
    if (placed) {
        mine.placedInPlay = s_live;
        mine.placedAt = std::chrono::steady_clock::now();
    } else {
        mine.placedInPlay = old->second.placedInPlay;
        mine.placedAt = old->second.placedAt;
    }
    s_mine[id] = mine;
    s_mineIds[entity] = id;
    s_claimQueue.push_back(r);
    s_buildQueue.push_back(id);
    ++s_waitingAdds;
    if (placed && s_live && s_pieceDepth == 0)   // in play (a load's own scan has a line of its own)
        fprintf(stderr, "[Neighbour] shared map: this town placed %s at tile %d,%d (%d x %d); its land is claimed\n",
                tmpl.c_str(), r.x, r.y, r.w, r.h);
    // A structure made of several (MultiObjectComponent: a bridge, a mod's walkway or pier) is drawn as
    // its pieces, each an entity of its own with its model and footprint, while the whole has no mesh to
    // show. The bridge tool registers the pieces first and reports only the whole, so each piece is
    // reported here too (the fence tool reports its pieces itself: they are known already). A piece goes
    // with the whole (MultiObjectComponent::OnRemove removes every child; NoteRemoved sees each).
    if (MultiObjectComponent* multi = entity->FindComponent<MultiObjectComponent>()) {
        int pieces = 0;
        ++s_pieceDepth;
        for (int i = 0; i < multi->children._size; ++i) {
            ComponentEntity* piece = multi->children._data[i].Get();
            if (piece == nullptr || piece == entity || s_mineIds.count(piece) != 0) continue;
            NoteStructure(piece);
            ++pieces;
        }
        --s_pieceDepth;
        if (pieces > 0 && placed && s_live && s_pieceDepth == 0)
            fprintf(stderr, "[Neighbour] shared map: %d piece(s) of it reported with it\n", pieces);
    }
}

void ScanExisting() {
    if (!s_on) return;
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_entityList == nullptr) return;
    const auto& all = g->m_entityList->masterArray;
    int n = 0, skipped = 0;
    std::map<std::string, int> kinds;   // what the scan saw, by template and the components that matter
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr) continue;
        MapComponent* mc = e->FindComponent<MapComponent>();
        if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) continue;
        if (e->FindComponent<NaturalResourceComponent>() != nullptr || e->FindComponent<CitizenComponent>() != nullptr) continue;
        // On the map but not a structure, and never reported in play: the removal tool's marks (a tile
        // each, drawn as an orange cross on the neighbour's ground), goods lying on the ground, animals.
        const bool clearRes = e->FindComponent<ClearResourceComponent>() != nullptr;
        const bool clearArea = e->FindComponent<ClearAreaComponent>() != nullptr;
        const bool raw = e->FindComponent<RawMaterialComponent>() != nullptr;
        const bool animal = e->FindComponent<AnimalComponent>() != nullptr;
        const std::string what = PathOf(e->_description) + (clearRes ? " clearRes" : "") + (clearArea ? " clearArea" : "") +
                                 (raw ? " raw" : "") + (animal ? " animal" : "");
        ++kinds[what];
        if (clearRes || raw || animal) {
            ++skipped;
            continue;
        }
        if (s_mineIds.count(e) != 0) continue;   // a piece its whole reported (NoteStructure)
        NoteStructure(e);
        ++n;
    }
    for (const auto& kv : kinds) fprintf(stderr, "[Neighbour] shared map: scan: %d x %s\n", kv.second, kv.first.c_str());
    fprintf(stderr, "[Neighbour] shared map: %d structure(s) of the loaded town reported (%d other thing(s) on the map "
                    "left out)\n", n, skipped);
}

void NoteRoadCell(uint32_t cellIdx) {
    if (!s_on || !EnsureGrid()) return;
    s_claimQueue.push_back(TileRect{static_cast<int>(cellIdx % static_cast<uint32_t>(s_dim)),
                                    static_cast<int>(cellIdx / static_cast<uint32_t>(s_dim)), 1, 1});
    ++s_waitingAdds;
}

void NoteRoadTemplate(uint8_t roadType, const ComponentDescription* tool) {
    if (!s_on) return;
    s_roadTmpl[roadType] = PathOf(tool);
}

void NoteRoadFinished(int cellIdx, uint32_t terrainType) {
    if (!s_on || !s_live || s_applying || !EnsureGrid() || cellIdx < 0 || cellIdx >= s_dim * s_dim) return;
    Map* map = TheMap();
    const MapCell& c = map->cells.GetData()[cellIdx];
    Message r("road");
    r.SetInt("cell", cellIdx);
    if (terrainType == 2) {
        if (PeerHoldsCell(static_cast<uint32_t>(cellIdx))) return;   // the neighbour's road: not ours to take up
        r.SetBool("off", true);
    } else {
        if (c.field_0x2A == 0xFF) return;   // no road here after all
        r.SetInt("type", c.field_0x2A).SetInt("tt", static_cast<int64_t>(terrainType & 0x1f));
        SetRoadLook(r, c.field_0x2A);
    }
    Queue(r);
}

void NoteRemoved(ComponentEntity* entity) {
    if (!s_on || entity == nullptr) return;
    s_livestock.erase(entity);
    // A structure of ours is gone (demolished, burnt, a site cancelled): the neighbour takes it off its
    // map and its land is free again. Dropped here, before the free, never looked at again.
    auto ours = s_mineIds.find(entity);
    if (ours != s_mineIds.end()) {
        auto it = s_mine.find(ours->second);
        if (it != s_mine.end() && it->second.entity.Get() == entity) {
            const TileRect& r = it->second.rect;
            fprintf(stderr, "[Neighbour] shared map: this town removed %s at tile %d,%d (%d x %d); its land is freed\n",
                    it->second.build.GetOr("tmpl", "?").c_str(), r.x, r.y, r.w, r.h);
            s_unbuildQueue.push_back(it->first);
            s_releaseQueue.push_back(it->second.rect);
            ++s_waitingAdds;
            s_mine.erase(it);
        }
        s_mineIds.erase(ours);
        return;
    }
    auto q = s_quiet.find(entity);
    if (q != s_quiet.end()) { s_quiet.erase(q); return; }
    if (!s_live || entity->FindComponent<NaturalResourceComponent>() == nullptr) return;
    const int cell = CellOf(entity);
    const std::string tmpl = PathOf(entity->_description);
    if (cell < 0 || tmpl.empty()) return;
    Message n("nat");
    n.Set("op", "rm").SetInt("cell", cell).Set("tmpl", tmpl);
    Queue(n);
    MarkSyncDirty(cell);
}

void NotePlanted(ComponentEntity* entity) {
    if (!s_on || !s_live || s_applying || entity == nullptr) return;
    const int cell = CellOf(entity);
    const std::string tmpl = PathOf(entity->_description);
    if (cell < 0 || tmpl.empty()) return;
    Message n("nat");
    n.Set("op", "add").SetInt("cell", cell).Set("tmpl", tmpl);
    // Its look (the model variant a spreading plant rolls), so both copies draw the same tree.
    if (ModelComponent* model = entity->FindComponent<ModelComponent>(); model != nullptr && model->meshIndex >= 0)
        n.SetInt("grp", model->meshGroup).SetInt("var", model->meshIndex);
    Queue(n);
    MarkSyncDirty(cell);
}

bool DecidesNature(int x, int y) {
    if (!s_on || !s_live) return true;
    Map* map = TheMap();
    if (map == nullptr || x < 0 || y < 0 || x >= map->size || y >= map->size) return true;
    const uint32_t cell = static_cast<uint32_t>(y * map->size + x);
    if (PeerHoldsCell(cell)) return false;                                // the neighbour's land: its crops
    if (map->cells.GetData()[cell].footprintCount != 0) return true;   // this town's own structure
    if (s_seat < 0) return false;
    return (x < map->size / 2 ? 0 : 1) == s_seat;
}

void SetSeat(int seat) {
    s_seat = (seat == 0 || seat == 1) ? seat : -1;
}

bool SpreadsHere(int x, int y) {
    if (!s_on || !s_live) return true;
    Map* map = TheMap();
    if (map == nullptr || x < 0 || y < 0 || x >= map->size || y >= map->size) return true;
    if (s_seat < 0) return false;
    if (PeerHoldsCell(static_cast<uint32_t>(y * map->size + x))) return false;
    return (x < map->size / 2 ? 0 : 1) == s_seat;
}

// Drift check: an order-free fingerprint of the land both copies must agree on -- every natural
// resource (tile, template) and every finished road tile (tile, type). Logged when it changes, at most
// every kFingerprintFrames; two paused games on the same land print the same numbers.
constexpr uint64_t kFingerprintFrames = 1800;
std::string s_fingerprintLogged;

uint64_t Mix(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

void LogFingerprint() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    if (g == nullptr || g->m_entityList == nullptr || map == nullptr) return;
    uint64_t natSum = 0, roadSum = 0, halfSum[2] = {0, 0};
    size_t nat = 0, roads = 0, half[2] = {0, 0};
    const auto& all = g->m_entityList->masterArray;
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr || ((e->_entityFlags >> 10) & 1) || e->FindComponent<NaturalResourceComponent>() == nullptr)
            continue;
        const int cell = CellOf(e);
        if (cell < 0) continue;
        uint64_t th = 1469598103934665603ull;
        for (unsigned char c : PathOf(e->_description)) th = (th ^ c) * 1099511628211ull;
        const uint64_t h = Mix(th ^ (static_cast<uint64_t>(cell) << 1));
        natSum += h;
        ++nat;
        const int side = (cell % map->size) < map->size / 2 ? 0 : 1;
        halfSum[side] += h;
        ++half[side];
    }
    const MapCell* cells = map->cells.GetData();
    const int n = map->size * map->size;
    for (int i = 0; i < n; ++i) {
        if (cells[i].field_0x2A == 0xFF || cells[i].field_0x2B == 0x80) continue;
        roadSum += Mix((static_cast<uint64_t>(i) << 8) | cells[i].field_0x2A);
        ++roads;
    }
    char line[240];
    snprintf(line, sizeof line, "nature %zu (%016llx) [west %zu %08llx, east %zu %08llx], roads %zu (%016llx)", nat,
             static_cast<unsigned long long>(natSum), half[0], static_cast<unsigned long long>(halfSum[0] >> 32), half[1],
             static_cast<unsigned long long>(halfSum[1] >> 32), roads, static_cast<unsigned long long>(roadSum));
    if (line == s_fingerprintLogged) return;
    s_fingerprintLogged = line;
    fprintf(stderr, "[Neighbour] map fingerprint: %s\n", line);
}

// ---- Nature sync (nature_sync.h): the half this town does not own, checked against its owner ------
// A round: this town asks the owner for its half's digest (natSyncReq), compares it with its own copy,
// asks for the blocks that differ (natBlockReq) and makes them match the owner's lists (natBlock). Its
// requests go in the same queue as its own changes and the owner answers in the same queue as its own,
// so each side has the other's earlier changes before it counts; a block this town changes after it
// asks, or holds a resource of its own on the way out (felled, not yet freed), waits for the next round.
namespace {

constexpr double kSyncFirstSeconds = 20.0;    // after the neighbour arrives: its replays land first
constexpr double kSyncEverySeconds = 45.0;
constexpr double kSyncGiveUpSeconds = 90.0;   // an answer that never comes (the neighbour left, a throttle)
constexpr double kSyncAnswerSeconds = 5.0;    // the owner answers one round at most this often
constexpr size_t kSyncChars = 11000;          // a sync message's lists, well under kMaxMessageBytes

struct SyncRound {
    int64_t id = 0;
    int     stage = 0;   // 0 none, 1 waiting for the digest, 2 waiting for the blocks
    std::chrono::steady_clock::time_point sentAt;
    std::map<int, NatureSync::BlockSum> digest;   // the owner's, as its lists come
    std::map<int, NatureSync::BlockSum> ownerSums;   // the owner's sum of each block asked for
    std::set<int> asked;                          // blocks still to come
    std::map<int, std::vector<NatureSync::Plant>> partial;
    int blocks = 0, differed = 0, skipped = 0, added = 0, removed = 0, waiting = 0, blocked = 0;
    std::map<std::string, int> blockedByTmpl;
};
SyncRound s_round;
std::set<int> s_syncDirty;   // blocks of the neighbour's half this town changed since its last request
// Blocks this copy cannot make match (a rock or deposit it has lost cannot be put back; a tile under one
// of this town's structures takes no plant): the owner's sum and this copy's when it was settled. Not
// asked for again while both stay so.
std::map<int, NatureSync::Settled> s_syncSettled;
int64_t s_syncSerial = 0;
bool s_peerOnline = false;
std::chrono::steady_clock::time_point s_syncNext;
bool s_syncScheduled = false;
std::string s_syncLogged;
// The owner's side: when it last answered a round, and which.
std::chrono::steady_clock::time_point s_syncAnsweredAt;
int64_t s_syncAnsweredId = -1;

const char* HalfName(int half) {
    return half == 0 ? "west" : "east";
}

NatureSync::Plant PlantOf(ComponentEntity* e, int cell) {
    NatureSync::Plant p;
    p.cell = cell;
    p.tmpl = PathOf(e->_description);
    if (ModelComponent* model = e->FindComponent<ModelComponent>(); model != nullptr && model->meshIndex >= 0) {
        p.grp = model->meshGroup;
        p.var = model->meshIndex;
    }
    NaturalResourceComponent* nr = e->FindComponent<NaturalResourceComponent>();
    GrowthComponent* growth = e->FindComponent<GrowthComponent>();
    if (nr != nullptr && growth != nullptr && growth->growthScale > 0.0f) {
        const float f = std::clamp(nr->resourceScale / growth->growthScale, 0.0f, 50.0f);
        p.prog = static_cast<int>(std::lround(f * 1000.0f));
    }
    return p;
}

// The tiles this town's structures claim, built once a frame for CollectHalf.
const std::vector<uint8_t>& OwnHeld(Map* map) {
    static std::vector<uint8_t> grid;
    static uint64_t builtFrame = ~0ull;
    const size_t n = static_cast<size_t>(map->size) * map->size;
    if (builtFrame == s_frame && grid.size() == n) return grid;
    builtFrame = s_frame;
    grid.assign(n, 0);
    for (const auto& kv : s_mine) {
        const TileRect& r = kv.second.rect;
        for (int y = std::max(0, r.y); y < std::min(map->size, r.y + r.h); ++y)
            for (int x = std::max(0, r.x); x < std::min(map->size, r.x + r.w); ++x)
                grid[static_cast<size_t>(y) * map->size + x] = 1;
    }
    return grid;
}

// This copy's natural resources on one half, outside structures: its digest, the plants and entities of
// `blocks`, and the blocks holding a resource this town's own game is taking away (flagged, not yet
// freed: its removal is not sent yet).
void CollectHalf(int half, std::map<int, NatureSync::BlockSum>* sums, const std::set<int>* blocks,
                 std::map<int, std::vector<NatureSync::Plant>>* plants,
                 std::map<int, std::vector<ComponentEntity*>>* ents, std::set<int>* busy) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    if (g == nullptr || g->m_entityList == nullptr || map == nullptr) return;
    const std::vector<uint8_t>& ownHeld = OwnHeld(map);
    const auto& all = g->m_entityList->masterArray;
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr || e->FindComponent<NaturalResourceComponent>() == nullptr) continue;
        const int cell = CellOf(e);
        if (cell < 0 || NatureSync::HalfOf(cell, map->size) != half) continue;
        // A tile either town's structure claims (a field's crops, an orchard's trees) is that town's to
        // keep, by its own changes, not the half's owner's: left out of the sync on both sides (this
        // town's structures' rectangles are what the neighbour holds as PeerHolds, with its roads, which
        // grow nothing; a claim on its way may differ for a round).
        if (ownHeld[static_cast<size_t>(cell)] || PeerHoldsCell(static_cast<uint32_t>(cell))) continue;
        if ((e->_entityFlags >> 10) & 1) {
            if (busy != nullptr && s_quiet.count(e) == 0) busy->insert(NatureSync::BlockOf(cell, map->size));
            continue;
        }
        if (sums != nullptr) NatureSync::AddTo(*sums, map->size, cell, PathOf(e->_description));
        if (blocks == nullptr) continue;
        const int block = NatureSync::BlockOf(cell, map->size);
        if (blocks->count(block) == 0) continue;
        if (plants != nullptr) (*plants)[block].push_back(PlantOf(e, cell));
        if (ents != nullptr) (*ents)[block].push_back(e);
    }
}

void MarkSyncDirty(int cell) {
    Map* map = TheMap();
    if (map == nullptr || s_seat < 0 || cell < 0 || NatureSync::HalfOf(cell, map->size) == s_seat) return;
    s_syncDirty.insert(NatureSync::BlockOf(cell, map->size));
}

void ResetSync() {
    s_round = SyncRound();
    s_syncDirty.clear();
    s_syncSettled.clear();
    s_peerOnline = false;
    s_syncScheduled = false;
    s_syncLogged.clear();
    s_syncAnsweredId = -1;
    // Round ids from the clock: an owner's answer to a round of an earlier run of this game never
    // matches one of this run.
    s_syncSerial = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count() &
                                        0x3FFFFFFFFFll) << 8;
}

void ScheduleSync(double seconds) {
    const auto wait = std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
    s_syncNext = std::chrono::steady_clock::now() + wait;
    s_syncScheduled = true;
}

void LogSync(const std::string& line) {
    if (line == s_syncLogged) return;
    s_syncLogged = line;
    fprintf(stderr, "[Neighbour] nature sync: %s\n", line.c_str());
}

void FinishRound() {
    const SyncRound& r = s_round;
    const int half = 1 - s_seat;
    if (r.differed == 0 && r.skipped == 0) {
        const std::string settled =
            s_syncSettled.empty() ? "" : ", " + std::to_string(s_syncSettled.size()) + " settled as they are";
        LogSync(std::string("the neighbor's ") + HalfName(half) + " half matches here (" +
                std::to_string(r.blocks) + " block(s) with nature" + settled + ")");
    } else {
        std::string top;
        std::vector<std::pair<int, std::string>> worst;
        for (const auto& kv : r.blockedByTmpl) worst.emplace_back(kv.second, kv.first);
        std::sort(worst.rbegin(), worst.rend());
        for (size_t i = 0; i < worst.size() && i < 3; ++i)
            top += (i ? ", " : "") + worst[i].second + " x" + std::to_string(worst[i].first);
        char line[800];
        snprintf(line, sizeof line,
                 "the neighbor's %s half differed here in %d of %d block(s) (%d left for the next round: "
                 "changed here meanwhile); mended: %d plant(s) added, %d removed, %d wait for their tile to "
                 "clear, %d cannot be put here%s%s%s",
                 HalfName(half), r.differed + r.skipped, r.blocks, r.skipped, r.added, r.removed, r.waiting,
                 r.blocked,
                 top.empty() ? "" : " (", top.c_str(), top.empty() ? "" : ")");
        LogSync(line);
    }
    s_round = SyncRound();
    ScheduleSync(kSyncEverySeconds);
}

// A request of this round still waiting in the queue (a long backlog): not yet unanswered.
bool RequestQueued() {
    for (const Message& m : s_pending) {
        if (m.type != "natSyncReq" && m.type != "natBlockReq") continue;
        int64_t id = 0;
        if (m.GetInt("id", id) && id == s_round.id) return true;
    }
    return false;
}

void SyncTick(Link* link) {
    if (s_seat < 0 || !s_peerOnline || link == nullptr || !link->Connected()) return;
    const auto now = std::chrono::steady_clock::now();
    if (s_round.stage != 0) {
        if (std::chrono::duration<double>(now - s_round.sentAt).count() > kSyncGiveUpSeconds) {
            if (RequestQueued()) {
                s_round.sentAt = now;
                return;
            }
            LogSync("no answer from the neighbor (an older game, or it left); asking again later");
            s_round = SyncRound();
            ScheduleSync(kSyncEverySeconds);
        }
        return;
    }
    if (!s_syncScheduled) ScheduleSync(kSyncFirstSeconds);
    if (now < s_syncNext) return;
    s_round = SyncRound();
    s_round.id = ++s_syncSerial;
    s_round.stage = 1;
    s_round.sentAt = now;
    s_syncDirty.clear();
    Message q("natSyncReq");
    q.SetInt("id", s_round.id).SetInt("half", 1 - s_seat);
    Queue(q);
}

// The owner's side: this town's half as it stands, to the neighbour that asked (one round at most every
// kSyncAnswerSeconds: each answer walks every entity and fills the queue).
void AnswerDigest(const Message& m) {
    int64_t id = 0, half = -1;
    if (s_seat < 0 || !m.GetInt("id", id) || !m.GetInt("half", half) || half != s_seat) return;
    const auto now = std::chrono::steady_clock::now();
    const double sinceLast = std::chrono::duration<double>(now - s_syncAnsweredAt).count();
    if (s_syncAnsweredId >= 0 && sinceLast < kSyncAnswerSeconds) return;
    s_syncAnsweredAt = now;
    s_syncAnsweredId = id;
    std::map<int, NatureSync::BlockSum> sums;
    CollectHalf(s_seat, &sums, nullptr, nullptr, nullptr, nullptr);
    const std::vector<std::string> lists = NatureSync::EncodeDigest(sums, kSyncChars);
    for (size_t i = 0; i < lists.size(); ++i) {
        Message d("natDigest");
        d.SetInt("id", id).SetInt("half", half).Set("list", lists[i]).SetBool("end", i + 1 == lists.size());
        Queue(d);
    }
}

void AnswerBlocks(const Message& m) {
    Map* map = TheMap();
    int64_t id = 0, half = -1;
    std::vector<int> blocks;
    // Only for the round this town last sent a digest for (a long list of blocks comes in several asks).
    if (map == nullptr || s_seat < 0 || !m.GetInt("id", id) || id != s_syncAnsweredId ||
        !m.GetInt("half", half) || half != s_seat || !NatureSync::ParseInts(m.GetOr("blocks", ""), blocks))
        return;
    const int perRow = (map->size + NatureSync::kBlock - 1) / NatureSync::kBlock;
    std::set<int> want;
    std::vector<int> order;
    for (int b : blocks)
        if (b >= 0 && b < perRow * perRow && want.insert(b).second) order.push_back(b);
    std::map<int, std::vector<NatureSync::Plant>> plants;
    CollectHalf(s_seat, nullptr, &want, &plants, nullptr, nullptr);
    for (const NatureSync::BlockMessage& b : NatureSync::PackBlocks(order, plants, kSyncChars)) {
        Message out("natBlock");
        out.SetInt("id", id).SetInt("half", half).Set("tmpls", b.tmpls).Set("list", b.list).Set("done", b.done);
        Queue(out);
    }
}

// The asking side: the owner's digest, then its lists of the blocks that differ.
void TakeDigest(const Message& m) {
    int64_t id = 0, half = -1;
    bool end = false;
    if (s_round.stage != 1 || !m.GetInt("id", id) || id != s_round.id || !m.GetInt("half", half) ||
        half != 1 - s_seat)
        return;
    m.GetBool("end", end);
    if (!NatureSync::DecodeDigest(m.GetOr("list", ""), s_round.digest)) {
        LogSync("the neighbor's digest is malformed; asking again later");
        s_round = SyncRound();
        ScheduleSync(kSyncEverySeconds);
        return;
    }
    if (!end) return;
    std::map<int, NatureSync::BlockSum> here;
    std::set<int> busy;
    CollectHalf(static_cast<int>(half), &here, nullptr, nullptr, nullptr, &busy);
    std::set<int> withNature;
    for (const auto& kv : s_round.digest) withNature.insert(kv.first);
    for (const auto& kv : here) withNature.insert(kv.first);
    s_round.blocks = static_cast<int>(withNature.size());
    // Settled blocks stay out while neither copy of them changed.
    const std::set<int> settledNow = NatureSync::StillSettled(s_syncSettled, s_round.digest, here);
    const NatureSync::BlockSum none;
    std::set<int> skip = s_syncDirty;
    skip.insert(busy.begin(), busy.end());
    std::vector<int> differ;
    for (int b : NatureSync::Differing(s_round.digest, here, skip, &s_round.skipped))
        if (settledNow.count(b) == 0) differ.push_back(b);
    for (int b : differ) {
        const auto o = s_round.digest.find(b);
        s_round.ownerSums[b] = o == s_round.digest.end() ? none : o->second;
    }
    s_round.digest.clear();
    if (differ.empty()) {
        FinishRound();
        return;
    }
    s_round.differed = static_cast<int>(differ.size());
    s_round.asked.insert(differ.begin(), differ.end());
    s_round.stage = 2;
    s_round.sentAt = std::chrono::steady_clock::now();
    s_syncDirty.clear();
    // The block numbers, a few thousand characters at most on the largest map; cut anyway.
    std::vector<int> part;
    size_t chars = 0;
    for (size_t i = 0; i < differ.size(); ++i) {
        part.push_back(differ[i]);
        chars += std::to_string(differ[i]).size() + 1;
        if (chars > kSyncChars || i + 1 == differ.size()) {
            Message q("natBlockReq");
            q.SetInt("id", id).SetInt("half", half).Set("blocks", NatureSync::JoinInts(part));
            Queue(q);
            part.clear();
            chars = 0;
        }
    }
}

void TakeBlocks(const Message& m) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    int64_t id = 0, half = -1;
    if (g == nullptr || g->m_entityList == nullptr || map == nullptr || s_round.stage != 2 ||
        !m.GetInt("id", id) || id != s_round.id || !m.GetInt("half", half) || half != 1 - s_seat)
        return;
    NatureSync::BlockMessage b;
    b.tmpls = m.GetOr("tmpls", "");
    b.list = m.GetOr("list", "");
    b.done = m.GetOr("done", "");
    std::map<int, std::vector<NatureSync::Plant>> got;
    std::vector<int> done;
    if (!NatureSync::UnpackBlocks(b, map->size, got, done)) {
        LogSync("a block list from the neighbor is malformed; asking again later");
        s_round = SyncRound();
        ScheduleSync(kSyncEverySeconds);
        return;
    }
    // Only what was asked for, on the half that was asked about.
    for (auto& kv : got) {
        if (s_round.asked.count(kv.first) == 0) continue;
        std::vector<NatureSync::Plant>& into = s_round.partial[kv.first];
        for (NatureSync::Plant& p : kv.second)
            if (NatureSync::HalfOf(p.cell, map->size) == half) into.push_back(std::move(p));
    }
    std::set<int> ready;
    for (int block : done)
        if (s_round.asked.count(block)) ready.insert(block);
    std::map<int, std::vector<NatureSync::Plant>> herePlants;
    std::map<int, std::vector<ComponentEntity*>> hereEnts;
    std::set<int> busy;
    if (!ready.empty()) CollectHalf(static_cast<int>(half), nullptr, &ready, &herePlants, &hereEnts, &busy);
    for (int block : ready) {
        s_round.asked.erase(block);
        const std::vector<NatureSync::Plant> owner = std::move(s_round.partial[block]);
        s_round.partial.erase(block);
        // Changed here since the request, or a resource of this town's on its way out: the next round.
        if (s_syncDirty.count(block) || busy.count(block)) {
            ++s_round.skipped;
            --s_round.differed;
            continue;
        }
        const NatureSync::Mend mend = NatureSync::PlanMend(herePlants[block], owner);
        for (size_t idx : mend.remove) {
            ComponentEntity* e = hereEnts[block][idx];
            if ((e->_entityFlags >> 10) & 1) continue;
            s_quiet.insert(e);
            g->m_entityList->QueueDestroy(&e);
            ++s_round.removed;
        }
        bool cannot = false;
        for (const NatureSync::Plant& p : mend.add) {
            const float progress = p.prog >= 0 ? static_cast<float>(p.prog) / 1000.0f : 0.0f;
            switch (PlantAt(p.cell, p.tmpl, p.grp, p.var, progress)) {
            case Planted::kYes:
                ++s_round.added;
                continue;
            case Planted::kTakenByNature:   // another resource there, perhaps one just removed: next round
                ++s_round.waiting;
                continue;
            case Planted::kTaken:
            case Planted::kNotAPlant:
            case Planted::kNoTemplate:
                break;
            }
            cannot = true;
            ++s_round.blocked;
            if (s_round.blockedByTmpl.size() < 32 || s_round.blockedByTmpl.count(p.tmpl))
                ++s_round.blockedByTmpl[p.tmpl];
        }
        // What cannot be put here stays as it is until either copy of the block changes.
        if (cannot) {
            NatureSync::Settled st;
            st.owner = s_round.ownerSums[block];
            s_syncSettled[block] = st;
        }
    }
    s_natIndexFrame = ~0ull;   // the mend planted and removed: ApplyNat's index is built again
    if (s_round.asked.empty()) FinishRound();
}

}  // namespace

namespace {

// A stockpile's piles as StorageDisplayComponent draws them: the goods it stores (their templates), and
// for each footprint tile the good and the mesh of that good's pile model the tile shows (a mesh the
// goods do not explain, or a tile not in the scene, is empty). False when it draws no piles.
bool PilesOf(ComponentEntity* e, std::string* goodList, std::string* pileList) {
    StorageDisplayComponent* display = e->FindComponent<StorageDisplayComponent>();
    if (display == nullptr) return false;
    std::vector<std::string> goods;
    std::unordered_map<const Graphics::GraphicsMesh*, MirrorLook::Pile> meshes;
    if (StorageComponent* storage = e->FindComponent<StorageComponent>()) {
        for (int i = 0; i < storage->storageSlots._size && goods.size() < kMaxPileGoods; ++i) {
            const StorageComponent::Item& item = storage->storageSlots._data[i];
            if (item._count == 0 || item._resource == nullptr) continue;
            const ModelDescription* md = item._resource->FindDescription<ModelDescription>();
            if (md == nullptr || md->_meshGroups == nullptr || md->_meshGroupCount < 2) continue;
            const ModelMeshGroup& mg = md->_meshGroups[1];   // SetPileMesh's group
            const std::string path = PathOf(item._resource);
            const bool usable = path.size() <= 200 && path.find('|') == std::string::npos;
            if (mg.meshes == nullptr || path.empty() || !usable) continue;
            int good = -1;
            for (size_t k = 0; k < goods.size(); ++k)
                if (goods[k] == path) good = static_cast<int>(k);
            if (good < 0) {
                good = static_cast<int>(goods.size());
                goods.push_back(path);
            }
            for (int k = 0; k < mg.meshCount && k <= MirrorLook::kMaxPileMesh; ++k)
                if (mg.meshes[k] != nullptr) meshes.emplace(mg.meshes[k], MirrorLook::Pile{good, k});
        }
    }
    const int tiles = display->piles._size > 0 ? display->piles._size : 0;
    std::vector<MirrorLook::Pile> piles(static_cast<size_t>(tiles));
    for (size_t i = 0; i < piles.size(); ++i) {
        const StorageDisplayComponent::Pile& p = display->piles._data[i];
        if (p.sceneId < 0) continue;
        auto it = meshes.find(p.mesh._mesh.Get());
        if (it != meshes.end()) piles[i] = it->second;
    }
    *goodList = MirrorLook::GoodList(goods);
    *pileList = MirrorLook::PileList(piles);
    return true;
}

void ScanLivestock() {
    s_livestock.clear();
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_entityList == nullptr) return;
    const auto& all = g->m_entityList->masterArray;
    for (int i = 0; i < all._size; ++i) {
        ComponentEntity* e = all._data[i].Get();
        if (e == nullptr || ((e->_entityFlags >> 10) & 1)) continue;   // on its way out
        if (e->FindComponent<LivestockComponent>() != nullptr) s_livestock.insert(e);
    }
}

void PilesTick(Link* link) {
    if (!s_peerOnline) {
        s_pilesAll = true;   // the neighbour comes back to all of them
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - s_pilesCheckAt).count() < kPilesCheckSeconds) return;
    s_pilesCheckAt = now;
    if (std::chrono::duration<double>(now - s_pilesAllAt).count() >= kPilesEverySeconds) s_pilesAll = true;
    bool allSent = true;
    for (auto& kv : s_mine) {
        Mine& mine = kv.second;
        ComponentEntity* e = mine.entity.Get();
        if (!mine.display || e == nullptr) continue;
        std::string goods, list;
        if (!PilesOf(e, &goods, &list)) continue;
        const std::string sig = goods + "\n" + list;
        if (!s_pilesAll && sig == mine.pilesSent) continue;
        if (goods.size() + list.size() > kMaxPileChars) {
            if (!s_pilesTooLong) {
                s_pilesTooLong = true;
                fprintf(stderr, "[Neighbour] shared map: %s's piles do not fit a message (%zu chars); "
                                "not sent\n", kv.first.c_str(), goods.size() + list.size());
            }
            continue;
        }
        if (!Spend()) {   // the rest at the next check
            allSent = false;
            break;
        }
        Message p("piles");
        p.Set("id", kv.first).Set("goods", goods).Set("list", list);
        link->Send(p, /*ephemeral=*/true);
        // Taken as sent: an ephemeral message the link drops (a line going down) is put right by the
        // next full round (kPilesEverySeconds, or the neighbour arriving again).
        mine.pilesSent = sig;
    }
    if (s_pilesAll && allSent) {
        s_pilesAll = false;
        s_pilesAllAt = now;
    }
}

void OnPiles(const Message& m) {
    std::string id, goods, list;
    if (!m.Get("id", id) || !m.Get("goods", goods) || !m.Get("list", list) || id.empty() || id.size() > 40 ||
        goods.size() + list.size() > kMaxPileChars)
        return;
    auto it = s_mirrors.find(id);
    if (it == s_mirrors.end()) {
        DropPendingPiles(id);
        if (s_pendingPileChars + goods.size() + list.size() > kMaxPendingPileChars) return;
        s_pendingPiles[id] = PendingPiles{goods, list};
        s_pendingPileChars += goods.size() + list.size();
        return;
    }
    if (it->second.pileGoods == goods && it->second.pileList == list) return;
    DrawPiles(it->second, goods, list);
}

}  // namespace

void OwnLivestock(std::vector<ComponentEntity*>* out) {
    out->clear();
    if (!s_on) return;
    out->assign(s_livestock.begin(), s_livestock.end());
}

void SetPeerOnline(bool online) {
    // Arrived (perhaps again, on a new line while its old one was still counted) or gone: a round in
    // flight went with the old line. The next one starts a little after the arrival.
    s_round = SyncRound();
    if (online) ScheduleSync(kSyncFirstSeconds);
    s_peerOnline = online;
    if (online) s_pilesAll = true;   // every stockpile's piles again (the relay keeps none)
}

void Tick(Link* link, uint64_t frame) {
    if (!s_on || link == nullptr) return;
    s_frame = frame;
    EnsureGrid();
    s_live = true;   // from the first frame of play on, a change is news
    if (frame % kFingerprintFrames == 0) {
        LogFingerprint();
        LogNature();
    }
    SyncTick(link);
    // Construction going on: the neighbour sees each stage. (Structures that are gone left s_mine in
    // NoteRemoved, before their free.)
    if (frame % kCheckFrames == 0) {
        ScanLivestock();
        const Applied& a = s_applied;
        const Applied& l = s_appliedLogged;
        if (a.roads != l.roads || a.unroads != l.unroads || a.removed != l.removed || a.planted != l.planted ||
            a.solid != l.solid || a.refused != l.refused || a.grounds != l.grounds || a.unbuilt != l.unbuilt) {
            // Structures, solid tiles and grounds as they stand now (a rejoin re-sends the same buildings,
            // which replace their mirrors: a running total would count them again).
            size_t solidNow = 0, groundsNow = 0;
            for (uint8_t ref : s_solidRef) solidNow += ref > 0 ? 1 : 0;
            for (const auto& kv : s_mirrors) groundsNow += kv.second.decalId >= 0 ? 1 : 0;
            fprintf(stderr, "[Neighbour] shared map: from the neighbor so far: %d road tile(s) laid, %d taken up, "
                            "%d resource(s) gone, %d planted, %d structure(s) taken down; now %zu of their "
                            "structure(s) drawn, %zu tile(s) solid, %zu ground(s) painted; %d not applied%s%s\n",
                    a.roads, a.unroads, a.removed, a.planted, a.unbuilt, s_mirrors.size(), solidNow, groundsNow,
                    a.refused,
                    a.refused > 0 ? " (last: " : "", a.refused > 0 ? (std::string(s_lastRefusal) + ")").c_str() : "");
            s_appliedLogged = s_applied;
        }
        for (auto it = s_mine.begin(); it != s_mine.end();) {
            ComponentEntity* e = it->second.entity.Get();
            if (e == nullptr) {
                s_unbuildQueue.push_back(it->first);
                s_releaseQueue.push_back(it->second.rect);
                ++s_waitingAdds;
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
    InvTick(link);
    PilesTick(link);
    // Backpressure: the link writes one message at a time, and a slow line (the Mac's waits for each
    // write) fell behind and once dropped what it held. Hand it more only when it is nearly empty; the
    // backlog stays in these queues, which the save keeps.
    if (link->Queued() > kLinkLowWater) return;
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

void OnWelcome() {
    if (!s_on) return;
    ++s_joinCount;
    // The relay sends the neighbour's land as it stands (claimBlock); land it gave up while the line
    // was down sent no `released` here.
    if (!s_peer.empty()) std::fill(s_peer.begin(), s_peer.end(), 0);
    // A round in flight went with the old line: the next one starts a little after this join.
    s_round = SyncRound();
    s_syncScheduled = false;
    // The replay of the neighbour's roads starts (roadsEnd counts it); this town's list goes a little after.
    // Every structure goes again (OnJoined): a refusal on the old line says nothing about this one.
    s_roadsReplayed = 0;
    s_refusedIds.clear();
    s_invScheduled = true;
    s_invDue = std::chrono::steady_clock::now() +
               std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                   std::chrono::duration<double>(kInvFirstSeconds));
}

void OnLineDown() {
    s_invScheduled = false;   // the next welcome schedules the next list
}

void OnJoined(Link* link) {
    if (!s_on || link == nullptr) return;
    // Claims are idempotent at the relay; builds replace the record by id.
    for (const auto& kv : s_mine) {
        s_claimQueue.push_back(kv.second.rect);
        s_buildQueue.push_back(kv.first);
    }
    ++s_waitingAdds;
}

void ExpectReload() {
    if (s_on) s_rescanRoads = true;
}

uint64_t WaitingGeneration() {
    // An addition moves the counter, a send shrinks a queue: either changes the pair.
    const uint64_t sizes = s_claimQueue.size() + (s_releaseQueue.size() << 16) + (s_unbuildQueue.size() << 32) +
                           (static_cast<uint64_t>(s_pending.size()) << 44);
    return sizes ^ (s_waitingAdds * 0x9E3779B97F4A7C15ull);
}

void SaveWaiting(SavedState& s) {
    s.mapWaiting.clear();
    s.mapWaiting.reserve(s_pending.size());
    for (const Message& m : s_pending)
        if (m.type == "road" || m.type == "nat") s.mapWaiting.push_back(m.Encode());   // not the sync's
    s.mapUnbuilds = s_unbuildQueue;
    s.mapClaims = s_claimQueue;
    s.mapReleases = s_releaseQueue;
}

void RestoreWaiting(const SavedState& s) {
    if (!s_on) return;
    for (const std::string& text : s.mapWaiting) {
        Message m;
        if (Parse(text, m) && (m.type == "road" || m.type == "nat")) Queue(m);
    }
    s_unbuildQueue.insert(s_unbuildQueue.end(), s.mapUnbuilds.begin(), s.mapUnbuilds.end());
    s_claimQueue.insert(s_claimQueue.end(), s.mapClaims.begin(), s.mapClaims.end());
    s_releaseQueue.insert(s_releaseQueue.end(), s.mapReleases.begin(), s.mapReleases.end());
    ++s_waitingAdds;
    if (!s.mapWaiting.empty() || !s.mapUnbuilds.empty() || !s.mapReleases.empty())
        fprintf(stderr, "[Neighbour] shared map: %u change(s), %u removed structure(s) and %u land release(s) from the "
                        "save wait for the relay\n",
                static_cast<unsigned>(s.mapWaiting.size()), static_cast<unsigned>(s.mapUnbuilds.size()),
                static_cast<unsigned>(s.mapReleases.size()));
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
        if (!ok)
            fprintf(stderr, "[Neighbour] shared map: the relay refused land this town claimed (%s): the neighbor holds it\n",
                    m.GetOr("refused", "?").c_str());
        return true;
    }
    if (m.type == "buildRefused") {
        // Both towns placed on the same land at once and the neighbour's claim came first: the relay does
        // not mirror ours. A site this player placed seconds ago, still a site, is taken back (nothing can
        // have been delivered to it yet), so neither map has two buildings in one place. Anything older (a
        // half-built house found on the neighbour's land at a rejoin, with materials in it) stays, and the
        // player is told.
        const std::string id = m.GetOr("id", "");
        auto it = s_mine.find(id);
        if (it != s_mine.end()) s_refusedIds.insert(id);
        ComponentEntity* e = it != s_mine.end() ? it->second.entity.Get() : nullptr;
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        const double age = it != s_mine.end()
                               ? std::chrono::duration<double>(std::chrono::steady_clock::now() - it->second.placedAt).count()
                               : 1e9;
        const bool fresh = e != nullptr && it->second.placedInPlay && age < kTakeBackSeconds &&
                           e->FindComponent<BuildComponent>() != nullptr;
        if (fresh && g != nullptr && g->m_entityList != nullptr && !((e->_entityFlags >> 10) & 1)) {
            g->m_entityList->QueueDestroy(&e);
            fprintf(stderr, "[Neighbour] shared map: %s taken back: the neighbor got that land first\n", id.c_str());
            if (notice != nullptr) *notice = "Your neighbor got that land first: the building you placed there is taken back.";
        } else if (e != nullptr) {
            fprintf(stderr, "[Neighbour] shared map: %s stands on the neighbor's land (already being built)\n", id.c_str());
            if (notice != nullptr) *notice = "A building of yours stands on your neighbor's land; your neighbor does not see it.";
        }
        return true;
    }
    if (m.type == "build") { AddMirror(m); return true; }
    if (m.type == "unbuild") {
        DropPendingPiles(m.GetOr("id", ""));
        RemoveMirror(m.GetOr("id", ""));
        return true;
    }
    if (m.type == "piles") { OnPiles(m); return true; }
    if (m.type == "mirrorEnd") {   // the neighbour's land is in: a loaded game's roads can go again
        // What the list did not name was taken down while the line was down (a reconnect keeps the
        // mirrors it had; the relay deletes a structure taken down while this town is away).
        size_t gone = 0;
        for (auto it = s_mirrors.begin(); it != s_mirrors.end();) {
            if (it->second.join == s_joinCount) {
                ++it;
                continue;
            }
            FreeMirror(it->second, true);
            it = s_mirrors.erase(it);
            ++gone;
        }
        if (gone > 0)
            fprintf(stderr, "[Neighbour] shared map: %zu of the neighbor's structure(s) taken down while away removed\n",
                    gone);
        // Piles for a structure the list did not name will not be drawn: the neighbour has no such structure.
        for (auto it = s_pendingPiles.begin(); it != s_pendingPiles.end();) {
            if (s_mirrors.count(it->first) != 0) {
                ++it;
                continue;
            }
            s_pendingPileChars -= it->second.goods.size() + it->second.list.size();
            it = s_pendingPiles.erase(it);
        }
        if (s_rescanRoads) {
            s_rescanRoads = false;
            ScanRoads();
        }
        return true;
    }
    if (m.type == "roadsEnd") {   // every road the neighbour holds is in (restate.h)
        int64_t count = -1;
        m.GetInt("count", count);
        TakeUpStaleRoads(count);
        return true;
    }
    if (m.type == "want") { OnWant(m); return true; }
    if (m.type == "road") { ApplyRoad(m); return true; }
    if (m.type == "nat") { ApplyNat(m); return true; }
    if (m.type == "natBatch" || m.type == "roadBatch" || m.type == "roadsOff") { ApplyBatch(m); return true; }
    if (m.type == "natSyncReq") { AnswerDigest(m); return true; }
    if (m.type == "natBlockReq") { AnswerBlocks(m); return true; }
    if (m.type == "natDigest") { TakeDigest(m); return true; }
    if (m.type == "natBlock") { TakeBlocks(m); return true; }
    return false;
}

}  // namespace SharedMap
}  // namespace Neighbour

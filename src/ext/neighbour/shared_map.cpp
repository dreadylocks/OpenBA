// ============================================================================
// src/ext/neighbour/shared_map.cpp -- NOT RETAIL. ext.neighbour M2: claims and mirrors (shared_map.h)
// ============================================================================
// Our own code over the game's objects (the guards are ours to place).
// ============================================================================

#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "math/Math.h"

#include "ext/neighbour/shared_map.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <new>
#include <vector>

#include "Component/ComponentDescription.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/ModelDescription.h"
#include "Component/NaturalResourceComponent.h"
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

bool s_on = false;
int  s_dim = 0;
std::vector<uint8_t> s_peer;                // 1 = the neighbour holds the tile

// This town's structures, by id ("s<x>_<y>": a footprint origin holds one structure).
struct Mine {
    Object::InternalPtr<ComponentEntity> entity;
    Message                              build;   // the "build" message
    TileRect                             rect;
};
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
};
std::map<std::string, Mirror> s_mirrors;

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

void FreeMirror(Mirror& m, bool sceneAlive) {
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
    for (ComponentEntity* e : doomed) g->m_entityList->QueueDestroy(&e);
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
    Graphics::GraphicsMesh* gm = mg.meshes[mg.meshCount - 1];   // the finished building
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
    }
}

void RemoveMirror(const std::string& id) {
    auto it = s_mirrors.find(id);
    if (it == s_mirrors.end()) return;
    FreeMirror(it->second, true);
    s_mirrors.erase(it);
}

void SendClaims(Link* link) {
    // At most kMaxRects rectangles a message.
    while (!s_claimQueue.empty()) {
        const size_t n = s_claimQueue.size() < static_cast<size_t>(kMaxRects) ? s_claimQueue.size() : kMaxRects;
        std::vector<TileRect> part(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        s_claimQueue.erase(s_claimQueue.begin(), s_claimQueue.begin() + static_cast<long>(n));
        Message c("claim");
        c.Set("id", "c" + std::to_string(++s_claimSerial)).Set("rects", EncodeRects(part));
        link->Send(c);
    }
    while (!s_releaseQueue.empty()) {
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
    const std::string id = "s" + std::to_string(r.x) + "_" + std::to_string(r.y);

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
     .SetInt("tx", r.x).SetInt("ty", r.y).SetInt("tw", r.w).SetInt("th", r.h).SetInt("built", 100);
    Mine mine;
    mine.entity = Object::InternalPtr<ComponentEntity>(entity);
    mine.build = b;
    mine.rect = r;
    s_mine[id] = mine;
    s_claimQueue.push_back(r);
    s_buildQueue.push_back(id);
}

void NoteRoadCell(uint32_t cellIdx) {
    if (!s_on || !EnsureGrid()) return;
    s_claimQueue.push_back(TileRect{static_cast<int>(cellIdx % static_cast<uint32_t>(s_dim)),
                                    static_cast<int>(cellIdx / static_cast<uint32_t>(s_dim)), 1, 1});
}

void Tick(Link* link, uint64_t frame) {
    if (!s_on || link == nullptr) return;
    EnsureGrid();
    // Structures that are gone: unbuild, release their land.
    if (frame % kCheckFrames == 0) {
        for (auto it = s_mine.begin(); it != s_mine.end();) {
            if (it->second.entity.Get() == nullptr) {
                s_unbuildQueue.push_back(it->first);
                s_releaseQueue.push_back(it->second.rect);
                it = s_mine.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!link->Connected()) return;   // the queues wait; OnJoined sends the structures again
    SendClaims(link);
    for (const std::string& id : s_buildQueue) {
        auto it = s_mine.find(id);
        if (it != s_mine.end()) link->Send(it->second.build);
    }
    s_buildQueue.clear();
    for (const std::string& id : s_unbuildQueue) {
        Message u("unbuild");
        u.Set("id", id);
        link->Send(u);
    }
    s_unbuildQueue.clear();
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
            *notice = "Some land you built on was already your neighbour's; the two towns overlap there.";
        return true;
    }
    if (m.type == "build") { AddMirror(m); return true; }
    if (m.type == "unbuild") { RemoveMirror(m.GetOr("id", "")); return true; }
    if (m.type == "mirrorEnd") return true;
    return false;
}

}  // namespace SharedMap
}  // namespace Neighbour

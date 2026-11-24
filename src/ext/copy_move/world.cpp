// ============================================================================
// src/ext/copy_move/world.cpp -- what the copy / move tool reads from the game (world.h)
// ============================================================================
// NOT RETAIL. Standalone code, not a retail body: the guards are ours to place.
// ============================================================================
#include "math/Math.h"
#include "picking/Scene.h"
#include "input/Input.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Component/ComponentDescription.h"
#include "Component/CreateDragDescription.h"
#include "Component/CreateFencedDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/HighlightComponent.h"
#include "Component/MapComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Player/exe_bodies.h"
#include "ext/copy_move/world.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>

namespace Ext {
namespace CopyMove {

namespace {

constexpr uint32_t kMarkedForRemoval = 0x400u;   // ComponentEntity::MarkForRemoval (action_base_hunt.cpp)
constexpr float kPickRange = 400.0f;              // EditTool::OnKey's pick range (edittool.cpp)

// The tool-kind of each template seen, so a sweep of the whole master array tests most entities with one
// lookup. Templates live as long as the game's externals; the cache is cleared with the game (Reset below
// is called by the tool's Startup when the game changed).
std::unordered_map<const ComponentDescription*, int> g_kindCache;   // -1 = not copyable, else a Kind
const GameUpdateLoop* g_cacheGame = nullptr;

template <class T>
bool HasDescription(const ComponentDescription* tmpl) {
    const int32_t n = tmpl->_subDescCount;
    for (int32_t i = 0; i < n; ++i) {
        const Description* d = tmpl->_subDescs[i];
        if (d != nullptr && d->Type() == T::TypeId()) {
            return true;
        }
    }
    return false;
}

EntityList* TheList() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_entityList : nullptr;
}

}  // namespace

Map* TheMap() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_game : nullptr;
}

bool KindOf(const ComponentDescription* tmpl, Kind* kind) {
    if (tmpl == nullptr) {
        return false;
    }
    if (g_cacheGame != GameUpdateLoop::_singleton) {
        g_kindCache.clear();
        g_cacheGame = GameUpdateLoop::_singleton;
    }
    auto it = g_kindCache.find(tmpl);
    int k = -1;
    if (it != g_kindCache.end()) {
        k = it->second;
    } else {
        if (HasDescription<CreatePlacedDescription>(tmpl)) {
            k = static_cast<int>(Kind::Building);
        } else if (HasDescription<CreateDragDescription>(tmpl)) {
            k = static_cast<int>(Kind::Field);
        } else if (HasDescription<CreateFencedDescription>(tmpl)) {
            k = static_cast<int>(Kind::Pasture);
        }
        g_kindCache[tmpl] = k;
    }
    if (k < 0) {
        return false;
    }
    *kind = static_cast<Kind>(k);
    return true;
}

EntityRef Remember(ComponentEntity* entity) {
    EntityRef ref;
    ref.entity = entity;
    ref.hint = entity != nullptr ? entity->_entityId : -1;
    return ref;
}

bool Alive(EntityRef& ref) {
    EntityList* list = TheList();
    if (list == nullptr || ref.entity == nullptr) {
        return false;
    }
    const int n = list->masterArray._size;
    if (ref.hint < 0 || ref.hint >= n || list->masterArray[ref.hint].Get() != ref.entity) {
        ref.hint = -1;
        for (int i = 0; i < n; ++i) {
            if (list->masterArray[i].Get() == ref.entity) {
                ref.hint = i;
                break;
            }
        }
        if (ref.hint < 0) {
            return false;
        }
    }
    return (ref.entity->_entityFlags & kMarkedForRemoval) == 0;
}

bool RectOf(ComponentEntity* entity, Rect* rect, int* degrees) {
    MapComponent* mc = entity != nullptr ? entity->FindComponent<MapComponent>() : nullptr;
    if (mc == nullptr || mc->tileX1 <= 0 || mc->tileY1 <= 0) {
        return false;
    }
    rect->col = static_cast<int16_t>(mc->tileX0);
    rect->row = static_cast<int16_t>(mc->tileY0);
    rect->w = static_cast<int16_t>(mc->tileX1);
    rect->h = static_cast<int16_t>(mc->tileY1);
    if (degrees != nullptr) {
        int d = mc->extraField % 360;
        if (d < 0) {
            d += 360;
        }
        *degrees = d;
    }
    return true;
}

std::vector<ComponentEntity*> EntitiesIn(const Rect& area) {
    std::vector<ComponentEntity*> out;
    EntityList* list = TheList();
    if (list == nullptr) {
        return out;
    }
    const int n = list->masterArray._size;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* e = list->masterArray[i].Get();
        if (e == nullptr || (e->_entityFlags & kMarkedForRemoval) != 0) {
            continue;
        }
        Kind kind;
        if (!KindOf(e->_description, &kind)) {
            continue;
        }
        Rect r;
        if (RectOf(e, &r, nullptr) && Overlaps(r, area)) {
            out.push_back(e);
        }
    }
    // A pasture's shelter and fence pieces are entities of their own (its MultiObjectComponent's children,
    // fencedtool_calcpath.cpp) made from placeable templates: they go with their pasture, never alone. A
    // child's rect lies inside its master's, so a master is in `out` whenever one of its children is.
    std::unordered_set<const ComponentEntity*> children;
    for (ComponentEntity* e : out) {
        MultiObjectComponent* multi = e->FindComponent<MultiObjectComponent>();
        if (multi == nullptr) {
            continue;
        }
        for (int i = 0; i < multi->children._size; ++i) {
            children.insert(multi->children[i].Get());
        }
    }
    if (!children.empty()) {
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&children](ComponentEntity* e) { return children.count(e) != 0; }),
                  out.end());
    }
    return out;
}

ComponentEntity* MasterOf(ComponentEntity* entity) {
    Rect r;
    if (entity == nullptr || !RectOf(entity, &r, nullptr)) {
        return entity;
    }
    EntityList* list = TheList();
    const int n = list != nullptr ? list->masterArray._size : 0;
    for (int i = 0; i < n; ++i) {
        ComponentEntity* e = list->masterArray[i].Get();
        if (e == nullptr || e == entity) {
            continue;
        }
        MultiObjectComponent* multi = e->FindComponent<MultiObjectComponent>();
        if (multi == nullptr) {
            continue;
        }
        for (int k = 0; k < multi->children._size; ++k) {
            if (multi->children[k].Get() == entity) {
                return e;
            }
        }
    }
    return entity;
}

int CursorCell() {
    return PlayerRay_ScreenToCell_101270(nullptr);
}

ComponentEntity* EntityUnderCursor(int cursorCell) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    if (g == nullptr || map == nullptr) {
        return nullptr;
    }
    // The pick mesh, as EditTool::OnKey picks (edittool.cpp): the ray from the mouse, capped at the
    // terrain hit so nothing behind the ground answers.
    float origin[3] = { 0.0f, 0.0f, 0.0f };
    float dir[3] = { 0.0f, 0.0f, 0.0f };
    PlayerRay_GetMouseRay_1005a0(nullptr, origin, dir);
    float maxDist = kPickRange;
    if (g->m_worldGeometry != nullptr) {
        float range[2] = { kPickRange, 0.0f };
        float hit[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g->m_worldGeometry->TerrainMarch(origin, dir, range, hit, &maxDist);
    }
    if (g->m_pickingScene != nullptr) {
        Math::Vector3 hitPoint(0.0f, 0.0f, 0.0f);
        Math::Vector3 hitNormal(0.0f, 0.0f, 0.0f);
        const int id = g->m_pickingScene->Intersect(Math::Vector3(origin[0], origin[1], origin[2]),
                                                    Math::Vector3(dir[0], dir[1], dir[2]),
                                                    Math::Scalar(maxDist), hitPoint, hitNormal);
        if (id >= 0) {
            // The picking component stores the entity a pick answers (a pasture's shelter answers its
            // pasture): retail's void* user data.
            ComponentEntity* e = static_cast<ComponentEntity*>(g->m_pickingScene->GetUserData(static_cast<uint32_t>(id)));
            Kind kind;
            if (e != nullptr && KindOf(e->_description, &kind)) {
                return MasterOf(e);
            }
        }
    }
    if (cursorCell < 0) {
        return nullptr;
    }
    Rect here;
    here.col = cursorCell % map->size;
    here.row = cursorCell / map->size;
    std::vector<ComponentEntity*> under = EntitiesIn(here);
    return under.empty() ? nullptr : under.front();
}

void SetOutline(ComponentEntity* entity, bool on) {
    if (entity == nullptr) {
        return;
    }
    const uint8_t flag = on ? 1 : 0;
    MultiObjectComponent* multi = entity->FindComponent<MultiObjectComponent>();
    if (multi != nullptr) {
        for (int i = 0; i < multi->children._size; ++i) {
            ComponentEntity* child = multi->children[i].Get();
            HighlightComponent* hi = child != nullptr ? child->FindComponent<HighlightComponent>() : nullptr;
            if (hi != nullptr && hi->highlightFlag != flag) {
                hi->highlightFlag = flag;
                hi->Repaint();
            }
        }
    }
    HighlightComponent* hi = entity->FindComponent<HighlightComponent>();
    if (hi != nullptr && hi->highlightFlag != flag) {
        hi->highlightFlag = flag;
        hi->Repaint();
    }
}

bool HasRoad(int cell) {
    Map* map = TheMap();
    return map != nullptr && map->cells.GetData()[cell].field_0x2A != 0xFF;
}

bool RoadPending(int cell) {
    Map* map = TheMap();
    return map != nullptr && map->cells.GetData()[cell].field_0x2B == 0x80;
}

uint8_t RoadType(int cell) {
    Map* map = TheMap();
    return map != nullptr ? map->cells.GetData()[cell].field_0x2A : 0xFF;
}

ComponentDescription* RoadTemplate(uint8_t type) {
    // Every loaded template whose CreateRoadDescription places this road type (a mod's road tools too);
    // retail's two are Template\StoneRoad.rsc (0) and Template\DirtRoad.rsc (1).
    static const GameUpdateLoop* s_game = nullptr;
    static std::map<uint8_t, ComponentDescription*> s_byType;
    if (s_game != GameUpdateLoop::_singleton) {
        s_byType.clear();
        s_game = GameUpdateLoop::_singleton;
    }
    auto it = s_byType.find(type);
    if (it != s_byType.end()) {
        return it->second;
    }
    ComponentDescription* found = nullptr;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f != nullptr) {
        const uint32_t tid = ComponentDescription::TypeId();
        for (const ::Object::External* ext = f->FindNext(tid, nullptr); ext != nullptr && found == nullptr;
             ext = f->FindNext(tid, ext)) {
            if (!::Object::IsDerivedFromNullSafe(ext, tid)) {
                continue;
            }
            ComponentDescription* d = const_cast<ComponentDescription*>(static_cast<const ComponentDescription*>(ext));
            const CreateRoadDescription* road = d->FindDescription<CreateRoadDescription>();
            if (road != nullptr && static_cast<uint8_t>(road->_b20) == type) {
                found = d;
            }
        }
        if (found == nullptr && type <= 1) {
            const char* path = type == 0 ? "Template\\StoneRoad.rsc" : "Template\\DirtRoad.rsc";
            ComponentDescription* d = f->FindOrLoadAs<ComponentDescription>(System::Filename(path));
            if (::Object::IsDerivedFromNullSafe(d, tid) && d->FindDescription<CreateRoadDescription>() != nullptr) {
                found = d;
            }
        }
    }
    if (found == nullptr) {
        std::fprintf(stderr, "[CopyMove] no road template places road type %u\n", static_cast<unsigned>(type));
    }
    s_byType[type] = found;
    return found;
}

void RectCentre(const Rect& r, Math::Vector3* out) {
    Map* map = TheMap();
    *out = Math::Vector3(0.0f, 0.0f, 0.0f);
    if (map == nullptr) {
        return;
    }
    const float centreOff = static_cast<float>(map->size >> 1) - 0.5f;
    float inA[3] = { static_cast<float>(r.col) - centreOff, static_cast<float>(r.row) - centreOff, 0.0f };
    float inB[3] = { static_cast<float>(r.col + r.w - 1) - centreOff, static_cast<float>(r.row + r.h - 1) - centreOff,
                     0.0f };
    float a[3] = { 0.0f, 0.0f, 0.0f };
    float b[3] = { 0.0f, 0.0f, 0.0f };
    map->GetTerrainPos(a, inA, '\0', nullptr);
    map->GetTerrainPos(b, inB, '\0', nullptr);
    out->x = (a[0] + b[0]) * 0.5f;
    out->y = (a[1] + b[1]) * 0.5f;
    out->z = 0.0f;
}

}  // namespace CopyMove
}  // namespace Ext

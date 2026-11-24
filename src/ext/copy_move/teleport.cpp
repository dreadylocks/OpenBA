// ============================================================================
// src/ext/copy_move/teleport.cpp -- the instant move (teleport.h)
// ============================================================================
// NOT RETAIL. Every call below is a retail body by name; what is ours is the order and the choice of which
// components move (teleport.h, COPY_MOVE.md §3).
// ============================================================================
#include "graphics/Mesh.h"
#include "Action/PathAvl.h"   // AvlInsert28: the map's pending-road set (LiftRoad)
#include "math/Math.h"
#include "object/Internal.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/WorldGeometry.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Component/BuildComponent.h"
#include "Component/ClearAreaDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/MapDescription.h"
#include "Component/DecalComponent.h"
#include "Component/DestroyComponent.h"
#include "Component/FloorComponent.h"
#include "Component/MapComponent.h"
#include "Component/ModelComponent.h"
#include "Component/MultiObjectComponent.h"
#include "Component/NaturalResourceComponent.h"
#include "Component/ParticleComponent.h"
#include "Component/PickingComponent.h"
#include "Component/ZoneComponent.h"
#include "Component/exe_bodies.h"
#include "ext/neighbour/shared_map.h"
#include "ext/copy_move/world.h"
#include "ext/copy_move/teleport.h"

#include <cstdio>
#include <utility>

namespace Ext {
namespace CopyMove {

namespace {

constexpr uint32_t kCellsClaimed = 0x100000u;   // MapComponent _flags bit 20: AddToMap wrote the cells

// What a lifted entity had on the map, to set it down the same way.
struct Lifted {
    ComponentEntity* entity = nullptr;
    bool claimed = false;     // MapComponent had claimed its cells
    bool floor = false;       // FloorComponent had placed its floor
};

// The rigid motion of one move: about the old rect's centre by `turns` quarter turns, onto the new centre.
struct Motion {
    Math::Vector3 from;
    Math::Vector3 to;
    int turns = 0;
    Rect oldRect;   // the master's rect before and after: a child's rect turns inside it
    Rect newRect;
};

void Spin(float dx, float dy, int turns, float* ox, float* oy) {
    // One quarter turn is TurnCell's (x, y) -> (-y, x) (layout.h): the turn PlacementTool's yaw + 90 makes.
    for (int i = 0; i < (turns & 3); ++i) {
        const float t = dx;
        dx = -dy;
        dy = t;
    }
    *ox = dx;
    *oy = dy;
}

Rect MoveRect(const Rect& r, const Motion& m) {
    Rect rel = r;
    rel.col -= m.oldRect.col;
    rel.row -= m.oldRect.row;
    int boxW = m.oldRect.w;
    int boxH = m.oldRect.h;
    for (int i = 0; i < (m.turns & 3); ++i) {
        rel = TurnRect(rel, boxW, boxH);
        std::swap(boxW, boxH);
    }
    rel.col += m.newRect.col;
    rel.row += m.newRect.row;
    return rel;
}

void Lift(ComponentEntity* e, Lifted* out) {
    out->entity = e;
    for (int i = 0; i < e->_components._size; ++i) {
        Component* c = e->_components[i].Get();
        if (c == nullptr) {
            continue;
        }
        const uint32_t t = c->Type();
        if (t == PickingComponent::TypeId()) {
            static_cast<PickingComponent*>(c)->OnRemove();
        } else if (t == DecalComponent::TypeId()) {
            static_cast<DecalComponent*>(c)->OnRemove();
        } else if (t == FloorComponent::TypeId()) {
            FloorComponent* f = static_cast<FloorComponent*>(c);
            out->floor = f->floorId >= 0;
            if (out->floor) {
                f->OnRemove();
            }
        } else if (t == ZoneComponent::TypeId()) {
            static_cast<ZoneComponent*>(c)->OnRemove();
        } else if (t == MapComponent::TypeId()) {
            MapComponent* m = static_cast<MapComponent*>(c);
            out->claimed = (m->_flags & kCellsClaimed) != 0;
            m->OnRemove();
        } else if (t == ModelComponent::TypeId()) {
            static_cast<ModelComponent*>(c)->OnRemove();
        }
    }
}

void SetDown(const Lifted& l, const Motion& m, bool master) {
    ComponentEntity* e = l.entity;
    MapComponent* map = e->FindComponent<MapComponent>();
    if (map != nullptr) {
        Rect r;
        r.col = static_cast<int16_t>(map->tileX0);
        r.row = static_cast<int16_t>(map->tileY0);
        r.w = static_cast<int16_t>(map->tileX1);
        r.h = static_cast<int16_t>(map->tileY1);
        const Rect moved = master ? m.newRect : MoveRect(r, m);
        map->tileX0 = static_cast<int16_t>(moved.col);
        map->tileY0 = static_cast<int16_t>(moved.row);
        map->tileX1 = static_cast<int16_t>(moved.w);
        map->tileY1 = static_cast<int16_t>(moved.h);
        int degrees = (map->extraField + 90 * (m.turns & 3)) % 360;
        if (degrees < 0) {
            degrees += 360;
        }
        map->extraField = degrees;
    }
    // The models first: picking, particles and the decal read the model's transform.
    for (int i = 0; i < e->_components._size; ++i) {
        Component* c = e->_components[i].Get();
        if (c == nullptr || c->Type() != ModelComponent::TypeId()) {
            continue;
        }
        ModelComponent* model = static_cast<ModelComponent*>(c);
        float dx = 0.0f;
        float dy = 0.0f;
        Spin(model->transform.translation.x - m.from.x, model->transform.translation.y - m.from.y, m.turns, &dx, &dy);
        Math::Vector3 pos(m.to.x + dx, m.to.y + dy, model->transform.translation.z);
        model->SetTransformFromYaw(&pos, model->yaw + 90.0f * static_cast<float>(m.turns & 3));
        model->OnAdded();
    }
    for (int i = 0; i < e->_components._size; ++i) {
        Component* c = e->_components[i].Get();
        if (c == nullptr) {
            continue;
        }
        const uint32_t t = c->Type();
        if (t == MapComponent::TypeId()) {
            if (l.claimed) {
                static_cast<MapComponent*>(c)->AddToMap();
            }
        } else if (t == ZoneComponent::TypeId()) {
            static_cast<ZoneComponent*>(c)->OnAdded();
        } else if (t == DecalComponent::TypeId()) {
            static_cast<DecalComponent*>(c)->OnAdded();
        } else if (t == PickingComponent::TypeId()) {
            static_cast<PickingComponent*>(c)->RefreshMesh();
        } else if (t == FloorComponent::TypeId()) {
            if (l.floor) {
                static_cast<FloorComponent*>(c)->PlaceFloor();
            }
        } else if (t == ParticleComponent::TypeId()) {
            ParticleComponent* p = static_cast<ParticleComponent*>(c);
            for (int k = 0; k < p->emitters._size && k < p->active._size; ++k) {
                if (p->active[k]) {
                    p->StartEmitter(k);
                }
            }
        }
    }
    e->SendEvent(0);   // as CreateAt and ModelComponent::OnRemove do: the outline and the model's readers
}

// One moving building and its children, lifted.
struct Moving {
    const Item* item = nullptr;
    ComponentEntity* entity = nullptr;
    Lifted master;
    std::vector<Lifted> children;
    Rect oldRect;
    int oldDegrees = 0;
};

// One moving road cell, lifted.
struct MovingRoad {
    const Item* item = nullptr;
    int from = -1;
    uint8_t type = 0xFF;
    bool pending = false;
    uint8_t orientation = 0;
    uint32_t terrain = 0;
};

void LiftBuilding(Moving* mv) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    ComponentEntity* e = mv->entity;
    if (g->m_citizenList != nullptr) {
        TownMgr_AbortCitizenActionsFor_89e30(g->m_citizenList, &e);
    }
    Neighbour::SharedMap::NoteRemoved(e);
    MultiObjectComponent* multi = e->FindComponent<MultiObjectComponent>();
    if (multi != nullptr) {
        for (int i = 0; i < multi->children._size; ++i) {
            ComponentEntity* child = multi->children[i].Get();
            if (child == nullptr) {
                continue;
            }
            Lifted l;
            Lift(child, &l);
            mv->children.push_back(l);
        }
    }
    Lift(e, &mv->master);
}

void SetDownBuilding(Moving* mv, const Rect& to, int turns) {
    Motion m;
    m.oldRect = mv->oldRect;
    m.newRect = to;
    m.turns = turns;
    RectCentre(mv->oldRect, &m.from);
    RectCentre(to, &m.to);
    SetDown(mv->master, m, true);
    for (const Lifted& child : mv->children) {
        SetDown(child, m, false);
    }
    // A site ClearArea flattened (ClearAreaComponent::FinishClear) has its region's heights refreshed:
    // where it stood and where it stands now.
    const ClearAreaDescription* clear = mv->entity->_description->FindDescription<ClearAreaDescription>();
    if (clear != nullptr && clear->_regrowTerrain) {
        Map* map = TheMap();
        map->ResetTerrainHeights(mv->oldRect.col, mv->oldRect.row, mv->oldRect.col + mv->oldRect.w,
                                 mv->oldRect.row + mv->oldRect.h);
        map->ResetTerrainHeights(to.col, to.row, to.col + to.w, to.row + to.h);
    }
    Neighbour::SharedMap::NoteStructure(mv->entity);
}

bool PendingIndexed(const Map* map, int cell) {
    const AvlNode28* node = map->field_0xE8.root;
    while (node != nullptr) {
        if (cell < node->key) {
            node = node->left;
        } else if (node->key < cell) {
            node = node->right;
        } else {
            return true;
        }
    }
    return false;
}

void LiftRoad(MovingRoad* r) {
    Map* map = TheMap();
    MapCell& c = map->cells.GetData()[r->from];
    r->type = c.field_0x2A;
    r->pending = c.field_0x2B == 0x80;
    r->orientation = c.field_0x2B;
    r->terrain = c.terrainType & 0x1f;
    if (r->pending) {
        // Map::RemovePendingRoad finds the planned cell in the map's pending-road set (+0xE8) before it
        // cancels its BuildRoad job and restores the cell. A game loaded from a save comes back without that
        // set (the BuildRoad jobs are loaded, the set is not rebuilt: docs/known_port_issues.md row 1334), and the removal then does nothing. The cell goes back into the set the way
        // Map::PlaceRoadCell puts it there, so the removal is retail's.
        if (!PendingIndexed(map, r->from)) {
            AvlNode28* inserted = nullptr;
            const int32_t key = r->from;
            AvlInsert28(&map->field_0xE8, &inserted, &key);
        }
        map->RemovePendingRoad(r->from);
    } else {
        map->FinishRoadCell(r->from, 0, 2);
    }
    if (c.field_0x2A != 0xFF) {
        std::fprintf(stderr,
                     "[CopyMove] road cell %d (type %u, %s) still has a road (type %u, state 0x%02x) after "
                     "it was lifted\n",
                     r->from, static_cast<unsigned>(r->type), r->pending ? "planned" : "finished",
                     static_cast<unsigned>(c.field_0x2A), static_cast<unsigned>(c.field_0x2B));
    }
}

// The natural resources standing on a cell go, the way the game removes an entity (EntityList::QueueDestroy):
// a finished road is laid at once, so nobody comes to clear them first.
void ClearNature(int cell) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Map* map = TheMap();
    const MapCell& c = map->cells.GetData()[cell];
    ComponentEntity* found[2] = { c.occupant.Get(), c.blocker.Get() };
    for (int i = 0; i < 2; ++i) {
        ComponentEntity* e = found[i];
        if (e == nullptr || (i == 1 && e == found[0]) || e->FindComponent<NaturalResourceComponent>() == nullptr) {
            continue;
        }
        g->m_entityList->QueueDestroy(&e);
    }
}

void SetDownRoadCell(const MovingRoad& r, int cell);

void SetDownRoad(const MovingRoad& r, int cell) {
    SetDownRoadCell(r, cell);
    const MapCell& c = TheMap()->cells.GetData()[cell];
    if (c.field_0x2A == 0xFF || c.field_0x2A > r.type) {
        std::fprintf(stderr, "[CopyMove] road cell %d -> %d (type %u, %s): the cell holds type %u, state 0x%02x\n",
                     r.from, cell, static_cast<unsigned>(r.type), r.pending ? "planned" : "finished",
                     static_cast<unsigned>(c.field_0x2A), static_cast<unsigned>(c.field_0x2B));
    }
}

void SetDownRoadCell(const MovingRoad& r, int cell) {
    Map* map = TheMap();
    MapCell& c = map->cells.GetData()[cell];
    if (c.field_0x2A != 0xFF && c.field_0x2A <= r.type && (c.field_0x2B != 0x80 || r.pending)) {
        return;   // a road as good is there already (Map::PlaceRoadCell's own test)
    }
    if (r.pending) {
        ComponentDescription* tmpl = RoadTemplate(r.type);
        const CreateRoadDescription* road = tmpl != nullptr ? tmpl->FindDescription<CreateRoadDescription>() : nullptr;
        const MapDescription* md = tmpl != nullptr ? tmpl->FindDescription<MapDescription>() : nullptr;
        if (road != nullptr && md != nullptr) {
            map->PlaceRoadCell(static_cast<uint32_t>(cell), r.type, static_cast<uint64_t>(cell), md->_cellType,
                               road->_created);
        }
        return;
    }
    // A finished road, laid finished: the neighbour's ApplyRoad steps (shared_map.cpp), which are
    // Map::PlaceRoadCell's count and the CompleteRoad action's Map::FinishRoadCell. A road planned there is
    // cancelled first, as LiftRoad cancels one.
    if (c.field_0x2A != 0xFF && c.field_0x2B == 0x80) {
        MovingRoad planned;
        planned.from = cell;
        LiftRoad(&planned);
    }
    ClearNature(cell);
    const uint32_t flag = 1u << (c.terrainType & 0x1f);
    if ((flag & 3) == 0 && c.field_0x2A == 0xFF) {
        c.footprintCount = static_cast<uint8_t>(c.footprintCount + 1);
    }
    c.field_0x2A = r.type;
    map->FinishRoadCell(cell, r.orientation, r.terrain);   // tells ext.neighbour itself (its EXT SEAM)
}

}  // namespace

bool CanTeleport(ComponentEntity* entity) {
    Kind kind;
    if (entity == nullptr || !KindOf(entity->_description, &kind) || kind != Kind::Building) {
        return false;
    }
    MapComponent* map = entity->FindComponent<MapComponent>();
    if (map == nullptr || (map->_flags & kCellsClaimed) == 0) {
        return false;
    }
    BuildComponent* build = entity->FindComponent<BuildComponent>();
    if (build != nullptr && !build->IsBuildComplete()) {
        return false;
    }
    DestroyComponent* destroy = entity->FindComponent<DestroyComponent>();
    if (destroy != nullptr && destroy->InRemovalState()) {
        return false;
    }
    return true;
}

bool Teleport(const Layout& layout, const Cell& origin, const std::vector<std::unique_ptr<Placer>>& placers,
              std::vector<ComponentEntity*>* moved) {
    Map* map = TheMap();
    if (map == nullptr) {
        return false;
    }
    const std::vector<Item>& items = layout.Items();
    std::vector<Moving> buildings;
    std::vector<MovingRoad> roads;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        if (it.kind == Kind::Road && it.sourceCell >= 0 && HasRoad(it.sourceCell)) {
            MovingRoad r;
            r.item = &it;
            r.from = it.sourceCell;
            roads.push_back(r);
        } else if (it.kind == Kind::Building && it.source != nullptr) {
            ComponentEntity* e = static_cast<ComponentEntity*>(it.source);
            if (!CanTeleport(e)) {
                continue;
            }
            Moving mv;
            mv.item = &it;
            mv.entity = e;
            if (!RectOf(e, &mv.oldRect, &mv.oldDegrees)) {
                continue;
            }
            buildings.push_back(mv);
        }
    }
    if (buildings.empty() && roads.empty()) {
        return false;
    }
    for (Moving& mv : buildings) {
        LiftBuilding(&mv);
    }
    for (MovingRoad& r : roads) {
        LiftRoad(&r);
    }
    // With everything lifted, each new place is tested as a fresh placement would be (an empty move set: a
    // road needs no template to be moved).
    const MoveSet lifted;
    bool fits = true;
    auto placerOf = [&](const Item* it) -> Placer* {
        return placers[static_cast<size_t>(it - items.data())].get();
    };
    for (const Moving& mv : buildings) {
        fits = fits && placerOf(mv.item)->Check(*mv.item, Layout::Placed(*mv.item, origin), &lifted, nullptr);
    }
    for (const MovingRoad& r : roads) {
        fits = fits && placerOf(r.item)->Check(*r.item, Layout::Placed(*r.item, origin), &lifted, nullptr);
    }
    const int turns = layout.Turns();
    for (Moving& mv : buildings) {
        if (fits) {
            SetDownBuilding(&mv, Layout::Placed(*mv.item, origin), turns);
            if (moved != nullptr) {
                moved->push_back(mv.entity);
            }
        } else {
            SetDownBuilding(&mv, mv.oldRect, 0);
        }
    }
    for (const MovingRoad& r : roads) {
        const Rect to = Layout::Placed(*r.item, origin);
        SetDownRoad(r, fits ? to.row * map->size + to.col : r.from);
    }
    std::fprintf(stderr, "[CopyMove] move: %zu building(s), %zu road cell(s) %s\n", buildings.size(), roads.size(),
                 fits ? "moved" : "put back (a new place did not fit)");
    return fits;
}

}  // namespace CopyMove
}  // namespace Ext

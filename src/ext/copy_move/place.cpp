// ============================================================================
// src/ext/copy_move/place.cpp -- one copied thing, placed (place.h)
// ============================================================================
// NOT RETAIL. The checks are the retail tools' own, cell by cell (PlayerTileRender_BuildRecords_1380's loop,
// tool_base_tilerender.cpp, for a building; the tool's CalcCost for an area; the road tool's end-cell test
// for a road); the pastes are the retail tools' commits.
// ============================================================================
#include "graphics/Mesh.h"
#include "graphics/Scene.h"
#include "math/Math.h"
#include "object/External.h"
#include "object/Internal.h"
#include "object/TypeInfo.h"
#include "system/MemoryAllocator.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerToolManager.h"
#include "Game/WorldGeometry.h"
#include "Entity/ComponentEntity.h"
#include "Component/ComponentDescription.h"
#include "Component/CreatePlacedDescription.h"
#include "Component/CreateRoadDescription.h"
#include "Component/DecalDescription.h"
#include "Component/MapDescription.h"
#include "Component/ModelDescription.h"
#include "Component/RadiusDescription.h"
#include "Player/DragPlacementTool.h"
#include "Player/FencedTool.h"
#include "Player/PlacementTool.h"
#include "Player/RotationSetting.h"
#include "Player/exe_bodies.h"
#include "Player/edittool_funcs.h"   // PlacementTool_Factory_SA: a PlacementTool built as the reflected create builds it
#include "ext/neighbour/shared_map.h"
#include "ext/copy_move/place.h"

#include <cstdio>
#include <new>

namespace Ext {
namespace CopyMove {

namespace {

constexpr float kHeightTolerance = 1000.0f;   // DAT_140172248: the tolerance PlacementTool tests a footprint with
constexpr uint32_t kRoadForbidden = 0x5e0;   // RoadTool::CalcPath's end-cell terrain mask (roadtool_calcpath.cpp)

const ModelMeshGroup* GroupFor(const ModelDescription* model, int variant) {
    if (model == nullptr || model->_meshGroups == nullptr || model->_meshGroupCount <= 0) {
        return nullptr;
    }
    const int g = (variant >= 0 && variant < model->_meshGroupCount) ? variant : 0;
    return &model->_meshGroups[g];
}

// A cell a move leaves: its blocker or its road goes with the move.
bool BlockerMoves(const MapCell& c, const MoveSet* moving) {
    return moving != nullptr && c.blocker.Get() != nullptr && moving->entities.count(c.blocker.Get()) != 0;
}
bool RoadMoves(int cell, const MoveSet* moving) {
    return moving != nullptr && moving->roadCells.count(cell) != 0;
}

}  // namespace

int FencedRotationFor(int turn) {
    return (4 - (turn & 3)) & 3;
}

int TurnOfDegrees(int degrees) {
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    return ((d + 45) / 90) & 3;
}

Placer::Placer(const Item& item, PlayerToolManager* manager) : _manager(manager) {
    ComponentDescription* tmpl = const_cast<ComponentDescription*>(static_cast<const ComponentDescription*>(item.tmpl));
    switch (item.kind) {
        case Kind::Building: {
            _placed = tmpl->FindDescription<CreatePlacedDescription>();
            _map = tmpl->FindDescription<MapDescription>();
            _model = tmpl->FindDescription<ModelDescription>();
            const ModelMeshGroup* group = GroupFor(_model, item.variant);
            if (group != nullptr && group->meshes != nullptr && group->meshCount > 0) {
                void* mem = System::MemoryAllocator::MemAlloc(sizeof(Graphics::MeshInstance), 8);
                if (mem != nullptr) {
                    _ghost = ::new (mem) Graphics::MeshInstance();
                    _ghost->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(group->meshes[group->meshCount - 1]));
                    _ghost->UpdateHierarchy();
                }
            }
            break;
        }
        case Kind::Field:
        case Kind::Pasture: {
            const uint32_t type = item.kind == Kind::Field ? DragPlacementTool::TypeId() : FencedTool::TypeId();
            ::Object::Base* obj = ::Object::Factory::_singleton->CreateObject(type);
            if (!::Object::IsDerivedFromNullSafe(obj, DragTool::TypeId())) {
                std::fprintf(stderr, "[CopyMove] the area tool (type %u) did not build\n", type);
                delete obj;
                break;
            }
            _tool = static_cast<DragTool*>(obj);
            if (item.kind == Kind::Pasture) {
                void* mem = System::MemoryAllocator::MemAlloc(sizeof(RotationSetting), 8);
                _rotation = mem != nullptr ? ::new (mem) RotationSetting() : nullptr;
            }
        // PlayerToolManager::SelectTool's step 8 (toolbardialog.cpp), our own setting in place of the
        // registry node's.
            _tool->dialogPtr = manager;
            _tool->descriptionPtr = tmpl;
            _tool->toolSetting = _rotation;
            _tool->Startup();
            break;
        }
        case Kind::Road:
            _roadTmpl = RoadTemplate(static_cast<uint8_t>(item.variant));
            break;
    }
}

Placer::~Placer() {
    Ghost(Item(), Rect(), false);
    if (_ghost != nullptr) {
        delete _ghost;
        _ghost = nullptr;
    }
    if (_tool != nullptr) {
        _tool->Shutdown();
        delete _tool;
        _tool = nullptr;
    }
    if (_rotation != nullptr) {
        delete _rotation;
        _rotation = nullptr;
    }
}

bool Placer::Check(const Item& item, const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles) {
    Map* map = TheMap();
    if (map == nullptr) {
        return false;
    }
    // Off the map, or on the column-0 edge PlacementTool::GetPlacement refuses: nothing to draw either.
    if (rect.col < 1 || rect.row < 0 || rect.col + rect.w > map->size || rect.row + rect.h > map->size) {
        return false;
    }
    switch (item.kind) {
        case Kind::Building:
            return CheckBuilding(item, rect, moving, tiles);
        case Kind::Field:
        case Kind::Pasture:
            return CheckArea(item, rect, tiles);
        case Kind::Road:
            return CheckRoad(item, rect, moving, tiles);
    }
    return false;
}

bool Placer::CheckBuilding(const Item& item, const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles) {
    Map* map = TheMap();
    if (_placed == nullptr || _map == nullptr) {
        return false;
    }
    // PlayerTileRender_BuildRecords_1380's walk (tool_base_tilerender.cpp): each cell's footprint and code
    // characters, read through the rotation, and Map::TestPlacement on the cell -- with a move's own cells
    // counted free (its blocker or its road leaves with it).
    const int yaw = (item.turn & 3) * 90;
    const int colEnd = rect.col + rect.w - 1;
    const int rowEnd = rect.row + rect.h - 1;
    const char* footprint = _map->_footprint._data;
    const bool hasFootprint = footprint != nullptr && _map->_footprint.GetLength() != 0;
    const char* codeMap = _placed->_name._data;
    const bool hasCodes = _placed->_placeArray._size > 0 && codeMap != nullptr && _placed->_name.GetLength() != 0;
    bool all = true;
    for (int row = rect.row; row <= rowEnd; ++row) {
        for (int col = rect.col; col <= colEnd; ++col) {
            const int relCol = col - rect.col;
            const int relRow = row - rect.row;
            const int colCd = colEnd - col;
            const int rowCd = rowEnd - row;
            int i15 = relCol;
            int i5 = relRow;
            int i7 = rect.h;
            int i14 = rect.w;
            if (yaw == 90) {
                i15 = relRow;
                i5 = colCd;
                i7 = rect.w;
                i14 = rect.h;
            } else if (yaw == 180) {
                i15 = colCd;
                i5 = rowCd;
                i7 = rect.h;
                i14 = rect.w;
            } else if (yaw == 270) {
                i15 = rowCd;
                i5 = relCol;
                i7 = rect.w;
                i14 = rect.h;
            }
            const int flat = (i14 + 1) * ((i7 - i5) - 1) + i15;
            const bool openCell = hasFootprint && footprint[flat] == '.';
            uint32_t code = _placed->_category;
            if (hasCodes) {
                code = _placed->_placeArray._data[static_cast<int64_t>(codeMap[flat]) - 0x30];
            }
            const int cell = row * map->size + col;
            const MapCell& c = map->cells.GetData()[cell];
            const char overBlocker = BlockerMoves(c, moving) ? 1 : 0;
            const char overRoad = (openCell || RoadMoves(cell, moving)) ? 1 : 0;
            const bool ok = map->TestPlacement(col, row, code, 0.0f, kHeightTolerance, 0.0f, overBlocker, overRoad,
                                               _placed->_created);
            all = all && ok;
            if (tiles != nullptr) {
                tiles->push_back(Tile{ col, row, ok });
            }
        }
    }
    return all;
}

bool Placer::CheckArea(const Item& item, const Rect& rect, std::vector<Tile>* tiles) {
    Map* map = TheMap();
    bool ok = false;
    if (_tool != nullptr) {
        const int a = rect.row * map->size + rect.col;
        const int b = (rect.row + rect.h - 1) * map->size + (rect.col + rect.w - 1);
        _tool->startCell = a;
        _tool->endCell = b;
        if (_rotation != nullptr) {
            _rotation->rotation = FencedRotationFor(item.turn);
        }
        uint32_t valid[2] = { 0, 0 };
        int costA[2] = { 0, 0 };
        int costB[2] = { 0, 0 };
        int length = 0;
        ok = _tool->CalcCost(a, b, valid, costA, costB, &length) == 0;
    }
    if (tiles != nullptr) {
        for (int row = rect.row; row < rect.row + rect.h; ++row) {
            for (int col = rect.col; col < rect.col + rect.w; ++col) {
                tiles->push_back(Tile{ col, row, ok });
            }
        }
    }
    return ok;
}

bool Placer::CheckRoad(const Item& item, const Rect& rect, const MoveSet* moving, std::vector<Tile>* tiles) {
    Map* map = TheMap();
    const int cell = rect.row * map->size + rect.col;
    const MapCell& c = map->cells.GetData()[cell];
    // RoadTool::CalcPath's end-cell test (no road on forbidden ground, nothing standing there) and the
    // neighbour's land. A road already there is fine, as for the road tool: Map::PlaceRoadCell only
    // replaces a worse one and leaves the rest (a move does the same, teleport.cpp SetDownRoadCell).
    bool ok = (kRoadForbidden & (1u << (c.terrainType & 0x1f))) == 0;
    ok = ok && (c.blocker.Get() == nullptr || BlockerMoves(c, moving));
    ok = ok && !Neighbour::SharedMap::PeerHoldsCell(static_cast<uint32_t>(cell));
    ok = ok && (moving != nullptr || _roadTmpl != nullptr);
    if (tiles != nullptr) {
        tiles->push_back(Tile{ rect.col, rect.row, ok });
    }
    return ok;
}

void Placer::Ghost(const Item& item, const Rect& rect, bool show) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    Graphics::Scene* scene = g != nullptr ? g->m_graphicsScene : nullptr;
    if (!show) {
        if (_ghostScene >= 0 && scene != nullptr) {
            scene->RemoveObject(static_cast<uint32_t>(_ghostScene));
        }
        _ghostScene = -1;
        return;
    }
    if (_ghost == nullptr || scene == nullptr) {
        return;
    }
    if (_ghostScene < 0) {
        _ghostScene = static_cast<int>(scene->AddObject(_ghost));
    }
    Math::Vector3 pos;
    RectCentre(rect, &pos);
    _ghost->SetTransform(Math::Transform::FromYawPitchOffset(static_cast<float>((item.turn & 3) * 90), 0.0f, pos));
    _ghost->UpdateHierarchy();
}

bool Placer::Paste(const Item& item, const Rect& rect) {
    Map* map = TheMap();
    if (map == nullptr) {
        return false;
    }
    switch (item.kind) {
        case Kind::Building: {
            if (_placed == nullptr || _map == nullptr || _model == nullptr) {
                return false;
            }
        // A PlacementTool set up as its Startup would (placementtool.cpp), without the preview mesh it does
        // not need here, and the copied mesh group in place of Startup's random one. CreateAt runs the
        // retail footprint test again before it creates the site.
            PlacementTool* t = PlacementTool_Factory_SA();
            if (t == nullptr) {
                return false;
            }
            ComponentDescription* tmpl = const_cast<ComponentDescription*>(static_cast<const ComponentDescription*>(item.tmpl));
            t->dialogPtr = _manager;
            t->descriptionPtr = tmpl;
            t->descComponent = const_cast<CreatePlacedDescription*>(_placed);
            t->mapComponent = const_cast<MapDescription*>(_map);
            t->createComponent = const_cast<ModelDescription*>(_model);
            t->radiusComponent = tmpl->FindDescription<RadiusDescription>();
            t->meshVariant = (item.variant >= 0 && item.variant < _model->_meshGroupCount) ? item.variant : 0;
            Math::Vector3 pos;
            RectCentre(rect, &pos);
            const float yaw = static_cast<float>((item.turn & 3) * 90);
            const bool placed = (t->CreateAt(rect.col, rect.row, rect.col + rect.w - 1, rect.row + rect.h - 1, &pos,
                                             yaw) &
                                 0xff) != 0;
            delete t;
            return placed;
        }
        case Kind::Field:
        case Kind::Pasture: {
            if (_tool == nullptr) {
                return false;
            }
            const int a = rect.row * map->size + rect.col;
            const int b = (rect.row + rect.h - 1) * map->size + (rect.col + rect.w - 1);
            _tool->startCell = a;
            _tool->endCell = b;
            if (_rotation != nullptr) {
                _rotation->rotation = FencedRotationFor(item.turn);
            }
            return _tool->CalcPath(a, b);
        }
        case Kind::Road: {
            if (_roadTmpl == nullptr || !Check(item, rect, nullptr, nullptr)) {
                return false;
            }
            const CreateRoadDescription* road = _roadTmpl->FindDescription<CreateRoadDescription>();
            const MapDescription* md = _roadTmpl->FindDescription<MapDescription>();
            if (road == nullptr || md == nullptr) {
                return false;
            }
        // RoadTool::CalcPath's per-commit steps (roadtool_calcpath.cpp): the road type's mesh batch material,
        // then one Map::PlaceRoadCell per cell.
            GameUpdateLoop* g = GameUpdateLoop::_singleton;
            const DecalDescription* decal = _roadTmpl->FindDescription<DecalDescription>();
            if (g->m_worldGeometry != nullptr && decal != nullptr && decal->_materials != nullptr && decal->_materialCount > 0) {
                g->m_worldGeometry->StoreBatchMaterial(static_cast<int>(static_cast<uint8_t>(road->_b20)), decal->_materials[0]);
            }
            Neighbour::SharedMap::NoteRoadTemplate(static_cast<uint8_t>(road->_b20), _roadTmpl);
            const int cell = rect.row * map->size + rect.col;
            map->PlaceRoadCell(static_cast<uint32_t>(cell), static_cast<uint8_t>(road->_b20), static_cast<uint64_t>(cell),
                               md->_cellType, road->_created);
            return true;
        }
    }
    return false;
}

}  // namespace CopyMove
}  // namespace Ext

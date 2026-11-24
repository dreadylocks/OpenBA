// ============================================================================
// src/ext/copy_move/teleport.h -- the instant move (docs/fork/COPY_MOVE.md)
// ============================================================================
// NOT RETAIL. A move lifts the moving things off the map, tests their new places, and sets them down there
// -- or, when one does not fit, back where they were. The entity stays the same object, so its workers,
// residents, stored goods and every pointer to it carry on.
//
// WHAT MOVES (the user's ruling, 2026-11-24: instant). A finished fixed-footprint building (its cells
// claimed, its BuildComponent complete, not being removed) and road cells (finished ones stay finished, ones
// being built stay planned). Fields, pastures, orchards and unfinished sites are copied but not moved yet:
// their crops, trees, animals, fences and queued jobs hold absolute cells (handins in COPY_MOVE.md §4).
//
// HOW A BUILDING IS LIFTED AND SET DOWN. Not by its components' OnRemove / OnAdded as a whole: several are
// spawn / destroy hooks (a WorkPlace releases its workers, a Residence evicts, a Storage empties). Only the
// components that put the building on the map are cycled, each with its own pair (COPY_MOVE.md §3):
//   MapComponent      OnRemove (cells, minimap)    ... AddToMap (only when it had claimed its cells)
//   ZoneComponent     OnRemove (blocker, zones)    ... OnAdded
//   DecalComponent    OnRemove                     ... OnAdded (retail's own event-5 rebuild pair)
//   PickingComponent  OnRemove                     ... RefreshMesh (after the model moved)
//   ModelComponent    OnRemove (scene)             ... SetTransformFromYaw + OnAdded
//   FloorComponent    OnRemove when placed         ... PlaceFloor
//   ParticleComponent (kept)                       ... StartEmitter for each running emitter (re-placed)
// Every ModelComponent and every MultiObject child moves rigidly with the building's centre and turn.
// Citizens' actions that name the building are dropped first (TownMgr_AbortCitizenActionsFor, as retail does
// when a storage burns), so nobody walks to the old door.
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "ext/copy_move/layout.h"
#include "ext/copy_move/place.h"

class ComponentEntity;

namespace Ext {
namespace CopyMove {

// Whether the tool can move this entity now (see WHAT MOVES).
bool CanTeleport(ComponentEntity* entity);

// The move: each item of `layout` with a source goes to its rect with the box at `origin`, all or none.
// `placers` are the items' placers, in `layout` order. On success `moved` gets the moved entities.
bool Teleport(const Layout& layout, const Cell& origin, const std::vector<std::unique_ptr<Placer>>& placers,
              std::vector<ComponentEntity*>* moved);

}  // namespace CopyMove
}  // namespace Ext

// ============================================================================
// src/ext/create_on_demand/create_on_demand.cpp -- NOT RETAIL. ext.create_on_demand
// ============================================================================
// Class A (docs/extensions/handins/CREATEONDEMAND.md); create_on_demand.h has the contract.
// ============================================================================

#include "ext/create_on_demand/create_on_demand.h"

#include "create_on_demand/create_on_demand_backend.h"
#include "create_on_demand/payload.h"
#include "ext/create_on_demand/level_source.h"
#include "ext/create_on_demand/prefetch.h"
#include "graphics/Mesh.h"
#include "graphics/ParticleInstance.h"
#include "Game/WorldGeometry.h"
#include "ext/registry.h"
#include "io/FileSystem.h"
#include "ui/SpriteSheet.h"
#include "video/ImageBuffer.h"

#include <cstdio>

namespace Ext { namespace CreateOnDemand {

namespace {

void HandleClosing(Platform::FileHandle h) {
    ::CreateOnDemand::Maps().Detach(h);
}

}  // namespace

void Boot() {
    const bool on = Ext().create_on_demand && CreateOnDemandBackend::Supported();
    if (Ext().create_on_demand && !on)
        std::fprintf(stderr, "[CreateOnDemand] the renderer does not support deferred creates: all made at load\n");
    CreateOnDemandBackend::Request(on, Val().create_on_demand_idle_min);
    ::IO::FileSystem::_extHandleClosing = on ? &HandleClosing : nullptr;
    ::Video::ImageBuffer::_extLevelSource = on ? &ReadLevels : nullptr;
    ::Graphics::MeshInstance::_extMeshAssigned = on ? &PrefetchMesh : nullptr;
    ::Graphics::MeshInstance::_extMaterialAssigned = on ? &PrefetchMaterial : nullptr;
    ::Graphics::MeshInstance::_extLevelGathered = on ? &PrefetchGathered : nullptr;
    ::Graphics::ParticleInstance::_extParticleAssigned = on ? &PrefetchParticle : nullptr;
    ::UI::SpriteSheet::_extLoaded = on ? &KeepSpriteSheet : nullptr;
    ::WorldGeometry::_extMaterialTaken = on ? &KeepMaterial : nullptr;
}

}}  // namespace Ext::CreateOnDemand

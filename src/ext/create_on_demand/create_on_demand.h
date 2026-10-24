#pragma once
// src/ext/create_on_demand/create_on_demand.h -- ext.create_on_demand (class A, next start, default ON)
//
// NOT RETAIL (docs/extensions/handins/CREATEONDEMAND.md, issue #13). Retail creates every static
// vertex buffer, index buffer and texture of a resource file while the file loads: a MegaMod 7-part
// set makes ~9.8 GB of them at boot, of which well under 1 % is drawn in a session. With the row on,
// the renderer backend records each one whose bytes it can read again from the package instead of
// creating it, the load skips those bytes, and the object is created at its first use (a bind, a
// lock, a read), from the bytes re-decoded out of a read-only mapping of the package.
//
// WHERE IT RUNS.
//   - The backend (src/render/common/create_on_demand/create_on_demand_backend.h; the DX11 body is
//     src/render/dx11/create_on_demand/): Boot passes the row's value to CreateOnDemandBackend::
//     Request, once (objects already made stay as they are: the toggle is for the next start).
//   - IO::FileSystem::_extHandleClosing: a package handle the engine closes is never used to find the
//     extension's own mapping again.
//   - Video::ImageBuffer::_extLevelSource (level_source.h): a file-seeded texture's level sizes are
//     read, its level bytes skipped; the backend's Create records the texture for its first use.
//   - Graphics::MeshInstance::_extMeshAssigned (prefetch.h): when the world gives an instance its
//     mesh, the first-use creates of its buffers and textures are queued for the backend's workers.
// Never under the faithful profile.

namespace Ext { namespace CreateOnDemand {

// Boot only (registry_boot.cpp, before the first load).
void Boot();

}}  // namespace Ext::CreateOnDemand

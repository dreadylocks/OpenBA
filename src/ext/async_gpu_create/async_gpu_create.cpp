// ============================================================================
// src/ext/async_gpu_create/async_gpu_create.cpp -- NOT RETAIL. ext.async_gpu_create
// ============================================================================
// Class A (docs/extensions/handins/ASYNCGPUCREATE.md); async_gpu_create.h has the contract.
// ============================================================================

#include "ext/async_gpu_create/async_gpu_create.h"

#include "async_gpu_create/async_gpu_create_backend.h"
#include "ext/registry.h"

namespace Ext { namespace AsyncGpuCreate {

void Sync() {
    AsyncGpuCreateBackend::Request(Ext().async_gpu_create);
}

}}  // namespace Ext::AsyncGpuCreate

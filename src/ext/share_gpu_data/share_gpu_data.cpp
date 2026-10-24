// ============================================================================
// src/ext/share_gpu_data/share_gpu_data.cpp -- NOT RETAIL. ext.share_gpu_data
// ============================================================================
// Class A (docs/extensions/handins/SHAREGPUDATA.md); share_gpu_data.h has the contract.
// ============================================================================

#include "ext/share_gpu_data/share_gpu_data.h"

#include "share_gpu_data/share_gpu_data_backend.h"
#include "ext/registry.h"

#include <cstdio>

namespace Ext { namespace ShareGpuData {

void Boot() {
    const bool on = Ext().share_gpu_data && ShareGpuDataBackend::Supported();
    if (Ext().share_gpu_data && !on)
        std::fprintf(stderr, "[ShareGpuData] the renderer does not support shared objects: one per create\n");
    ShareGpuDataBackend::Request(on);
}

}}  // namespace Ext::ShareGpuData

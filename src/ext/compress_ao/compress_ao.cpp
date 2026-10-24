// ============================================================================
// src/ext/compress_ao/compress_ao.cpp -- NOT RETAIL. ext.compress_ao
// ============================================================================
// Class A (docs/extensions/handins/COMPRESSAO.md); compress_ao.h has the contract.
// ============================================================================

#include "ext/compress_ao/compress_ao.h"

#include "compress_ao/compress_ao_backend.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/registry.h"

#include <cstdio>

namespace Ext { namespace CompressAo {

void Boot() {
    const bool on = Ext().compress_ao && CompressAoBackend::Supported();
    if (Ext().compress_ao && !on)
        std::fprintf(stderr, "[CompressAo] the renderer does not support BC4 creates: the AO maps stay R8\n");
    CompressAoBackend::Request(on, on ? DerivedCache::OpenShared() : nullptr);
}

}}  // namespace Ext::CompressAo

#pragma once
// ============================================================================
// src/render/metal/seeded_create.h -- the Metal backend's creates of data-seeded buffers and textures
// ============================================================================
// NOT RETAIL. One body for the two places that make a seeded object: the load (VertexBufferData /
// IndexBufferData::Create, ImageBufferData::Create in backend.mm) and ext.create_on_demand's first-use
// create (create_on_demand/lazy_create.mm), so both run the same chain in the same order:
// ext.compress_ao's BC4 conversion (PlanUploads), then ext.share_gpu_data's create-or-share
// (MakeTexture / MakeBuffer), else the plain create. Defined in backend.mm (they use its device and
// its allocation bookkeeping). Any thread: the device's creates are thread-safe and the bookkeeping
// takes the backend's state lock.
// ============================================================================

#import <Metal/Metal.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "share_gpu_data/share_create.h"

namespace VideoMetal {

// What ImageBufferData::Create reads of its Video::ImageBuffer, kept by value so a deferred create
// does not read the image again.
struct TextureShape {
    int      width      = 0;
    int      height     = 0;
    int      format     = 0;   // the engine format (0..6; 7, depth, is never seeded)
    int      kind       = 0;   // 2: seeded from a resource file
    unsigned faces      = 1;   // 6 for a cube
    int      levelCount = 1;   // Video::ImageBuffer::GetLevelCount
};

// One level blob as the engine hands it to Create.
struct SourceLevel {
    const void* data;
    size_t      size;
};

// One level as it is uploaded: R8 rows, a retail format's blocks, or ext.compress_ao's BC4 blocks.
struct LevelUpload {
    unsigned                 mip;
    unsigned                 face;
    int                      mw;
    int                      mh;
    ShareGpuData::LevelBytes bytes;
};

struct UploadPlan {
    std::vector<uint8_t>     bc4;          // ext.compress_ao's encoded levels (the uploads point into it)
    std::vector<LevelUpload> uploads;
    bool                     shortLevel = false;   // a level smaller than its upload: the ones before it only
    bool                     complete   = false;   // every mip of every face is uploaded
};

// The uploads Create makes for `count` levels on descriptor `d`. ext.compress_ao: a 2D R8 kind-2
// texture with the whole chain and a top level a multiple of 4 each way becomes BC4 (d's pixel format
// changes, the plan holds the encoded blocks).
void PlanUploads(const TextureShape& shape, MTLTextureDescriptor* d, const SourceLevel* levels, int count,
                 UploadPlan* plan);

// The texture of `d` with the plan's uploads; `seeded` (kind 2 with levels) goes through ext.share_gpu_data's
// create-or-share. *shared: the texture is in the share table. Nil on failure.
id<MTLTexture> MakeTexture(MTLTextureDescriptor* d, const UploadPlan& plan, bool seeded, bool* shared);

// A vertex (`index` false) or index buffer of `size` bytes filled from `data` (null: unfilled), through
// ext.share_gpu_data's create-or-share. *shared: the buffer is in the share table. Nil on failure.
id<MTLBuffer> MakeBuffer(bool index, bool dynamic, const void* data, uint32_t size, bool* shared);

// A buffer its companion lets go of outside the destructor (ext.create_on_demand's eviction): the
// backend's caches keyed by it drop it (the widened-input cache holds a strong reference on its source,
// which would keep an evicted buffer alive), as ~VertexBufferData does.
void ForgetBufferUses(id<MTLBuffer> buffer);

}  // namespace VideoMetal

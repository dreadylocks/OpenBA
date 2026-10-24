#pragma once
// ============================================================================
// src/render/metal/share_gpu_data/share_create.h -- ext.share_gpu_data, the Metal side
// (docs/extensions/handins/SHAREGPUDATA.md, issue #13)
// ============================================================================
// NOT RETAIL. The Metal twin of src/render/dx11/share_gpu_data/share_create.h. With the row on, the
// creates of objects the backend never writes after creation go through here:
//   - a static (not dynamic), data-seeded vertex / index buffer (VertexBufferData / IndexBufferData
//     ::Create: D3D11 makes these IMMUTABLE), and
//   - a texture seeded from a resource file (ImageBufferData::Create, kind 2: MTLTextureUsageShaderRead
//     only, every mip of every face uploaded, after ext.compress_ao's conversion);
// each is keyed by the device, its descriptor (length / storage mode for a buffer; pixel format, type,
// size, mip count, array length, usage and storage mode for a texture) and the XXH64 of every seed
// byte (chained over the levels with each one's row pitch and size), and an equal key hands out the
// object made first. Samplers stay per object.
//
// IMMUTABILITY. Metal itself would let the backend write these objects (storage mode Shared); the
// backend's own write paths refuse an object that came through here, exactly as D3D11 refuses an
// IMMUTABLE one: SetData does nothing and returns true (UpdateSubresource on an immutable buffer is a
// no-op), Lock returns null (Map fails), ImageBufferData::Update returns false. Read and the save arm
// (Serialize) only copy from it. A buffer read back by Serialize's load arm is created unseeded, so it
// is never shared.
//
// LIFETIMES (ARC). The table (src/render/common/share_gpu_data/share_table.h) holds a +1 of its own on
// each entry's object (CFBridgingRetain at Insert, CFBridgingRelease when the last user drops or at
// Clear). A user's own reference is its strong member (`buffer` / `texture`). Every user drops itself
// (DropBuffer / DropTexture) before that member lets go of the object: the destructors, a re-Create,
// ImageBufferData::Attach*. DeviceData::Shutdown (exit, a device rebuild) clears the table.
// ============================================================================

#import <Metal/Metal.h>

#include <cstddef>
#include <cstdint>

namespace VideoMetal {
namespace ShareGpuData {

// The row's boot value (ShareGpuDataBackend::Request). One relaxed load.
bool Requested();

// The bytes one texture level is seeded with, as ImageBufferData::Create uploads them.
struct LevelBytes {
    const void* data;
    size_t      row;    // bytesPerRow
    size_t      size;   // the bytes replaceRegion reads
};

// A vertex (`index` false) or index buffer of `size` bytes seeded with `data`. Not dynamic, seeded and
// the row on: shared; anything else is `make()` as before. `make` returns a new buffer filled with the
// seed (nil on failure). *shared is true when the returned buffer is in the table (its user must
// DropBuffer it and never write it).
id<MTLBuffer> CreateBuffer(id<MTLDevice> device, bool index, bool dynamic, const void* data, size_t size,
                           id<MTLBuffer> (^make)(void), bool* shared);

// A kind-2 texture described by `desc` whose every subresource is `levels[0..count)`. `qualifies` is
// false when it is not shareable (a level missing or short): it is then `make()`, counted as refused.
// `make` returns the texture created and uploaded (nil on failure). *shared as CreateBuffer.
id<MTLTexture> CreateTexture(id<MTLDevice> device, MTLTextureDescriptor* desc, const LevelBytes* levels, int count,
                             bool qualifies, id<MTLTexture> (^make)(void), bool* shared);

// Before a shared user lets go of its object. When it was the last user the table's reference goes.
void DropBuffer(id<MTLBuffer> buffer);
void DropTexture(id<MTLTexture> texture);

// DeviceData::Shutdown: the table forgets every object and releases its references (users keep theirs).
void Clear();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything was shared since
// the last.
void LoadEnd();

}  // namespace ShareGpuData
}  // namespace VideoMetal

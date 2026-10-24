#pragma once
// ============================================================================
// src/render/dx11/create_on_demand/lazy_create.h -- ext.create_on_demand, the DX11 side
// (docs/extensions/handins/CREATEONDEMAND.md, issue #13)
// ============================================================================
// NOT RETAIL. Retail's VideoDX11 creates every static buffer and texture in its load: the vertex /
// index buffers in GpuBuffer_Serialize's load arm, the file-seeded textures in ImageBufferData::Create.
// With the row on:
//   - a STATIC buffer (not dynamic, size > 0, no buffer yet) read from a packed resource file is not
//     read: its Locator is recorded with the descriptor GpuBuffer_Create would build, the stream skips
//     the payload, and the owner keeps the size / dynamic it just read (Create's two stores);
//   - a usage-2 texture whose level bytes the engine's level reader skipped (Video::ImageBuffer::
//     _extLevelSource, pending_levels.h) runs every engine-visible step of Create (the registry, the
//     released interfaces, the descriptor, the pitches) and makes its SAMPLER as Create does; only the
//     texture and its SRV are recorded.
// The object is made at its first use: Ensure(this) at the top of every slot that reads its handles
// (Bind, Lock, SetData, Update, Read, the save arm), right after ext.async_gpu_create's Await; the
// fast path is the handle's own null test. A made object unused for ext.create_on_demand_idle_min
// minutes is given back (handles released and nulled, the record idle again) and re-made at its next
// use; each such slot holds a UseScope for its body, so no eviction meets a use. The materialization re-decodes the payload from the
// package (payload.h) and runs the create the load would have run: ext.compress_ao's BC4 conversion
// (its derived cache keeps the encode), then ext.share_gpu_data's create-or-share (it hashes the bytes
// there), else the plain create. Destructors and re-creates Forget(this) first.
// Off (the row, /profile:faithful): one relaxed load per site, retail's path.
// ============================================================================

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>

#include "create_on_demand/lazy_table.h"

namespace IO { class Stream; }

namespace VideoDX11 {
namespace CreateOnDemand {

// The table while the row is on (set once at boot, before the first load), else null.
extern std::atomic<::CreateOnDemand::LazyTable*> g_table;

inline bool Requested() {
    return g_table.load(std::memory_order_relaxed) != nullptr;
}

// Before a use of a companion whose handle is null: make it now (or wait for the worker making it).
inline void Ensure(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) t->Ensure(obj);
}

// Before a destructor or a re-create: no deferred create of `obj` runs after this.
inline void Forget(const void* obj) {
    if (::CreateOnDemand::LazyTable* const t = g_table.load(std::memory_order_relaxed)) t->Forget(obj);
}

// GpuBuffer_Serialize's load arm, before its SerializeBytes: record the buffer's create (`desc`, the
// handle at `*out`) and skip its payload in `stream`. False: nothing done, the load reads the payload.
bool DeferBuffer(const void* obj, IO::Stream* stream, const D3D11_BUFFER_DESC& desc, ID3D11Buffer** out);

// ImageBufferData::Create of a usage-2 texture: true when the engine's level reader skipped this
// image's level bytes (it left their Locators pending for `owner`). Takes them.
bool HasPendingLevels(const void* owner);

// The texture's deferred create: `desc` and `pitches[i]` as Create built them for `count`
// subresources, the SRV descriptor (null = the default view), and, when `bc4SourceFormat` is not 0,
// ext.compress_ao's conversion of R8 levels of that engine format. Writes `*outTexture` / `*outSrv`.
void DeferTexture(const void* obj, const void* owner, const D3D11_TEXTURE2D_DESC& desc, const UINT* pitches,
                  int count, const D3D11_SHADER_RESOURCE_VIEW_DESC* srvDesc, uint32_t bc4SourceFormat,
                  ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSrv);

// DeviceData::Shutdown (exit, a device-lost recovery, a rebuild): no materialization runs against
// the device going away. DeviceData::CreateDevice's success lets them go on.
void Pause();
void Resume();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything changed.
void LoadEnd();
// After a sweep gave objects back (the table's sweep hook): the same summary.
void AfterSweep(int64_t evicted);

// Every slot that reads a companion's handles holds one for its whole body: no eviction runs while it
// does (the table's UseGate), and the object's use is stamped. Nothing while the row is off.
class UseScope {
public:
    explicit UseScope(const void* obj) : _table(g_table.load(std::memory_order_relaxed)) {
        if (_table == nullptr) return;
        _table->Enter();
        _table->Use(obj);
    }
    ~UseScope() {
        if (_table != nullptr) _table->Leave();
    }
    UseScope(const UseScope&) = delete;
    UseScope& operator=(const UseScope&) = delete;

private:
    ::CreateOnDemand::LazyTable* const _table;
};

}  // namespace CreateOnDemand
}  // namespace VideoDX11

#pragma once
// src/ext/async_gpu_create/async_gpu_create.h -- ext.async_gpu_create (class A, live: next load)
//
// NOT RETAIL (docs/extensions/handins/ASYNCGPUCREATE.md, issue #13). Retail creates every GPU
// resource synchronously inside its Serialize, on the loader thread; with a large mod set a third of
// the loader's time is the driver's CreateBuffer / CreateTexture2D. With the row on, the renderer
// backend makes the loader's data-seeded immutable buffers and plain textures on worker threads while
// the loader keeps deserialising: the same resources with the same contents and the same failure
// state, every one of them made before the load ends, and any earlier use waits for its own create.
//
// WHERE IT RUNS. Entirely in the backend (src/render/common/async_gpu_create/
// async_gpu_create_backend.h; the DX11 body is src/render/dx11/async_gpu_create/): Sync passes the
// row's value to AsyncGpuCreateBackend::Request, which the backend reads when the loader opens its
// next load. A backend without the worker path, or a driver that reports no concurrent creates,
// keeps retail's synchronous creates.

namespace Ext { namespace AsyncGpuCreate {

// The backend's request follows Ext().async_gpu_create (boot, and every toggle: the next load).
void Sync();

}}  // namespace Ext::AsyncGpuCreate

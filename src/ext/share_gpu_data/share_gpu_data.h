#pragma once
// src/ext/share_gpu_data/share_gpu_data.h -- ext.share_gpu_data (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/SHAREGPUDATA.md, issue #13). Retail creates one GPU object per
// engine object, even when two resource files carry the very same mesh or texture: on a MegaMod
// 7-part boot 2.5 GB of the 9.8 GB of file-seeded GPU data are byte-identical copies (mesh variants,
// the *_edge meshes, AO maps shared by building variants). With the row on, the renderer backend
// makes each immutable, data-seeded vertex buffer, index buffer and texture once per distinct
// (descriptor, bytes) and hands that one object to every engine object that asks for an equal one.
// Immutable objects cannot be written, so what each engine object draws is unchanged.
//
// WHERE IT RUNS. In the backend (src/render/common/share_gpu_data/share_gpu_data_backend.h; the DX11
// body is src/render/dx11/share_gpu_data/, the table src/render/common/share_gpu_data/share_table.h).
// Boot passes the row's value to ShareGpuDataBackend::Request, once: objects already made stay as
// they are, so the toggle is recorded for the next start. Never under the faithful profile.

namespace Ext { namespace ShareGpuData {

// Boot only (registry_boot.cpp, before the first load).
void Boot();

}}  // namespace Ext::ShareGpuData

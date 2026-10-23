// ============================================================================
// src/render/dx11/render_backend_dx11.cpp -- the DX11 backend's src/render/common/render_backend.h
// ============================================================================
// NOT RETAIL. Thin forwarders from the backend-neutral entry points to the DX11
// plugin's own (src/render/dx11/VideoDX11Plugin.h): VideoDX11-x64.dll's two exports
// (src/render/dx11/exports.cpp, 0x1800011a0 / 0x180001230) and the cutover-only DX9
// placeholder (src/render/dx11/dx9_placeholder.cpp). Built only with
// BANISHED_RENDER_BACKEND=dx11 (src/render/dx11/backend.cmake).
// ============================================================================

#include "render_backend.h"     // src/render/common: the declarations
#include "VideoDX11Plugin.h"    // VideoDX11::Dx11_* / Dx9Placeholder_*

namespace Render {

const char* DeviceName() {
    return VideoDX11::Dx11_GetDeviceName();
}

::Video::DeviceData* CreateDeviceData(::Video::Device* device) {
    return VideoDX11::Dx11_CreateDeviceData(device);
}

uint64_t GraphicsWindowFlags() {
    return 0;
}

const char* PlaceholderDeviceName() {
    return VideoDX11::Dx9Placeholder_GetDeviceName();
}

::Video::DeviceData* PlaceholderCreateDeviceData(::Video::Device* device) {
    return VideoDX11::Dx9Placeholder_CreateDeviceData(device);
}

}  // namespace Render

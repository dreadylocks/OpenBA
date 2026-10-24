// ============================================================================
// src/platform/posix/websocket_posix.cpp -- NOT RETAIL. The WebSocket seam off Windows
// ============================================================================
// platform_websocket.h. Not implemented yet on Linux (Windows has WinHTTP, macOS its
// Network.framework twin, src/platform/darwin/websocket_darwin.cpp). Connect fails with a
// reason, so the extension reports "offline" and the game carries on.
// ============================================================================

#include "platform/common/platform_websocket.h"

namespace Platform {

WebSocketHandle WebSocketConnect(const char* /*url*/, uint32_t /*timeoutMs*/, std::string& error) {
    error = "neighbour towns are not available on this platform yet";
    return nullptr;
}
bool WebSocketSendText(WebSocketHandle, const char*, size_t) { return false; }
WebSocketRecv WebSocketReceiveText(WebSocketHandle, std::string& out, size_t) {
    out.clear();
    return WebSocketRecv::Closed;
}
void WebSocketCancel(WebSocketHandle) {}
void WebSocketFree(WebSocketHandle) {}

}  // namespace Platform

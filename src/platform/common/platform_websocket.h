#pragma once
// ============================================================================
// src/platform/common/platform_websocket.h -- NOT RETAIL. A WebSocket client seam
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. For ext.neighbour
// (src/ext/neighbour/, docs/fork/NEIGHBOUR_TOWNS.md §3): text frames to the relay.
//   Windows: src/platform/win/websocket_win.cpp, the OS's own WinHTTP client (ws:// and
//            wss://, TLS from the OS; no third-party library).
//   macOS:   src/platform/darwin/websocket_darwin.cpp, the OS's Network.framework client (ws:// and
//            wss://, TLS from the OS).
//   Linux:   src/platform/posix/websocket_posix.cpp, sockets + OpenSSL (TLS 1.2+, the system CA
//            store); its pure parts in websocket_frames.{h,cpp}.
//
// THREADING. One thread may block in WebSocketReceiveText while another calls
// WebSocketSendText; WebSocketCancel may be called from any thread and makes a blocked
// receive return. WebSocketFree only after both are done with the handle.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace Platform {

struct WebSocketObject;
using WebSocketHandle = WebSocketObject*;

// Blocking connect + upgrade to `url` ("ws://host[:port]/path" or "wss://..."). Name
// resolution, connect and the upgrade each give up after `timeoutMs`. nullptr on failure,
// with a short reason in `error`.
WebSocketHandle WebSocketConnect(const char* url, uint32_t timeoutMs, std::string& error);

// One UTF-8 text frame. False when the line is gone.
bool WebSocketSendText(WebSocketHandle ws, const char* data, size_t size);

enum class WebSocketRecv : uint8_t {
    Text,     // `out` holds one whole message
    Closed,   // the peer closed, or WebSocketCancel ran
    Error,    // the line failed, or a message was longer than `maxBytes`
};
// Blocking: the next whole text message (fragments joined; binary messages skipped).
WebSocketRecv WebSocketReceiveText(WebSocketHandle ws, std::string& out, size_t maxBytes);

// Unblock a pending receive and refuse further I/O. Any thread; idempotent.
void WebSocketCancel(WebSocketHandle ws);
// Release the handle (after every thread is done with it). Null is fine.
void WebSocketFree(WebSocketHandle ws);

}  // namespace Platform

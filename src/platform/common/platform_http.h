#pragma once
// ============================================================================
// src/platform/common/platform_http.h -- NOT RETAIL. One HTTP(S) POST
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. For ext.crash_reports
// (src/ext/crash_report/): a crash report goes to a Sentry-compatible server. Each twin carries it
// beside its WebSocket client (platform_websocket.h), which owns the connection code it reuses:
//   Windows: src/platform/win/websocket_win.cpp (WinHTTP; TLS from the OS)
//   macOS:   src/platform/darwin/websocket_darwin.cpp (Network.framework; TLS from the OS)
//   Linux:   src/platform/posix/websocket_posix.cpp (sockets + OpenSSL, the system CA store)
// The request and the status line: src/platform/common/websocket_frames.h (PostRequest, HttpStatus).
// ============================================================================

#include <cstdint>
#include <string>

namespace Platform {

// Blocking: POST `body` to `url` ("https://host[:port]/path?query" or "http://...") as
// `contentType`. Name resolution, connect and the exchange each give up after `timeoutMs`.
// The answer's HTTP status, or 0 when none came (a short reason in `error`). Any thread.
int HttpPost(const char* url, const char* contentType, const std::string& body, uint32_t timeoutMs,
             std::string& error);

}  // namespace Platform

#pragma once
// ============================================================================
// src/platform/common/platform_http.h -- NOT RETAIL. One HTTP(S) POST, one GET
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. HttpPost is for ext.crash_reports
// (src/ext/crash_report/): a crash report goes to a Sentry-compatible server. HttpGet is for
// ext.auto_update (src/ext/auto_update/, docs/fork/AUTO_UPDATE.md): the update manifest and the
// package. Each twin carries them beside its WebSocket client (platform_websocket.h), which owns the
// connection code they reuse:
//   Windows: src/platform/win/websocket_win.cpp (WinHTTP; TLS from the OS)
//   macOS:   src/platform/darwin/websocket_darwin.cpp (Network.framework; TLS from the OS)
//   Linux:   src/platform/posix/websocket_posix.cpp (sockets + OpenSSL, the system CA store)
// The POST's request and status line: src/platform/common/websocket_frames.h (PostRequest,
// HttpStatus). The GET's request, answer and redirects on macOS and Linux:
// src/platform/common/http_reader.h (WinHTTP does those itself).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Platform {

// Blocking: POST `body` to `url` ("https://host[:port]/path?query" or "http://...") as
// `contentType`. Name resolution, connect and the exchange each give up after `timeoutMs`.
// The answer's HTTP status, or 0 when none came (a short reason in `error`). Any thread.
int HttpPost(const char* url, const char* contentType, const std::string& body, uint32_t timeoutMs,
             std::string& error);

// Blocking: GET `url` ("https://..." or "http://..."), following up to five redirects. The body of a
// 200 answer goes to `sink` in pieces as it arrives; `sink` returns false to stop (the call then
// fails with "stopped"). A body longer than `maxBytes` fails. Name resolution, connect and each wait
// for data give up after `timeoutMs`. Returns the final HTTP status (200 = the whole body reached
// `sink`), or 0 when none came, with a short reason in `error` (also set for a status other than 200).
// Any thread.
int HttpGet(const char* url, uint64_t maxBytes, uint32_t timeoutMs,
            const std::function<bool(const char* data, size_t size)>& sink, std::string& error);

}  // namespace Platform

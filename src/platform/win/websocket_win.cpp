// ============================================================================
// src/platform/win/websocket_win.cpp -- NOT RETAIL. The WebSocket seam on WinHTTP
// ============================================================================
// platform_websocket.h. Synchronous WinHTTP (Windows 8+): WinHttpOpen / Connect /
// OpenRequest with WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, SendRequest, ReceiveResponse,
// a 101 check, then WinHttpWebSocketCompleteUpgrade. wss:// is WINHTTP_FLAG_SECURE: the
// OS does TLS and checks the certificate. Cancel closes the WebSocket handle, which is how
// WinHTTP aborts a synchronous call in flight; a blocked receive then fails and reads as
// Closed.
//
// Platform::HttpPost (platform_http.h) lives here too: the same WinHTTP session, a POST.
// ============================================================================

#include "platform/common/platform_websocket.h"
#include "platform/common/platform_http.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <vector>

namespace Platform {

struct WebSocketObject {
    HINTERNET         session = nullptr;
    HINTERNET         connect = nullptr;
    HINTERNET         ws      = nullptr;
    std::atomic<bool> cancelled{false};
};

namespace {

std::wstring Widen(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    w.resize(static_cast<size_t>(n - 1));
    return w;
}

std::string Why(const char* step) {
    const DWORD e = GetLastError();
    char buf[96];
    wsprintfA(buf, "%s failed (WinHTTP error %lu)", step, static_cast<unsigned long>(e));
    return buf;
}

// `ws` is closed by WebSocketCancel when it ran (the handle value is left in place, so a
// thread still inside a call reads a closed handle, which WinHTTP rejects, never a torn one).
void CloseAll(WebSocketObject* o) {
    if (o->ws != nullptr && !o->cancelled.load()) WinHttpCloseHandle(o->ws);
    if (o->connect != nullptr) WinHttpCloseHandle(o->connect);
    if (o->session != nullptr) WinHttpCloseHandle(o->session);
    o->ws = o->connect = o->session = nullptr;
}

}  // namespace

WebSocketHandle WebSocketConnect(const char* url, uint32_t timeoutMs, std::string& error) {
    error.clear();
    std::wstring wurl = Widen(url != nullptr ? url : "");
    // WinHttpCrackUrl knows http(s) only: ws -> http, wss -> https.
    bool secure;
    if (wurl.compare(0, 6, L"wss://") == 0)      { secure = true;  wurl.replace(0, 3, L"https"); }
    else if (wurl.compare(0, 5, L"ws://") == 0)  { secure = false; wurl.replace(0, 2, L"http"); }
    else { error = "the server address must start with ws:// or wss://"; return nullptr; }

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    wchar_t extra[1024] = {};
    uc.lpszHostName = host;     uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = 1023;
    uc.lpszExtraInfo = extra;   uc.dwExtraInfoLength = 1023;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { error = "the server address is not a valid URL"; return nullptr; }
    std::wstring target = path[0] ? path : L"/";
    target += extra;

    auto* o = new WebSocketObject();
    o->session = WinHttpOpen(L"OpenBA-Neighbour/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (o->session == nullptr) { error = Why("WinHttpOpen"); delete o; return nullptr; }
    const int t = static_cast<int>(timeoutMs);
    // Resolve, connect, send, receive. The receive timeout stays infinite once the line is a
    // WebSocket: a quiet relay is not a dead one (the Link pings it).
    WinHttpSetTimeouts(o->session, t, t, t, t);

    o->connect = WinHttpConnect(o->session, host, uc.nPort, 0);
    if (o->connect == nullptr) { error = Why("WinHttpConnect"); CloseAll(o); delete o; return nullptr; }

    HINTERNET req = WinHttpOpenRequest(o->connect, L"GET", target.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (req == nullptr) { error = Why("WinHttpOpenRequest"); CloseAll(o); delete o; return nullptr; }
    bool ok = WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
              WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!ok) error = Why("connecting to the server");
    if (ok && !WinHttpReceiveResponse(req, nullptr)) { ok = false; error = Why("waiting for the server"); }
    if (ok) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status != 101) {
            ok = false;
            char buf[96];
            wsprintfA(buf, "the server answered HTTP %lu instead of a WebSocket", static_cast<unsigned long>(status));
            error = buf;
        }
    }
    if (ok) {
        o->ws = WinHttpWebSocketCompleteUpgrade(req, 0);
        if (o->ws == nullptr) { ok = false; error = Why("WinHttpWebSocketCompleteUpgrade"); }
    }
    WinHttpCloseHandle(req);
    if (!ok) { CloseAll(o); delete o; return nullptr; }
    DWORD infinite = 0;
    WinHttpSetOption(o->ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &infinite, sizeof(infinite));
    return o;
}

bool WebSocketSendText(WebSocketHandle o, const char* data, size_t size) {
    if (o == nullptr || o->cancelled.load()) return false;
    return WinHttpWebSocketSend(o->ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                const_cast<char*>(data), static_cast<DWORD>(size)) == ERROR_SUCCESS;
}

WebSocketRecv WebSocketReceiveText(WebSocketHandle o, std::string& out, size_t maxBytes) {
    out.clear();
    if (o == nullptr || o->cancelled.load()) return WebSocketRecv::Closed;
    std::vector<char> buf(4096);
    for (;;) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        const DWORD rc = WinHttpWebSocketReceive(o->ws, buf.data(), static_cast<DWORD>(buf.size()), &read, &type);
        if (rc != ERROR_SUCCESS)
            return o->cancelled.load() ? WebSocketRecv::Closed : WebSocketRecv::Error;
        switch (type) {
        case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
            return WebSocketRecv::Closed;
        case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
        case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
            break;   // binary: not ours, skipped
        case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
        case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
            if (out.size() + read > maxBytes) return WebSocketRecv::Error;
            out.append(buf.data(), read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) return WebSocketRecv::Text;
            break;
        default:
            break;
        }
    }
}

void WebSocketCancel(WebSocketHandle o) {
    if (o == nullptr) return;
    if (o->cancelled.exchange(true)) return;
    // Closing the handle aborts the synchronous receive in flight (it returns an error,
    // read as Closed above).
    if (o->ws != nullptr) WinHttpCloseHandle(o->ws);
}

void WebSocketFree(WebSocketHandle o) {
    if (o == nullptr) return;
    WebSocketCancel(o);
    CloseAll(o);
    delete o;
}

int HttpPost(const char* url, const char* contentType, const std::string& body, uint32_t timeoutMs,
             std::string& error) {
    error.clear();
    const std::wstring wurl = Widen(url != nullptr ? url : "");
    if (wurl.compare(0, 8, L"https://") != 0 && wurl.compare(0, 7, L"http://") != 0) {
        error = "the address must start with http:// or https://";
        return 0;
    }
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    wchar_t extra[2048] = {};
    uc.lpszHostName = host;     uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = 2047;
    uc.lpszExtraInfo = extra;   uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        error = "the address is not a valid URL";
        return 0;
    }
    std::wstring target = path[0] ? path : L"/";
    target += extra;

    WebSocketObject handles;   // its session and connect, closed by CloseAll
    handles.session = WinHttpOpen(L"OpenBA/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
    if (handles.session == nullptr) {
        error = Why("WinHttpOpen");
        return 0;
    }
    const int t = static_cast<int>(timeoutMs);
    WinHttpSetTimeouts(handles.session, t, t, t, t);
    handles.connect = WinHttpConnect(handles.session, host, uc.nPort, 0);
    if (handles.connect == nullptr) {
        error = Why("WinHttpConnect");
        CloseAll(&handles);
        return 0;
    }
    HINTERNET req = WinHttpOpenRequest(handles.connect, L"POST", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req == nullptr) {
        error = Why("WinHttpOpenRequest");
        CloseAll(&handles);
        return 0;
    }
    const std::wstring headers = L"Content-Type: " + Widen(contentType != nullptr ? contentType : "") + L"\r\n";
    const DWORD        size = static_cast<DWORD>(body.size());
    DWORD              status = 0;
    if (!WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L), const_cast<char*>(body.data()), size, size, 0)) {
        error = Why("sending");
    } else if (!WinHttpReceiveResponse(req, nullptr)) {
        error = Why("waiting for the answer");
    } else {
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    }
    WinHttpCloseHandle(req);
    CloseAll(&handles);
    return static_cast<int>(status);
}

}  // namespace Platform

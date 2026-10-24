// ============================================================================
// src/platform/darwin/websocket_darwin.cpp -- NOT RETAIL. The WebSocket seam on macOS
// ============================================================================
// platform_websocket.h, for ext.neighbour (docs/fork/NEIGHBOUR_TOWNS.md). Replaces
// posix/websocket_posix.cpp (the Linux client) on Apple (src/platform/darwin/README.md). The OS's own
// client, Network.framework (nw_connection + its WebSocket protocol), so ws:// and wss:// both
// work with TLS from the OS and no third-party library, as WinHTTP does on Windows.
//
// THREADS. Network.framework calls back on one private serial queue per connection. The callbacks
// only touch the WebSocketObject under its mutex and wake the game's threads through its
// condition variable; a receive is re-armed from its own completion. WebSocketFree cancels the
// connection, waits for the final "cancelled" state (after which nothing calls back) and drains
// the queue before releasing anything.
// ============================================================================

#include "platform/common/platform_websocket.h"

#include <Network/Network.h>
#include <dispatch/dispatch.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace Platform {

struct WebSocketObject {
    nw_connection_t         conn = nullptr;
    dispatch_queue_t        queue = nullptr;
    std::mutex              mu;
    std::condition_variable cv;
    bool                    ready = false;
    bool                    finished = false;    // failed, closed by the peer, or cancelled
    bool                    cancelled = false;   // the final state: no callback follows
    bool                    cancelRequested = false;
    std::string             error;
    std::deque<std::string> inbox;
};

namespace {

constexpr size_t   kMaxFrame       = 1u << 20;   // the OS refuses longer messages outright
constexpr uint32_t kSendTimeoutMs  = 10000;
constexpr uint32_t kCancelWaitMs   = 3000;

std::string Describe(nw_error_t error, const char* fallback) {
    if (error == nullptr) return fallback;
    const int code = nw_error_get_error_code(error);
    switch (nw_error_get_error_domain(error)) {
    case nw_error_domain_posix: return std::string(fallback) + ": " + std::strerror(code);
    case nw_error_domain_dns:   return std::string(fallback) + ": the relay's name was not found";
    case nw_error_domain_tls:   return std::string(fallback) + ": the secure (TLS) handshake failed";
    default:                    return std::string(fallback) + " (error " + std::to_string(code) + ")";
    }
}

nw_ws_opcode_t OpcodeOf(nw_content_context_t context) {
    if (context == nullptr) return nw_ws_opcode_invalid;
    nw_protocol_definition_t def = nw_protocol_copy_ws_definition();
    nw_protocol_metadata_t meta = nw_content_context_copy_protocol_metadata(context, def);
    nw_ws_opcode_t op = nw_ws_opcode_invalid;
    if (meta != nullptr) {
        op = nw_ws_metadata_get_opcode(meta);
        nw_release(meta);
    }
    nw_release(def);
    return op;
}

void Finish(WebSocketObject* ws, const std::string& why) {
    std::lock_guard<std::mutex> lock(ws->mu);
    if (!ws->finished && ws->error.empty()) ws->error = why;
    ws->finished = true;
    ws->cv.notify_all();
}

// One whole message at a time; the next receive is armed from this one's completion.
void ArmReceive(WebSocketObject* ws) {
    nw_connection_receive_message(ws->conn, ^(dispatch_data_t content, nw_content_context_t context,
                                              bool /*isComplete*/, nw_error_t error) {
        if (error != nullptr) {
            Finish(ws, Describe(error, "the line to the relay failed"));
            return;
        }
        const nw_ws_opcode_t op = OpcodeOf(context);
        if (op == nw_ws_opcode_close) {
            Finish(ws, "");
            return;
        }
        if (op == nw_ws_opcode_text && content != nullptr) {
            __block std::string text;
            dispatch_data_apply(content, ^bool(dispatch_data_t /*region*/, size_t /*offset*/, const void* buffer,
                                               size_t size) {
                text.append(static_cast<const char*>(buffer), size);
                return true;
            });
            std::lock_guard<std::mutex> lock(ws->mu);
            ws->inbox.push_back(std::move(text));
            ws->cv.notify_all();
        } else if (content == nullptr && op == nw_ws_opcode_invalid) {
            Finish(ws, "");   // the stream ended
            return;
        }
        // Binary, ping and pong messages: skipped (pings are answered by the OS).
        bool stop = false;
        {
            std::lock_guard<std::mutex> lock(ws->mu);
            stop = ws->finished;
        }
        if (!stop) ArmReceive(ws);
    });
}

void ReleaseAll(WebSocketObject* ws) {
    if (ws->conn != nullptr) {
        bool needCancel = false;
        {
            std::lock_guard<std::mutex> lock(ws->mu);
            needCancel = !ws->cancelRequested;
            ws->cancelRequested = true;
        }
        if (needCancel) nw_connection_cancel(ws->conn);
        std::unique_lock<std::mutex> lock(ws->mu);
        ws->cv.wait_for(lock, std::chrono::milliseconds(kCancelWaitMs), [ws] { return ws->cancelled; });
        lock.unlock();
        dispatch_sync(ws->queue, ^{});   // every callback queued before "cancelled" has run
        nw_release(ws->conn);
        ws->conn = nullptr;
    }
    if (ws->queue != nullptr) {
        dispatch_release(ws->queue);
        ws->queue = nullptr;
    }
    delete ws;
}

}  // namespace

WebSocketHandle WebSocketConnect(const char* url, uint32_t timeoutMs, std::string& error) {
    error.clear();
    if (url == nullptr || (std::strncmp(url, "ws://", 5) != 0 && std::strncmp(url, "wss://", 6) != 0)) {
        error = "the relay address must start with ws:// or wss://";
        return nullptr;
    }
    const bool secure = std::strncmp(url, "wss://", 6) == 0;
    nw_endpoint_t endpoint = nw_endpoint_create_url(url);
    if (endpoint == nullptr) {
        error = "the relay address is not a valid URL";
        return nullptr;
    }
    nw_parameters_t params = nw_parameters_create_secure_tcp(
        secure ? NW_PARAMETERS_DEFAULT_CONFIGURATION : NW_PARAMETERS_DISABLE_PROTOCOL,
        NW_PARAMETERS_DEFAULT_CONFIGURATION);
    nw_protocol_options_t wsOptions = nw_ws_create_options(nw_ws_version_13);
    nw_ws_options_set_auto_reply_ping(wsOptions, true);
    nw_ws_options_set_maximum_message_size(wsOptions, kMaxFrame);
    nw_protocol_stack_t stack = nw_parameters_copy_default_protocol_stack(params);
    nw_protocol_stack_prepend_application_protocol(stack, wsOptions);
    nw_release(stack);
    nw_release(wsOptions);

    WebSocketObject* ws = new WebSocketObject();
    ws->queue = dispatch_queue_create("openba.neighbour.websocket", DISPATCH_QUEUE_SERIAL);
    ws->conn = nw_connection_create(endpoint, params);
    nw_release(endpoint);
    nw_release(params);
    nw_connection_set_queue(ws->conn, ws->queue);
    nw_connection_set_state_changed_handler(ws->conn, ^(nw_connection_state_t state, nw_error_t err) {
        bool arm = false;
        {
            std::lock_guard<std::mutex> lock(ws->mu);
            switch (state) {
            case nw_connection_state_ready:
                ws->ready = true;
                arm = true;
                break;
            case nw_connection_state_waiting:   // no route, or refused: report it, do not wait for the OS's retry
            case nw_connection_state_failed:
                if (ws->error.empty()) ws->error = Describe(err, "cannot reach the relay");
                ws->finished = true;
                break;
            case nw_connection_state_cancelled:
                ws->finished = true;
                ws->cancelled = true;
                break;
            default:
                break;
            }
            ws->cv.notify_all();
        }
        if (arm) ArmReceive(ws);
    });
    nw_connection_start(ws->conn);

    bool ok = false;
    {
        std::unique_lock<std::mutex> lock(ws->mu);
        ws->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [ws] { return ws->ready || ws->finished; });
        ok = ws->ready && !ws->finished;
        if (!ok) error = !ws->error.empty() ? ws->error : std::string("the relay did not answer in time");
    }
    if (!ok) {
        ReleaseAll(ws);
        return nullptr;
    }
    return ws;
}

bool WebSocketSendText(WebSocketHandle ws, const char* data, size_t size) {
    if (ws == nullptr) return false;
    {
        std::lock_guard<std::mutex> lock(ws->mu);
        if (ws->finished) return false;
    }
    struct Done {
        std::mutex              mu;
        std::condition_variable cv;
        bool                    done = false;
        bool                    ok = false;
    };
    std::shared_ptr<Done> done = std::make_shared<Done>();
    dispatch_data_t body = dispatch_data_create(data, size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);   // copies
    nw_protocol_metadata_t meta = nw_ws_create_metadata(nw_ws_opcode_text);
    nw_content_context_t context = nw_content_context_create("text");
    nw_content_context_set_metadata_for_protocol(context, meta);
    nw_connection_send(ws->conn, body, context, true, ^(nw_error_t error) {
        std::lock_guard<std::mutex> lock(done->mu);
        done->done = true;
        done->ok = error == nullptr;
        done->cv.notify_all();
    });
    nw_release(context);
    nw_release(meta);
    dispatch_release(body);
    std::unique_lock<std::mutex> lock(done->mu);
    done->cv.wait_for(lock, std::chrono::milliseconds(kSendTimeoutMs), [&done] { return done->done; });
    return done->done && done->ok;
}

WebSocketRecv WebSocketReceiveText(WebSocketHandle ws, std::string& out, size_t maxBytes) {
    out.clear();
    if (ws == nullptr) return WebSocketRecv::Closed;
    std::unique_lock<std::mutex> lock(ws->mu);
    ws->cv.wait(lock, [ws] { return !ws->inbox.empty() || ws->finished; });
    if (!ws->inbox.empty()) {
        out = std::move(ws->inbox.front());
        ws->inbox.pop_front();
        if (out.size() > maxBytes) {
            out.clear();
            return WebSocketRecv::Error;
        }
        return WebSocketRecv::Text;
    }
    return (ws->cancelRequested || ws->error.empty()) ? WebSocketRecv::Closed : WebSocketRecv::Error;
}

void WebSocketCancel(WebSocketHandle ws) {
    if (ws == nullptr) return;
    bool needCancel = false;
    {
        std::lock_guard<std::mutex> lock(ws->mu);
        needCancel = !ws->cancelRequested;
        ws->cancelRequested = true;
        ws->finished = true;
        ws->cv.notify_all();
    }
    if (needCancel) nw_connection_cancel(ws->conn);
}

void WebSocketFree(WebSocketHandle ws) {
    if (ws == nullptr) return;
    ReleaseAll(ws);
}

}  // namespace Platform

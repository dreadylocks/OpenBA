#pragma once
// ============================================================================
// src/platform/common/http_reader.h -- NOT RETAIL. The pure half of Platform::HttpGet
// ============================================================================
// platform_http.h's GET on the twins that write HTTP/1.1 themselves (macOS on Network.framework,
// Linux on sockets + OpenSSL; Windows' WinHTTP does all of this itself). The twin only connects,
// sends the request and hands every received byte to a Response; this file writes the request,
// reads the answer's head and body (Content-Length, chunked, or until the line closes) and follows
// redirects. Pure: no OS header, no socket (tests/unit/http_reader_test.cpp).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "platform/common/websocket_frames.h"   // Url, ParseHttpUrl

namespace Platform {
namespace HttpReader {

constexpr int    kMaxRedirects = 5;
constexpr size_t kMaxHeadBytes = 64 * 1024;

// "GET <target> HTTP/1.1" with Host, Accept, Connection: close and the User-Agent.
std::string GetRequest(const WebSocketFrames::Url& url);

// `location` (a Location header's value) against the URL it answered: an absolute http(s) URL as it
// is, "/path" on the same scheme, host and port. "" for anything else (a relative path, another
// scheme).
std::string ResolveLocation(const WebSocketFrames::Url& base, const std::string& location);

using Sink = std::function<bool(const char* data, size_t size)>;

// One answer, fed in pieces as they arrive.
class Response {
public:
    enum class State {
        Head,      // still reading the status line and headers
        Body,      // a 200's body is arriving
        Done,      // the answer is complete (status() says which), or a non-200 head is in
        Failed,    // malformed, too long, cut short, or the sink said stop (error() says which)
    };

    // `maxBody`: a 200's body longer than this fails. `sink`: the 200's body, piece by piece.
    Response(uint64_t maxBody, Sink sink);

    // Feed received bytes; the state afterwards. Bytes after Done are ignored.
    State Feed(const char* data, size_t size);
    // The line closed: ends a body that runs until the close, fails one cut short.
    State Finish();

    State              state() const { return state_; }
    int                status() const { return status_; }
    const std::string& location() const { return location_; }
    const std::string& error() const { return error_; }
    uint64_t           bodyBytes() const { return delivered_; }

private:
    enum class Framing { Length, Chunked, Close };
    enum class Chunk { Size, Data, DataEnd, Trailer };

    State Fail(const std::string& why);
    bool  ParseHead(const std::string& head);
    bool  Deliver(const char* data, size_t size);
    State FeedChunked(const char* data, size_t size);

    uint64_t    maxBody_;
    Sink        sink_;
    State       state_ = State::Head;
    std::string head_;
    int         status_ = 0;
    std::string location_;
    std::string error_;
    Framing     framing_ = Framing::Close;
    uint64_t    remaining_ = 0;    // Length: body bytes still to come; Chunked: of this chunk
    uint64_t    delivered_ = 0;
    Chunk       chunk_ = Chunk::Size;
    std::string line_;             // Chunked: a size or trailer line being read
};

// One exchange on a fresh connection: connect to `url` (TLS for https), send `request`, then hand
// every received byte to `onBytes` until it returns false or the peer closes the line. False with a
// reason when the line could not be made or failed mid-way (a close by the peer is not a failure).
using Exchange = std::function<bool(const WebSocketFrames::Url& url, const std::string& request,
                                    const std::function<bool(const char*, size_t)>& onBytes, std::string& error)>;

// HttpGet's whole job on such a twin: the request, redirects (at most kMaxRedirects), the answer.
// Returns the final status (200: the whole body reached `sink`), or 0 with a reason.
int Get(const std::string& url, uint64_t maxBody, const Exchange& exchange, const Sink& sink, std::string& error);

}  // namespace HttpReader
}  // namespace Platform

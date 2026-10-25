#pragma once
// ============================================================================
// src/platform/common/websocket_frames.h -- NOT RETAIL. RFC 6455's wire format, pure
// ============================================================================
// The parts of a WebSocket client that are only bytes in, bytes out, for the Linux seam
// (src/platform/posix/websocket_posix.cpp, which adds the sockets and OpenSSL) and its tests
// (tests/unit/websocket_frames_test.cpp). No OS header and no OpenSSL: the SHA-1 the
// handshake needs is a small one here.
//
//   ParseUrl              "ws[s]://host[:port][/path][?query]" -> host, port, request target
//   AcceptKey             Sec-WebSocket-Key -> the Sec-WebSocket-Accept the server must send
//   UpgradeRequest        the client's HTTP/1.1 upgrade request
//   CheckUpgradeResponse  101, Upgrade, Connection and Sec-WebSocket-Accept checked
//   EncodeClientFrame     one masked frame, FIN set (the client never fragments)
//   Reader                server frames -> whole text messages, pings and the close; binary
//                         messages are dropped as they stream in, pongs ignored
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace Platform {
namespace WebSocketFrames {

// Opcodes (RFC 6455 §5.2).
constexpr uint8_t kContinuation = 0x0;
constexpr uint8_t kText         = 0x1;
constexpr uint8_t kBinary       = 0x2;
constexpr uint8_t kClose        = 0x8;
constexpr uint8_t kPing         = 0x9;
constexpr uint8_t kPong         = 0xA;

// Close codes the client sends (§7.4.1).
constexpr uint16_t kCloseNormal        = 1000;
constexpr uint16_t kCloseProtocolError = 1002;
constexpr uint16_t kCloseTooBig        = 1009;

struct Url {
    bool        secure = false;
    std::string host;         // for the resolver: an IPv6 literal without its brackets
    uint16_t    port = 0;     // 80 / 443 when the URL names none
    std::string target;       // path and query, at least "/" (the fragment is dropped)
    std::string hostHeader;   // the Host header: brackets kept, ":port" only when not the default
};
// False with a short reason in `error` (the twins' wording) when `url` is not ws:// or wss://,
// has no host, a bad port, credentials, or a space or control byte anywhere.
bool ParseUrl(const std::string& url, Url& out, std::string& error);

void        Sha1(const void* data, size_t size, uint8_t digest[20]);
std::string Base64(const void* data, size_t size);
// base64(SHA-1(key + the RFC's GUID)), §4.2.2.
std::string AcceptKey(const std::string& key);

// "GET <target> HTTP/1.1" with Host, Upgrade, Connection, Sec-WebSocket-Key and -Version 13.
std::string UpgradeRequest(const Url& url, const std::string& key);
// The offset just past the blank line that ends an HTTP head in `buffer`, or npos.
size_t HeadEnd(const std::string& buffer);

// Plain HTTP for Platform::HttpPost (platform_http.h), which the twins carry beside their WebSocket
// clients. ParseHttpUrl: ParseUrl's rules for "http[s]://..." (`secure` for https).
bool ParseHttpUrl(const std::string& url, Url& out, std::string& error);
// "POST <target> HTTP/1.1" with Host, Content-Type, Content-Length, Connection: close; the body follows.
std::string PostRequest(const Url& url, const char* contentType, size_t contentLength);
// The status code of an HTTP head ("HTTP/1.1 200 OK" -> 200), 0 when it is not one.
int HttpStatus(const std::string& head);
// `head`: the status line and headers (HeadEnd's span). True for a 101 whose Upgrade,
// Connection and Sec-WebSocket-Accept match `key` and that names no extension.
bool CheckUpgradeResponse(const std::string& head, const std::string& key, std::string& error);

// One whole client frame: FIN, `opcode`, the masked payload.
std::string EncodeClientFrame(uint8_t opcode, const void* payload, size_t size, const uint8_t mask[4]);
// A close frame's payload: the code big-endian, or empty for 0.
std::string ClosePayload(uint16_t code);

// The receiving side's frame parser. Append what the line delivered, then call Next until it
// says NeedMore. A message longer than `maxBytes` (counted across its fragments) or a protocol
// violation is Error, after which Next keeps returning Error and ErrorCode names the close
// code to send.
class Reader {
public:
    enum class Event : uint8_t {
        NeedMore,   // no whole message, ping or close buffered yet
        Text,       // `out`: one whole text message, fragments joined
        Ping,       // `out`: the ping's payload, to be sent back in a pong
        Close,      // `out`: the close frame's payload (code and reason, possibly empty)
        Error,      // see ErrorCode
    };

    void     Append(const void* data, size_t size);
    Event    Next(std::string& out, size_t maxBytes);
    uint16_t ErrorCode() const { return errorCode_; }

private:
    Event More();
    Event Fail(uint16_t code);

    std::string buf_;
    size_t      pos_ = 0;         // bytes of buf_ already consumed
    uint8_t     messageOp_ = 0;   // kText / kBinary while a fragmented message is open
    std::string message_;         // the open text message's fragments so far
    uint64_t    skip_ = 0;        // payload bytes of a binary frame still to drop
    uint16_t    errorCode_ = 0;   // non-zero once failed
};

}  // namespace WebSocketFrames
}  // namespace Platform

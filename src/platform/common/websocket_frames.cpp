// ============================================================================
// src/platform/common/websocket_frames.cpp -- NOT RETAIL. RFC 6455's wire format, pure
// ============================================================================
// websocket_frames.h. SHA-1 is FIPS 180-4 §6.1, used only for the handshake's accept key.
// ============================================================================

#include "platform/common/websocket_frames.h"

#include <cstring>

namespace Platform {
namespace WebSocketFrames {

namespace {

constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string LowerCopy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = Lower(c);
    return out;
}

bool StartsWithNoCase(const std::string& s, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (Lower(s[i]) != Lower(prefix[i])) return false;
    }
    return true;
}

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// True when the comma-separated `list` holds `token` (ASCII case-insensitive).
bool HasToken(const std::string& list, const char* token) {
    const std::string want = LowerCopy(token);
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (LowerCopy(Trim(list.substr(start, comma - start))) == want) return true;
        start = comma + 1;
    }
    return false;
}

uint32_t Rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void Sha1Block(uint32_t h[5], const uint8_t* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const uint32_t t = Rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rotl(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

bool IsDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

}  // namespace

bool ParseUrl(const std::string& url, Url& out, std::string& error) {
    out = Url();
    size_t rest;
    if (StartsWithNoCase(url, "wss://")) {
        out.secure = true;
        out.port = 443;
        rest = 6;
    } else if (StartsWithNoCase(url, "ws://")) {
        out.secure = false;
        out.port = 80;
        rest = 5;
    } else {
        error = "the relay address must start with ws:// or wss://";
        return false;
    }
    error = "the relay address is not a valid URL";
    // A space or control byte would end up inside the request line or a header.
    for (char c : url) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7F) return false;
    }
    size_t authorityEnd = url.find_first_of("/?#", rest);
    if (authorityEnd == std::string::npos) authorityEnd = url.size();
    const std::string authority = url.substr(rest, authorityEnd - rest);
    if (authority.find('@') != std::string::npos) return false;   // no credentials in a relay address

    std::string portText;
    bool        bracketed = false;
    if (!authority.empty() && authority[0] == '[') {
        const size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        out.host = authority.substr(1, close - 1);
        bracketed = true;
        const std::string after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after[0] != ':') return false;
            portText = after.substr(1);
            if (portText.empty()) return false;
        }
    } else {
        const size_t colon = authority.find(':');
        if (colon != std::string::npos) {
            if (authority.find(':', colon + 1) != std::string::npos) return false;   // IPv6 needs brackets
            portText = authority.substr(colon + 1);
            if (portText.empty()) return false;
        }
        out.host = authority.substr(0, colon);
    }
    if (out.host.empty()) return false;
    const uint16_t defaultPort = out.port;
    if (!portText.empty()) {
        if (!IsDigits(portText) || portText.size() > 5) return false;
        const unsigned long port = std::stoul(portText);
        if (port == 0 || port > 65535) return false;
        out.port = static_cast<uint16_t>(port);
    }

    size_t targetEnd = url.find('#', authorityEnd);
    if (targetEnd == std::string::npos) targetEnd = url.size();
    out.target = url.substr(authorityEnd, targetEnd - authorityEnd);
    if (out.target.empty() || out.target[0] == '?') out.target.insert(0, "/");

    out.hostHeader = bracketed ? "[" + out.host + "]" : out.host;
    if (out.port != defaultPort) out.hostHeader += ":" + std::to_string(out.port);
    error.clear();
    return true;
}

void Sha1(const void* data, size_t size, uint8_t digest[20]) {
    uint32_t       h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t         left = size;
    while (left >= 64) {
        Sha1Block(h, p);
        p += 64;
        left -= 64;
    }
    // The tail, a 1 bit, zeros, and the message length in bits (big-endian) in one or two blocks.
    uint8_t tail[128] = {};
    if (left > 0) std::memcpy(tail, p, left);
    tail[left] = 0x80;
    const size_t   tailSize = left < 56 ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8u;
    for (int i = 0; i < 8; ++i) tail[tailSize - 1 - i] = static_cast<uint8_t>(bits >> (8 * i));
    Sha1Block(h, tail);
    if (tailSize == 128) Sha1Block(h, tail + 64);
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
}

std::string Base64(const void* data, size_t size) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t* p = static_cast<const uint8_t*>(data);
    std::string    out;
    out.reserve((size + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(p[i]) << 16) | (static_cast<uint32_t>(p[i + 1]) << 8) | p[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (size - i == 1) {
        const uint32_t v = static_cast<uint32_t>(p[i]) << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (size - i == 2) {
        const uint32_t v = (static_cast<uint32_t>(p[i]) << 16) | (static_cast<uint32_t>(p[i + 1]) << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string AcceptKey(const std::string& key) {
    const std::string joined = key + kGuid;
    uint8_t           digest[20];
    Sha1(joined.data(), joined.size(), digest);
    return Base64(digest, sizeof(digest));
}

std::string UpgradeRequest(const Url& url, const std::string& key) {
    std::string r;
    r += "GET " + url.target + " HTTP/1.1\r\n";
    r += "Host: " + url.hostHeader + "\r\n";
    r += "Upgrade: websocket\r\n";
    r += "Connection: Upgrade\r\n";
    r += "Sec-WebSocket-Key: " + key + "\r\n";
    r += "Sec-WebSocket-Version: 13\r\n";
    r += "User-Agent: OpenBA-Neighbour/1\r\n";
    r += "\r\n";
    return r;
}

size_t HeadEnd(const std::string& buffer) {
    const size_t at = buffer.find("\r\n\r\n");
    return at == std::string::npos ? std::string::npos : at + 4;
}

bool CheckUpgradeResponse(const std::string& head, const std::string& key, std::string& error) {
    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos) lineEnd = head.size();
    const std::string status = head.substr(0, lineEnd);
    const size_t      space = status.find(' ');
    if (status.compare(0, 5, "HTTP/") != 0 || space == std::string::npos) {
        error = "the relay did not answer with HTTP";
        return false;
    }
    const std::string codeText = status.substr(space + 1, 3);
    if (!IsDigits(codeText) || codeText.size() != 3) {
        error = "the relay did not answer with HTTP";
        return false;
    }
    if (codeText != "101") {
        error = "the relay answered HTTP " + codeText + " instead of a WebSocket";
        return false;
    }

    bool        upgrade = false;
    bool        connection = false;
    std::string accept;
    bool        extensions = false;
    size_t      at = lineEnd + 2;
    while (at < head.size()) {
        size_t end = head.find("\r\n", at);
        if (end == std::string::npos) end = head.size();
        const std::string line = head.substr(at, end - at);
        at = end + 2;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = LowerCopy(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));
        if (name == "upgrade") {
            upgrade = upgrade || HasToken(value, "websocket");
        } else if (name == "connection") {
            connection = connection || HasToken(value, "upgrade");
        } else if (name == "sec-websocket-accept") {
            accept = value;
        } else if (name == "sec-websocket-extensions") {
            extensions = extensions || !value.empty();
        }
    }
    if (!upgrade) {
        error = "the relay's WebSocket answer has no \"Upgrade: websocket\"";
        return false;
    }
    if (!connection) {
        error = "the relay's WebSocket answer has no \"Connection: Upgrade\"";
        return false;
    }
    if (accept != AcceptKey(key)) {
        error = "the relay's WebSocket answer has the wrong Sec-WebSocket-Accept";
        return false;
    }
    if (extensions) {
        error = "the relay asked for a WebSocket extension that was not offered";
        return false;
    }
    error.clear();
    return true;
}

std::string EncodeClientFrame(uint8_t opcode, const void* payload, size_t size, const uint8_t mask[4]) {
    std::string frame;
    frame.reserve(size + 14);
    frame += static_cast<char>(0x80 | (opcode & 0x0F));
    if (size < 126) {
        frame += static_cast<char>(0x80 | size);
    } else if (size <= 0xFFFF) {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>((size >> 8) & 0xFF);
        frame += static_cast<char>(size & 0xFF);
    } else {
        frame += static_cast<char>(0x80 | 127);
        const uint64_t n = size;
        for (int i = 7; i >= 0; --i) frame += static_cast<char>((n >> (8 * i)) & 0xFF);
    }
    for (int i = 0; i < 4; ++i) frame += static_cast<char>(mask[i]);
    const uint8_t* p = static_cast<const uint8_t*>(payload);
    for (size_t i = 0; i < size; ++i) frame += static_cast<char>(p[i] ^ mask[i & 3]);
    return frame;
}

std::string ClosePayload(uint16_t code) {
    if (code == 0) return std::string();
    std::string out;
    out += static_cast<char>(code >> 8);
    out += static_cast<char>(code & 0xFF);
    return out;
}

void Reader::Append(const void* data, size_t size) {
    buf_.append(static_cast<const char*>(data), size);
}

// The consumed bytes are dropped only here, so a partial frame is kept whole for the next Append.
Reader::Event Reader::More() {
    buf_.erase(0, pos_);
    pos_ = 0;
    return Event::NeedMore;
}

Reader::Event Reader::Fail(uint16_t code) {
    errorCode_ = code;
    return Event::Error;
}

Reader::Event Reader::Next(std::string& out, size_t maxBytes) {
    out.clear();
    if (errorCode_ != 0) return Event::Error;
    for (;;) {
        if (skip_ > 0) {
            const uint64_t avail = buf_.size() - pos_;
            const uint64_t drop = skip_ < avail ? skip_ : avail;
            pos_ += static_cast<size_t>(drop);
            skip_ -= drop;
        }
        const size_t avail = buf_.size() - pos_;
        if (skip_ > 0 || avail < 2) return More();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(buf_.data()) + pos_;
        const bool     fin = (p[0] & 0x80) != 0;
        const uint8_t  op = p[0] & 0x0F;
        if ((p[0] & 0x70) != 0) return Fail(kCloseProtocolError);   // no extension was agreed
        if ((p[1] & 0x80) != 0) return Fail(kCloseProtocolError);   // a server never masks (§5.1)
        uint64_t len = p[1] & 0x7F;
        size_t   header = 2;
        if (len == 126) {
            header = 4;
            if (avail < header) return More();
            len = (static_cast<uint64_t>(p[2]) << 8) | p[3];
        } else if (len == 127) {
            header = 10;
            if (avail < header) return More();
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i];
            if ((len >> 63) != 0) return Fail(kCloseProtocolError);
        }

        if ((op & 0x08) != 0) {
            // Control frames: whole, short, and allowed between a message's fragments (§5.5).
            if (!fin || len > 125) return Fail(kCloseProtocolError);
            if (op != kClose && op != kPing && op != kPong) return Fail(kCloseProtocolError);
            if (avail < header + len) return More();
            const std::string payload(reinterpret_cast<const char*>(p) + header, static_cast<size_t>(len));
            pos_ += header + static_cast<size_t>(len);
            if (op == kPong) continue;
            out = payload;
            return op == kPing ? Event::Ping : Event::Close;
        }

        if (op == kContinuation) {
            if (messageOp_ == 0) return Fail(kCloseProtocolError);
        } else if (op == kText || op == kBinary) {
            if (messageOp_ != 0) return Fail(kCloseProtocolError);
        } else {
            return Fail(kCloseProtocolError);
        }
        const uint8_t messageOp = op == kContinuation ? messageOp_ : op;
        if (messageOp == kBinary) {
            // Not ours: dropped as it arrives, whatever its size.
            pos_ += header;
            skip_ = len;
            messageOp_ = fin ? 0 : kBinary;
            continue;
        }
        if (message_.size() + len > maxBytes) return Fail(kCloseTooBig);
        if (avail < header + len) return More();
        message_.append(reinterpret_cast<const char*>(p) + header, static_cast<size_t>(len));
        pos_ += header + static_cast<size_t>(len);
        if (!fin) {
            messageOp_ = kText;
            continue;
        }
        messageOp_ = 0;
        out.swap(message_);
        message_.clear();
        return Event::Text;
    }
}

}  // namespace WebSocketFrames
}  // namespace Platform

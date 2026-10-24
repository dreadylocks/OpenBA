// ============================================================================
// tests/unit/websocket_frames_test.cpp -- RFC 6455's wire format (websocket_frames.h)
// ============================================================================
// The pure half of the Linux WebSocket seam (src/platform/posix/websocket_posix.cpp): URL
// parsing, the accept key and its SHA-1 / base64, the upgrade exchange, client frame
// encoding and the server frame reader. Runs on every platform (no sockets, no OpenSSL).
// ============================================================================
#include "unit.h"

#include <string>

#include "platform/common/websocket_frames.h"

namespace WSF = Platform::WebSocketFrames;

namespace {

std::string Hex(const uint8_t* p, size_t n) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += kDigits[p[i] >> 4];
        out += kDigits[p[i] & 15];
    }
    return out;
}

std::string Sha1Hex(const std::string& s) {
    uint8_t d[20];
    WSF::Sha1(s.data(), s.size(), d);
    return Hex(d, sizeof(d));
}

// An unmasked server frame.
std::string ServerFrame(bool fin, uint8_t opcode, const std::string& payload) {
    std::string f;
    f += static_cast<char>((fin ? 0x80 : 0) | opcode);
    const size_t n = payload.size();
    if (n < 126) {
        f += static_cast<char>(n);
    } else if (n <= 0xFFFF) {
        f += static_cast<char>(126);
        f += static_cast<char>(n >> 8);
        f += static_cast<char>(n & 0xFF);
    } else {
        f += static_cast<char>(127);
        for (int i = 7; i >= 0; --i) f += static_cast<char>((static_cast<uint64_t>(n) >> (8 * i)) & 0xFF);
    }
    return f + payload;
}

void Feed(WSF::Reader& r, const std::string& bytes) {
    r.Append(bytes.data(), bytes.size());
}

const char kRfcKey[] = "dGhlIHNhbXBsZSBub25jZQ==";   // RFC 6455 §1.3

}  // namespace

UNIT_TEST(websocket_sha1_fips_vectors) {
    CHECK_STREQ(Sha1Hex("").c_str(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK_STREQ(Sha1Hex("abc").c_str(), "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK_STREQ(Sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str(),
                "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    // 55 / 56 / 64 bytes: the length field fits the tail block, spills to a second one, a whole block.
    CHECK_STREQ(Sha1Hex(std::string(55, 'a')).c_str(), "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    CHECK_STREQ(Sha1Hex(std::string(56, 'a')).c_str(), "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    CHECK_STREQ(Sha1Hex(std::string(64, 'a')).c_str(), "0098ba824b5c16427bd7a1122a5a442a25ec644d");
    CHECK_STREQ(Sha1Hex(std::string(1000000, 'a')).c_str(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

UNIT_TEST(websocket_base64_rfc4648_vectors) {
    CHECK_STREQ(WSF::Base64("", 0).c_str(), "");
    CHECK_STREQ(WSF::Base64("f", 1).c_str(), "Zg==");
    CHECK_STREQ(WSF::Base64("fo", 2).c_str(), "Zm8=");
    CHECK_STREQ(WSF::Base64("foo", 3).c_str(), "Zm9v");
    CHECK_STREQ(WSF::Base64("foob", 4).c_str(), "Zm9vYg==");
    CHECK_STREQ(WSF::Base64("fooba", 5).c_str(), "Zm9vYmE=");
    CHECK_STREQ(WSF::Base64("foobar", 6).c_str(), "Zm9vYmFy");
    const uint8_t high[3] = {0xFB, 0xFF, 0xBF};
    CHECK_STREQ(WSF::Base64(high, 3).c_str(), "+/+/");
}

UNIT_TEST(websocket_accept_key_rfc_sample) {
    CHECK_STREQ(WSF::AcceptKey(kRfcKey).c_str(), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

UNIT_TEST(websocket_parse_url_accepts) {
    WSF::Url    u;
    std::string error;
    CHECK(WSF::ParseUrl("wss://openba-neighbour.spellpress.workers.dev/lobby", u, error));
    CHECK(u.secure);
    CHECK_STREQ(u.host.c_str(), "openba-neighbour.spellpress.workers.dev");
    CHECK_EQ(u.port, 443);
    CHECK_STREQ(u.target.c_str(), "/lobby");
    CHECK_STREQ(u.hostHeader.c_str(), "openba-neighbour.spellpress.workers.dev");
    CHECK(error.empty());

    CHECK(WSF::ParseUrl("ws://127.0.0.1:8791/room/abc", u, error));
    CHECK(!u.secure);
    CHECK_STREQ(u.host.c_str(), "127.0.0.1");
    CHECK_EQ(u.port, 8791);
    CHECK_STREQ(u.target.c_str(), "/room/abc");
    CHECK_STREQ(u.hostHeader.c_str(), "127.0.0.1:8791");

    CHECK(WSF::ParseUrl("ws://[::1]:9000/a?b=c#frag", u, error));
    CHECK_STREQ(u.host.c_str(), "::1");
    CHECK_EQ(u.port, 9000);
    CHECK_STREQ(u.target.c_str(), "/a?b=c");
    CHECK_STREQ(u.hostHeader.c_str(), "[::1]:9000");

    CHECK(WSF::ParseUrl("ws://[::1]/", u, error));
    CHECK_EQ(u.port, 80);
    CHECK_STREQ(u.hostHeader.c_str(), "[::1]");

    CHECK(WSF::ParseUrl("ws://example.com", u, error));
    CHECK_STREQ(u.target.c_str(), "/");
    CHECK_EQ(u.port, 80);
    CHECK(WSF::ParseUrl("ws://example.com?x=1", u, error));
    CHECK_STREQ(u.target.c_str(), "/?x=1");
    // The default port named explicitly stays out of the Host header; another one goes in.
    CHECK(WSF::ParseUrl("wss://example.com:443/", u, error));
    CHECK_STREQ(u.hostHeader.c_str(), "example.com");
    CHECK(WSF::ParseUrl("wss://example.com:80/", u, error));
    CHECK_STREQ(u.hostHeader.c_str(), "example.com:80");
    CHECK(WSF::ParseUrl("WSS://Example.com/", u, error));
    CHECK(u.secure);
}

UNIT_TEST(websocket_parse_url_rejects) {
    WSF::Url    u;
    std::string error;
    CHECK(!WSF::ParseUrl("http://example.com/", u, error));
    CHECK_STREQ(error.c_str(), "the relay address must start with ws:// or wss://");
    CHECK(!WSF::ParseUrl("", u, error));
    CHECK_STREQ(error.c_str(), "the relay address must start with ws:// or wss://");
    const char* bad[] = {
        "ws://",          "ws:///path",     "ws://:80/",         "ws://h:0/",      "ws://h:65536/",
        "ws://h:abc/",    "ws://h:/",       "ws://u:p@h/",       "ws://h/a b",     "ws://h/\r\nX: y",
        "ws://[::1/",     "ws://[::1]x/",   "ws://::1/",         "ws://h:123456/", "ws://[]/",
    };
    for (const char* url : bad) {
        error.clear();
        const bool ok = WSF::ParseUrl(url, u, error);
        if (ok) std::printf("  (accepted %s)\n", url);
        CHECK(!ok);
        CHECK_STREQ(error.c_str(), "the relay address is not a valid URL");
    }
}

UNIT_TEST(websocket_upgrade_request) {
    WSF::Url    u;
    std::string error;
    CHECK(WSF::ParseUrl("ws://127.0.0.1:8791/room/x?y", u, error));
    const std::string r = WSF::UpgradeRequest(u, kRfcKey);
    CHECK(r.compare(0, 27, "GET /room/x?y HTTP/1.1\r\nHos") == 0);
    CHECK(r.find("\r\nHost: 127.0.0.1:8791\r\n") != std::string::npos);
    CHECK(r.find("\r\nUpgrade: websocket\r\n") != std::string::npos);
    CHECK(r.find("\r\nConnection: Upgrade\r\n") != std::string::npos);
    CHECK(r.find("\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n") != std::string::npos);
    CHECK(r.find("\r\nSec-WebSocket-Version: 13\r\n") != std::string::npos);
    CHECK_EQ(WSF::HeadEnd(r), r.size());
}

UNIT_TEST(websocket_head_end) {
    CHECK_EQ(WSF::HeadEnd("HTTP/1.1 101 OK\r\n"), std::string::npos);
    CHECK_EQ(WSF::HeadEnd("HTTP/1.1 101 OK\r\n\r\n"), static_cast<size_t>(19));
    CHECK_EQ(WSF::HeadEnd("HTTP/1.1 101 OK\r\n\r\n\x81\x02hi"), static_cast<size_t>(19));
}

UNIT_TEST(websocket_check_upgrade_response) {
    std::string error;
    const std::string good = "HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    CHECK(WSF::CheckUpgradeResponse(good, kRfcKey, error));
    CHECK(error.empty());
    // Header names and the Upgrade / Connection tokens are case-insensitive; Connection is a list.
    const std::string loose = "HTTP/1.1 101 Switching Protocols\r\n"
                              "upgrade: WebSocket\r\n"
                              "connection: keep-alive, upgrade\r\n"
                              "sec-websocket-accept:   s3pPLMBiTxaQ9kYGzzhZRbK+xOo=  \r\n\r\n";
    CHECK(WSF::CheckUpgradeResponse(loose, kRfcKey, error));

    CHECK(!WSF::CheckUpgradeResponse("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", kRfcKey, error));
    CHECK_STREQ(error.c_str(), "the relay answered HTTP 404 instead of a WebSocket");
    CHECK(!WSF::CheckUpgradeResponse("SSH-2.0-OpenSSH\r\n\r\n", kRfcKey, error));
    CHECK_STREQ(error.c_str(), "the relay did not answer with HTTP");

    const std::string wrongAccept = "HTTP/1.1 101 Switching Protocols\r\n"
                                    "Upgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n";
    CHECK(!WSF::CheckUpgradeResponse(wrongAccept, kRfcKey, error));
    CHECK_STREQ(error.c_str(), "the relay's WebSocket answer has the wrong Sec-WebSocket-Accept");

    const std::string noUpgrade = "HTTP/1.1 101 Switching Protocols\r\n"
                                  "Connection: Upgrade\r\n"
                                  "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    CHECK(!WSF::CheckUpgradeResponse(noUpgrade, kRfcKey, error));
    const std::string noConnection = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    CHECK(!WSF::CheckUpgradeResponse(noConnection, kRfcKey, error));
    const std::string extension = good.substr(0, good.size() - 2) + "Sec-WebSocket-Extensions: permessage-deflate\r\n\r\n";
    CHECK(!WSF::CheckUpgradeResponse(extension, kRfcKey, error));
}

UNIT_TEST(websocket_encode_client_frame) {
    // RFC 6455 §5.7: a masked "Hello".
    const uint8_t     mask[4] = {0x37, 0xFA, 0x21, 0x3D};
    const std::string hello = WSF::EncodeClientFrame(WSF::kText, "Hello", 5, mask);
    const uint8_t     want[] = {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
    CHECK_EQ(hello.size(), sizeof(want));
    CHECK(hello == std::string(reinterpret_cast<const char*>(want), sizeof(want)));

    const uint8_t     zero[4] = {0, 0, 0, 0};
    const std::string empty = WSF::EncodeClientFrame(WSF::kClose, nullptr, 0, zero);
    CHECK(empty == std::string("\x88\x80\x00\x00\x00\x00", 6));

    const std::string mid(126, 'x');
    const std::string f126 = WSF::EncodeClientFrame(WSF::kText, mid.data(), mid.size(), zero);
    CHECK_EQ(f126.size(), static_cast<size_t>(4 + 4 + 126));
    CHECK_EQ(static_cast<uint8_t>(f126[1]), 0xFE);
    CHECK_EQ(static_cast<uint8_t>(f126[2]), 0x00);
    CHECK_EQ(static_cast<uint8_t>(f126[3]), 126);
    CHECK(f126.compare(8, 126, mid) == 0);

    const std::string big(65536, 'y');
    const std::string f64k = WSF::EncodeClientFrame(WSF::kBinary, big.data(), big.size(), mask);
    CHECK_EQ(f64k.size(), static_cast<size_t>(2 + 8 + 4 + 65536));
    CHECK_EQ(static_cast<uint8_t>(f64k[0]), 0x82);
    CHECK_EQ(static_cast<uint8_t>(f64k[1]), 0xFF);
    CHECK(f64k.compare(2, 8, std::string("\x00\x00\x00\x00\x00\x01\x00\x00", 8)) == 0);
    CHECK_EQ(static_cast<uint8_t>(f64k[14]), static_cast<uint8_t>('y' ^ 0x37));
    CHECK_EQ(static_cast<uint8_t>(f64k[15]), static_cast<uint8_t>('y' ^ 0xFA));

    CHECK(WSF::ClosePayload(1000) == std::string("\x03\xE8", 2));
    CHECK(WSF::ClosePayload(0).empty());
}

UNIT_TEST(websocket_reader_text_and_fragments) {
    WSF::Reader r;
    std::string out;
    CHECK(r.Next(out, 1024) == WSF::Reader::Event::NeedMore);
    // RFC 6455 §5.7: a single-frame unmasked text message, then the same in two fragments
    // with a ping between them.
    Feed(r, std::string("\x81\x05Hello", 7));
    Feed(r, std::string("\x01\x03Hel", 5));
    Feed(r, std::string("\x89\x05Hello", 7));
    Feed(r, std::string("\x80\x02lo", 4));
    CHECK(r.Next(out, 1024) == WSF::Reader::Event::Text);
    CHECK_STREQ(out.c_str(), "Hello");
    CHECK(r.Next(out, 1024) == WSF::Reader::Event::Ping);
    CHECK_STREQ(out.c_str(), "Hello");
    CHECK(r.Next(out, 1024) == WSF::Reader::Event::Text);
    CHECK_STREQ(out.c_str(), "Hello");
    CHECK(r.Next(out, 1024) == WSF::Reader::Event::NeedMore);
    CHECK(out.empty());
}

UNIT_TEST(websocket_reader_byte_at_a_time) {
    const std::string payload(300, 'z');
    const std::string stream = ServerFrame(true, WSF::kText, payload) + ServerFrame(true, WSF::kText, "{}");
    WSF::Reader       r;
    std::string       out;
    int               texts = 0;
    for (char c : stream) {
        r.Append(&c, 1);
        for (;;) {
            const WSF::Reader::Event e = r.Next(out, 1024);
            if (e == WSF::Reader::Event::NeedMore) break;
            CHECK(e == WSF::Reader::Event::Text);
            if (e != WSF::Reader::Event::Text) return;
            ++texts;
            if (texts == 1) CHECK(out == payload);
            if (texts == 2) CHECK_STREQ(out.c_str(), "{}");
        }
    }
    CHECK_EQ(texts, 2);
}

UNIT_TEST(websocket_reader_long_lengths) {
    WSF::Reader       r;
    std::string       out;
    const std::string big(70000, 'q');
    Feed(r, ServerFrame(true, WSF::kText, big));
    CHECK(r.Next(out, 100000) == WSF::Reader::Event::Text);
    CHECK(out == big);
    const std::string mid(126, 'm');
    Feed(r, ServerFrame(true, WSF::kText, mid));
    CHECK(r.Next(out, 100000) == WSF::Reader::Event::Text);
    CHECK(out == mid);
}

UNIT_TEST(websocket_reader_close_pong_binary) {
    WSF::Reader r;
    std::string out;
    // A pong is ignored; a binary message (fragmented, larger than maxBytes, arriving in
    // pieces) is dropped; the text after it still comes through; then the close.
    Feed(r, ServerFrame(true, WSF::kPong, "p"));
    const std::string blob(5000, 'b');
    const std::string binary = ServerFrame(false, WSF::kBinary, blob) + ServerFrame(true, WSF::kContinuation, blob);
    Feed(r, binary.substr(0, 3000));
    CHECK(r.Next(out, 64) == WSF::Reader::Event::NeedMore);
    Feed(r, binary.substr(3000));
    Feed(r, ServerFrame(true, WSF::kText, "after"));
    Feed(r, ServerFrame(true, WSF::kClose, std::string("\x03\xE8" "bye", 5)));
    CHECK(r.Next(out, 64) == WSF::Reader::Event::Text);
    CHECK_STREQ(out.c_str(), "after");
    CHECK(r.Next(out, 64) == WSF::Reader::Event::Close);
    CHECK(out == std::string("\x03\xE8" "bye", 5));
}

UNIT_TEST(websocket_reader_too_big) {
    {
        WSF::Reader r;
        std::string out;
        // Refused from the header alone, before the payload arrives.
        Feed(r, std::string("\x81\x05He", 4));
        CHECK(r.Next(out, 4) == WSF::Reader::Event::Error);
        CHECK_EQ(r.ErrorCode(), WSF::kCloseTooBig);
        Feed(r, std::string("llo", 3));
        CHECK(r.Next(out, 4) == WSF::Reader::Event::Error);
    }
    {
        WSF::Reader r;
        std::string out;
        // Counted across the fragments.
        Feed(r, ServerFrame(false, WSF::kText, "abc"));
        Feed(r, ServerFrame(true, WSF::kContinuation, "de"));
        CHECK(r.Next(out, 4) == WSF::Reader::Event::Error);
        CHECK_EQ(r.ErrorCode(), WSF::kCloseTooBig);
    }
    {
        WSF::Reader r;
        std::string out;
        Feed(r, ServerFrame(true, WSF::kText, "abcd"));
        CHECK(r.Next(out, 4) == WSF::Reader::Event::Text);
        CHECK_STREQ(out.c_str(), "abcd");
    }
}

UNIT_TEST(websocket_reader_protocol_errors) {
    const std::string cases[] = {
        std::string("\x81\x81\x00\x00\x00\x00x", 7),   // masked server frame
        std::string("\xC1\x01x", 3),                   // RSV1 set
        std::string("\x80\x01x", 3),                   // continuation with no message open
        ServerFrame(false, WSF::kText, "a") + ServerFrame(true, WSF::kText, "b"),   // new message mid-message
        std::string("\x09\x00", 2),                    // fragmented ping
        ServerFrame(true, WSF::kPing, std::string(126, 'p')),   // control frame too long
        std::string("\x83\x00", 2),                    // reserved data opcode
        std::string("\x8B\x00", 2),                    // reserved control opcode
        std::string("\x81\x7F\x80\x00\x00\x00\x00\x00\x00\x00", 10),   // 64-bit length with the top bit set
    };
    int n = 0;
    for (const std::string& bytes : cases) {
        ++n;
        WSF::Reader r;
        std::string out;
        Feed(r, bytes);
        WSF::Reader::Event e = r.Next(out, 1024);
        if (e == WSF::Reader::Event::Text) e = r.Next(out, 1024);   // none of them should get this far
        if (e != WSF::Reader::Event::Error) std::printf("  (case %d not refused)\n", n);
        CHECK(e == WSF::Reader::Event::Error);
        CHECK_EQ(r.ErrorCode(), WSF::kCloseProtocolError);
    }
}

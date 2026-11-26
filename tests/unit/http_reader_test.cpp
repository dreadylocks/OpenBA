// ============================================================================
// tests/unit/http_reader_test.cpp -- the pure half of Platform::HttpGet (http_reader.h)
// ============================================================================
// The GET request, Content-Length / chunked / close framing fed in pieces of every size, the body
// limit, a stopping sink, and redirects through a scripted Exchange (no socket). Every platform.
// ============================================================================
#include "unit.h"

#include <string>
#include <vector>

#include "platform/common/http_reader.h"

namespace HR = Platform::HttpReader;
namespace WSF = Platform::WebSocketFrames;

namespace {

// Feed `wire` in pieces of `step` bytes; the body the sink saw, the final state.
HR::Response::State FeedAll(const std::string& wire, size_t step, uint64_t maxBody, std::string& body, int& status,
                            bool finish = true) {
    body.clear();
    HR::Response r(maxBody, [&body](const char* d, size_t n) {
        body.append(d, n);
        return true;
    });
    for (size_t i = 0; i < wire.size(); i += step) {
        const HR::Response::State s = r.Feed(wire.data() + i, wire.size() - i < step ? wire.size() - i : step);
        if (s == HR::Response::State::Done || s == HR::Response::State::Failed) break;
    }
    if (finish) r.Finish();
    status = r.status();
    return r.state();
}

}  // namespace

UNIT_TEST(http_reader_get_request) {
    WSF::Url    u;
    std::string error;
    CHECK(WSF::ParseHttpUrl("https://github.com/dreadylocks/OpenBA/releases/latest/download/x.txt?a=1", u, error));
    const std::string r = HR::GetRequest(u);
    CHECK(r.find("GET /dreadylocks/OpenBA/releases/latest/download/x.txt?a=1 HTTP/1.1\r\n") == 0);
    CHECK(r.find("\r\nHost: github.com\r\n") != std::string::npos);
    CHECK(r.find("\r\nConnection: close\r\n") != std::string::npos);
    CHECK(r.size() >= 4 && r.compare(r.size() - 4, 4, "\r\n\r\n") == 0);
}

UNIT_TEST(http_reader_content_length_every_split) {
    const std::string wire = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nhello worldEXTRA";
    for (size_t step = 1; step <= wire.size(); ++step) {
        std::string body;
        int         status = 0;
        CHECK(FeedAll(wire, step, 1000, body, status, false) == HR::Response::State::Done);
        CHECK_EQ(status, 200);
        CHECK_STREQ(body.c_str(), "hello world");
    }
}

UNIT_TEST(http_reader_chunked_every_split) {
    const std::string wire =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n1;ext=1\r\n \r\nA\r\n0123456789\r\n0\r\nX-Trailer: y\r\n\r\n";
    for (size_t step = 1; step <= wire.size(); ++step) {
        std::string body;
        int         status = 0;
        CHECK(FeedAll(wire, step, 1000, body, status, false) == HR::Response::State::Done);
        CHECK_STREQ(body.c_str(), "hello 0123456789");
    }
    std::string body;
    int         status = 0;
    CHECK(FeedAll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n", 4, 1000, body, status) ==
          HR::Response::State::Failed);
    // Cut short mid-chunk.
    CHECK(FeedAll("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel", 3, 1000, body, status) ==
          HR::Response::State::Failed);
}

UNIT_TEST(http_reader_close_framing_and_limits) {
    std::string body;
    int         status = 0;
    // No length, no chunks: the body runs until the line closes.
    CHECK(FeedAll("HTTP/1.0 200 OK\r\n\r\nuntil the end", 5, 1000, body, status) == HR::Response::State::Done);
    CHECK_STREQ(body.c_str(), "until the end");
    // A length that is too long fails at once; a body cut short fails at the close.
    CHECK(FeedAll("HTTP/1.1 200 OK\r\nContent-Length: 2000\r\n\r\nx", 7, 1000, body, status) == HR::Response::State::Failed);
    CHECK(FeedAll("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", 7, 1000, body, status) == HR::Response::State::Failed);
    // A close-framed body over the limit fails.
    CHECK(FeedAll("HTTP/1.1 200 OK\r\n\r\n" + std::string(50, 'x'), 9, 20, body, status) == HR::Response::State::Failed);
    // Not HTTP, or the line closed before a head.
    CHECK(FeedAll("SSH-2.0-OpenSSH\r\n\r\n", 4, 1000, body, status) == HR::Response::State::Failed);
    CHECK(FeedAll("HTTP/1.1 200", 4, 1000, body, status) == HR::Response::State::Failed);
    // A 404's body is not delivered.
    CHECK(FeedAll("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found", 6, 1000, body, status) ==
          HR::Response::State::Done);
    CHECK_EQ(status, 404);
    CHECK(body.empty());
    // A sink that says stop.
    HR::Response r(1000, [](const char*, size_t) { return false; });
    const std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    CHECK(r.Feed(wire.data(), wire.size()) == HR::Response::State::Failed);
    CHECK_STREQ(r.error().c_str(), "stopped");
}

UNIT_TEST(http_reader_resolve_location) {
    WSF::Url    base;
    std::string error;
    CHECK(WSF::ParseHttpUrl("https://github.com:8443/a/b", base, error));
    CHECK_STREQ(HR::ResolveLocation(base, "https://objects.example.com/x?y=1").c_str(), "https://objects.example.com/x?y=1");
    CHECK_STREQ(HR::ResolveLocation(base, "/c/d").c_str(), "https://github.com:8443/c/d");
    CHECK_STREQ(HR::ResolveLocation(base, "relative").c_str(), "");
    CHECK_STREQ(HR::ResolveLocation(base, "//other.host/x").c_str(), "");
    CHECK_STREQ(HR::ResolveLocation(base, "ftp://x/y").c_str(), "");
}

UNIT_TEST(http_reader_get_follows_redirects) {
    // A scripted server: github.com redirects to the CDN, which answers in chunks of 3 bytes.
    std::vector<std::string> hosts;
    const HR::Exchange exchange = [&hosts](const WSF::Url& where, const std::string& request,
                                           const std::function<bool(const char*, size_t)>& onBytes, std::string&) {
        hosts.push_back(where.host);
        std::string answer;
        if (where.host == "github.com") {
            answer = "HTTP/1.1 302 Found\r\nLocation: https://cdn.example.com/pkg.zip\r\nContent-Length: 0\r\n\r\n";
        } else if (where.host == "cdn.example.com" && request.find("GET /pkg.zip ") == 0) {
            answer = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\npackage-body";
        } else {
            answer = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
        for (size_t i = 0; i < answer.size(); i += 3) {
            if (!onBytes(answer.data() + i, answer.size() - i < 3 ? answer.size() - i : 3)) return true;
        }
        return true;
    };
    std::string body;
    std::string error;
    const int   status = HR::Get("https://github.com/x/releases/latest/download/pkg.zip", 1000, exchange,
                                 [&body](const char* d, size_t n) {
                                     body.append(d, n);
                                     return true;
                                 },
                                 error);
    CHECK_EQ(status, 200);
    CHECK_STREQ(body.c_str(), "package-body");
    CHECK_EQ(hosts.size(), static_cast<size_t>(2));
    CHECK_STREQ(error.c_str(), "");

    // A redirect loop gives up.
    const HR::Exchange loop = [](const WSF::Url&, const std::string&,
                                 const std::function<bool(const char*, size_t)>& onBytes, std::string&) {
        const std::string answer = "HTTP/1.1 301 Moved\r\nLocation: /again\r\n\r\n";
        onBytes(answer.data(), answer.size());
        return true;
    };
    CHECK_EQ(HR::Get("http://example.com/start", 1000, loop, nullptr, error), 0);
    CHECK_STREQ(error.c_str(), "too many redirects");

    // A line that fails before an answer.
    const HR::Exchange dead = [](const WSF::Url&, const std::string&, const std::function<bool(const char*, size_t)>&,
                                 std::string& lineError) {
        lineError = "cannot reach the server";
        return false;
    };
    CHECK_EQ(HR::Get("https://example.com/x", 1000, dead, nullptr, error), 0);
    CHECK_STREQ(error.c_str(), "cannot reach the server");
}

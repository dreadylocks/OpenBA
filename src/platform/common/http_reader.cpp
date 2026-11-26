// src/platform/common/http_reader.cpp -- NOT RETAIL. The pure half of Platform::HttpGet
// (http_reader.h).
#include "platform/common/http_reader.h"

#include <cctype>
#include <cstring>

namespace Platform {
namespace HttpReader {

namespace WSF = WebSocketFrames;

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

bool ParseDecimal(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + uint64_t(c - '0');
    }
    out = v;
    return true;
}

bool ParseHexSize(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 15) return false;
    uint64_t v = 0;
    for (char c : s) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) return false;
        v = v * 16 + uint64_t(d);
    }
    out = v;
    return true;
}

}  // namespace

std::string GetRequest(const WSF::Url& url) {
    std::string r;
    r += "GET " + url.target + " HTTP/1.1\r\n";
    r += "Host: " + url.hostHeader + "\r\n";
    r += "Accept: */*\r\n";
    r += "Connection: close\r\n";
    r += "User-Agent: OpenBA/1\r\n";
    r += "\r\n";
    return r;
}

std::string ResolveLocation(const WSF::Url& base, const std::string& location) {
    const std::string l = Lower(location.substr(0, 8));
    if (l.compare(0, 8, "https://") == 0 || l.compare(0, 7, "http://") == 0) return location;
    if (!location.empty() && location[0] == '/' && (location.size() < 2 || location[1] != '/')) {
        return std::string(base.secure ? "https://" : "http://") + base.hostHeader + location;
    }
    return std::string();
}

Response::Response(uint64_t maxBody, Sink sink) : maxBody_(maxBody), sink_(std::move(sink)) {}

Response::State Response::Fail(const std::string& why) {
    if (state_ != State::Failed) error_ = why;
    state_ = State::Failed;
    return state_;
}

bool Response::ParseHead(const std::string& head) {
    status_ = WSF::HttpStatus(head);
    if (status_ == 0) return false;
    bool   haveLength = false;
    bool   chunked = false;
    size_t at = head.find("\r\n");
    while (at != std::string::npos && at + 2 < head.size()) {
        const size_t start = at + 2;
        const size_t end = head.find("\r\n", start);
        if (end == std::string::npos || end == start) break;
        const std::string line = head.substr(start, end - start);
        at = end;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = Lower(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));
        if (name == "content-length") {
            uint64_t n = 0;
            if (!ParseDecimal(value, n)) return false;
            if (haveLength && n != remaining_) return false;   // two different lengths
            remaining_ = n;
            haveLength = true;
        } else if (name == "transfer-encoding") {
            if (Lower(value).find("chunked") != std::string::npos) chunked = true;
        } else if (name == "location") {
            location_ = value;
        }
    }
    if (chunked) {
        framing_ = Framing::Chunked;
        remaining_ = 0;
    } else if (haveLength) {
        framing_ = Framing::Length;
    } else {
        framing_ = Framing::Close;
    }
    return true;
}

bool Response::Deliver(const char* data, size_t size) {
    if (size == 0) return true;
    if (delivered_ + size > maxBody_) {
        Fail("the answer is longer than " + std::to_string(maxBody_) + " bytes");
        return false;
    }
    delivered_ += size;
    if (sink_ && !sink_(data, size)) {
        Fail("stopped");
        return false;
    }
    return true;
}

Response::State Response::FeedChunked(const char* data, size_t size) {
    size_t i = 0;
    while (i < size && state_ == State::Body) {
        switch (chunk_) {
        case Chunk::Size:
        case Chunk::Trailer: {
            const char c = data[i++];
            if (c != '\n') {
                if (line_.size() > 1024) return Fail("a chunk line is too long");
                line_ += c;
                break;
            }
            if (!line_.empty() && line_.back() == '\r') line_.pop_back();
            if (chunk_ == Chunk::Trailer) {
                if (line_.empty()) {
                    state_ = State::Done;
                    return state_;
                }
                line_.clear();
                break;
            }
            const size_t semi = line_.find(';');
            uint64_t     n = 0;
            if (!ParseHexSize(Trim(line_.substr(0, semi)), n)) return Fail("a bad chunk size");
            line_.clear();
            if (n == 0) {
                chunk_ = Chunk::Trailer;
            } else {
                remaining_ = n;
                chunk_ = Chunk::Data;
            }
            break;
        }
        case Chunk::Data: {
            const size_t take = size - i < remaining_ ? size - i : static_cast<size_t>(remaining_);
            if (!Deliver(data + i, take)) return state_;
            i += take;
            remaining_ -= take;
            if (remaining_ == 0) chunk_ = Chunk::DataEnd;
            break;
        }
        case Chunk::DataEnd: {
            const char c = data[i++];
            if (c == '\r') break;
            if (c != '\n') return Fail("a chunk without its line end");
            chunk_ = Chunk::Size;
            break;
        }
        }
    }
    return state_;
}

Response::State Response::Feed(const char* data, size_t size) {
    if (state_ == State::Done || state_ == State::Failed) return state_;
    if (state_ == State::Head) {
        const size_t before = head_.size();
        head_.append(data, size);
        const size_t end = WSF::HeadEnd(head_);
        if (end == std::string::npos) {
            if (head_.size() > kMaxHeadBytes) return Fail("the answer's head is too long");
            return state_;
        }
        if (!ParseHead(head_.substr(0, end))) return Fail("not an HTTP answer");
        const size_t bodyStart = end - before;   // into this piece
        head_.clear();
        if (status_ != 200) {
            state_ = State::Done;   // a redirect or an error: its body is not wanted
            return state_;
        }
        state_ = State::Body;
        if (framing_ == Framing::Length && remaining_ > maxBody_) {
            return Fail("the answer is longer than " + std::to_string(maxBody_) + " bytes");
        }
        if (framing_ == Framing::Length && remaining_ == 0) {
            state_ = State::Done;
            return state_;
        }
        data += bodyStart;
        size -= bodyStart;
    }
    if (framing_ == Framing::Chunked) return FeedChunked(data, size);
    if (framing_ == Framing::Length) {
        const size_t take = size < remaining_ ? size : static_cast<size_t>(remaining_);
        if (!Deliver(data, take)) return state_;
        remaining_ -= take;
        if (remaining_ == 0) state_ = State::Done;
        return state_;
    }
    Deliver(data, size);
    return state_;
}

Response::State Response::Finish() {
    if (state_ == State::Done || state_ == State::Failed) return state_;
    if (state_ == State::Head) return Fail("the line closed before an answer came");
    if (framing_ == Framing::Close) {
        state_ = State::Done;
        return state_;
    }
    return Fail("the answer was cut short");
}

int Get(const std::string& url, uint64_t maxBody, const Exchange& exchange, const Sink& sink, std::string& error) {
    error.clear();
    std::string at = url;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        WSF::Url where;
        if (!WSF::ParseHttpUrl(at, where, error)) return 0;
        Response    answer(maxBody, sink);
        std::string lineError;
        const bool  lineOk = exchange(where, GetRequest(where), [&answer](const char* data, size_t size) {
            const Response::State s = answer.Feed(data, size);
            return s == Response::State::Head || s == Response::State::Body;
        }, lineError);
        if (answer.state() == Response::State::Head || answer.state() == Response::State::Body) {
            if (!lineOk) {
                error = lineError.empty() ? std::string("the line to the server failed") : lineError;
                return 0;
            }
            answer.Finish();
        }
        if (answer.state() == Response::State::Failed) {
            error = answer.error();
            return 0;
        }
        const int status = answer.status();
        const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (!redirect) {
            if (status != 200) error = "the server answered " + std::to_string(status);
            return status;
        }
        const std::string next = ResolveLocation(where, answer.location());
        if (next.empty()) {
            error = "a redirect without a usable address";
            return 0;
        }
        at = next;
    }
    error = "too many redirects";
    return 0;
}

}  // namespace HttpReader
}  // namespace Platform

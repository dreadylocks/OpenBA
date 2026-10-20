// ============================================================================
// tests/unit/ext_neighbour_test.cpp -- ext.neighbour's pure parts (NOT RETAIL)
// ============================================================================
// src/ext/neighbour/protocol.cpp (the JSON messages, Sanitize, the save payload) and
// src/ext/neighbour/link.cpp (the connection's threads) driven by a fake transport.
// docs/fork/NEIGHBOUR_TOWNS.md.
// ============================================================================
#include "unit.h"

#include "ext/neighbour/link.h"
#include "ext/neighbour/protocol.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Neighbour::Message;

UNIT_TEST(ext_neighbour_message_round_trip) {
    Message m("chat");
    m.Set("text", "Hello \"there\"\n\\ok").SetInt("seq", -42).SetBool("online", true);
    const std::string wire = m.Encode();
    CHECK_STREQ(wire.c_str(), "{\"t\":\"chat\",\"text\":\"Hello \\\"there\\\"\\n\\\\ok\",\"seq\":-42,\"online\":true}");

    Message back;
    CHECK(Neighbour::Parse(wire, back));
    CHECK_STREQ(back.type.c_str(), "chat");
    std::string text;
    CHECK(back.Get("text", text));
    CHECK_STREQ(text.c_str(), "Hello \"there\"\n\\ok");
    int64_t seq = 0;
    CHECK(back.GetInt("seq", seq));
    CHECK_EQ(seq, -42);
    bool online = false;
    CHECK(back.GetBool("online", online));
    CHECK(online);
    // The wrong kind is a miss, not a conversion.
    CHECK(!back.GetInt("text", seq));
    CHECK(!back.Get("seq", text));
    CHECK_STREQ(back.GetOr("absent", "dflt").c_str(), "dflt");
}

UNIT_TEST(ext_neighbour_parse_tolerates_newer_servers) {
    // Unknown nested values, null, floats and whitespace are skipped; the rest is read.
    Message m;
    CHECK(Neighbour::Parse(" { \"x\" : {\"a\":[1,2,{\"b\":null}]}, \"t\" : \"welcome\", \"f\": 1.5e3,"
                           " \"n\": null, \"peer\": \"Ann\\u00e9\\ud83d\\ude00\" } ", m));
    CHECK_STREQ(m.type.c_str(), "welcome");
    std::string peer;
    CHECK(m.Get("peer", peer));
    CHECK_STREQ(peer.c_str(), "Ann\xC3\xA9\xF0\x9F\x98\x80");   // UTF-8 for é and the emoji
    int64_t f = 0;
    CHECK(!m.GetInt("f", f));                                    // a float is not an integer
}

UNIT_TEST(ext_neighbour_parse_rejects_bad_input) {
    Message m;
    const char* bad[] = {
        "", "[]", "{}", "{\"t\":1}", "{\"t\":\"\"}", "{\"a\":\"b\"}",         // no string "t"
        "{\"t\":\"x\"", "{\"t\":\"x\",}", "{\"t\":\"x\"} trailing", "{t:\"x\"}",  // syntax
        "{\"t\":\"x\\q\"}", "{\"t\":\"x\\ud800\"}", "{\"t\":\"\x01\"}",            // bad strings
        "{\"t\":\"x\",\"n\":-}", "{\"t\":\"x\",\"n\":1.}",                       // bad numbers
    };
    for (const char* s : bad) {
        if (Neighbour::Parse(s, m)) std::printf("  accepted: %s\n", s);
        CHECK(!Neighbour::Parse(s, m));
    }
    // Too deep, too big.
    std::string deep = "{\"t\":\"x\",\"d\":";
    for (int i = 0; i < 40; ++i) deep += "[";
    for (int i = 0; i < 40; ++i) deep += "]";
    deep += "}";
    CHECK(!Neighbour::Parse(deep, m));
    std::string big = "{\"t\":\"x\",\"s\":\"" + std::string(Neighbour::kMaxMessageBytes, 'a') + "\"}";
    CHECK(!Neighbour::Parse(big, m));
    // Integers past int64 are skipped, not wrapped.
    CHECK(Neighbour::Parse("{\"t\":\"x\",\"n\":99999999999999999999}", m));
    int64_t n = 0;
    CHECK(!m.GetInt("n", n));
}

UNIT_TEST(ext_neighbour_sanitize) {
    CHECK_STREQ(Neighbour::Sanitize("  hello \t\n world  ", 100).c_str(), "hello world");
    CHECK_STREQ(Neighbour::Sanitize("caf\xC3\xA9 \xF0\x9F\x98\x80!", 100).c_str(), "caf? ?!");
    CHECK_STREQ(Neighbour::Sanitize("a\x01\x7F" "b", 100).c_str(), "ab");
    CHECK_STREQ(Neighbour::Sanitize("abcdefgh", 5).c_str(), "abcde");
    CHECK_STREQ(Neighbour::Sanitize("abcd efgh", 5).c_str(), "abcd");   // no dangling space
    CHECK(Neighbour::Sanitize(" \n ", 10).empty());
}

UNIT_TEST(ext_neighbour_saved_state) {
    Neighbour::SavedState s;
    s.room = "river-folk";
    s.lastSeq = 123456789012LL;
    const std::vector<uint8_t> bytes = Neighbour::EncodeState(s);
    Neighbour::SavedState back;
    CHECK(Neighbour::DecodeState(Neighbour::kPayloadFormat, bytes, back));
    CHECK_STREQ(back.room.c_str(), "river-folk");
    CHECK_EQ(back.lastSeq, 123456789012LL);
    // Every cut fails, a longer buffer fails, an unknown version fails; `out` is untouched.
    for (size_t n = 0; n < bytes.size(); ++n) {
        std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<long>(n));
        CHECK(!Neighbour::DecodeState(1, cut, back));
    }
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    CHECK(!Neighbour::DecodeState(1, longer, back));
    CHECK(!Neighbour::DecodeState(2, bytes, back));
    CHECK_STREQ(back.room.c_str(), "river-folk");
}

// ---------------------------------------------------------------------------
// The Link over a fake transport
// ---------------------------------------------------------------------------

namespace {

// One shared script for every transport the factory makes.
struct FakeNet {
    std::mutex              mu;
    std::condition_variable cv;
    int                     connects = 0;
    int                     failFirst = 0;          // this many Connects fail
    std::vector<std::string> sent;                  // every frame sent, in order
    std::deque<std::string>  toReceive;             // frames the next Receives return
    bool                     dropLine = false;      // the current line "breaks"
};
FakeNet* g_net = nullptr;

class FakeTransport final : public Neighbour::Transport {
public:
    bool Connect(const std::string&, std::string& error) override {
        std::lock_guard<std::mutex> lock(g_net->mu);
        ++g_net->connects;
        if (g_net->connects <= g_net->failFirst) { error = "refused"; return false; }
        g_net->dropLine = false;
        return true;
    }
    bool Send(const std::string& text) override {
        std::lock_guard<std::mutex> lock(g_net->mu);
        if (closed_) return false;
        g_net->sent.push_back(text);
        g_net->cv.notify_all();
        return true;
    }
    Recv Receive(std::string& out) override {
        std::unique_lock<std::mutex> lock(g_net->mu);
        g_net->cv.wait(lock, [this] { return closed_ || g_net->dropLine || !g_net->toReceive.empty(); });
        if (closed_) return Recv::Closed;
        if (g_net->dropLine) return Recv::Error;
        out = g_net->toReceive.front();
        g_net->toReceive.pop_front();
        return Recv::Text;
    }
    void Close() override {
        std::lock_guard<std::mutex> lock(g_net->mu);
        closed_ = true;
        g_net->cv.notify_all();
    }
private:
    bool closed_ = false;
};

std::unique_ptr<Neighbour::Transport> MakeFake() {
    return std::unique_ptr<Neighbour::Transport>(new FakeTransport());
}

// Poll until `pred(all incoming so far)` holds, or ~3 s pass.
template <class Pred>
bool PollUntil(Neighbour::Link& link, std::vector<Neighbour::Incoming>& all, Pred pred) {
    for (int i = 0; i < 300; ++i) {
        link.Poll(all);
        if (pred(all)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

size_t SentCount() {
    std::lock_guard<std::mutex> lock(g_net->mu);
    return g_net->sent.size();
}

}  // namespace

UNIT_TEST(ext_neighbour_link_hello_send_receive) {
    FakeNet net;
    g_net = &net;
    Neighbour::Link link(&MakeFake);
    Neighbour::Link::Options opt;
    opt.url = "ws://fake";
    opt.hello = Message("hello").Set("room", "r");
    link.Start(opt);

    std::vector<Neighbour::Incoming> got;
    CHECK(PollUntil(link, got, [](const std::vector<Neighbour::Incoming>& v) {
        return !v.empty() && v[0].kind == Neighbour::Incoming::Kind::Connected;
    }));
    link.Send(Message("chat").Set("text", "hi"));
    for (int i = 0; i < 300 && SentCount() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        std::lock_guard<std::mutex> lock(net.mu);
        CHECK_EQ(static_cast<int>(net.sent.size()), 2);
        if (net.sent.size() == 2) {
            CHECK_STREQ(net.sent[0].c_str(), "{\"t\":\"hello\",\"room\":\"r\"}");   // the hello first
            CHECK_STREQ(net.sent[1].c_str(), "{\"t\":\"chat\",\"text\":\"hi\"}");
        }
        net.toReceive.push_back("{\"t\":\"chat\",\"from\":\"Ann\",\"text\":\"yo\",\"seq\":1}");
        net.toReceive.push_back("not json");   // dropped, never crashes
        net.toReceive.push_back("{\"t\":\"peer\",\"name\":\"Ann\",\"online\":false}");
        net.cv.notify_all();
    }
    got.clear();
    CHECK(PollUntil(link, got, [](const std::vector<Neighbour::Incoming>& v) { return v.size() >= 2; }));
    CHECK_EQ(static_cast<int>(got.size()), 2);
    if (got.size() == 2) {
        CHECK_STREQ(got[0].message.type.c_str(), "chat");
        CHECK_STREQ(got[1].message.type.c_str(), "peer");
    }
    link.Stop();
    CHECK(!link.Running());
    link.Stop();   // twice is fine
    g_net = nullptr;
}

UNIT_TEST(ext_neighbour_link_reconnects) {
    FakeNet net;
    net.failFirst = 1;   // the first connect fails, the second works
    g_net = &net;
    Neighbour::Link link(&MakeFake);
    Neighbour::Link::Options opt;
    opt.url = "ws://fake";
    opt.hello = Message("hello");
    opt.firstRetryMs = 20;
    opt.maxRetryMs = 40;
    link.Start(opt);

    std::vector<Neighbour::Incoming> got;
    auto connectedCount = [](const std::vector<Neighbour::Incoming>& v) {
        int n = 0;
        for (const auto& in : v) n += in.kind == Neighbour::Incoming::Kind::Connected;
        return n;
    };
    CHECK(PollUntil(link, got, [&](const std::vector<Neighbour::Incoming>& v) { return connectedCount(v) >= 1; }));
    CHECK(got.size() >= 2);
    if (got.size() >= 2) {
        CHECK(got[0].kind == Neighbour::Incoming::Kind::Disconnected);
        CHECK_STREQ(got[0].detail.c_str(), "refused");
    }
    // The line breaks: Disconnected("connection lost"), then a fresh connect and a new hello.
    {
        std::lock_guard<std::mutex> lock(net.mu);
        net.dropLine = true;
        net.cv.notify_all();
    }
    CHECK(PollUntil(link, got, [&](const std::vector<Neighbour::Incoming>& v) { return connectedCount(v) >= 2; }));
    bool sawLost = false;
    for (const auto& in : got) sawLost |= in.detail == "connection lost";
    CHECK(sawLost);
    for (int i = 0; i < 300 && SentCount() < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(SentCount() >= 2);   // a hello per connection
    link.Stop();
    g_net = nullptr;
}

UNIT_TEST(ext_neighbour_link_stop_while_offline) {
    FakeNet net;
    net.failFirst = 1000;   // never connects
    g_net = &net;
    Neighbour::Link link(&MakeFake);
    Neighbour::Link::Options opt;
    opt.url = "ws://fake";
    opt.firstRetryMs = 60000;   // a long pause: Stop must not wait it out
    link.Start(opt);
    std::vector<Neighbour::Incoming> got;
    CHECK(PollUntil(link, got, [](const std::vector<Neighbour::Incoming>& v) { return !v.empty(); }));
    const auto t0 = std::chrono::steady_clock::now();
    link.Stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 2000);
    g_net = nullptr;
}

UNIT_TEST(ext_neighbour_room_url) {
    CHECK_STREQ(Neighbour::RoomUrl("wss://relay.example.workers.dev", "river").c_str(),
                "wss://relay.example.workers.dev/room/river");
    CHECK_STREQ(Neighbour::RoomUrl("ws://127.0.0.1:8787//", "Our Two Towns!").c_str(),
                "ws://127.0.0.1:8787/room/Our%20Two%20Towns%21");
    CHECK_STREQ(Neighbour::RoomUrl("ws://h", "a/b?c").c_str(), "ws://h/room/a%2Fb%3Fc");
}

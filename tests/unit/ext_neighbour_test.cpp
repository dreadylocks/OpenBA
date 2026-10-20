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
    s.outbox.push_back({"a-1", "goods", "Template/Fish.rsc:25", ""});
    s.received = {"x-1", "x-2"};
    s.waiting.push_back({"x-3", "family", "30.0/1/0;6.5/0/1", "Ann"});
    const std::vector<uint8_t> bytes = Neighbour::EncodeState(s);
    Neighbour::SavedState back;
    CHECK(Neighbour::DecodeState(Neighbour::kPayloadFormat, bytes, back));
    CHECK_STREQ(back.room.c_str(), "river-folk");
    CHECK_EQ(back.lastSeq, 123456789012LL);
    CHECK_EQ(static_cast<int>(back.outbox.size()), 1);
    CHECK_EQ(static_cast<int>(back.received.size()), 2);
    CHECK_EQ(static_cast<int>(back.waiting.size()), 1);
    if (back.waiting.size() == 1) {
        CHECK_STREQ(back.waiting[0].body.c_str(), "30.0/1/0;6.5/0/1");
        CHECK_STREQ(back.waiting[0].from.c_str(), "Ann");
    }
    // Every cut fails, a longer buffer fails, a newer version fails; `out` is untouched.
    for (size_t n = 0; n < bytes.size(); ++n) {
        std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<long>(n));
        CHECK(!Neighbour::DecodeState(Neighbour::kPayloadFormat, cut, back));
    }
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    CHECK(!Neighbour::DecodeState(Neighbour::kPayloadFormat, longer, back));
    CHECK(!Neighbour::DecodeState(Neighbour::kPayloadFormat + 1, bytes, back));
    CHECK_STREQ(back.room.c_str(), "river-folk");

    // A version 1 payload (M0's saves): the room and the sequence number only.
    std::vector<uint8_t> v1 = {4, 0, 0, 0, 'r', 'o', 'o', 'm', 7, 0, 0, 0, 0, 0, 0, 0};
    Neighbour::SavedState old;
    CHECK(Neighbour::DecodeState(1, v1, old));
    CHECK_STREQ(old.room.c_str(), "room");
    CHECK_EQ(old.lastSeq, 7);
    CHECK(old.outbox.empty() && old.received.empty() && old.waiting.empty());
}

UNIT_TEST(ext_neighbour_parcel_bodies) {
    // Goods: the count after the LAST ':', so a path may hold one.
    std::vector<std::pair<std::string, int>> g;
    CHECK(Neighbour::DecodeGoods("Template/Fish.rsc:25,C:/x/Logs.rsc:3", g));
    CHECK_EQ(static_cast<int>(g.size()), 2);
    if (g.size() == 2) {
        CHECK_STREQ(g[0].first.c_str(), "Template/Fish.rsc");
        CHECK_EQ(g[0].second, 25);
        CHECK_STREQ(g[1].first.c_str(), "C:/x/Logs.rsc");
        CHECK_EQ(g[1].second, 3);
    }
    CHECK_STREQ(Neighbour::EncodeGoods(g).c_str(), "Template/Fish.rsc:25,C:/x/Logs.rsc:3");
    const char* badGoods[] = {"", "Fish", "Fish:", ":5", "Fish:0", "Fish:-1", "Fish:1x", "Fish:100001",
                              "Fish:5,", "Fi\x01sh:5"};
    for (const char* b : badGoods) CHECK(!Neighbour::DecodeGoods(b, g));

    // A family: ages in tenths, adults first; it must have an adult.
    std::vector<Neighbour::FamilyMember> m = {{30.04f, 1, false}, {28.46f, 0, false}, {6.0f, 0, true}};
    const std::string wire = Neighbour::EncodeMembers(m);
    CHECK_STREQ(wire.c_str(), "30.0/1/0;28.5/0/0;6.0/0/1");
    std::vector<Neighbour::FamilyMember> back;
    CHECK(Neighbour::DecodeMembers(wire, back));
    CHECK_EQ(static_cast<int>(back.size()), 3);
    if (back.size() == 3) {
        CHECK(back[2].child);
        CHECK_EQ(back[1].gender, 0);
        CHECK(back[1].age > 28.4f && back[1].age < 28.6f);
    }
    CHECK(Neighbour::DecodeMembers("31/0/0", back));   // a whole number of years is fine
    const char* badFamilies[] = {"", "6.0/0/1", "30/2/0", "30/1/2", "30/1", "300/1/0", "30.55/1/0", "a/1/0",
                                 "30/1/0;", "30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0;30/1/0"};
    for (const char* b : badFamilies) {
        if (Neighbour::DecodeMembers(b, back)) std::printf("  accepted family: %s\n", b);
        CHECK(!Neighbour::DecodeMembers(b, back));
    }
    CHECK(Neighbour::ValidParcelId("lx3k2-1"));
    CHECK(!Neighbour::ValidParcelId(""));
    CHECK(!Neighbour::ValidParcelId("Bad Id"));
    CHECK(!Neighbour::ValidParcelId(std::string(41, 'a')));
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

// ---------------------------------------------------------------------------
// M2: the lobby's conversations (lobby.cpp) over the fake transport
// ---------------------------------------------------------------------------
#include "ext/neighbour/lobby.h"

UNIT_TEST(ext_neighbour_lobby_settings) {
    Message m("welcome");
    m.SetInt("seed", 4000000000LL).SetInt("size", 1).SetInt("disasters", 0)
     .Set("start", "Game/StartCondition/StartEasy.rsc:resource").Set("terrain", "Game\\Terrain\\Valleys.rsc")
     .Set("climate", "Game/Climate/Mild.rsc");
    Neighbour::RoomSettings s;
    CHECK(Neighbour::ReadSettings(m, s));
    CHECK_EQ(s.seed, 4000000000LL);
    CHECK_EQ(s.size, 1);
    CHECK(!s.disasters);
    CHECK_STREQ(Neighbour::DescribeSettings(s).c_str(), "Medium map, Valleys, Mild, StartEasy start, disasters off");
    Message bad = m;
    bad.SetInt("size", 3);
    CHECK(!Neighbour::ReadSettings(bad, s));
    Message noPath("welcome");
    noPath.SetInt("seed", 1).SetInt("size", 0).SetInt("disasters", 1);
    CHECK(!Neighbour::ReadSettings(noPath, s));
    CHECK_STREQ(Neighbour::LobbyUrl("wss://x.dev/").c_str(), "wss://x.dev/lobby");
}

UNIT_TEST(ext_neighbour_lobby_list_and_join) {
    FakeNet net;
    g_net = &net;
    // The list: two rooms (one with a bad size, dropped), then the end.
    net.toReceive.push_back("{\"t\":\"room\",\"name\":\"river\",\"creator\":\"Ann\",\"seats\":1,\"size\":2,\"disasters\":1,"
                            "\"start\":\"S.rsc\",\"terrain\":\"T.rsc\",\"climate\":\"C.rsc\",\"lastActive\":5}");
    net.toReceive.push_back("{\"t\":\"room\",\"name\":\"broken\",\"seats\":1,\"size\":9,\"disasters\":1,"
                            "\"start\":\"S.rsc\",\"terrain\":\"T.rsc\",\"climate\":\"C.rsc\"}");
    net.toReceive.push_back("{\"t\":\"roomsEnd\",\"count\":2}");
    std::vector<Neighbour::RoomInfo> rooms;
    std::string why;
    CHECK(Neighbour::ListRooms(&MakeFake, "ws://fake", rooms, why, 3000));
    CHECK_EQ(static_cast<int>(rooms.size()), 1);
    if (rooms.size() == 1) {
        CHECK_STREQ(rooms[0].name.c_str(), "river");
        CHECK_EQ(rooms[0].seats, 1);
        CHECK_EQ(rooms[0].settings.size, 2);
    }
    {
        std::lock_guard<std::mutex> lock(net.mu);
        CHECK(!net.sent.empty() && net.sent[0] == "{\"t\":\"list\"}");
        net.sent.clear();
    }

    // A join: the welcome's seed and settings come back; the hello asked to join.
    net.toReceive.push_back("{\"t\":\"welcome\",\"seat\":1,\"peer\":\"Ann\",\"online\":true,\"created\":true,\"creator\":\"Ann\","
                            "\"seed\":77,\"size\":0,\"disasters\":1,\"start\":\"S.rsc\",\"terrain\":\"T.rsc\",\"climate\":\"C.rsc\"}");
    Neighbour::RoomJoin j;
    CHECK(Neighbour::JoinRoom(&MakeFake, "ws://fake", "river", "pw", "Bob", j, why, 3000));
    CHECK_EQ(j.seat, 1);
    CHECK_EQ(j.settings.seed, 77);
    CHECK_STREQ(j.creator.c_str(), "Ann");
    {
        std::lock_guard<std::mutex> lock(net.mu);
        CHECK(!net.sent.empty() && net.sent[0].find("\"join\":true") != std::string::npos);
    }

    // A refusal ends the conversation with the relay's reason; a room without settings is refused too.
    net.toReceive.push_back("{\"t\":\"error\",\"why\":\"wrong room password\"}");
    CHECK(!Neighbour::JoinRoom(&MakeFake, "ws://fake", "river", "bad", "Bob", j, why, 3000));
    CHECK_STREQ(why.c_str(), "wrong room password");
    net.toReceive.push_back("{\"t\":\"welcome\",\"seat\":0,\"peer\":\"\",\"online\":false,\"created\":false}");
    CHECK(!Neighbour::JoinRoom(&MakeFake, "ws://fake", "old", "pw", "Bob", j, why, 3000));
    CHECK(why.find("lobby") != std::string::npos);

    // No answer: a timeout, not a hang.
    CHECK(!Neighbour::ListRooms(&MakeFake, "ws://fake", rooms, why, 300));
    g_net = nullptr;
}

#pragma once
// ============================================================================
// src/ext/neighbour/link.h -- NOT RETAIL. ext.neighbour: the connection to the relay
// ============================================================================
// The game never waits on the network. The Link owns one worker thread that connects,
// sends the hello, writes the outgoing queue and reconnects with a growing pause when the
// line drops; a second thread blocks in Receive and fills the incoming queue. The game's
// main thread only calls Send (queue a message) and Poll (take what arrived), once per
// frame from Neighbour::BeginFrame.
//
// The transport is an interface so the unit tests drive the Link with a fake one; the game's
// is the Platform WebSocket seam (link_platform.cpp, src/platform/common/platform_websocket.h).
// Standard C++ only: tests/unit/ext_neighbour_test.cpp links this file with protocol.cpp.
// ============================================================================

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ext/neighbour/protocol.h"

namespace Neighbour {

class Transport {
public:
    enum class Recv : uint8_t { Text, Closed, Error };
    virtual ~Transport() = default;
    // Blocking. False with `error` set on failure.
    virtual bool Connect(const std::string& url, std::string& error) = 0;
    // One text frame. May be called while another thread blocks in Receive.
    virtual bool Send(const std::string& text) = 0;
    // Blocking: one whole text message (at most kMaxMessageBytes; a larger one is an Error).
    virtual Recv Receive(std::string& out) = 0;
    // Unblocks a pending Receive (it then returns Closed or Error). Any thread, idempotent.
    virtual void Close() = 0;
};
using TransportFactory = std::unique_ptr<Transport> (*)();

// What Poll hands the game: a message from the relay, or a change of line state.
struct Incoming {
    enum class Kind : uint8_t { Message, Connected, Disconnected };
    Kind        kind = Kind::Message;
    Message     message;   // Kind::Message
    std::string detail;    // Kind::Disconnected: why (empty for a deliberate Stop)
};

class Link {
public:
    struct Options {
        std::string url;
        Message     hello;               // sent first on every (re)connect
        uint32_t    firstRetryMs = 2000;
        uint32_t    maxRetryMs   = 30000;
        size_t      maxQueued    = 64;   // outgoing messages kept while offline; oldest dropped
        uint32_t    pingMs       = 25000; // a {"t":"ping"} after this long with nothing sent
    };

    explicit Link(TransportFactory factory) : factory_(factory) {}
    ~Link() { Stop(); }
    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    void Start(const Options& options);   // no-op while running
    void Stop();                          // joins both threads; safe to call twice
    bool Running() const { return worker_.joinable(); }
    bool Connected() const { return connected_.load(); }

    // Main thread. The hello for the NEXT connect (e.g. a newer lastSeq).
    void SetHello(const Message& hello);
    void Send(const Message& message);
    // Main thread: move everything that arrived into `out` (appended).
    void Poll(std::vector<Incoming>& out);

private:
    void WorkerMain();
    void Push(Incoming in);
    // Waits up to `ms`, or until Stop. True when stopping.
    bool PauseOrStop(uint32_t ms);

    TransportFactory      factory_;
    Options               options_;
    std::thread           worker_;
    std::atomic<bool>     stop_{false};
    std::atomic<bool>     connected_{false};

    std::mutex              mu_;          // guards everything below
    std::condition_variable cv_;
    std::deque<std::string> outgoing_;
    std::vector<Incoming>   incoming_;
    bool                    readerDone_ = false;
    Transport*              live_ = nullptr;   // the connected transport, for Stop's Close
};

}  // namespace Neighbour

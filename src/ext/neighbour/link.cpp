// ============================================================================
// src/ext/neighbour/link.cpp -- NOT RETAIL. ext.neighbour's relay connection (link.h)
// ============================================================================

#include "ext/neighbour/link.h"

#include <chrono>
#include <utility>

namespace Neighbour {

void Link::Start(const Options& options) {
    if (worker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        options_ = options;
        readerDone_ = false;
    }
    stop_.store(false);
    worker_ = std::thread([this] { WorkerMain(); });
}

void Link::Stop() {
    if (!worker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_.store(true);
        if (live_ != nullptr) live_->Close();   // unblocks the reader
    }
    cv_.notify_all();
    worker_.join();
    connected_.store(false);
}

void Link::SetHello(const Message& hello) {
    std::lock_guard<std::mutex> lock(mu_);
    options_.hello = hello;
}

void Link::Send(const Message& message) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        outgoing_.push_back(message.Encode());
        while (outgoing_.size() > options_.maxQueued) outgoing_.pop_front();
    }
    cv_.notify_all();
}

void Link::Poll(std::vector<Incoming>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    for (Incoming& in : incoming_) out.push_back(std::move(in));
    incoming_.clear();
}

void Link::Push(Incoming in) {
    std::lock_guard<std::mutex> lock(mu_);
    incoming_.push_back(std::move(in));
}

bool Link::PauseOrStop(uint32_t ms) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
    return stop_.load();
}

void Link::WorkerMain() {
    uint32_t retryMs = options_.firstRetryMs;
    while (!stop_.load()) {
        std::unique_ptr<Transport> t = factory_();
        std::string error;
        std::string url;
        {
            std::lock_guard<std::mutex> lock(mu_);
            url = options_.url;
        }
        if (t == nullptr || !t->Connect(url, error)) {
            Incoming in;
            in.kind = Incoming::Kind::Disconnected;
            in.detail = error.empty() ? "could not connect" : error;
            Push(std::move(in));
            if (PauseOrStop(retryMs)) break;
            retryMs = retryMs * 2 > options_.maxRetryMs ? options_.maxRetryMs : retryMs * 2;
            continue;
        }

        std::string hello;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stop_.load()) break;
            live_ = t.get();
            readerDone_ = false;
            hello = options_.hello.Encode();
        }
        connected_.store(true);
        retryMs = options_.firstRetryMs;
        {
            Incoming in;
            in.kind = Incoming::Kind::Connected;
            Push(std::move(in));
        }

        // The reader: blocks in Receive until the line closes or Stop closes it.
        Transport* raw = t.get();
        std::thread reader([this, raw] {
            std::string text;
            for (;;) {
                const Transport::Recv r = raw->Receive(text);
                if (r != Transport::Recv::Text) break;
                Incoming in;
                if (Parse(text, in.message)) Push(std::move(in));   // malformed: dropped
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                readerDone_ = true;
            }
            cv_.notify_all();
        });

        // The writer: the hello, then the queue, until the reader ends or Stop.
        // A quiet line gets a ping every kPingMs, so idle-connection timeouts on the way
        // (proxies, the relay's host) never see it as dead.
        bool ok = t->Send(hello);
        while (ok) {
            std::string next;
            {
                std::unique_lock<std::mutex> lock(mu_);
                const bool woke = cv_.wait_for(lock, std::chrono::milliseconds(options_.pingMs), [this] {
                    return stop_.load() || readerDone_ || !outgoing_.empty();
                });
                if (stop_.load() || readerDone_) break;
                if (woke) {
                    next = std::move(outgoing_.front());
                    outgoing_.pop_front();
                } else {
                    next = Message("ping").Encode();
                }
            }
            ok = t->Send(next);
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            live_ = nullptr;
        }
        t->Close();
        reader.join();
        connected_.store(false);

        Incoming in;
        in.kind = Incoming::Kind::Disconnected;
        in.detail = stop_.load() ? "" : "connection lost";
        Push(std::move(in));
        if (stop_.load()) break;
        if (PauseOrStop(retryMs)) break;
        retryMs = retryMs * 2 > options_.maxRetryMs ? options_.maxRetryMs : retryMs * 2;
    }
}

}  // namespace Neighbour

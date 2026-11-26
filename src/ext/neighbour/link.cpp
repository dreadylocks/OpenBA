// ============================================================================
// src/ext/neighbour/link.cpp -- NOT RETAIL. ext.neighbour's relay connection (link.h)
// ============================================================================

#include "ext/neighbour/link.h"

#include <chrono>
#include <utility>

namespace Neighbour {

namespace {

using Clock = std::chrono::steady_clock;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

}  // namespace

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

void Link::Send(const Message& message, bool ephemeral) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (ephemeral) {
            if (!connected_.load()) return;
            newest_[message.type + '\x1f' + message.GetOr("tmpl", "")] = message.Encode();
        } else {
            outgoing_.push_back(message.Encode());
        }
        while (outgoing_.size() > options_.maxQueued) {
            outgoing_.pop_front();
            dropped_.fetch_add(1);
        }
    }
    cv_.notify_all();
}

size_t Link::Queued() {
    std::lock_guard<std::mutex> lock(mu_);
    return outgoing_.size();
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
                heardMs_.store(NowMs());
                Incoming in;
                if (Parse(text, in.message)) Push(std::move(in));   // malformed: dropped
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                readerDone_ = true;
            }
            cv_.notify_all();
        });

        // The writer: the hello, then the queue, until the reader ends or Stop. A ping every pingMs,
        // busy or not: idle-connection timeouts on the way (proxies, the relay's host) never see the line
        // as dead, and its pong shows this end the line is alive (Options::deadMs).
        heardMs_.store(NowMs());
        Clock::time_point pingAt = Clock::now() + std::chrono::milliseconds(options_.pingMs);
        bool quiet = false;   // nothing came for deadMs
        bool ok = t->Send(hello);
        while (ok) {
            std::string next;
            bool queued = false;   // `next` came off the queue (a ping did not)
            {
                std::unique_lock<std::mutex> lock(mu_);
                const Clock::time_point heard{std::chrono::milliseconds(heardMs_.load())};
                Clock::time_point wakeAt = pingAt;
                if (options_.deadMs > 0 && heard + std::chrono::milliseconds(options_.deadMs) < wakeAt)
                    wakeAt = heard + std::chrono::milliseconds(options_.deadMs);
                cv_.wait_until(lock, wakeAt, [this] {
                    return stop_.load() || readerDone_ || !newest_.empty() || !outgoing_.empty();
                });
                if (stop_.load() || readerDone_) break;
                const Clock::time_point now = Clock::now();
                const Clock::time_point heardNow{std::chrono::milliseconds(heardMs_.load())};
                if (options_.deadMs > 0 && now - heardNow >= std::chrono::milliseconds(options_.deadMs)) {
                    quiet = true;
                    break;
                }
                if (now >= pingAt) {
                    next = Message("ping").Encode();
                    pingAt = now + std::chrono::milliseconds(options_.pingMs);
                } else if (!newest_.empty()) {   // the newest villagers first, never resent
                    next = std::move(newest_.begin()->second);
                    newest_.erase(newest_.begin());
                } else if (!outgoing_.empty()) {
                    next = std::move(outgoing_.front());
                    outgoing_.pop_front();
                    queued = true;
                } else {
                    continue;   // woke for the dead-line check alone
                }
            }
            ok = t->Send(next);
            if (!ok && queued) {   // not written: first in line again for the next connection
                std::lock_guard<std::mutex> lock(mu_);
                outgoing_.push_front(std::move(next));
            }
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            live_ = nullptr;
            newest_.clear();   // stale by the next connection
        }
        t->Close();
        reader.join();
        connected_.store(false);

        Incoming in;
        in.kind = Incoming::Kind::Disconnected;
        in.detail = stop_.load() ? "" : (quiet ? "the relay went quiet" : "connection lost");
        Push(std::move(in));
        if (stop_.load()) break;
        if (PauseOrStop(retryMs)) break;
        retryMs = retryMs * 2 > options_.maxRetryMs ? options_.maxRetryMs : retryMs * 2;
    }
}

}  // namespace Neighbour

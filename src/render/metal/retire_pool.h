#pragma once
// Pure C++ (no Metal types): the bookkeeping rules of the Metal backend's buffer recycling and its pipeline record
// cap, kept here so unit tests can reproduce the orderings that matter.
//
// INVARIANT (buffer recycling): a buffer may be handed out again only when EVERY command buffer that encoded it has
// completed. Two checks enforce it together: (1) the retired entry's own ticket (the last use known when the buffer
// was renamed away) and (2) the buffer's CURRENT last-use ticket, asked through `busy` at take time. (2) matters
// because a draw can record a use after the rename retired the buffer; so uses must be recorded in the same
// critical section that snapshots the binding, and Take must never trust the retire-time ticket alone.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VideoMetal {

using RetireTicket = std::shared_ptr<std::atomic<bool>>;

inline bool TicketDone(const RetireTicket& ticket) {
    return !ticket || ticket->load(std::memory_order_acquire);
}

// Retired buffers waiting for the GPU. Not thread safe: the caller holds its own lock.
template <class Buffer>
class RetirePool {
public:
    struct Entry {
        Buffer buffer{};
        uint64_t length = 0;
        RetireTicket ticket;
        uint64_t retiredFrame = 0;
    };
    // `maxEntries` bounds the list: past it, the oldest entries whose ticket completed are dropped (an unfinished one
    // is never dropped, so the bound is soft only while the GPU still owns buffers).
    explicit RetirePool(size_t maxEntries = 128) : maxEntries_(maxEntries) {}

    void Retire(Buffer buffer, size_t length, RetireTicket ticket, uint64_t frame) {
        entries_.push_back(Entry{ std::move(buffer), length, std::move(ticket), frame });
        for (auto it = entries_.begin(); entries_.size() > maxEntries_ && it != entries_.end();) {
            if (TicketDone(it->ticket)) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // `busy(buffer)` answers whether the buffer is still referenced by an unfinished command buffer.
    template <class BusyFn>
    bool Take(size_t length, BusyFn busy, Buffer& out) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->length != length || !TicketDone(it->ticket) || busy(it->buffer)) {
                continue;
            }
            out = std::move(it->buffer);
            entries_.erase(it);
            return true;
        }
        return false;
    }
    void Trim(uint64_t frame, uint64_t maxAge) {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (frame - it->retiredFrame > maxAge) {
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }
    void Clear() {
        entries_.clear();
    }
    size_t Size() const {
        return entries_.size();
    }
    const std::vector<Entry>& Entries() const {
        return entries_;
    }

private:
    size_t maxEntries_;
    std::vector<Entry> entries_;
};

// pipelines.txt cap: when a new record arrives at a full table, evict one stale record first (`isStale(key)`), else
// refuse. Returns true when the new record may be added (after any eviction). `keys` is any container with
// size()/erase by key; stale candidates come from `candidates`.
template <class Keys, class StaleFn>
bool MakeRoomForRecord(Keys& keys, const std::vector<std::string>& candidates, size_t cap, StaleFn isStale) {
    if (keys.size() < cap) {
        return true;
    }
    for (const auto& key : candidates) {
        if (isStale(key) && keys.erase(key) > 0) {
            return keys.size() < cap;
        }
    }
    return false;
}

}  // namespace VideoMetal

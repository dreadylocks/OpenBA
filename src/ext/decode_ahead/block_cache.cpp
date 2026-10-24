// ============================================================================
// src/ext/decode_ahead/block_cache.cpp -- NOT RETAIL. ext.decode_ahead's block cache
// ============================================================================
// block_cache.h has the contract. One mutex guards the job table and the queue; a worker
// releases it only while it decodes, into the job's own buffer, from a private copy of the
// compressed bytes (the copy gives the decoder retail's slack after the block: DecompressFromSource
// decodes from a 0x8000 stack buffer, never from the end of a mapping).
// ============================================================================

#include "ext/decode_ahead/block_cache.h"

#include <cstring>

namespace Ext { namespace DecodeAhead {

BlockCache::BlockCache(int workers, int maxJobs, DecodeFn decode) : decode_(decode), jobs_(maxJobs) {
    for (Job& j : jobs_) j.out.resize(kBlock);
    for (int i = 0; i < workers; ++i) threads_.emplace_back([this] { WorkerMain(); });
}

BlockCache::~BlockCache() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    work_.notify_all();
    for (std::thread& t : threads_) t.join();
}

BlockCache::Job* BlockCache::Find(const void* owner, int32_t index) {
    for (Job& j : jobs_)
        if (j.state != State::Free && j.owner == owner && j.index == index) return &j;
    return nullptr;
}

bool BlockCache::Submit(const void* owner, int32_t index, uint32_t offset, const uint8_t* src,
                        uint16_t csize, uint16_t usize) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (csize > kBlock) return false;   // no window holds it: the loader decodes it itself (retail's path)
        if (Find(owner, index) != nullptr) return false;
        Job* slot = nullptr;
        for (Job& j : jobs_) {
            if (j.state == State::Free) {
                slot = &j;
                break;
            }
        }
        if (slot == nullptr) {
            ++stats_.full;
            return false;
        }
        slot->state  = State::Queued;
        slot->owner  = owner;
        slot->index  = index;
        slot->offset = offset;
        slot->src    = src;
        slot->csize  = csize;
        slot->usize  = usize;
        slot->result = 0;
        queue_.push_back(slot);
        ++stats_.submitted;
    }
    work_.notify_one();
    return true;
}

int BlockCache::Take(const void* owner, int32_t index, uint32_t offset, const uint8_t* src,
                     uint16_t csize, uint8_t* dst) {
    std::unique_lock<std::mutex> lk(mu_);
    Job* const j = Find(owner, index);
    if (j == nullptr) return -1;
    if (j->state != State::Done) {
        ++stats_.waited;
        done_.wait(lk, [j] { return j->state == State::Done; });
    }
    int got = -1;
    if (j->offset == offset && j->src == src && j->csize == csize && j->result == j->usize) {
        std::memcpy(dst, j->out.data(), j->usize);
        got = j->usize;
        ++stats_.taken;
    } else {
        ++stats_.rejected;
    }
    j->state = State::Free;
    j->owner = nullptr;
    return got;
}

void BlockCache::Drop(const void* owner) {
    std::unique_lock<std::mutex> lk(mu_);
    for (Job& j : jobs_) {
        if (j.state == State::Free || j.owner != owner) continue;
        if (j.state == State::Queued) {
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                if (*it == &j) {
                    queue_.erase(it);
                    break;
                }
            }
        } else if (j.state == State::Running) {
            done_.wait(lk, [&j] { return j.state == State::Done; });
        }
        j.state = State::Free;
        j.owner = nullptr;
        ++stats_.dropped;
    }
}

BlockCache::Stats BlockCache::GetStats() {
    std::lock_guard<std::mutex> lk(mu_);
    return stats_;
}

void BlockCache::WorkerMain() {
    std::vector<uint8_t> scratch(kBlock + kSlack, 0);
    std::unique_lock<std::mutex> lk(mu_);
    for (;;) {
        work_.wait(lk, [this] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        Job* const j = queue_.front();
        queue_.pop_front();
        j->state = State::Running;
        const uint8_t* const src = j->src;
        const uint16_t csize = j->csize;
        uint8_t* const out = j->out.data();
        lk.unlock();

        std::memcpy(scratch.data(), src, csize);
        const int result = decode_(scratch.data(), csize, out, kBlock);

        lk.lock();
        j->result = result;
        j->state = State::Done;
        done_.notify_all();
    }
}

}}  // namespace Ext::DecodeAhead

// ============================================================================
// src/ext/async_save_compress/async_save_compress.cpp -- NOT RETAIL. ext.async_save_compress
// ============================================================================
// Class A (docs/extensions/handins/HITCH_SAVE.md). Retail compresses a save on the main
// thread: GameUpdateLoop::SaveRestore writes the whole game state through a
// CompressedMemoryStream with two 32 KB windows, and every time a write needs a third
// block, GetAvailableWindow evicts the older window through CompressWindow, i.e. one
// System::Compression::Compress of 32 KB in the middle of the serialize. On the stress
// town that is ~775 calls, ~35 ms of the ~95 ms save frame.
//
// With the extension on, CompressWindow (through the engine seam
// IO::CompressedStream::_extAsyncCompress, point kExtAsyncCompressWindow: SubmitWindow below;
// the body moved here from src/engine/io/compressed_stream_async.cpp on 2026-10-20) copies the
// window and queues it; ONE worker thread runs the jobs in submission order. That order, the
// inputs (the copy is the bytes retail's Compress would read at that moment) and the one
// Compression object whose hash table carries over between calls are all retail's, so
// each call returns what the synchronous call returns, and the archive Commit writes is
// byte-identical. What moves is WHEN the block lands: the worker's result (the allocated
// block, its compressedSize) is stored by the main thread, in submission order (a block
// compressed twice keeps the second result, as retail's overwrite does), at the points
// that need it:
//   * every submit stores the jobs already finished (no wait);
//   * Commit, after it has flushed the resident windows (point kExtAsyncEnd: End waits for
//     the rest);
//   * Release, should a stream be destroyed without a Commit (the same point).
// SetWindow's in-memory branch (a seek back into an evicted block) is the one reader of a
// block before Commit: for a block whose job is still queued it copies the job's bytes
// into the window instead of decoding the stored block (point kExtAsyncCopyQueued).
// Decompress(Compress(x)) is x, so the window holds what retail's decode writes; the job
// still lands, in order. uncompressedSize is stored at submit time, as retail stores it
// before compressing, so GetSize never sees a difference. Nothing else reads a block or a
// compressedSize while the stream is writing.
//
// The main thread never calls Compress while a stream is attached, so the worker owns the
// table. Blocks are allocated by the worker through the locked MemAlloc retail uses.
// ============================================================================

#include "ext/async_save_compress/async_save_compress.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "io/Stream.h"
#include "system/Compression.h"
#include "system/MemoryAllocator.h"

namespace Ext { namespace AsyncSaveCompress {

using IO::CompressedStream;

namespace {

// The attached stream, null when no save is attached. Written by Begin / End; read by the seam
// (any stream's CompressWindow / SetWindow / Commit / Release reaches it while the seam is set).
const CompressedStream* g_owner = nullptr;

constexpr int32_t kWin = RtIo::kCompressedStreamWindowSize;   // 0x8000
// Jobs in flight before the main thread waits: 1024 x 32 KB = 32 MB of copies at most. The
// stress town's save is ~775 windows, and its entity section writes them about as fast as
// the worker compresses (a 32-job cap made the main thread wait ~6 ms), so a save of that
// size never waits before Commit. The copies are freed when the save ends.
constexpr size_t  kMaxQueued = 1024;

struct Job {
    int32_t  blk = 0;
    uint16_t len = 0;
    uint16_t compressedSize = 0;
    void*    out = nullptr;
    bool     done = false;
    uint8_t* src = nullptr;   // a kWin buffer from the pool
};

struct AsyncState {
    std::mutex              mu;
    std::condition_variable work;   // worker: a job was queued
    std::condition_variable done;   // main: a job finished
    std::deque<Job*>        queue;  // not yet started, FIFO
    std::deque<Job*>        order;  // every job not yet stored, submission order
    std::vector<uint8_t*>   pool;   // spare kWin copy buffers
    System::Compression*    comp = nullptr;
    bool                    started = false;
    // counters of the current / last attachment
    Stats                   stats{};
    double                  workerMs = 0.0;
};

AsyncState& S() {
    static AsyncState* s = new AsyncState();   // never destroyed: the worker outlives exit
    return *s;
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void WorkerMain() {
    AsyncState& s = S();
    uint8_t scratch[kWin];   // the same 0x8000 output capacity CompressWindow hands Compress
    std::unique_lock<std::mutex> lk(s.mu);
    for (;;) {
        s.work.wait(lk, [&] { return !s.queue.empty(); });
        Job* const j = s.queue.front();
        s.queue.pop_front();
        System::Compression* const comp = s.comp;
        lk.unlock();

        const double t0 = NowMs();
        // CompressWindow's body from the Compress call on (1800a989d..1800a9909).
        const int rc = comp->Compress(j->src, j->len, scratch, static_cast<uint32_t>(kWin));
        uint16_t cs = static_cast<uint16_t>(rc);
        if (cs > j->len) cs = 0;                                  // the unsigned JBE
        const uint32_t stored = cs != 0 ? cs : j->len;            // StoredLength
        void* const dst = ::System::MemoryAllocator::MemAlloc(stored, 4);
        std::memcpy(dst, cs != 0 ? scratch : j->src, stored);
        const double t1 = NowMs();

        lk.lock();
        j->compressedSize = cs;
        j->out  = dst;
        j->done = true;
        s.workerMs += t1 - t0;
        s.done.notify_all();
    }
}

// Store the finished jobs at the front of the submission order. `wait` = store all of
// them, waiting for the worker. Caller holds the lock.
void StoreFinished(CompressedStream* cs, std::unique_lock<std::mutex>& lk, bool wait) {
    AsyncState& s = S();
    for (;;) {
        while (!s.order.empty() && s.order.front()->done) {
            Job* const j = s.order.front();
            s.order.pop_front();
            // CompressWindow's block replacement (1800a986e..7a, 1800a98aa, 1800a98d8),
            // by index: the arrays may have been reallocated since the submit.
            void** const blocks = cs->_blockData;
            if (blocks[j->blk] != nullptr) ::System::MemoryAllocator::MemFree(blocks[j->blk]);
            cs->_headerData[j->blk].compressedSize = j->compressedSize;
            blocks[j->blk] = j->out;
            s.pool.push_back(j->src);
            delete j;
        }
        if (!wait || s.order.empty()) return;
        const double t0 = NowMs();
        s.done.wait(lk, [&] { return s.order.front()->done; });
        s.stats.endWaitMs += NowMs() - t0;
    }
}

// Point kExtAsyncCompressWindow: CompressWindow(i) of the attached stream.
void SubmitWindow(CompressedStream* cs, int i) {
    RtIo::CompressedStreamWindow* const w = &cs->_windowData[i];
    if (w->start < 0) return;                                    // as retail: nothing to encode

    const double t0 = NowMs();
    AsyncState& s = S();
    const int32_t blk = w->start >> 15;
    const uint16_t len = static_cast<uint16_t>(w->end - w->start);
    cs->_headerData[blk].uncompressedSize = len;                 // 1800a987f..98, at once

    uint8_t* buf;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.pool.empty()) {
            buf = new uint8_t[kWin];
        } else {
            buf = s.pool.back();
            s.pool.pop_back();
        }
    }
    std::memcpy(buf, w->buffer, len);
    Job* const j = new Job();
    j->blk = blk;
    j->len = len;
    j->src = buf;

    std::unique_lock<std::mutex> lk(s.mu);
    s.queue.push_back(j);
    s.order.push_back(j);
    ++s.stats.jobs;
    s.work.notify_one();
    s.stats.mainCopyMs += NowMs() - t0;
    StoreFinished(cs, lk, false);
    if (static_cast<int>(s.order.size()) > s.stats.maxQueued) s.stats.maxQueued = static_cast<int>(s.order.size());
    if (s.order.size() > kMaxQueued) {
        const double t1 = NowMs();
        s.done.wait(lk, [&] { return s.order.front()->done; });
        s.stats.fullWaitMs += NowMs() - t1;
        StoreFinished(cs, lk, false);
    }
}

// Point kExtAsyncCopyQueued: SetWindow's in-memory branch. When a job for `blk` is still
// queued, copy the bytes it holds (the latest submitted for `blk`) to `dst` and return true;
// else false (the stored block is final, the engine decodes it as retail does).
bool CopyQueued(CompressedStream* cs, int blk, uint8_t* dst) {
    AsyncState& s = S();
    std::unique_lock<std::mutex> lk(s.mu);
    StoreFinished(cs, lk, false);
    for (size_t k = s.order.size(); k-- > 0;) {
        const Job* const j = s.order[k];
        if (j->blk != blk) continue;
        // The latest job for `blk`: its copy is what retail's decode of the block it will
        // store returns (Decompress(Compress(x)) == x; a stored block is x itself), and it
        // fills the same [0, len) of the window. The worker only reads the copy.
        std::memcpy(dst, j->src, j->len);
        ++s.stats.copies;
        return true;
    }
    return false;
}

// Point kExtAsyncEnd: Commit (after its flush, before PreCommit) or Release. Store every
// queued block (waiting for the worker), then detach: the seam goes back to null.
void End(CompressedStream* cs) {
    AsyncState& s = S();
    std::unique_lock<std::mutex> lk(s.mu);
    StoreFinished(cs, lk, true);   // every queued block, waiting for the worker
    for (uint8_t* b : s.pool) delete[] b;
    s.pool.clear();
    s.pool.shrink_to_fit();
    s.stats.workerMs = s.workerMs;
    s.comp = nullptr;
    CompressedStream::_extAsyncCompress = nullptr;
    g_owner = nullptr;
}

// The engine seam (io/Stream.h, CompressedStream::ExtAsyncCompressFn). Set only while a stream
// is attached; any other stream that reaches a seam point meanwhile gets false (retail's body).
bool Seam(CompressedStream& cs, int32_t point, int32_t index, uint8_t* dst) {
    if (&cs != g_owner) return false;
    switch (point) {
    case CompressedStream::kExtAsyncCompressWindow: SubmitWindow(&cs, index);   return true;
    case CompressedStream::kExtAsyncCopyQueued:     return CopyQueued(&cs, index, dst);
    case CompressedStream::kExtAsyncEnd:            End(&cs);                   return true;
    default:                                        return false;
    }
}

}  // namespace

void Begin(CompressedStream& cs) {
    // Only a writing stream with its Compression object, and one stream at a time; any
    // other case keeps retail's synchronous body.
    if (g_owner != nullptr || cs._direction != 1 || cs._compression == nullptr) return;
    AsyncState& s = S();
    std::lock_guard<std::mutex> lk(s.mu);
    s.comp     = cs._compression;
    s.stats    = Stats{};
    s.workerMs = 0.0;
    if (!s.started) {
        s.started = true;
        std::thread(WorkerMain).detach();
    }
    g_owner = &cs;
    CompressedStream::_extAsyncCompress = &Seam;
}

Stats LastStats() {
    AsyncState& s = S();
    std::lock_guard<std::mutex> lk(s.mu);
    return s.stats;
}

}}  // namespace Ext::AsyncSaveCompress

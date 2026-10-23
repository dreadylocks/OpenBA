// ============================================================================
// src/ext/render_pipe/handoff.h -- RENDERPIPE-1: the depth-1 hand-off between
// the main thread and the render thread
// ============================================================================
// NOT RETAIL. ext.render_pipe (docs/extensions/handins/RENDERPIPE1.md section 2).
//
// Standard C++ only (std::mutex / std::condition_variable / std::atomic, as
// src/ext/path_workers/pool.h): the unit test drives it with a std::thread.
//
// ONE job slot. The main thread writes the job (the packet, held by the caller)
// and then Publish()es it; the render thread blocks in WaitJob() until a job or
// Stop() arrives, runs it, writes its result (held by the caller) and calls
// Done(). The main thread's WaitIdle() returns once the render thread is idle.
// Nobody spins: both sides park on a condition variable.
//
// RENDERPIPE_FENCE (docs/extensions/handins/RENDERPIPE_FENCE.md): a job may also
// Release() itself before it is Done -- the emit does so after the post-draw
// callbacks, before EndCommands / the frame index / Present. WaitReleased()
// returns at that point (or at Done, for a job that never released: a failed
// BeginCommands). Only the fence waits on it; every other join waits for idle.
//
// MEMORY ORDERING (the pairs; each mutex unlock is a release, each lock an
// acquire, and the atomic is the fast path):
//   publish  main:   [writes the packet] -> lock; ready = true; busy = true
//                    (relaxed: the unlock releases it); unlock (RELEASE)
//            render: lock (ACQUIRE) in WaitJob, sees ready -> every packet
//                    write made before Publish happens-before the emit
//   idle     render: [the emit, Finish, the result] -> lock;
//                    busy.store(false, RELEASE); unlock; notify
//            main:   busy.load(ACQUIRE) == false (the fast path), or lock
//                    (ACQUIRE) + wait until !busy -> every write of the emit
//                    (the context, _frameUpdateTime, SaveLoad's capture flags,
//                    the road tool's path, the result) happens-before what
//                    main runs next (steps, the next main half's writes)
//   released render: [the emit up to PostDraw] -> lock;
//                    released.store(true, RELEASE); unlock; notify
//            main:   released.load(ACQUIRE) == true (the fast path), or lock
//                    (ACQUIRE) + wait until released -> every write the emit
//                    made up to its post-draw callbacks (pass 2, the post-scene
//                    draws, SaveLoad's capture flags, the road tool's path)
//                    happens-before the steps. The tail's own writes (the
//                    resolve, _frameUpdateTime, ++Context::_frameIndex, Present,
//                    the result) are NOT covered: they wait for idle.
//   Done stores busy = false BEFORE released = true, so a main thread that sees
//   released also sees busy == false when the job is complete (a failed
//   BeginCommands: released only by Done).
//   stop     main (render idle): lock; stop = true; unlock; notify ->
//            render: WaitJob returns false, Run returns, ~Thread joins
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace Ext {
namespace RenderPipe {

class Handoff {
public:
    // ---- the main thread -------------------------------------------------
    // The job (written by the caller before this call) is handed over. The
    // render thread must be idle (the caller joined it first: depth 1).
    void Publish() {
        {
            std::lock_guard<std::mutex> lk(_m);
            _ready = true;
            _busy.store(true, std::memory_order_relaxed);
            _released.store(false, std::memory_order_relaxed);
        }
        _work.notify_one();
    }

    // Block until the render thread is idle. True when it had to wait.
    bool WaitIdle() {
        if (!_busy.load(std::memory_order_acquire)) return false;
        std::unique_lock<std::mutex> lk(_m);
        _idle.wait(lk, [this] { return !_busy.load(std::memory_order_acquire); });
        return true;
    }

    // Block until the job released itself (Release) or is done. True when it had to wait.
    bool WaitReleased() {
        if (_released.load(std::memory_order_acquire)) return false;
        std::unique_lock<std::mutex> lk(_m);
        _idle.wait(lk, [this] { return _released.load(std::memory_order_acquire); });
        return true;
    }

    bool Busy() const { return _busy.load(std::memory_order_acquire); }

    // The render thread is idle (WaitIdle returned): make its WaitJob return false.
    void Stop() {
        {
            std::lock_guard<std::mutex> lk(_m);
            _stop = true;
        }
        _work.notify_one();
    }

    // ---- the render thread -----------------------------------------------
    // Park until a job (true) or Stop (false).
    bool WaitJob() {
        std::unique_lock<std::mutex> lk(_m);
        _work.wait(lk, [this] { return _ready || _stop; });
        if (_ready) {
            _ready = false;
            return true;
        }
        return false;
    }

    // The part of the job the fence waits for is complete; the rest may still run.
    void Release() {
        {
            std::lock_guard<std::mutex> lk(_m);
            _released.store(true, std::memory_order_release);
        }
        _idle.notify_one();
    }

    // The job and its result are complete. busy first: see the ordering banner.
    void Done() {
        {
            std::lock_guard<std::mutex> lk(_m);
            _busy.store(false, std::memory_order_release);
            _released.store(true, std::memory_order_release);
        }
        _idle.notify_one();
    }

private:
    std::mutex              _m;
    std::condition_variable _work;
    std::condition_variable _idle;
    bool                    _ready = false;   // guarded by _m
    bool                    _stop  = false;   // guarded by _m
    std::atomic<bool>       _busy{false};
    std::atomic<bool>       _released{true};   // RENDERPIPE_FENCE: false from Publish to Release / Done
};

}  // namespace RenderPipe
}  // namespace Ext

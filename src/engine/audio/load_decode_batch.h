#pragma once
// ============================================================================
// src/engine/audio/load_decode_batch.h -- the sound-effect decodes of one load, in parallel
// ============================================================================
// NOT RETAIL (block SNDPAR, docs/extensions/handins/SNDPAR.md). Retail handed xWMA packets
// to XAudio2; the port decodes each non-streamed SoundBuffer to PCM at load
// (SoundBuffer::DecodeAtLoad, Phase D.4 SDL-4), inside its Serialize, one after another on
// the loading thread. This module lets those decodes run as one fork-join batch per load:
//
//   * Object::External::Load opens a Scope for its whole body. The OUTERMOST Scope on a
//     thread is the batch: a SoundBuffer serialized under it is QUEUED (Defer) instead of
//     decoded, and when that outermost Load ends, every queued decode runs through
//     System::ParallelFor (largest first), then every FINISH (the step that publishes the
//     PCM into the buffer) runs on the loading thread in queue order.
//   * Every reader of the decoded fields calls Await(buffer) first (SoundBuffer::
//     EnsureDecodedAtLoad: PlayPlatform, Serialize, ~SoundBuffer). With nothing queued
//     anywhere that is one atomic load. A reader that reaches a queued buffer before its
//     batch finished it completes that one decode itself (or waits for the thread that is
//     running it), so no reader ever sees the packets the decode replaces.
//
// EXACTNESS. A job's decode is a pure function of the item (each SoundBuffer has its own
// decoder, packets and output); the FINISH runs exactly once per job. Both run under the
// floating-point environment (MXCSR and the x87 control word) the deferring thread had when
// it queued the job, whichever thread runs them, so a decode gives the bytes it gave when it
// ran inline on that thread.
//
// No flag, no toggle. With a 0-worker pool (one core) ParallelFor runs the batch inline on
// the loading thread: the serial path.
// ============================================================================

#include <atomic>
#include <cstdint>

namespace Audio {
namespace LoadDecode {

// decode(item, result): the pure part (reads the item, writes only `result`). Any thread.
// finish(item, result): publishes `result` into the item and releases `result`. Once.
using JobFn = void (*)(void* item, void* result);

// One per Object::External::Load call; the outermost one on a thread runs the batch in its
// destructor. Not copyable.
class Scope {
public:
    Scope();
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// True when a Scope is open on this thread (a Defer would queue).
bool BatchOpen();

// Queue `item` on this thread's batch. `cost` orders the parallel phase (largest first; it
// changes nothing else). False, and nothing queued, when no Scope is open on this thread
// or `item` is already queued: the caller then runs decode + finish itself, now.
bool Defer(void* item, void* result, uint64_t cost, JobFn decode, JobFn finish);

namespace detail {
extern std::atomic<int> g_queued;   // jobs queued and not yet finished, all threads
void AwaitSlow(const void* item);
}  // namespace detail

// Returns once `item` is not queued: finished by its batch, by this call, or never queued.
inline void Await(const void* item) {
    if (detail::g_queued.load(std::memory_order_acquire) != 0) detail::AwaitSlow(item);
}

}  // namespace LoadDecode
}  // namespace Audio

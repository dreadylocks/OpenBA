// ============================================================================
// src/engine/system/parallel_for.h -- one fork-join helper for data-parallel loads
// ============================================================================
// NOT RETAIL (block WGPAR, docs/extensions/handins/WGPAR.md). The game ran every
// load on one thread; this splits a loop over [begin, end) into chunks that a fixed
// pool of worker threads and the CALLING thread run together, and returns when every
// chunk is done (fork-join). It is a faithful-path speedup tool: a caller may use it
// only where each output element is computed by exactly the same operations as the
// sequential loop, with no cross-element accumulation and no RNG draw inside the
// loop -- the result is then byte-identical whatever the worker count or chunk order.
// Each call site says in a comment why its split is exact.
//
//   ParallelFor(begin, end, grain, f)   f(int chunkBegin, int chunkEnd) for chunks of
//                                       `grain` indices (the last one shorter); grain
//                                       <= 0 picks ~4 chunks per thread.
//   ParallelForRaw(...)                 the same over a plain function pointer + ctx
//                                       (what the template forwards to: no
//                                       std::function, no allocation per call).
//
// THE POOL: created lazily by the first call that needs it (or by Workers()), once:
// Cpu::ParallelWorkers(Cpu::Get().physical) threads (src/platform/common/cpu/roster.h: the
// physical cores inside `sys.cpu_limit` minus the caller, at most kMaxWorkers). With
// 0 workers (one core) every call runs `f(begin, end)` inline on the caller. Workers
// park on a condition variable between calls (no CPU), register with DiagThreads as
// role 'J' (the sampler sees them), and run with the caller's MXCSR (rounding /
// denormal mode) copied in for every call. Shutdown() (also run at static
// destruction) stops and joins them.
//
// RULES: calls are serialised (one fork-join at a time; a second thread's call waits).
// A nested call -- from inside `f` on any thread -- runs inline on that thread. `f`
// must not throw. Meant for coarse loops (a call wakes every worker: ~10-50 us of
// overhead), e.g. one call per world-generation pass, never one per small brush.
// ============================================================================
#pragma once

#include <type_traits>

namespace System {

namespace Parallel {

constexpr int kMaxWorkers = 7;   // == Cpu::kMaxParallelWorkers

using RangeFn = void (*)(void* ctx, int begin, int end);

// The worker count (builds the pool on first use).
int Workers();

// Replace the pool by one of `workers` threads (clamped to [0, kMaxWorkers]); the unit
// tests use it to run 0 / 1 / 2 / 4 / 7 workers. Not for game code.
void SetWorkers(int workers);

// Stop and join the workers. A later call rebuilds the pool from the roster.
void Shutdown();

}  // namespace Parallel

void ParallelForRaw(int begin, int end, int grain, Parallel::RangeFn fn, void* ctx);

template <class F>
inline void ParallelFor(int begin, int end, int grain, F&& f) {
    using Fn = typename std::remove_reference<F>::type;   // a non-const callable
    Fn* callable = &f;
    ParallelForRaw(begin, end, grain,
                   [](void* ctx, int b, int e) { (*static_cast<Fn*>(ctx))(b, e); },  // CAST-OK: user_data
                   static_cast<void*>(callable));
}

}  // namespace System

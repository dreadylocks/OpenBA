// ============================================================================
// tests/unit/load_decode_batch_test.cpp -- Audio::LoadDecode (block SNDPAR)
// ============================================================================
// The batch that runs a load's sound-effect decodes in parallel
// (src/engine/audio/load_decode_batch.{h,cpp}). No game audio here (CLAUDE.md: no game
// content in the tree): the jobs decode a SYNTHETIC buffer -- a seeded oscillator bank in
// float, converted to S16 through Audio::FloatToS16, the decoder's own conversion -- whose
// result depends on every float operation, so a job run under a different environment or
// a job run twice would show. What is pinned:
//   * the bytes: every job's output through the batch, for 0 / 1 / 2 / 4 / 7 workers,
//     memcmp-equal to the same job run inline (the serial path);
//   * the order: the finishes run on the scope's thread in Defer order, each once;
//   * the boundary: nothing is published before the OUTERMOST Scope closes; without a Scope
//     Defer refuses (the caller decodes inline);
//   * the reader: an Await from another thread while the batch is still open completes
//     that job there, once, and the batch then skips it;
//   * the environment: a job decodes under the deferring thread's MXCSR / x87 control word,
//     on whichever thread runs it.
// tests/unit/xwma_decode_test.cpp runs the real WMA v2 decode through the batch when a
// game install is present.
// ============================================================================
#include "unit.h"

#include "audio/load_decode_batch.h"
#include "audio/xwma_decode.h"
#include "platform_thread.h"
#include "system/parallel_for.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#define LDB_TEST_MXCSR 1
#endif

namespace {

// ---- the synthetic "sound" -------------------------------------------------------------------
struct Item {
    uint32_t seed = 0;
    uint32_t frames = 0;
    std::vector<int16_t> published;   // what the finish stores (the buffer's `_data`)
    std::atomic<int> finishes{0};
    int finishOrder = -1;
    uint32_t sawCsr = 0;              // the MXCSR the decode ran under
    uint16_t sawFcw = 0;              // the x87 control word the decode ran under
};

struct Result {
    std::vector<int16_t> pcm;
    uint32_t csr = 0;
    uint16_t fcw = 0;
};

std::atomic<int> g_finishCounter{0};

void Synthesize(uint32_t seed, uint32_t frames, std::vector<int16_t>& out) {
    out.resize(frames);
    float phase[3] = {0.0f, 0.0f, 0.0f};
    const float step[3] = {0.0137f * float(seed % 7 + 1), 0.0291f + float(seed % 13) * 1e-3f,
                           0.00071f * float(seed % 5 + 3)};
    float env = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        env = env * 0.9993f + 0.0007f;
        float x = 0.0f;
        for (int k = 0; k < 3; ++k) {
            phase[k] += step[k];
            if (phase[k] > 6.2831853f) phase[k] -= 6.2831853f;
            x += std::sin(phase[k]) * (0.31f / float(k + 1));
        }
        x = x * env / 0.57f + float((seed * 2654435761u + i * 40503u) & 0xff) * (1.0f / 65536.0f);
        out[i] = Audio::FloatToS16(x);
    }
}

void ReadEnv(uint32_t& csr, uint16_t& fcw) {
    csr = 0;
    fcw = 0;
#ifdef LDB_TEST_MXCSR
    csr = _mm_getcsr();
#endif
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("fnstcw %0" : "=m"(fcw));
#endif
}

void DecodeFn(void* item, void* result) {
    const Item* it = static_cast<const Item*>(item);
    Result* r = static_cast<Result*>(result);
    ReadEnv(r->csr, r->fcw);
    Synthesize(it->seed, it->frames, r->pcm);
}

void FinishFn(void* item, void* result) {
    Item* it = static_cast<Item*>(item);
    Result* r = static_cast<Result*>(result);
    it->published.swap(r->pcm);
    it->sawCsr = r->csr;
    it->sawFcw = r->fcw;
    it->finishOrder = g_finishCounter.fetch_add(1);
    it->finishes.fetch_add(1);
    delete r;
}

std::vector<Item> MakeItems(int n) {
    std::vector<Item> items(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        items[static_cast<size_t>(i)].seed = uint32_t(i) * 2246822519u + 7u;
        // A spread of lengths, largest-first scheduling has something to do.
        items[static_cast<size_t>(i)].frames = 2000u + uint32_t((i * 7919) % 23) * 1500u;
    }
    return items;
}

bool DeferAll(std::vector<Item>& items) {
    bool all = true;
    for (Item& it : items)
        all = Audio::LoadDecode::Defer(&it, new Result(), it.frames, &DecodeFn, &FinishFn) && all;
    return all;
}

// ---- the Await from another thread -------------------------------------------------------
struct Awaiter {
    Item* item = nullptr;
    std::atomic<int> done{0};
};

unsigned int AwaitOne(void* arg) {
    Awaiter* a = static_cast<Awaiter*>(arg);
    Audio::LoadDecode::Await(a->item);
    a->done.store(a->item->finishes.load() == 1 ? 1 : 2);
    return 0;
}

}  // namespace

UNIT_TEST(load_decode_batch_bytes_and_order_match_the_serial_path_for_every_pool_size) {
    const int n = 37;
    std::vector<Item> serial = MakeItems(n);
    for (Item& it : serial) {   // the reference: decode + finish inline, as without a Scope
        CHECK(!Audio::LoadDecode::BatchOpen());
        CHECK(!Audio::LoadDecode::Defer(&it, nullptr, 0, &DecodeFn, &FinishFn));   // refused
        Result* r = new Result();
        DecodeFn(&it, r);
        FinishFn(&it, r);
    }

    for (int workers : {0, 1, 2, 4, 7}) {
        System::Parallel::SetWorkers(workers);
        std::vector<Item> items = MakeItems(n);
        g_finishCounter.store(0);
        {
            Audio::LoadDecode::Scope outer;
            CHECK(Audio::LoadDecode::BatchOpen());
            {
                Audio::LoadDecode::Scope nested;   // a dependency Load: not the batch
                CHECK(DeferAll(items));
            }
            // The nested Scope closed: nothing may be published yet.
            int early = 0;
            for (const Item& it : items) early += it.finishes.load();
            CHECK_EQ(early, 0);
            CHECK(Audio::LoadDecode::detail::g_queued.load() == n);
        }
        CHECK(!Audio::LoadDecode::BatchOpen());
        CHECK_EQ(Audio::LoadDecode::detail::g_queued.load(), 0);
        int sameBytes = 0, once = 0, inOrder = 0;
        for (int i = 0; i < n; ++i) {
            const Item& a = items[static_cast<size_t>(i)];
            const Item& b = serial[static_cast<size_t>(i)];
            if (a.published.size() == b.published.size() &&
                std::memcmp(a.published.data(), b.published.data(), a.published.size() * 2) == 0)
                ++sameBytes;
            if (a.finishes.load() == 1) ++once;
            if (a.finishOrder == i) ++inOrder;
        }
        std::printf("  workers=%d: %d/%d byte-identical, %d finished once, %d in Defer order\n",
                    workers, sameBytes, n, once, inOrder);
        CHECK_EQ(sameBytes, n);
        CHECK_EQ(once, n);
        CHECK_EQ(inOrder, n);
    }
    System::Parallel::Shutdown();   // the next user rebuilds the pool from the roster
}

UNIT_TEST(load_decode_batch_an_await_from_another_thread_finishes_its_job_once) {
    System::Parallel::SetWorkers(2);
    std::vector<Item> items = MakeItems(9);
    std::vector<Item> serial = MakeItems(9);
    for (Item& it : serial) {
        Result* r = new Result();
        DecodeFn(&it, r);
        FinishFn(&it, r);
    }
    g_finishCounter.store(0);
    Awaiter a;
    a.item = &items[4];
    {
        Audio::LoadDecode::Scope batch;
        CHECK(DeferAll(items));
        Platform::ThreadHandle t = Platform::ThreadCreateSuspended(&AwaitOne, &a, 0x40000);
        CHECK(t != nullptr);
        if (t) {
            Platform::ThreadResume(t);
            Platform::ThreadJoin(t);
            Platform::ThreadClose(t);
        }
        // The awaited job is published before the batch closes; the others are not.
        CHECK_EQ(a.done.load(), 1);
        CHECK_EQ(items[4].finishes.load(), 1);
        CHECK_EQ(items[3].finishes.load(), 0);
        CHECK_EQ(Audio::LoadDecode::detail::g_queued.load(), 8);
    }
    int sameBytes = 0, once = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].published == serial[i].published) ++sameBytes;
        if (items[i].finishes.load() == 1) ++once;
    }
    CHECK_EQ(sameBytes, 9);
    CHECK_EQ(once, 9);
    CHECK_EQ(items[4].finishOrder, 0);   // first, from the other thread
    CHECK_EQ(Audio::LoadDecode::detail::g_queued.load(), 0);
    Audio::LoadDecode::Await(&items[4]);   // gone from the registry: returns at once
    System::Parallel::Shutdown();
}

UNIT_TEST(load_decode_batch_jobs_run_under_the_deferring_threads_fp_environment) {
#if defined(LDB_TEST_MXCSR) && (defined(__x86_64__) || defined(__i386__))
    System::Parallel::SetWorkers(4);
    uint32_t csr0;
    uint16_t fcw0;
    ReadEnv(csr0, fcw0);
    // Round toward zero in both units, x87 at 24-bit precision: not what a fresh pool
    // thread starts with.
    const uint32_t csrWant = (csr0 & ~0x6000u) | 0x6000u;
    const uint16_t fcwWant = static_cast<uint16_t>((fcw0 & ~0x0f00u) | 0x0c00u);
    std::vector<Item> items = MakeItems(16);
    {
        Audio::LoadDecode::Scope batch;
        _mm_setcsr(csrWant);
        __asm__ volatile("fldcw %0" : : "m"(fcwWant));
        CHECK(DeferAll(items));
        _mm_setcsr(csr0);
        __asm__ volatile("fldcw %0" : : "m"(fcw0));
    }
    int matched = 0;
    for (const Item& it : items)
        if ((it.sawCsr & 0xffc0u) == (csrWant & 0xffc0u) && it.sawFcw == fcwWant) ++matched;
    CHECK_EQ(matched, 16);
    // And this thread's own environment is back.
    uint32_t csr1;
    uint16_t fcw1;
    ReadEnv(csr1, fcw1);
    CHECK_EQ(csr1 & 0xffc0u, csr0 & 0xffc0u);
    CHECK_EQ(fcw1, fcw0);
    System::Parallel::Shutdown();
#else
    UNIT_SKIP("no x86 FP environment on this target");
#endif
}

# SNDPAR -- the sound-effect decodes of a load, in parallel (hand-in)

Block SNDPAR of the loading-speed session, the open item AUDIODEC.md §6 left ("Parallel decode not done").
Branch `worktree-agent-a4c5a0810b052ddec`, base main `01fbb55ca`. Static gate only: the game was NOT run
(the coordinator runs every game test, §5).

Credit: the idea came from the unbanished project's `docs/PERFORMANCE.md` (its `EffectCache::prepare`
decodes all effect buffers in parallel). Nothing was taken from its code; the batch here is built on this
tree's own `System::ParallelFor` (WGPAR).

## 1. What

Before: every non-streamed xWMA `SoundBuffer` was decoded to S16 PCM inside its own `Serialize`
(`DecodeAtLoad`, Phase D.4 SDL-4), one after another on the loading thread. After AUDIODEC that is
~80-90 ms of WMA v2 decoding per route (new game, save load), all serial.

After: the decode is QUEUED during `Serialize` and the whole load's queue runs as one fork-join batch.

* `Object::External::Load` opens an `Audio::LoadDecode::Scope` for its whole body. Loads nest (a
  `SoundEffect`'s `ExternalPtr<SoundBuffer>` serialize calls `FindOrLoad`, which calls `Load`), and the
  OUTERMOST Scope on a thread is the batch: for the loader route that is the one `Load` of the loop's
  `Resources.rsc` list, which pulls in every sound of the route.
* `SoundBuffer::DecodeAtLoad` is split into a pure half (`DecodeXwmaJob`: open a decoder over the
  buffer's format / packets / seek table, allocate, decode -- writes only the job) and the publishing half
  (`FinishXwmaJob`: the counters, the ext loop seam, freeing the packets, the stores into `_data` /
  `_length` / `_decodedXwma`, `RewriteFormatAsPcm`) -- the one-piece body's statements in their order.
  With no Scope open (any `Serialize` not under `External::Load`) the two run back to back: the old path.
* When the outermost `Load` returns, the batch runs every queued decode through `System::ParallelFor`
  (grain 1, largest packet payload first, so the long ambient loops start at once), then runs every
  FINISH on the loading thread in queue order. Then `Load` returns its object and `FindOrLoad` `Add`s it.
* Every reader of the decoded fields waits for the decode first (`LoadDecode::Await`): `PlayPlatform`
  (the only reader of the PCM / its length / its format, `audio_play_platform.cpp`), `SoundBuffer::
  Serialize` (a re-serialize frees `_data`) and `~SoundBuffer` (frees `_data`). With nothing queued in the
  process this is ONE acquire load of an atomic counter. A reader that meets a queued buffer finishes that
  one decode itself, now (or waits for the thread already running it), so no reader ever sees the packets
  the decode replaces. See §3.1 for why this is needed.
* No flag, no toggle, no instrument. On a one-core roster (`Parallel::Workers() == 0`) `ParallelFor` runs
  the batch inline on the loading thread: the serial path.

## 2. Exactness

The observables, and why each is unchanged:

1. **PCM bytes per buffer.** A job's decode reads only its own buffer (format window, packets, seek table)
   and writes only its own job; the decoder object is per call; the FFmpeg tables it shares are built
   under a real once-init (§3.2). The float environment is pinned: `Defer` records the deferring thread's
   MXCSR and x87 control word, and every decode and finish runs under exactly that environment
   (`FpEnvScope`, `load_decode_batch.cpp`), whichever thread runs it -- the pool copies only MXCSR, and
   pool threads start with the OS default x87 word, which can differ from the main thread's after the CRT's
   `_fpreset`; libm (`cos`, `exp2`, used by the decoder's init and `ff_exp10`) may run x87 code. So each
   buffer decodes in the environment it decoded in before, on the same thread kind as before. The proof
   in the game: the `[SoundDecode] decoded= pcmBytes= pcmHash=` exit line (AUDIODEC: a SUM of per-buffer
   FNV-1a hashes, so the order buffers finish in does not enter it).
2. **Counters and lines.** `[SoundDecode]` is printed once at exit; its counters are atomics summed in
   any order. `[SoundFormat]` (the census) still counts at `Serialize`, before the decode, unchanged. The
   ext loop seam (`ext.seamless_loops`, off unless its row is on) runs in the FINISH, i.e. on the loading
   thread in queue order -- the `[SeamlessLoops]` stderr lines keep their order. (A buffer an `Await` had
   to finish early prints its line then; §4.)
3. **Resource order.** Nothing in `External::Load`, `FindOrLoad` or `Add` moved; `[ExtLoad] calls=` and
   the loader list are untouched. The SoundBuffer object, its `_loopForever` / `_streamed` /
   `_durationSeconds` / strings are written by `Serialize` as before (`SoundEffect::Compile` reads only
   those). Only the five decoded fields are published later -- and §3.1 is why no reader can tell.
4. **Heap.** Same allocation sizes (PCM sized from the seek table's total); the PCM blocks are allocated
   later and in a different order (the allocator is locked, §3.3), and the packets of a batch are all
   held until its finishes run: a few MB more at the peak of a load, freed at its end.

### 2.1 The unit tests (synthetic; no game audio in the tree)

`tests/unit/load_decode_batch_test.cpp` (3 tests) drives the batch with a synthetic job: a seeded
oscillator bank in float (`std::sin`, accumulations), through `Audio::FloatToS16` (the decoder's own
conversion), so a job run under a different FP environment or twice would show.

* `load_decode_batch_bytes_and_order_match_the_serial_path_for_every_pool_size`: 37 jobs of
  2000-35000 frames through a nested pair of Scopes, for 0 / 1 / 2 / 4 / 7 workers: nothing published
  when the NESTED Scope closes, everything when the outer one does; every job's bytes memcmp-equal to the
  inline decode + finish; each finish once, in Defer order; `Defer` refused with no Scope open.
* `load_decode_batch_an_await_from_another_thread_finishes_its_job_once`: an `Await` from a second
  thread while the batch is open completes that job there (finish order 0), once; the batch skips it.
* `load_decode_batch_jobs_run_under_the_deferring_threads_fp_environment`: defer under round-toward-zero
  MXCSR + x87 24-bit precision: every job saw exactly that environment on the pool; the thread's own
  environment is back afterwards.

`tests/unit/xwma_decode_test.cpp` gains `xwma_decode_through_the_load_batch_is_byte_identical`: the
real WMA v2 decode of the Sheep1 fixture (read from the user's install at test time, SKIPPED without one)
24 times through the batch on 4 workers, each byte-identical to the inline decode, finishes in order.

## 3. Thread safety

### 3.1 Where the batch boundary is, and the reader that can cross it

Option (b) of the brief, splitting one buffer into bands, is out: WMA v2 carries state frame to frame (the
overlap-add and the bit reservoir; the decoder's chunk API exists only to continue one pass). So (a): defer
whole buffers and join at a boundary.

The boundary is the end of the outermost `External::Load` on the loading thread. It is NOT a point no
reader can reach before: `FindOrLoad` `Add`s each SoundBuffer to the factory as soon as ITS nested `Load`
returns (`external_compiled.cpp`, the miss path), and the SoundEffect after its own `Load`, both long before
the outer `Resources.rsc` load ends; `FindOrLoad` is not locked across threads (`res_load_lock.h`: only
`ExpandRelativePath` takes the resource lock), and the main thread loads concurrently during a loader route
(two tids measured during the menu load). A main-thread `FindOrLoad` of an effect the loader already added
would get a buffer whose decode is still queued. Hence the reader side: the only readers of the decoded
fields are `PlayPlatform` (main thread; `audio_stream_run.cpp` reads only STREAMED buffers, never
deferred), `Serialize` and the destructor, and each calls `Await` first. Publication ordering: the job is
registered (counter incremented, release) inside `Serialize`, before the buffer is `Add`ed; a reader holds
the pointer only after the `Add`. (The factory hand-off itself is the pre-existing unlocked one; on x86-64
store order makes the counter visible to any thread that sees the pointer.)

### 3.2 The decoder

* `Audio::XwmaDecoder` (`src/engine/audio/xwma_decode.{h,cpp}`): no static or global mutable state; its
  `_pending` is a member `std::vector`; "not thread-safe per object; separate decoders may run on separate
  threads" (its header), and one is built per call.
* `third_party/wmadec`: every file-scope table is `const`, or built under `ff_thread_once`, which the shim
  implements as a real once-init (`InitOnceExecuteOnce` / `pthread_once`, `shim/wmadec_shim.c`): the sine
  windows (`sinewin_tablegen.h`) and the transform tables (`tx_template.c`). `av_log` is silent. The
  VLC tables, the LSP / pow tables and the MDCT contexts are per decoder context. Already exercised
  concurrently today: the Stream worker decodes the music while the loader decodes effects.

### 3.3 The allocator

`rtcoll::MemAlloc` / `MemFree` -> `System::MemoryAllocator::MemAlloc` / `MemFree`
(`src/engine/system/allocator.cpp` ~611 / ~621): every call takes the allocator's lock
(`g_plat.lockEnter(&self->lock)`). The loader and main threads already allocate concurrently. The batch's
own bookkeeping (Job, registry) uses the std heap. The `XwmaJob` result is `new`ed and deleted by its
finish.

### 3.4 The pool

`System::ParallelFor` (`src/engine/system/parallel_for.cpp`) is usable from any thread: the pool is
built lazily under its call lock by whichever thread calls first, calls are serialised by that lock (a
second thread waits), a call from inside a job runs inline, workers copy the caller's MXCSR. The batch
runs at the end of a `Load`, holding no lock (`ResLoadGuard` is scoped inside `ExpandRelativePath`); the
render thread neither loads nor uses the pool; the loader's other pool user (worldgen) runs in the loop's
`Load()`, after `LoadResources` on the same thread, so the two never contend there. No path waits on a
thread that waits on it: the registry's condition variable is waited only for `decoding` / `finishing`
jobs, whose owner holds nothing and runs to the end.

## 4. Where (file:line)

| file | line | what |
|---|---|---|
| `src/engine/audio/load_decode_batch.h` | whole | the API: `Scope`, `BatchOpen`, `Defer`, `Await` (inline fast path) |
| `src/engine/audio/load_decode_batch.cpp` | 73 / 129 / 177 / 195 / 208 / 227 | `FpEnvScope`, `Complete`, `RunBatch`, `~Scope`, `Defer`, `AwaitSlow` |
| `src/engine/object/external_load.cpp` | 218-222 | the Scope over `External::Load` |
| `src/engine/audio/vtbatch7_soundbuffer_soundeffect.cpp` | 304-418 | `DecodeXwmaJob` / `FinishXwmaJob` / `DecodeAtLoad` (queue or inline) / `EnsureDecodedAtLoad` |
| same | 447 | `Serialize` awaits its own buffer first |
| `src/engine/audio/audio_play_platform.cpp` | 340-343 | `PlayPlatform` awaits the buffer |
| `src/engine/audio/batchc_audio_leaves.cpp` | 132-135 | `~SoundBuffer` awaits before the frees |
| `src/engine/audio/SoundBuffer.h` | 137-144 | `EnsureDecodedAtLoad` declared |
| `tests/unit/load_decode_batch_test.cpp`, `tests/unit/xwma_decode_test.cpp`, `tests/unit/sources.cmake` | | the tests (§2.1) |

## 5. What the coordinator checks

1. **Baseline hashes from main first** (debug exe; `[SoundDecode]` is stripped from perf / release):
   on main `01fbb55ca`, `tools/build.sh run` (new game), quit cleanly, then
   `grep '\[SoundDecode\]' "C:/GOG Games/Banished/standalone_debug.log"` (written at exit). Then the same for
   a save-load route (the usual resave / load-save route), and keep both lines. Then this branch's tip,
   the same two routes: **decoded / refused / short / xwmaBytes / pcmBytes / pcmHash identical**. Both runs
   must reach the same loads (the hash sums every buffer the process decoded, boot + game), so use the same
   route and quit point; the mods parked as for any gate.
2. **The census**: `[LoadTime] loader.resources` -> `loader.load` (stderr, `standalone_error.log`) on the
   perf exe, both routes, before / after. Expected: the ~80-90 ms of WMA decoding in that stage drops to
   roughly the longest single buffer plus the rest / threads (on 8 threads: ~15-25 ms), i.e. ~165 -> ~95 ms
   per new game, ~175 -> ~105 per save load (estimate, not measured). Boot's global-resource stage gains
   the same way where it loads sounds.
3. The usual gate: `[ExtLoad] calls=5694 openFileFail=0` unchanged (no load moved), resave 5/5 == golden,
   acceptance 0 off target, unit 0 failures.
4. By ear: menu click sounds, ambient loops, a new game's first minute (nothing silent, nothing garbled).
   With `ext.seamless_loops` on, the `[SeamlessLoops]` lines in the same order as on main.

## 6. Risks / unsettled

* **Not run.** Nothing here was run in the game; the batch is unit-tested with synthetic jobs and, given an
  install, with the real decoder.
* A main-thread `Load` whose batch is non-empty waits for the pool's call lock if another thread is in a
  `ParallelFor` (e.g. a worldgen pass, tens of ms). Today the main thread's own sound loads happen at boot
  and between loops, not during worldgen; if a census shows a main-thread stall, that is where.
* The early-`Await` path (a reader reaching a queued buffer) is correct but untested in the game: it would
  show as a `[SeamlessLoops]` line out of order (ext on) and nothing else.
* A `longjmp` out of a `Load` (fatal error path) would skip `~Scope`; the process is dying then. A later
  reader would still `Await` and finish the decode itself.
* Peak memory of a load rises by its packet bytes held until the finishes (a few MB).

## 7. Static gate (worktree)

`tools/build.sh build --jobs 2` (debug) OK, no warning in the changed files (the tree's existing
`-Wmisleading-indentation` / `extern`-initialised warnings are in untouched game files).
`tools/build.sh unit --jobs 2`: **1178 tests, 0 failures, 1 skipped** (the skip is
`ext_sheet_kit_crosscheck`, no kit output; pre-existing). The install-gated batch test ran (install
present): `load batch: 24/24 byte-identical to the inline decode, 24 in Defer order`; the synthetic one
37/37 byte-identical, once, in order for 0 / 1 / 2 / 4 / 7 workers. Perf / release flavours not built.

## Measured (coordinator, 2026-10-19 night, perf exe, machine CLEAN, mods parked)

New game, `[LoadTime] loader.resources -> loader.load` of the gameplay load, two runs each:
main `01fbb55ca` 170.3 / 163.9 ms, branch 100.8 / 101.3 ms (-65 ms, -39%). `[SoundDecode]` identical
to main (decoded=56 refused=0 short=0 xwmaBytes=2000696 pcmBytes=45594624 pcmHash=02c8071bd4ca4cea,
debug exe, seed 12345 small, faithful). Resave gate PASS (a save load route); unit 1182 / 0 / 0 on the
merged branch `perf-unbanished`; no crash log on any run.

# CPULIMIT -- `sys.cpu_limit`, the CPU topology and the thread-count roster (hand-in)

(moved to src/platform/common/cpu/ 2026-10-20: a platform helper, not an extension)

Block 1 of `THREADPLAN.md` §2. Branch `worktree-agent-a71a201c2c74c81af`, base main `eff2464a1`.
Static gate only (the game was not run): the coordinator's first masked run is the live proof.

## 1. The key

`sys.cpu_limit` in `settings.cfg` (`<Documents>\Banished\settings.cfg`), read ONCE at boot. Never
`getenv`, not a `BANISHED_*` flag, not a registry row (it is a measurement aid, THREADPLAN §1 row 4).

| value | meaning |
|---|---|
| `0x55` / `0X55` | hexadecimal, 1-16 digits after the prefix |
| `85` | decimal, a uint64 |
| `0`, or the key absent | unrestricted |
| anything else (empty, junk, a sign, trailing text, overflow) | ignored, unrestricted, one `[Cpu] ... is not a mask` line |

Spaces around the value are allowed. Bit i = logical CPU i (Windows: processor group 0; Linux: CPU i,
first 64). The mask is ANDed with the system mask: bits outside it are dropped and the boot line says
`limit=clamped requested=0x..`; a mask with no bit left inside is ignored (`has no CPU inside the
system mask` line, `limit=none`). A failed set is printed and reads `limit=none`. The grammar and the
clamp are pure functions (`Cpu::ParseMask`, `Cpu::DecideLimit`, `src/ext/cpu/roster.h`).

THREADPLAN §4's masks on this box: P4 = one thread of each of four physical cores (`0x55` if the SMT
siblings are adjacent; read the topology print first), S4 = `0xf`.

## 2. Where it is applied, and the proof no thread of ours exists yet

`Cpu::Boot()` is the first statement after the `freopen` / `setvbuf` pair in both `WinMain` and the
POSIX `main` (`src/app/main.cpp`), before `CrashHandler_OpenLog`. `IO::Persistence::ReadSettings`
runs inside `Engine::Startup`, long after threads exist, so Boot does **a pre-read of settings.cfg for
this one key**: `IO::SettingsStore::DefaultPath` + `Load(path, entries, nullptr)` (file only, no legacy
registry import; the store is allocator-free and already used before Startup by
`HarnessGuardUnattendedBoot`). The engine's later full read sees the key as an ordinary unknown entry
and `WriteSettings`' merge keeps it.

Thread creation sites checked (grep of `CreateThread`, `_beginthreadex`, `pthread_create`,
`std::thread`, `ThreadCreateSuspended`, `SDL_CreateThread` over `src/`), all reached after Boot:

| site | reached from |
|---|---|
| `src/diag/win/hw_watch.cpp:75` | `Init()` -> `HwWatch_Init` |
| `src/diag/win/sampler.cpp:204` | `Init()` -> `Sampler_Init` |
| `src/diag/win/redzone_alloc.cpp:289/296` | allocator cut-over, inside `Init()` / later |
| `src/diag/win/boot_state_watchdog.cpp:407`, posix `:302` | `Init()` -> `BootState::Start` |
| `src/engine/system/thread.cpp:145` (`System::Thread`: loader, persistence worker, audio streams, pathfinder) | `Engine::Startup` and later |
| `src/ext/path_workers/path_workers.cpp` (`PoolThread`) | in a game |
| SDL's own threads | `SDL_Init` after Boot |
| `src/platform/posix/thread_posix.cpp:89` | the POSIX `System::Thread` primitive (as above) |

No static constructor in the tree creates a thread. On Windows the OS loader's worker threads may
exist at entry; `SetProcessAffinityMask` constrains every existing thread of the process, so they
are covered. On Linux `sched_setaffinity(0)` binds only the calling thread and the threads created
after it: Boot runs on the only thread, so every later thread inherits the mask.

## 3. The topology query

`Cpu::QueryTopology()` (`src/ext/cpu/topology.cpp`) over a new platform seam
`src/platform/common/platform_cpu.h` (deviation from the brief, which put the OS calls in
`topology.cpp`: D4_RULES C1 keeps OS headers in `src/platform`, so `src/ext` stays portable):

| | process / system mask | physical cores inside the mask |
|---|---|---|
| Windows (`src/platform/win/cpu_win.cpp`) | `GetProcessAffinityMask` | `GetLogicalProcessorInformationEx(RelationProcessorCore)`: cores with a group-0 bit in the mask |
| Linux (`src/platform/posix/cpu_posix.cpp`) | `sched_getaffinity(0)`; system = `/sys/devices/system/cpu/online` (else the process mask) | distinct (`physical_package_id`, `core_id`) over `/sys/devices/system/cpu/cpuN/topology/` |

`logical` = popcount of the mask. Fallback: when the core query fails (or returns 0 or more cores than
logical CPUs) `physical = logical` and `source=fallback:<reason>`; when the affinity query itself
fails, `mask=0`, `logical = physical = hardware_concurrency` (`fallback:affinity-query`). Limits: one
processor group / the first 64 CPUs. `cpu_posix.cpp` was not compiled here (no Linux bed this session;
the hypervisor is off): the first Linux build compiles it.

## 4. The roster interface (for CULLPAR and RENDERPIPE-1)

```cpp
#include "ext/cpu/roster.h"     // pure, no OS call (the unit tests link it alone)
#include "ext/cpu/topology.h"   // Cpu::Get()

namespace Cpu {
constexpr int kMaxPathWorkers = 4;   // == PathWorkers::kMaxWorkers (static_assert)
constexpr int kMaxCullHelpers = 2;
bool RenderThread(int physicalCores);                  // n >= 3
int  PathWorkers(int physicalCores, bool renderPipe);  // clamp(n - 2 - pipe, 1, 4)
int  CullHelpers(int physicalCores, bool renderPipe);  // clamp(n - 5, 0, 2) pipe on, clamp(n - 4, 0, 2) off

struct Topology { uint64_t mask; int logical; int physical; bool topology;
                  const char* reason; Limit limit; uint64_t requested; };
const Topology& Get();                                  // cached by Boot, read-only afterwards
void Describe(const Topology&, char* buf, int cap);     // "mask=0x.. logical=.. physical=.. ..."
}
```

Call `Cpu::CullHelpers(Cpu::Get().physical, Ext::Ext().render_pipe)` and
`Cpu::RenderThread(Cpu::Get().physical)`. `ext.render_pipe` is NOT added here (RENDERPIPE-0 adds the
row); the path-worker call site passes `false` with the comment `// RENDERPIPE-1 passes
Ext::Ext().render_pipe`. Table at n = 4 / 6 / 8: render thread 1 / 1 / 1; path workers 1 (2) / 3 (4)
/ 4 (4); cull helpers (pipe on) 0 / 1 / 2 -- exactly THREADRESEARCH §7.4, asserted by
`ext_cpu_roster_threadresearch_table`.

## 5. Path workers

`PathWorkers::SelectForGame` takes `Cpu::PathWorkers(Cpu::Get().physical, false)`. `ChooseWorkers`
(clamp(hardware threads - 2, 1, 4)) and its unit test are deleted. On this box (8 cores / 16
threads, unrestricted) the count stays 4; under P4 it is 2 (render_pipe off), under S4 (2 physical)
it is 1, where `ChooseWorkers` would have said 2 from `hardware_concurrency` = 16 regardless of the mask.
Each pool thread is set to BELOW_NORMAL (`Platform::ThreadSetBelowNormalPriority`, new beside the
NORMAL seam: Win `THREAD_PRIORITY_BELOW_NORMAL`; POSIX a documented no-op -- SCHED_OTHER has no
pthread priority and a per-thread nice needs the kernel tid). The dispatcher is retail's
`PathfinderThread` and keeps NORMAL, as do all retail threads. Both changes move only WHEN a path
result lands, which the class B lock already covers; the registry text of `ext.path_workers` says
nothing about the count, so it is unchanged. The `[PathWorkers] selected` line now names the core
count and the priority.

## 6. The prints

- Boot, stderr: `[Cpu] mask=0xffff logical=16 physical=8 limit=none source=topology` (shape; the
  value on this box is the coordinator's first run). With a limit: `limit=applied` / `limit=clamped
  ... requested=0x..`.
- `[BootState] RUNNING (Engine::Startup returned at Ns) cpu mask=.. logical=.. physical=.. limit=..
  source=..` (Windows and POSIX watchdogs). `standalone_boot_state.txt` is unchanged (its `detail=`
  stays last).
- Stress: `standalone_stress.csv` gets a `# cpu mask=...` meta line; `tools/stress_report.py` prints
  every meta line in the report header (and the baseline's), and prints `cpu (not recorded ...)` for a
  CSV from an older exe. `tools/stress_run.sh` needed no change (its report is stress_report's).

## 7. Object proofs (perf, WALKGRID §3 method 1)

`tools/build.sh build perf --jobs 2` at `eff2464a1` before any edit, objects kept, rebuilt after;
per function `objdump -d -r --no-show-raw-insn -C`, then the differing ones again with rip
displacements / section addends stripped (scripts in `build/cpulimit/`, not committed).

| object | result |
|---|---|
| `engine/system/thread.cpp` | 10 / 10 functions **identical** (the faithful thread primitive) |
| `platform/win/thread_win.cpp` | 8 / 8 **identical**, + the new `ThreadSetBelowNormalPriority` |
| `ext/path_workers/pool.cpp` | 9 / 9 identical, `ChooseWorkers` removed |
| `ext/path_workers/path_workers.cpp` | 10 identical; `ReleaseFor` identical after the strip (a string moved); `Dispatch` (BuildState inlined) gains the priority call; `SelectForGame` changed on purpose |
| `app/main.cpp` | 12 identical; `WinMain` gains the `Cpu::Boot` call (and two alignment-padding nops change) |
| `diag/win/boot_state_watchdog.cpp` | 8 identical; `Start` / `Stop` identical after the strip; `WatchdogThread` changed on purpose |
| `diag/stress_harness.cpp` | 51 identical, 17 more identical after the strip; `EmitRow` changed on purpose (`OpenCsv`, inlined there, writes the meta line). The third commit's message words this badly ("unchanged in code"): it changed, by the one meta line |

`platform_thread.h` gained a declaration, so ~400 TUs recompiled; `thread.cpp` above is the sample
that shows such a TU unchanged. The faithful path's only new work is one call at boot.

## 8. Gate (static)

- `tools/build.sh build debug --jobs 2`, `build perf --jobs 2`: exit 0. **0 new warnings**: perf
  compared against this worktree's base perf build (`docs/runtime_port/phase_d/runs/warn_baseline_*.txt`
  named in the brief does not exist in the tree; gate.sh's `norm()` shape used); debug: no warning in
  any touched or new file.
- `tools/build.sh unit debug --jobs 2`: **1153 -> 1156 tests, 0 failures** (+4 `ext_cpu_*`, -1
  `ext_path_workers_choose_workers`).
- `python tools/castmetric.py src`: 2. `python tools/abi_audit.py`: OK (the first draft of
  `cpu_posix.cpp` used `long`; replaced by `int` / `uint64_t`). `tools/acceptance/acceptance.sh`: 0
  lines off target.
- Resave golden: the key writes no state and the faithful path is unchanged, so `971aaec2cf95` is
  expected to hold; the coordinator's gate proves it.

Worktree note: `third_party/SDL` is an uninitialised submodule in a fresh worktree; it was linked to
the main checkout's SDL (same commit `829a65d7`) with a directory junction, which makes plain
`git status` fail (`--ignore-submodules=all` works). Not committed; remove the junction with the
worktree.

## 9. Deviations from THREADRESEARCH §7 / the brief

- OS calls in a `Platform::` seam (`platform_cpu.h` + win / posix bodies), not in `topology.cpp`.
- The mask grammar and clamp live in `roster.h` (pure, unit-tested), not in `topology.h`.
- POSIX BELOW_NORMAL is a no-op.
- `n` counts physical cores (THREADPLAN §1 row 6), where §7 wrote the popcount: `logical` is printed
  beside it.

## 10. Open items (coordinator)

1. First run: read the `[Cpu]` line unrestricted, confirm `physical=8` and the SMT numbering, then set
   P4 / S4 from it and check `limit=applied`, the RUNNING line and a stress CSV's `# cpu` line.
2. The live check that `[PathWorkers] selected ... workers=` is 4 unrestricted, 2 under P4, 1 under S4.
3. The first Linux build compiles `cpu_posix.cpp` (never compiled here).
4. More than 64 logical CPUs / processor groups: read as group 0 only.

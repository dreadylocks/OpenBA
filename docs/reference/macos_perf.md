# macOS performance measurements

These are historical measurements from 2026-10-19 on an Apple M5 Pro arm64 Mac
with 24 GB memory running macOS 26. These numbers are historical observations,
not a reproducible machine baseline.

The memory comparison used perf builds at 640x360, normal new game and a
2,000-citizen stress town. Footprint and graphics memory were sampled at 60 s.
Frame throughput used seconds 30–55 after first presentation. The 1% low is
1000 divided by the mean of the slowest ceil(N/100) frame intervals in ms.
These measure CPU presentation throughput, not GPU execution or display refresh.

| Scenario | Footprint before / after | Graphics before / after | Average FPS before / after | 1% low before / after |
| --- | --- | --- | --- | --- |
| New game | 7285 / 668 MB | 6779 / 167 MB | 105.5 / 120.0 | 45.4 / 111.6 |
| Stress town | about 12.0 GiB / 595 MB | about 11.5 GiB / 190 MB | 333.2 / 361.3 | 74.9 / 75.0 |

Compilation and desktop activity overlapped some runs. This supports the memory
reduction, not a causal speedup claim. The earlier 25-minute runs had a large
constant graphics allocation; the post-fix validation comprised separate
60-second runs, not a repeated 25-minute soak.

The former macOS stress adapter and brief-run script depended on local helpers
and an installation path. They are not shipped as supported harnesses.
`tools/stress_run.sh` reports this on macOS instead of executing an absent script.
Build with `tools/build.sh build perf --jobs 4`. A future automated measurement
must isolate settings with a throwaway USERPROFILE, use background mode and dummy
audio, bound its duration and stop only its owned PID. No current smoke, soak or
performance pass is implied by these historical figures.

[Performance record](../history/macos/PERF.md),
[soak record](../history/macos/SOAK.md),
[smoke record](../history/macos/SMOKE.md), and
[resolved memory issue](../known_port_issues.md) retain the evidence limits.

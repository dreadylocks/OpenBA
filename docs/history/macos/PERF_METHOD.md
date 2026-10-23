# Historical macOS performance method — 2026-10-19

This adapter was local and is not a supported command in this checkout.

## Native macOS Metal measurement

`tools/build.sh stress --jobs 2` selects `build/mac-perf`, copies/signs only
`run/OpenBA`, and uses the macOS adapter in `local artifact “stress_run.py”` (local, not shipped).
`BANISHED_DIR` is the read-only asset install. Windows/Linux routes are unchanged.
The adapter defaults to 2000 requested citizens, 10x, 3000 world frames,
faithful profile and an unobtrusive 640x360 background window. It stops only its
owned process after at most 110 seconds (override `--timeout`, maximum 180).
For example, after building/deploying once:

```sh
tools/stress_run.sh --no-build --label metal --citizens 2000 --frames 3000 --timeout 95
# Large window ONLY for this short native Retina benchmark (3456x2234 display):
OPENBA_NATIVE_RETINA=1 OPENBA_WINDOW_WIDTH=1728 OPENBA_WINDOW_HEIGHT=1117 \
  tools/stress_run.sh --no-build --label native --frames 5000 --timeout 65
```

Results go to `perf/results/<stamp>_<label>/`: original stress CSV/component
profile, logs, executable hashes and `report.json`. Metal's optional
`OPENBA_METAL_STATS=1` records CPU present-to-present intervals in
`standalone_metal_frames.csv`. FPS aggregation uses seconds 30–55 after first
presentation, excluding boot and the self-quit tail. 1% low is 1000 divided by
the mean of the slowest ceil(N/100) frame intervals in milliseconds. These are
submission/throughput timings under a three-frame in-flight limit, not direct
GPU execution timestamps or a count of physical display refreshes. A 3-second
`sample` run is separate from FPS measurement. OS/desktop contention remains
uncontrolled; do not compare these numbers directly to the Windows baseline.
Screenshots and run metadata are also retained in `docs_metal/m3_*` (local, not shipped).

#pragma once
// ============================================================================
// src/diag/input_trace.h -- the hands-on test logger (BANISHED_INPUT_TRACE=1)
// ============================================================================
// Writes input_trace.log (one line per click / wheel / key / window event, mouse moves rate
// limited, plus a once-a-second S summary) in the process's working directory, the folder
// that also holds standalone_debug.log. Nothing in the game calls it: when the Opt() flag
// is on, main() calls Start() after SDL_Init and an SDL event watch is added; the once-a-second
// S line comes from a timer that hands the work to the main thread. With the flag off nothing
// is started. macOS only: the body is src/diag/darwin/input_trace_darwin.cpp, built on Apple alone, and
// the functions below are called only from src/platform/darwin/app_darwin.cpp and the
// hand-test harness. Format and use: docs/macos/HANDTEST.md, tools/macos/handtest/analyze.py.
// ============================================================================

namespace Diag {
namespace InputTrace {

// Called from main() after SDL_Init: Install("input_trace.log") when Opt().inputTrace is set.
void Start();

// Install the watch now and log to `path` (used by Start() and by
// tools/macos/handtest/trace_harness.cpp). Returns false when already installed or the
// file cannot be opened. SDL's video subsystem must be initialised.
bool Install(const char* path);

// Write the final line, remove the watch and close the file (also run at process exit).
void Shutdown();

}  // namespace InputTrace
}  // namespace Diag

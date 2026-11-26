#pragma once
// ============================================================================
// src/platform/posix/spawn_posix.h -- NOT RETAIL. Child processes for platform_update.h's twins
// ============================================================================
// Only src/platform/posix/update_posix.cpp and src/platform/darwin/update_darwin.cpp include this
// (the Darwin twin replaces the POSIX one whole-file; the process calls are the same on both, so
// they live here, in a file with no Darwin twin). posix_spawn, waitpid and kill.
// ============================================================================

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Platform {
namespace Spawn {

// Start argv[0] (a path) with `argv`. `quiet`: its stdout and stderr go to /dev/null. False with a
// reason.
bool Start(const std::vector<std::string>& argv, bool quiet, pid_t& pid, std::string& error);
// Wait up to `timeoutMs` for a child Start made. True when it has exited (`exitCode`: its status, or
// 128 + the signal that ended it).
bool Wait(pid_t pid, uint32_t timeoutMs, int& exitCode);
// Start, wait up to `timeoutMs` (then kill it), and its exit code.
bool Run(const std::vector<std::string>& argv, uint32_t timeoutMs, int& exitCode, std::string& error);
// Any process (not only a child): true once `pid` no longer exists, polling every 100 ms.
bool WaitGone(pid_t pid, uint32_t timeoutMs);
// execv(argv[0], argv): returns only on failure, with a reason.
bool Exec(const std::vector<std::string>& argv, std::string& error);

}  // namespace Spawn
}  // namespace Platform

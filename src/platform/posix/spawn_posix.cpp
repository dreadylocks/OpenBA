// src/platform/posix/spawn_posix.cpp -- NOT RETAIL. Child processes for the update twins
// (spawn_posix.h).
#include "spawn_posix.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace Platform {
namespace Spawn {

namespace {

std::vector<char*> Pointers(const std::vector<std::string>& argv) {
    std::vector<char*> p;
    p.reserve(argv.size() + 1);
    for (const std::string& a : argv) p.push_back(const_cast<char*>(a.c_str()));
    p.push_back(nullptr);
    return p;
}

int ExitCodeOf(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

}  // namespace

bool Start(const std::vector<std::string>& argv, bool quiet, pid_t& pid, std::string& error) {
    if (argv.empty()) {
        error = "nothing to start";
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (quiet) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }
    std::vector<char*> args = Pointers(argv);
    const int          rc = posix_spawn(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        error = "cannot start " + argv[0] + ": " + std::strerror(rc);
        return false;
    }
    return true;
}

bool Wait(pid_t pid, uint32_t timeoutMs, int& exitCode) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        int         status = 0;
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            exitCode = ExitCodeOf(status);
            return true;
        }
        if (r < 0 && errno != EINTR) {
            exitCode = 1;
            return true;   // not our child any more
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool Run(const std::vector<std::string>& argv, uint32_t timeoutMs, int& exitCode, std::string& error) {
    pid_t pid = 0;
    if (!Start(argv, true, pid, error)) return false;
    if (Wait(pid, timeoutMs, exitCode)) return true;
    kill(pid, SIGKILL);
    int ignored = 0;
    Wait(pid, 5000, ignored);
    error = argv[0] + " took too long";
    return false;
}

bool WaitGone(pid_t pid, uint32_t timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (kill(pid, 0) != 0 && errno == ESRCH) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool Exec(const std::vector<std::string>& argv, std::string& error) {
    if (argv.empty()) {
        error = "nothing to run";
        return false;
    }
    std::vector<char*> args = Pointers(argv);
    execv(argv[0].c_str(), args.data());
    error = "cannot run " + argv[0] + ": " + std::strerror(errno);
    return false;
}

}  // namespace Spawn
}  // namespace Platform

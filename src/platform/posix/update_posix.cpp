// ============================================================================
// src/platform/posix/update_posix.cpp -- NOT RETAIL. platform_update.h on Linux
// ============================================================================
// ext.auto_update (docs/fork/AUTO_UPDATE.md). InPlace, as Windows: the staging folder is
// OpenBA-update/ beside the program (/proc/self/exe), and a running program may be renamed. Packages
// are .tar.gz, unpacked by the system's tar. The child processes: spawn_posix.h. macOS replaces this
// file with src/platform/darwin/update_darwin.cpp.
// ============================================================================

#include "platform/common/platform_update.h"
#include "spawn_posix.h"

#include <climits>
#include <unistd.h>

#include <string>
#include <vector>

namespace Platform {
namespace Update {

struct Child {
    pid_t pid = 0;
};

namespace {

std::vector<std::string> s_args;

std::string Folder(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string Tar() {
    if (access("/usr/bin/tar", X_OK) == 0) return "/usr/bin/tar";
    return "/bin/tar";
}

}  // namespace

Model InstallModel() {
    return Model::InPlace;
}

const char* PackagePlatform() {
    return "linux-x64";
}

void SetArguments(int argc, char** argv) {
    s_args.clear();
    for (int i = 0; i < argc; ++i) s_args.emplace_back(argv[i]);
}

std::string SelfPath() {
    char          buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    return std::string(buf, static_cast<size_t>(n));
}

std::string StagingDir() {
    const std::string self = SelfPath();
    if (self.empty()) return std::string();
    return Folder(self) + "/OpenBA-update";
}

const char* ProgramName() {
    return "OpenBA";
}

std::string SelfBundle() {
    return std::string();
}

bool Unpack(const std::string& archive, const std::string& dest, std::string& error) {
    int code = 0;
    if (!Spawn::Run({Tar(), "-x", "-z", "-f", archive, "-C", dest}, 5 * 60 * 1000, code, error)) return false;
    if (code != 0) {
        error = "tar could not unpack the package (exit code " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool VerifyProgram(const std::string&, std::string& error) {
    error.clear();
    return true;
}

uint32_t ProcessId() {
    return static_cast<uint32_t>(getpid());
}

bool WaitForExit(uint32_t pid, uint32_t timeoutMs) {
    return Spawn::WaitGone(static_cast<pid_t>(pid), timeoutMs);
}

Child* StartAgain(const std::string& program, std::string& error) {
    std::vector<std::string> argv = s_args;
    if (argv.empty()) argv.push_back(program);
    argv[0] = program;
    Child* c = new Child();
    if (!Spawn::Start(argv, false, c->pid, error)) {
        delete c;
        return nullptr;
    }
    return c;
}

bool WaitChild(Child* child, uint32_t timeoutMs, int& exitCode) {
    if (child == nullptr) return true;
    return Spawn::Wait(child->pid, timeoutMs, exitCode);
}

void LetChildForeground(Child*) {}

void ReleaseChild(Child* child) {
    delete child;
}

bool HandOver(const std::string&, const std::string&, const std::string&, std::string& error) {
    error = "not on Linux (the InPlace model has no hand-over)";
    return false;
}

bool RestartSelf(std::string& error) {
    std::vector<std::string> argv = s_args;
    const std::string        self = SelfPath();
    if (argv.empty()) argv.push_back(self);
    argv[0] = self;
    return Spawn::Exec(argv, error);
}

}  // namespace Update
}  // namespace Platform

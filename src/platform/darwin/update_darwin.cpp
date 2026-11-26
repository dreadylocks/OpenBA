// ============================================================================
// src/platform/darwin/update_darwin.cpp -- NOT RETAIL. platform_update.h on macOS
// ============================================================================
// ext.auto_update (docs/fork/AUTO_UPDATE.md §6). Replaces src/platform/posix/update_posix.cpp.
// Versioned: the .app launcher (packaging/macos/launcher.mm) execs ~/Library/Application Support/
// OpenBA/Run/OpenBA, a link to the engine inside the bundle, with Run/ as the working folder. An
// installed build is Updates/<version>/OpenBA.app beside Run/; the hand-over points the Run/OpenBA
// link at that bundle's engine, sets BANISHED_AIRCONV to its converter (as the launcher does) and
// execs the link: the same process, the same Dock icon. Nothing is ever written into a bundle.
// Packages are .zip, unpacked by /usr/bin/ditto; an unpacked bundle must pass
// `codesign --verify --deep --strict` (an ad-hoc signature is enough). Neither the zip OpenBA writes
// nor what ditto unpacks from it gets com.apple.quarantine: OpenBA is not sandboxed and declares no
// LSFileQuarantineEnabled, so Gatekeeper never assesses an update.
// ============================================================================

#include "platform/common/platform_update.h"
#include "platform/posix/spawn_posix.h"

#include <mach-o/dyld.h>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
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

}  // namespace

Model InstallModel() {
    return Model::Versioned;
}

const char* PackagePlatform() {
    return "macos-arm64";
}

void SetArguments(int argc, char** argv) {
    s_args.clear();
    for (int i = 0; i < argc; ++i) s_args.emplace_back(argv[i]);
}

std::string SelfPath() {
    uint32_t          size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return std::string();
    return std::string(buf.data());
}

std::string StagingDir() {
    const std::string self = SelfPath();
    if (self.empty()) return std::string();
    return Folder(Folder(self)) + "/Updates";   // <support>/Run/OpenBA -> <support>/Updates
}

const char* ProgramName() {
    return "OpenBA.app";
}

std::string SelfBundle() {
    char real[PATH_MAX];
    if (realpath(SelfPath().c_str(), real) == nullptr) return std::string();
    std::string p = real;   // .../OpenBA.app/Contents/MacOS/OpenBA-engine
    for (int up = 0; up < 3; ++up) p = Folder(p);
    if (p.size() < 4 || p.compare(p.size() - 4, 4, ".app") != 0) return std::string();
    return p;
}

bool Unpack(const std::string& archive, const std::string& dest, std::string& error) {
    int code = 0;
    if (!Spawn::Run({"/usr/bin/ditto", "-x", "-k", archive, dest}, 5 * 60 * 1000, code, error)) return false;
    if (code != 0) {
        error = "ditto could not unpack the package (exit code " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool VerifyProgram(const std::string& path, std::string& error) {
    int code = 0;
    if (!Spawn::Run({"/usr/bin/codesign", "--verify", "--deep", "--strict", path}, 2 * 60 * 1000, code, error)) {
        return false;
    }
    if (code != 0) {
        error = "codesign --verify refused the bundle (exit code " + std::to_string(code) + ")";
        return false;
    }
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

bool HandOver(const std::string& link, const std::string& engine, const std::string& airconv, std::string& error) {
    if (access(engine.c_str(), X_OK) != 0) {
        error = engine + " is missing";
        return false;
    }
    // The new link beside the old one, then a rename over it: the link is never absent.
    const std::string fresh = link + ".update";
    unlink(fresh.c_str());
    if (symlink(engine.c_str(), fresh.c_str()) != 0 || rename(fresh.c_str(), link.c_str()) != 0) {
        error = "cannot point " + link + " at " + engine + ": " + std::strerror(errno);
        unlink(fresh.c_str());
        return false;
    }
    if (setenv("BANISHED_AIRCONV", airconv.c_str(), 1) != 0) {
        error = "cannot set BANISHED_AIRCONV";
        return false;
    }
    std::vector<std::string> argv = s_args;
    if (argv.empty()) argv.push_back(link);
    argv[0] = link;
    return Spawn::Exec(argv, error);
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

#pragma once
// ============================================================================
// src/platform/common/platform_update.h -- NOT RETAIL. What ext.auto_update needs from the OS
// ============================================================================
// D4_RULES C1: namespace `Platform::Update`, free functions, NO OS header. For src/ext/auto_update/
// (docs/fork/AUTO_UPDATE.md): where updates are kept, unpacking a package, and the processes
// around an install (the supervisor, the hand-over, a restart). The downloads themselves are
// Platform::HttpGet (platform_http.h).
//   Windows: src/platform/win/update_win.cpp      InPlace; tar.exe; CreateProcessW
//   macOS:   src/platform/darwin/update_darwin.cpp Versioned; ditto, codesign; execv
//   Linux:   src/platform/posix/update_posix.cpp   InPlace; tar; posix_spawn
// Every path is UTF-8.
// ============================================================================

#include <cstdint>
#include <string>

namespace Platform {
namespace Update {

// launch_plan.h has what each model means.
enum class Model {
    InPlace,     // the new program replaces this one in its folder (Windows, Linux)
    Versioned,   // installed builds sit side by side; the player's own bundle hands over (macOS)
};
Model InstallModel();

// The `platform` a manifest must name: "windows-x64", "macos-arm64" or "linux-x64".
const char* PackagePlatform();

// Before anything else in main / WinMain: keeps the arguments StartAgain, HandOver and RestartSelf
// pass on (Windows reads GetCommandLineW and ignores these).
void SetArguments(int argc, char** argv);

// This program as it was started. macOS: the Run/OpenBA link the launcher execs, not resolved.
std::string SelfPath();
// The folder updates are kept in (not created here). InPlace: <this program's folder>/OpenBA-update.
// Versioned: Updates/ beside the Run folder (~/Library/Application Support/OpenBA/Updates).
std::string StagingDir();
// The name of this program's file inside a package's root folder ("OpenBA.exe", "OpenBA").
// Versioned: the bundle ("OpenBA.app").
const char* ProgramName();
// Versioned: the bundle this program runs from (realpath of SelfPath, up to *.app). InPlace: "".
std::string SelfBundle();

// Unpack `archive` (this platform's package: a .zip on Windows and macOS, a .tar.gz on Linux) into
// the empty folder `dest` with the OS's own tool (tar.exe, ditto, tar). False with a reason.
bool Unpack(const std::string& archive, const std::string& dest, std::string& error);
// Versioned: `codesign --verify --deep --strict` on a bundle. InPlace: true.
bool VerifyProgram(const std::string& path, std::string& error);

uint32_t ProcessId();
// Block until process `pid` has exited or `timeoutMs` passed. True when it is gone (or never was).
bool WaitForExit(uint32_t pid, uint32_t timeoutMs);

// InPlace: start `program` with this process's own arguments, in its working folder.
struct Child;
Child* StartAgain(const std::string& program, std::string& error);
// Wait up to `timeoutMs` for `child`. True when it has exited (`exitCode` set).
bool WaitChild(Child* child, uint32_t timeoutMs, int& exitCode);
// Let the child's window come to the front (Windows: AllowSetForegroundWindow). Elsewhere nothing.
void LetChildForeground(Child* child);
void ReleaseChild(Child* child);

// Versioned: point the link `link` at `engine`, set BANISHED_AIRCONV to `airconv` (the hand-over
// target's converter; OptionsInit reads it after the exec) and replace this process with `link`,
// the same arguments. Returns only on failure, with a reason.
bool HandOver(const std::string& link, const std::string& engine, const std::string& airconv, std::string& error);

// After a clean shutdown (the end of main / WinMain), when Restart now asked for it: POSIX execv
// of SelfPath with the same arguments (it returns only on failure); Windows starts it again and
// returns true, and the caller's process then exits.
bool RestartSelf(std::string& error);

}  // namespace Update
}  // namespace Platform

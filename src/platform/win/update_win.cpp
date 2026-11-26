// ============================================================================
// src/platform/win/update_win.cpp -- NOT RETAIL. platform_update.h on Windows
// ============================================================================
// ext.auto_update (docs/fork/AUTO_UPDATE.md). InPlace: the staging folder is OpenBA-update\ beside
// OpenBA.exe (the same volume, so every move is a rename, and a running exe may be renamed but not
// overwritten). Packages are .zip, unpacked by the tar.exe Windows ships in System32 (bsdtar, since
// Windows 10 1803). The supervisor and the restart start this program again with CreateProcessW and
// this process's own command line (GetCommandLineW).
// ============================================================================

#include "platform/common/platform_update.h"

#include <windows.h>

#include <string>
#include <vector>

namespace Platform {
namespace Update {

struct Child {
    PROCESS_INFORMATION pi = {};
};

namespace {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(static_cast<size_t>(n - 1));
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    s.resize(static_cast<size_t>(n - 1));
    return s;
}

std::string LastError(const char* step) {
    return std::string(step) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

std::string Folder(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Run `commandLine` in `folder` with no window and wait up to `timeoutMs`; its exit code.
bool RunHidden(const std::wstring& program, std::wstring commandLine, const std::wstring& folder, uint32_t timeoutMs,
               DWORD& exitCode, std::string& error) {
    STARTUPINFOW        si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    if (!CreateProcessW(program.c_str(), &commandLine[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        folder.empty() ? nullptr : folder.c_str(), &si, &pi)) {
        error = LastError("starting tar.exe");
        return false;
    }
    CloseHandle(pi.hThread);
    if (WaitForSingleObject(pi.hProcess, timeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        error = "tar.exe took too long";
        return false;
    }
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace

Model InstallModel() {
    return Model::InPlace;
}

const char* PackagePlatform() {
    return "windows-x64";
}

void SetArguments(int, char**) {}   // GetCommandLineW has them

std::string SelfPath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return std::string();
        if (n < buf.size()) return Narrow(std::wstring(buf.data(), n));
        if (buf.size() > 32768) return std::string();
        buf.resize(buf.size() * 2);
    }
}

std::string StagingDir() {
    const std::string self = SelfPath();
    if (self.empty()) return std::string();
    return Folder(self) + "\\OpenBA-update";
}

const char* ProgramName() {
    return "OpenBA.exe";
}

std::string SelfBundle() {
    return std::string();
}

// tar.exe runs in the archive's folder with relative names: bsdtar's own command-line reading need
// not cope with a non-ASCII folder name.
bool Unpack(const std::string& archive, const std::string& dest, std::string& error) {
    wchar_t system[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(system, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        error = LastError("GetSystemDirectoryW");
        return false;
    }
    const std::wstring tar = std::wstring(system) + L"\\tar.exe";
    if (GetFileAttributesW(tar.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "this Windows has no tar.exe (Windows 10 1803 or later has it)";
        return false;
    }
    // ext.auto_update keeps both in the staging folder: <staging>\download\<package> into
    // <staging>\unpack, so from the archive's folder the destination is ..\unpack.
    const std::string  archiveFolder = Folder(archive);
    const std::string  destName = dest.substr(dest.find_last_of("\\/") + 1);
    const bool         siblings = Folder(archiveFolder) == Folder(dest);
    const std::wstring destArg = siblings ? L"..\\" + Widen(destName) : Widen(dest);
    const std::wstring folder = Widen(archiveFolder);
    const std::wstring name = Widen(archive.substr(archive.find_last_of("\\/") + 1));
    const std::wstring commandLine = L"\"" + tar + L"\" -x -f \"" + name + L"\" -C \"" + destArg + L"\"";
    DWORD code = 0;
    if (!RunHidden(tar, commandLine, folder, 5 * 60 * 1000, code, error)) return false;
    if (code != 0) {
        error = "tar.exe could not unpack the package (exit code " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

bool VerifyProgram(const std::string&, std::string& error) {
    error.clear();
    return true;
}

uint32_t ProcessId() {
    return GetCurrentProcessId();
}

bool WaitForExit(uint32_t pid, uint32_t timeoutMs) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h == nullptr) return true;   // gone already (or not ours to wait on)
    const DWORD r = WaitForSingleObject(h, timeoutMs);
    CloseHandle(h);
    return r == WAIT_OBJECT_0;
}

Child* StartAgain(const std::string& program, std::string& error) {
    const std::wstring path = Widen(program);
    std::wstring       commandLine = GetCommandLineW();
    STARTUPINFOW       si = {};
    si.cb = sizeof(si);
    Child* c = new Child();
    if (!CreateProcessW(path.c_str(), &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &c->pi)) {
        error = LastError("CreateProcessW");
        delete c;
        return nullptr;
    }
    CloseHandle(c->pi.hThread);
    c->pi.hThread = nullptr;
    return c;
}

bool WaitChild(Child* child, uint32_t timeoutMs, int& exitCode) {
    if (child == nullptr) return true;
    if (WaitForSingleObject(child->pi.hProcess, timeoutMs) != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(child->pi.hProcess, &code);
    exitCode = static_cast<int>(code);
    return true;
}

void LetChildForeground(Child* child) {
    if (child != nullptr) AllowSetForegroundWindow(child->pi.dwProcessId);
}

void ReleaseChild(Child* child) {
    if (child == nullptr) return;
    if (child->pi.hProcess != nullptr) CloseHandle(child->pi.hProcess);
    delete child;
}

bool HandOver(const std::string&, const std::string&, const std::string&, std::string& error) {
    error = "not on Windows (the InPlace model has no hand-over)";
    return false;
}

bool RestartSelf(std::string& error) {
    Child* c = StartAgain(SelfPath(), error);
    if (c == nullptr) return false;
    LetChildForeground(c);
    ReleaseChild(c);
    return true;
}

}  // namespace Update
}  // namespace Platform

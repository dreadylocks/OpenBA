#pragma once
// ============================================================================
// src/platform/common/platform_os.h -- process-level OS services (Phase D.4,
// THREADS seam, E.7)
// ============================================================================
// D4_RULES C1: namespace `Platform`, free functions, NO OS header. Windows bodies:
// src/platform/win/os_win.cpp, one call each.
// ============================================================================

#include <cstdint>

namespace Platform {

// COM: ComInit / ComUninit (CoInitializeEx(NULL, COINIT_MULTITHREADED) / CoUninitialize,
// Engine::Startup 0x1800dbc12 and Engine::Shutdown 0x1800db88b) were removed by Phase D.4
// SDL-5 D2: their three consumers (XAudio2, DirectInput 8, ShellExecuteW) are SDL's now.

// A diagnostic probe, not retail behaviour: true when the page holding `p` is
// committed and neither no-access nor a guard page (Windows: VirtualQuery succeeds,
// State == MEM_COMMIT, Protect has neither PAGE_NOACCESS nor PAGE_GUARD). Used by
// the serializer desync trace in game/Entity/componententity.cpp before it reads a
// dword through a candidate buffer pointer.
bool IsReadablePage(const void* p);

// Diagnostic probes, not retail behaviour (Phase D.4 LINUX-1: the VirtualQuery calls of
// game/Game/force_work.cpp, diag/entity_comp_validator.cpp and diag/scene_watch.cpp).
// QueryMemoryRegion describes the region holding `p` and is false when nothing is mapped
// there (Windows: VirtualQuery returned 0). The flags are the tests those callers made on
// MEMORY_BASIC_INFORMATION, so each caller's predicate is unchanged:
//   committed   State == MEM_COMMIT                       (POSIX: perms other than ---)
//   accessible  committed, Protect has neither PAGE_NOACCESS nor PAGE_GUARD
//   readable    accessible, Protect in READONLY | READWRITE | WRITECOPY | EXECUTE_READ |
//               EXECUTE_READWRITE | EXECUTE_WRITECOPY     (POSIX: r)
//   executable  accessible, Protect in EXECUTE | EXECUTE_READ | EXECUTE_READWRITE |
//               EXECUTE_WRITECOPY                         (POSIX: x)
//   image       Type == MEM_IMAGE: a mapped executable image (POSIX: a file-backed
//               mapping, inode != 0 in /proc/self/maps; vtables sit in the exe's or a
//               shared object's relro / rodata)
// base / size are the region's (BaseAddress / RegionSize; POSIX: one maps line).
struct MemoryRegion {
    uintptr_t base;
    uintptr_t size;
    bool committed;
    bool accessible;
    bool readable;
    bool executable;
    bool image;
};
bool QueryMemoryRegion(const void* p, MemoryRegion& out);

// The running executable's load address (Windows: GetModuleHandleW(NULL), the image
// base; POSIX: the main program's load bias, dl_iterate_phdr's first entry). A log
// line subtracts it from a return address to print an nm-able static address.
uintptr_t ExecutableImageBase();

// ExitProcess(exitCode): flush stdio, end the process at once, no destructors.
// POSIX body only (os_posix.cpp, `_exit`) for now: System::Error's fallback exit
// (src/engine/system/error.cpp) still declares kernel32!ExitProcess itself in its
// Windows arm, so the Windows objects are unchanged (Phase D.4 POSIX); moving that
// arm onto this function is a later `refactor:` with its own B11 verdict.
[[noreturn]] void ExitProcess(uint32_t exitCode);

// NOT RETAIL (ext.crash_reports): which OS this build is for, "windows", "macos" or "linux".
const char* OsName();

// Phase D.4 SDL step W7: the OS half of Application::LaunchProcess (retail 0x18009f5e0).
// Bodies: src/platform/sdl/process_sdl.cpp (SDL3).
namespace Process {
// Start `commandLine` (UTF-8; on Windows handed to CreateProcess verbatim, which splits
// it as retail's CreateProcessW(NULL, cmdLine, ...) did) and wait for it to exit
// (WaitForSingleObject(INFINITE)). False when it could not be started.
bool RunAndWait(const char* commandLine);
// Open `target` (UTF-8: a URL, or a file / folder path) with the user's default handler,
// as ShellExecuteW(NULL, L"open", target, ...). False when it could not be opened.
bool Open(const char* target);
}  // namespace Process

}  // namespace Platform

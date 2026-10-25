// ============================================================================
// src/platform/win/os_win.cpp -- Platform:: process-level services over Win32
// ============================================================================
// The Windows half of src/platform/common/platform_os.h. ComInit / ComUninit (THREADS
// E.7) were removed by Phase D.4 SDL-5 D2 with their callers.
// ============================================================================

#include "platform_os.h"

#include <windows.h>

namespace Platform {

bool IsReadablePage(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof(mbi)) &&
           mbi.State == MEM_COMMIT &&
           !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

bool QueryMemoryRegion(const void* p, MemoryRegion& out) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    const DWORD prot = mbi.Protect;
    const DWORD rd = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    const DWORD ex = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    out.base       = reinterpret_cast<uintptr_t>(mbi.BaseAddress);   // CAST-OK: ptr_int
    out.size       = mbi.RegionSize;
    out.committed  = mbi.State == MEM_COMMIT;
    out.accessible = out.committed && !(prot & (PAGE_NOACCESS | PAGE_GUARD));
    out.readable   = out.accessible && (prot & rd) != 0;
    out.executable = out.accessible && (prot & ex) != 0;
    out.image      = mbi.Type == MEM_IMAGE;
    return true;
}

uintptr_t ExecutableImageBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));   // CAST-OK: ptr_int
}

const char* OsName() {
    return "windows";
}

}  // namespace Platform

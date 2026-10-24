// ============================================================================
// src/platform/win/file_rw_win.cpp -- Platform::FileOpenExclusive / FileTruncate over Win32
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/COMPRESSAO.md). platform_file.h has the contract;
// the derived-data cache (src/ext/derived_cache/) is the caller. The POSIX twin is
// src/platform/posix/file_rw_posix.cpp.
// ============================================================================

#include "platform_file.h"

#include <windows.h>

namespace Platform {

FileHandle FileOpenExclusive(const PathChar* path) {
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path), GENERIC_READ | GENERIC_WRITE, 0, nullptr,  // CAST-OK: win32
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return reinterpret_cast<FileHandle>(h);  // CAST-OK: win32
}

int32_t FileTruncate(FileHandle h, int64_t size) {
    if (size < 0) return 0;
    LARGE_INTEGER d;
    d.QuadPart = size;
    if (!SetFilePointerEx(reinterpret_cast<HANDLE>(h), d, nullptr, FILE_BEGIN)) return 0;  // CAST-OK: win32
    return SetEndOfFile(reinterpret_cast<HANDLE>(h)) ? 1 : 0;  // CAST-OK: win32
}

}  // namespace Platform

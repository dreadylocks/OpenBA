// ============================================================================
// src/platform/win/file_map_win.cpp -- Platform::FileMap* over Win32 (NOT RETAIL)
// ============================================================================
// The Windows half of platform_file.h's read-only file mapping: CreateFileMappingW +
// MapViewOfFile over the WHOLE file, so a package's packed reads are a memcpy from the
// view instead of SetFilePointer + ReadFile on its one handle
// (src/engine/io/filesystem_openfile.cpp, FileSystem::ReadFile's packed arm).
// ============================================================================

#include "platform_file.h"

#include <windows.h>

namespace Platform {

bool FileMapOpen(FileHandle h, FileMapping* out) {
    *out = FileMapping{};
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0) return false;
    HANDLE section = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (section == nullptr) return false;
    const void* view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(section);
        return false;
    }
    out->data = static_cast<const uint8_t*>(view);
    out->size = static_cast<uint64_t>(size.QuadPart);
    out->os   = section;
    return true;
}

void FileMapClose(FileMapping* m) {
    if (m->data != nullptr) UnmapViewOfFile(m->data);
    if (m->os != nullptr) CloseHandle(m->os);
    *m = FileMapping{};
}

}  // namespace Platform

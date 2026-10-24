// ============================================================================
// src/platform/posix/file_map_posix.cpp -- Platform::FileMap* over mmap (NOT RETAIL)
// ============================================================================
// The POSIX half of platform_file.h's read-only file mapping, used on Linux and (no
// darwin twin: src/platform/darwin/README.md's rule adds nothing) on macOS. The handle is
// fd + 1 (file_posix.cpp / file_darwin.cpp). off_t is 64-bit on both (LP64), so a
// package past 4 GiB maps whole.
// ============================================================================

#include "platform_file.h"

#include <sys/mman.h>
#include <sys/stat.h>

namespace Platform {

namespace {
int Fd(FileHandle h) {
    return static_cast<int>(reinterpret_cast<uintptr_t>(h) - 1u);  // CAST-OK: ptr_int
}
}  // namespace

bool FileMapOpen(FileHandle h, FileMapping* out) {
    *out = FileMapping{};
    struct stat st;
    if (::fstat(Fd(h), &st) != 0 || st.st_size <= 0) return false;
    void* view = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, Fd(h), 0);
    if (view == MAP_FAILED) return false;
    out->data = static_cast<const uint8_t*>(view);
    out->size = static_cast<uint64_t>(st.st_size);
    return true;
}

void FileMapClose(FileMapping* m) {
    if (m->data != nullptr) ::munmap(const_cast<uint8_t*>(m->data), static_cast<size_t>(m->size));
    *m = FileMapping{};
}

}  // namespace Platform

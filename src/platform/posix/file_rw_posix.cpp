// ============================================================================
// src/platform/posix/file_rw_posix.cpp -- Platform::FileOpenExclusive / FileTruncate (NOT RETAIL)
// ============================================================================
// Issue #13 (docs/extensions/handins/COMPRESSAO.md): the derived-data cache's files. Linux and (no
// darwin twin: src/platform/darwin/README.md's rule adds nothing) macOS. The handle is fd + 1
// (file_posix.cpp / file_darwin.cpp). The lock is flock(LOCK_EX | LOCK_NB): advisory, held by the
// open file description, so a second open of the same path -- this process or another -- fails.
// ============================================================================

#include "platform_file.h"
#include "posix_path.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <string>

namespace Platform {

namespace {
int Fd(FileHandle h) {
    return static_cast<int>(reinterpret_cast<uintptr_t>(h) - 1u);  // CAST-OK: ptr_int
}
}  // namespace

FileHandle FileOpenExclusive(const PathChar* path) {
    const std::string native = Posix::ToNative(path);
    const int fd = ::open(native.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return kInvalidFileHandle;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return kInvalidFileHandle;
    }
    return reinterpret_cast<FileHandle>(static_cast<uintptr_t>(fd) + 1u);  // CAST-OK: ptr_int
}

int32_t FileTruncate(FileHandle h, int64_t size) {
    if (size < 0) return 0;
    return ::ftruncate(Fd(h), static_cast<off_t>(size)) == 0 ? 1 : 0;
}

}  // namespace Platform

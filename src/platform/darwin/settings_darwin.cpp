// ============================================================================
// src/platform/darwin/settings_darwin.cpp -- the settings seam, Darwin
// ============================================================================
// The Darwin twin of src/platform/posix/settings_posix.cpp (src/platform/darwin/README.md).
//
// DocumentsDir: `<home>/Documents` (saves in ~/Documents/Banished/Save, see the
// body), with <home> from the password database (getpwuid_r on the real
// uid), NEVER from an environment variable (CLAUDE.md, D4_RULES C11). GetSaveDirectory
// then appends "\Banished\Save\" as on Windows.
//
// ReadWholeFile / WriteFileAtomic: the same contract as the Windows bodies, written
// directly over POSIX calls (the paths are already UTF-8 here). ALLOCATOR-FREE:
// std::string and the C library only, because src/app/main.cpp's harness guard runs
// them before the engine allocator exists (platform_settings.h).
//
// ReadLegacySettings: there is no legacy store on POSIX; always false.
// ============================================================================

#include "platform_settings.h"

#include "app/options.h"   // Opt().userProfile: the test redirect of DocumentsDir
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <vector>

#include "system/String.h"

namespace {

bool HomeDir(std::string& out) {
    struct passwd pw;
    struct passwd* result = nullptr;
    std::vector<char> buf(16384);
    if (getpwuid_r(getuid(), &pw, buf.data(), buf.size(), &result) != 0 || result == nullptr)
        return false;
    if (pw.pw_dir == nullptr || pw.pw_dir[0] == 0)
        return false;
    out = pw.pw_dir;
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    return true;
}

// mkdir on every prefix of `dir`, ignoring failures (an existing directory fails
// harmlessly; a real failure surfaces at the open that follows), as settings_win.cpp.
void CreateDirectoryChain(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
            std::string prefix = dir.substr(0, i);
            for (char& c : prefix)
                if (c == '\\') c = '/';
            ::mkdir(prefix.c_str(), 0777);
        }
    }
}

std::string NativeSlashes(const char* s) {
    std::string out = s;
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}

bool WriteAll(int fd, const char* data, size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, data, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (w == 0) return false;
        data += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

}  // namespace

namespace Platform {

bool DocumentsDir(std::string& out) {
    // macOS: the user's ~/Documents, so GetSaveDirectory's "%s\Banished\" + "Save" lands in
    // ~/Documents/Banished/Save: the folder the original game and the Unbanished port use,
    // and the folder settings.cfg sits beside (IO::SettingsStore::DefaultPath).
    // <home> is the password database's, never an environment variable. The one redirect
    // is Opt().userProfile (USERPROFILE, read once at boot, the same switch the Windows
    // build and tools/resave_gate use): a test run points it at a throwaway folder so it
    // never touches the real Documents. The unit binary does not link options.cpp: the
    // test file supplies its own Opt() with no profile set (the test seam), so this body
    // is the same in both binaries.
    std::string home;
    if (Opt().userProfile != nullptr && Opt().userProfile[0] != 0) {
        home = Opt().userProfile;
        while (home.size() > 1 && home.back() == '/')
            home.pop_back();
    } else if (!HomeDir(home)) {
        return false;
    }
    out = home + "/Documents";
    return true;
}

bool DocumentsDir(System::String& out) {
    std::string dir;
    if (!DocumentsDir(dir))
        return false;
    out = System::String(dir.c_str());
    return true;
}

bool ReadWholeFile(const char* pathUtf8, std::string& out) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const std::string path = NativeSlashes(pathUtf8);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    std::string data;
    char chunk[16384];
    bool ok = true;
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n == 0) break;
        data.append(chunk, static_cast<size_t>(n));
    }
    ::close(fd);
    if (!ok)
        return false;
    out.swap(data);
    return true;
}

bool WriteFileAtomic(const char* pathUtf8, const char* data, size_t n) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const std::string path = NativeSlashes(pathUtf8);
    const std::string tmp = path + ".tmp";

    const size_t cut = path.find_last_of('/');
    if (cut != std::string::npos && cut > 0)
        CreateDirectoryChain(path.substr(0, cut));

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        return false;
    bool ok = WriteAll(fd, data, n);
    if (ok && ::fsync(fd) != 0)
        ok = false;
    if (::close(fd) != 0)
        ok = false;
    if (ok && ::rename(tmp.c_str(), path.c_str()) != 0)
        ok = false;
    if (!ok)
        ::unlink(tmp.c_str());
    return ok;
}

bool ReadLegacySettings(void (*)(void* ctx, const char* key, const char* value), void*) {
    return false;
}

}  // namespace Platform

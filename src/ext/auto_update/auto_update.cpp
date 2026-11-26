// src/ext/auto_update/auto_update.cpp -- NOT RETAIL. ext.auto_update: the launch, the check and the
// restart (auto_update.h; docs/fork/AUTO_UPDATE.md). The menu and game halves are auto_update_ui.cpp.
#include "ext/auto_update/auto_update.h"
#include "ext/auto_update/auto_update_state.h"
#include "ext/auto_update/launch_plan.h"
#include "ext/auto_update/manifest.h"
#include "ext/auto_update/sha256.h"
#include "ext/auto_update/staging.h"
#include "ext/auto_update/update_key.h"

#include "platform/common/platform_http.h"
#include "platform/common/platform_settings.h"   // DocumentsDir (allocator-free), ReadWholeFile
#include "platform/common/platform_update.h"

#include "ext/registry.h"   // Ext::Ext().auto_update

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifndef OPENBA_UPDATE_FEED
#define OPENBA_UPDATE_FEED ""   // CMakeLists.txt sets it for this file only (OPENBA_UPDATE_FEED cache variable)
#endif

namespace AutoUpdate {

namespace fs = std::filesystem;
namespace PU = Platform::Update;

namespace {

constexpr const char* kMine = OPENBA_VERSION;
constexpr uint32_t    kManifestTimeoutMs = 15000;
constexpr uint32_t    kPackageTimeoutMs  = 30000;   // per wait for data, not the whole download
constexpr auto        kFirstCheckDelay   = std::chrono::seconds(20);
constexpr auto        kCheckInterval     = std::chrono::hours(2);
constexpr auto        kSoonestRecheck    = std::chrono::minutes(5);
constexpr uint32_t    kRestartWaitMs     = 30000;

std::mutex              s_mu;
std::condition_variable s_cv;
fs::path                s_dir;            // the staging folder
std::string             s_feed;           // where the manifests are, ending in '/'
std::string             s_ready;          // a staged build
std::string             s_notice;         // for the menu
bool                    s_announce = false;
bool                    s_wake = false;
bool                    s_threadStarted = false;
std::chrono::steady_clock::time_point s_lastCheck;
std::atomic<bool>       s_restart{false};
std::vector<std::string> s_launchLines;   // AtLaunch's lines, printed by Start once the logs are open

void LaunchLog(const std::string& line) {
    s_launchLines.push_back(line);
    if (!s_dir.empty()) Staging::AppendLog(s_dir, "[launch " + std::string(kMine) + "] " + line);
}

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// ext.auto_update from settings.cfg, read before the settings store exists. On unless the key says
// 0 / false. (BANISHED_USER_DIR, the harness's user-dir override, is not applied yet: a harness run
// reads the real folder's key here, and its own at Start.)
bool EnabledInSettingsFile() {
    std::string docs;
    if (!Platform::DocumentsDir(docs)) return true;
    std::string text;
    if (!Platform::ReadWholeFile((docs + "/Banished/settings.cfg").c_str(), text)) return true;
    const std::string key = "ext.auto_update=";
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, key.size(), key) == 0) {
            const std::string v = Lower(line.substr(key.size()));
            return !(v == "0" || v == "false");
        }
        at = end + 1;
    }
    return true;
}

bool RemoveAll(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
    return !ec;
}

// Move `from` to `to`, replacing a file there.
bool MoveReplacing(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    if (fs::exists(to, ec) && !fs::is_directory(to, ec)) fs::remove(to, ec);
    ec.clear();
    fs::rename(from, to, ec);
    if (ec) {
        error = "cannot move " + Staging::Utf8(from) + " to " + Staging::Utf8(to) + ": " + ec.message();
        return false;
    }
    return true;
}

// ---- the launch: InPlace (Windows, Linux) -------------------------------------------------------

// The installed program could not start: the previous one goes back, the version is bad, and the
// previous program starts again (a fresh start that keeps the failed run's crash logs).
int RollBackInPlace(const fs::path& self, const fs::path& kept, const std::string& version, int exitCode) {
    std::string error;
    const fs::path failed = s_dir / "failed-program";
    RemoveAll(failed);
    std::error_code ec;
    fs::create_directories(failed, ec);
    MoveReplacing(self, failed / self.filename(), error);
    if (!MoveReplacing(kept, self, error)) {
        LaunchLog("rollback failed: " + error);
        return 1;
    }
    Staging::AddBad(s_dir, version);
    Staging::Remove(s_dir, "pending");
    const std::string notice = "OpenBA " + version + " did not start (exit code " + std::to_string(exitCode) +
                               "), so this is " + kMine + " again.";
    Staging::WriteLine(s_dir, "notice", notice);
    LaunchLog(notice);
    PU::Child* again = PU::StartAgain(Staging::Utf8(self), error);
    if (again == nullptr) {
        LaunchLog("cannot start the previous build again: " + error);
        return 1;
    }
    PU::LetChildForeground(again);
    PU::ReleaseChild(again);
    return 0;
}

// The old program after an install: waits for the new one to reach its menu (`started`) or exit.
int Supervise(PU::Child* child, const fs::path& self, const fs::path& kept, const std::string& version) {
    PU::LetChildForeground(child);
    int exitCode = 0;
    for (;;) {
        if (PU::WaitChild(child, 250, exitCode)) break;
        if (Staging::ReadLine(s_dir, "started") == version) {
            LaunchLog(version + " started; the supervisor is done");
            PU::ReleaseChild(child);
            return 0;
        }
    }
    PU::ReleaseChild(child);
    if (Staging::ReadLine(s_dir, "started") == version) return 0;   // it started, then quit
    if (exitCode == 0) {
        LaunchLog(version + " closed before its menu (exit 0): no verdict");
        Staging::Remove(s_dir, "pending");
        return 0;
    }
    LaunchLog(version + " exited with " + std::to_string(exitCode) + " before its menu: rolling back");
    return RollBackInPlace(self, kept, version, exitCode);
}

int LaunchInPlace(const StagingState& st, bool enabled) {
    const InPlacePlan plan = PlanInPlace(st, kMine, enabled);
    if (plan.step == InPlaceStep::Nothing) return -1;
    const fs::path staged = s_dir / "staged" / Staging::Path(plan.version);
    if (plan.step == InPlaceStep::Discard) {
        LaunchLog("not installing " + plan.version + ": " + plan.why);
        RemoveAll(staged);
        Staging::Remove(s_dir, "ready");
        return -1;
    }

    const fs::path self = Staging::Path(PU::SelfPath());
    const fs::path folder = self.parent_path();
    const fs::path keptDir = s_dir / "previous-program";
    const fs::path kept = keptDir / self.filename();
    std::string    error;
    RemoveAll(keptDir);
    std::error_code ec;
    fs::create_directories(keptDir, ec);
    if (!MoveReplacing(self, kept, error)) {
        // Most often the folder is not writable for this account. The build stays staged: the next
        // start tries again.
        const std::string notice = "OpenBA can't install " + plan.version + " in " + Staging::Utf8(folder) +
                                   ": " + error;
        LaunchLog(notice);
        Staging::WriteLine(s_dir, "notice", notice);
        return -1;
    }
    Staging::WriteLine(keptDir, "version", kMine);

    // Every file of the package's root folder into this program's folder; the program under this
    // program's own name.
    // The names first: moving entries out of a folder while iterating it may skip some.
    std::vector<fs::path> entries;
    for (fs::directory_iterator it(staged, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
    bool ok = !ec;
    for (const fs::path& from : entries) {
        if (!ok) break;
        const bool     isProgram = from.filename() == Staging::Path(PU::ProgramName());
        const fs::path to = isProgram ? self : folder / from.filename();
        if (fs::is_directory(from, ec)) {
            RemoveAll(to);
            ec.clear();
        }
        ok = MoveReplacing(from, to, error);
    }
    if (!ok || ec || !fs::exists(self, ec)) {
        if (error.empty()) error = ec ? ec.message() : std::string("the package has no ") + PU::ProgramName();
        const std::string notice = "OpenBA could not install " + plan.version + ": " + error;
        LaunchLog(notice);
        std::string undo;
        if (!MoveReplacing(kept, self, undo)) LaunchLog("and could not put this build back: " + undo);
        Staging::WriteLine(s_dir, "notice", notice);
        RemoveAll(staged);
        Staging::Remove(s_dir, "ready");
        return -1;
    }
    RemoveAll(staged);
    Staging::Remove(s_dir, "ready");
    Staging::WriteLine(s_dir, "pending", plan.version);
    LaunchLog("installed " + plan.version + " over " + kMine + "; starting it");

    PU::Child* child = PU::StartAgain(Staging::Utf8(self), error);
    if (child == nullptr) {
        LaunchLog("cannot start " + plan.version + ": " + error);
        return RollBackInPlace(self, kept, plan.version, -1);
    }
    return Supervise(child, self, kept, plan.version);
}

// ---- the launch: Versioned (macOS) --------------------------------------------------------------

bool IsInside(const fs::path& inner, const fs::path& outer) {
    std::error_code ec;
    const fs::path  a = fs::weakly_canonical(inner, ec);
    const fs::path  b = fs::weakly_canonical(outer, ec);
    const std::string as = Staging::Utf8(a);
    const std::string bs = Staging::Utf8(b) + "/";
    return as.compare(0, bs.size(), bs) == 0;
}

int LaunchVersioned(const StagingState& st, bool enabled) {
    const bool          selfInstalled = IsInside(Staging::Path(PU::SelfBundle()), s_dir);
    const VersionedPlan plan = PlanVersioned(st, kMine, selfInstalled, enabled);
    if (!plan.markBad.empty()) {
        Staging::AddBad(s_dir, plan.markBad);
        LaunchLog(plan.markBad + " never reached its menu: marked bad");
    }
    if (plan.clearPending) Staging::Remove(s_dir, "pending");
    if (plan.discardReady) {
        LaunchLog("not installing " + st.ready + ": " + plan.why);
        RemoveAll(s_dir / "staged" / Staging::Path(st.ready));
        Staging::Remove(s_dir, "ready");
    }
    if (plan.promoteReady) {
        const fs::path to = s_dir / Staging::Path(st.ready);
        RemoveAll(to);
        std::error_code ec;
        fs::rename(s_dir / "staged" / Staging::Path(st.ready), to, ec);
        if (ec) {
            LaunchLog("cannot install " + st.ready + ": " + ec.message());
            Staging::Remove(s_dir, "ready");
            return -1;
        }
        Staging::Remove(s_dir, "ready");
        LaunchLog("installed " + st.ready);
    }
    if (plan.stateChanged) {
        Staging::WriteLine(s_dir, "current", plan.current);
        Staging::WriteLine(s_dir, "previous", plan.previous);
    }
    if (!plan.notice.empty()) {
        Staging::WriteLine(s_dir, "notice", plan.notice);
        LaunchLog(plan.notice);
    }
    if (plan.run.empty()) return -1;

    const fs::path bundle = s_dir / Staging::Path(plan.run) / PU::ProgramName();
    const fs::path engine = bundle / "Contents" / "MacOS" / "OpenBA-engine";
    const fs::path airconv = bundle / "Contents" / "Frameworks" / "libairconv.dylib";
    if (plan.writePending) Staging::WriteLine(s_dir, "pending", plan.run);
    LaunchLog("handing over to " + plan.run);
    std::string error;
    PU::HandOver(PU::SelfPath(), Staging::Utf8(engine), Staging::Utf8(airconv), error);
    // Only a failed exec comes back here: that build is unusable.
    LaunchLog("cannot start " + plan.run + ": " + error);
    Staging::AddBad(s_dir, plan.run);
    Staging::Remove(s_dir, "pending");
    Staging::WriteLine(s_dir, "current", plan.previous);
    Staging::WriteLine(s_dir, "previous", std::string());
    Staging::WriteLine(s_dir, "notice", "OpenBA " + plan.run + " could not start, so this is " + kMine + ".");
    return -1;
}

// ---- the check ----------------------------------------------------------------------------------

void ThreadLog(const std::string& line) {
    Internal::Log(line);
}

bool DownloadText(const std::string& url, std::string& out, std::string& error) {
    out.clear();
    const int status = Platform::HttpGet(url.c_str(), kMaxManifestBytes, kManifestTimeoutMs,
                                         [&out](const char* data, size_t size) {
                                             out.append(data, size);
                                             return true;
                                         },
                                         error);
    return status == 200;
}

// The package into `path`, hashed on the way. (std::ofstream on an fs::path: the wide name on Windows,
// where fopen would read a UTF-8 name as the ANSI code page.)
bool DownloadPackage(const Manifest& m, const fs::path& path, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + Staging::Utf8(path);
        return false;
    }
    Sha256    sha;
    bool      writeFailed = false;
    const int status = Platform::HttpGet(m.url.c_str(), m.size, kPackageTimeoutMs,
                                         [&](const char* data, size_t size) {
                                             sha.Add(data, size);
                                             out.write(data, static_cast<std::streamsize>(size));
                                             if (!out) {
                                                 writeFailed = true;
                                                 return false;
                                             }
                                             return true;
                                         },
                                         error);
    out.close();
    const bool closed = static_cast<bool>(out);
    if (writeFailed || !closed) {
        error = "cannot write " + Staging::Utf8(path) + " (is the disk full?)";
        return false;
    }
    if (status != 200) return false;
    std::error_code ec;
    const uintmax_t got = fs::file_size(path, ec);
    if (ec || got != m.size) {
        error = "the package is " + std::to_string(ec ? 0 : got) + " bytes, the manifest says " + std::to_string(m.size);
        return false;
    }
    uint8_t digest[32];
    sha.Finish(digest);
    if (Sha256::Hex(digest) != m.sha256) {
        error = "the package's SHA-256 does not match the manifest";
        return false;
    }
    return true;
}

// The unpacked package's root: the folder holding ProgramName(), or the single folder that does
// (the Linux tarball has one top-level folder).
bool PackageRoot(const fs::path& unpacked, fs::path& root) {
    std::error_code ec;
    if (fs::exists(unpacked / PU::ProgramName(), ec)) {
        root = unpacked;
        return true;
    }
    fs::path only;
    int      count = 0;
    for (fs::directory_iterator it(unpacked, ec), end; !ec && it != end; it.increment(ec)) {
        ++count;
        only = it->path();
    }
    if (count == 1 && fs::exists(only / PU::ProgramName(), ec)) {
        root = only;
        return true;
    }
    return false;
}

void CheckOnce() {
    const std::string url = s_feed + "openba-update-" + PU::PackagePlatform() + ".txt";
    std::string       text;
    std::string       error;
    if (!DownloadText(url, text, error)) {
        ThreadLog("no update information at " + url + ": " + error);
        return;
    }
    Manifest m;
    if (!ParseManifest(text, m, error)) {
        ThreadLog("the update manifest is unusable: " + error);
        return;
    }
    if (m.platform != PU::PackagePlatform()) {
        ThreadLog("the update manifest is for " + m.platform + ", not " + PU::PackagePlatform());
        return;
    }
    if (!VerifyManifest(text, m, kUpdatePublicKey, error)) {
        ThreadLog("refusing the update manifest: " + error);
        return;
    }
    if (!IsNewer(m.version, kMine)) {
        ThreadLog("up to date: " + std::string(kMine) + " (the newest release is " + m.version + ")");
        return;
    }
    if (Staging::ReadBad(s_dir).count(m.version) != 0) {
        ThreadLog(m.version + " failed to start here before: not downloading it again");
        return;
    }
    if (Staging::ReadLine(s_dir, "ready") == m.version) {
        std::lock_guard<std::mutex> lock(s_mu);
        s_ready = m.version;
        return;
    }
    if (PU::InstallModel() == PU::Model::Versioned && Staging::ReadLine(s_dir, "current") == m.version) {
        std::error_code ec;
        if (fs::exists(s_dir / Staging::Path(m.version) / PU::ProgramName(), ec)) {
            std::lock_guard<std::mutex> lock(s_mu);
            s_ready = m.version;   // installed already; the next start runs it
            return;
        }
    }

    ThreadLog("downloading " + m.version + " (" + std::to_string(m.size) + " bytes) from " + m.url);
    const fs::path downloads = s_dir / "download";
    const fs::path unpacked = s_dir / "unpack";
    RemoveAll(downloads);
    RemoveAll(unpacked);
    std::error_code ec;
    fs::create_directories(downloads, ec);
    fs::create_directories(unpacked, ec);
    std::string name = m.url.substr(m.url.find_last_of('/') + 1);
    if (name.empty() || name.find_first_of("\\:?*\"<>|") != std::string::npos) name = "package";
    const fs::path package = downloads / Staging::Path(name);
    if (!DownloadPackage(m, package, error)) {
        ThreadLog("the download of " + m.version + " failed: " + error);
        RemoveAll(downloads);
        return;
    }
    if (!PU::Unpack(Staging::Utf8(package), Staging::Utf8(unpacked), error)) {
        ThreadLog("cannot unpack " + m.version + ": " + error);
        RemoveAll(downloads);
        RemoveAll(unpacked);
        return;
    }
    fs::path root;
    if (!PackageRoot(unpacked, root)) {
        ThreadLog("the package of " + m.version + " has no " + PU::ProgramName());
        RemoveAll(downloads);
        RemoveAll(unpacked);
        return;
    }
    if (!PU::VerifyProgram(Staging::Utf8(root / PU::ProgramName()), error)) {
        ThreadLog("the package of " + m.version + " failed its signature check: " + error);
        RemoveAll(downloads);
        RemoveAll(unpacked);
        return;
    }
    const fs::path staged = s_dir / "staged" / Staging::Path(m.version);
    RemoveAll(s_dir / "staged");
    fs::create_directories(s_dir / "staged", ec);
    fs::rename(root, staged, ec);
    RemoveAll(downloads);
    RemoveAll(unpacked);
    if (ec) {
        ThreadLog("cannot stage " + m.version + ": " + ec.message());
        return;
    }
    Staging::WriteLine(s_dir, "ready", m.version);
    ThreadLog(m.version + " (commit " + m.commit + ") is ready: it installs at the next start");
    std::lock_guard<std::mutex> lock(s_mu);
    s_ready = m.version;
    s_announce = true;
}

void CheckLoop() {
    std::unique_lock<std::mutex> lock(s_mu);
    s_cv.wait_for(lock, kFirstCheckDelay, [] { return s_wake; });
    for (;;) {
        s_wake = false;
        s_lastCheck = std::chrono::steady_clock::now();
        lock.unlock();
        CheckOnce();
        lock.lock();
        s_cv.wait_for(lock, kCheckInterval, [] { return s_wake; });
    }
}

}  // namespace

// ---- Internal -----------------------------------------------------------------------------------

namespace Internal {

fs::path StagingDir() {
    std::lock_guard<std::mutex> lock(s_mu);
    return s_dir;
}

std::string ReadyVersion() {
    std::lock_guard<std::mutex> lock(s_mu);
    return s_ready;
}

std::string Notice() {
    std::lock_guard<std::mutex> lock(s_mu);
    return s_notice;
}

void ClearNotice() {
    fs::path dir;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        if (s_notice.empty()) return;
        s_notice.clear();
        dir = s_dir;
    }
    if (!dir.empty()) Staging::Remove(dir, "notice");
}

bool TakeReadyAnnouncement(std::string& version) {
    std::lock_guard<std::mutex> lock(s_mu);
    if (!s_announce) return false;
    s_announce = false;
    version = s_ready;
    return true;
}

void RequestRestart() {
    s_restart.store(true);
}

void Log(const std::string& line) {
    std::fprintf(stderr, "[Update] %s\n", line.c_str());
    std::fflush(stderr);
    fs::path dir;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        dir = s_dir;
    }
    if (!dir.empty()) Staging::AppendLog(dir, "[" + std::string(kMine) + "] " + line);
}

}  // namespace Internal

// ---- the public calls ---------------------------------------------------------------------------

int AtLaunch(int argc, char** argv) {
    PU::SetArguments(argc, argv);
    if (std::string(OPENBA_UPDATE_FEED).empty()) return -1;   // a dev build never installs anything
    const fs::path dir = Staging::Path(PU::StagingDir());
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return -1;   // nothing was ever staged
    s_dir = dir;

    // Restart now's handover: on Windows the old process may still be closing.
    const std::string restartPid = Staging::ReadLine(dir, "restart");
    if (!restartPid.empty()) {
        Staging::Remove(dir, "restart");
        const unsigned long pid = std::strtoul(restartPid.c_str(), nullptr, 10);
        if (pid != 0 && pid != PU::ProcessId() && !PU::WaitForExit(static_cast<uint32_t>(pid), kRestartWaitMs)) {
            LaunchLog("the previous process " + restartPid + " is still running after 30 s; carrying on");
        }
    }

    const bool versioned = PU::InstallModel() == PU::Model::Versioned;
    const StagingState st = Staging::Read(dir, PU::ProgramName(), versioned);
    const bool enabled = EnabledInSettingsFile();
    return versioned ? LaunchVersioned(st, enabled) : LaunchInPlace(st, enabled);
}

void Start(const std::string& settingsFeed) {
    for (const std::string& line : s_launchLines) std::fprintf(stderr, "[Update] at launch: %s\n", line.c_str());
    s_launchLines.clear();
    const std::string built = OPENBA_UPDATE_FEED;
    if (built.empty()) {
        std::fprintf(stderr, "[Update] this build has no update feed (a dev build): no checks\n");
        return;
    }
    const fs::path dir = Staging::Path(PU::StagingDir());
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_dir = dir;
    }
    std::error_code ec;
    const bool      exists = fs::is_directory(dir, ec);
    if (exists) {
        std::lock_guard<std::mutex> lock(s_mu);
        s_notice = Staging::ReadLine(dir, "notice");
    }
    if (!Ext::Ext().auto_update) {
        std::fprintf(stderr, "[Update] updates are off (ext.auto_update=0)\n");
        if (exists && !Staging::ReadLine(dir, "ready").empty()) {
            RemoveAll(dir / "staged");
            Staging::Remove(dir, "ready");
            Internal::Log("updates are off: the staged build was removed");
        }
        return;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "[Update] cannot create %s (%s): no updates\n", Staging::Utf8(dir).c_str(),
                     ec.message().c_str());
        return;
    }
    std::string feed = !settingsFeed.empty() ? settingsFeed : built;
    if (feed.back() != '/') feed += '/';
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_feed = feed;
        const std::string ready = Staging::ReadLine(dir, "ready");
        if (!ready.empty() && IsNewer(ready, kMine)) s_ready = ready;
        if (s_threadStarted) return;
        s_threadStarted = true;
    }
    Internal::Log(std::string("this is ") + kMine + " (" + PU::PackagePlatform() + "); the feed is " + feed);
    try {
        std::thread(CheckLoop).detach();
    } catch (...) {
        std::fprintf(stderr, "[Update] cannot start the check thread\n");
    }
}

void CheckSoon(const char* why) {
    std::lock_guard<std::mutex> lock(s_mu);
    if (!s_threadStarted) return;
    const auto now = std::chrono::steady_clock::now();
    if (s_lastCheck != std::chrono::steady_clock::time_point() && now - s_lastCheck < kSoonestRecheck) return;
    std::fprintf(stderr, "[Update] checking now: %s\n", why != nullptr ? why : "asked");
    s_wake = true;
    s_cv.notify_all();
}

void AtExit() {
    if (!s_restart.load()) return;
    const fs::path dir = Internal::StagingDir();
    if (!dir.empty()) Staging::WriteLine(dir, "restart", std::to_string(PU::ProcessId()));
    Internal::Log("restarting");
    std::fflush(stdout);
    std::fflush(stderr);
    std::string error;
    if (!PU::RestartSelf(error)) {
        Internal::Log("cannot restart: " + error);
        if (!dir.empty()) Staging::Remove(dir, "restart");
    }
}

}  // namespace AutoUpdate

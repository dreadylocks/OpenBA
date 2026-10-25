// ============================================================================
// src/ext/crash_report/crash_report.cpp -- NOT RETAIL. ext.crash_reports: crashes kept and sent
// ============================================================================
// crash_report.h has the contract.
// ============================================================================

#include "ext/crash_report/crash_report.h"

#include "ext/crash_report/report.h"
#include "ext/registry.h"
#include "platform/common/platform_http.h"
#include "platform/common/platform_os.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>
#include <thread>
#include <vector>

// The build's crash report address (CMakeLists.txt OPENBA_CRASH_DSN), "" for none.
#ifndef OPENBA_CRASH_DSN
#define OPENBA_CRASH_DSN ""
#endif

namespace CrashReport {

namespace {

namespace fs = std::filesystem;

constexpr const char* kDir = "crash_reports";
constexpr size_t      kKeep = 5;          // the newest crashes kept
constexpr size_t      kSendAtOnce = 3;    // sent per start, oldest first
constexpr uint32_t    kTimeoutMs = 20000;

struct Log {
    const char* name;
    size_t      head;   // its start kept when it is longer than `max`
    size_t      max;
};
// What a crash keeps of each log.
const Log kLogs[] = {
    {"standalone_crash.log", 16384, 65536},
    {"standalone_hang.log", 16384, 262144},
    {"standalone_error.log", 65536, 524288},
    {"standalone_debug.log", 16384, 131072},
};

std::string s_kept;       // the folder KeepLastCrash made, for Start to report
const char* s_keptKind = "";

uint64_t SizeOf(const fs::path& path) {
    std::error_code ec;
    const uint64_t  n = fs::file_size(path, ec);
    return ec ? 0 : n;
}

int64_t WrittenAt(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? static_cast<int64_t>(st.st_mtime) : static_cast<int64_t>(std::time(nullptr));
}

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string   s;
    if (!in) return s;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) return s;
    s.resize(static_cast<size_t>(size));
    in.seekg(0);
    in.read(&s[0], size);
    s.resize(static_cast<size_t>(in.gcount()));
    return s;
}

// The log's start and end (HeadAndTail), reading only those.
std::string ReadCut(const char* path, size_t head, size_t max) {
    const uint64_t size = SizeOf(path);
    if (size <= max) return ReadAll(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    const size_t headBytes = head > max / 2 ? max / 2 : head;
    const size_t tailBytes = TailBytes(head, max);
    std::string  first(headBytes, '\0');
    in.read(&first[0], static_cast<std::streamsize>(headBytes));
    first.resize(static_cast<size_t>(in.gcount()));
    std::string last(tailBytes, '\0');
    in.seekg(static_cast<std::streamoff>(size - tailBytes));
    in.read(&last[0], static_cast<std::streamsize>(tailBytes));
    last.resize(static_cast<size_t>(in.gcount()));
    return JoinCut(first, last, size);
}

void Write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// The kept crashes, oldest first (their names are their times).
std::vector<fs::path> Kept() {
    std::vector<fs::path> dirs;
    std::error_code       ec;
    for (fs::directory_iterator it(kDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) dirs.push_back(it->path());
    }
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

std::string InfoValue(const std::string& info, const char* key) {
    const std::string k = std::string(key) + "=";
    size_t            at = info.find(k);
    if (at == std::string::npos || (at > 0 && info[at - 1] != '\n')) return std::string();
    at += k.size();
    const size_t end = info.find('\n', at);
    return info.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

std::string NewEventId() {
    static const char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64    rng((static_cast<uint64_t>(rd()) << 32) ^ rd() ^
                        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string id;
    for (int i = 0; i < 32; ++i) id += kHex[rng() & 15];
    return id;
}

// One kept crash to the server: true when it took it (or refused it for good), false to stop
// sending for this start (no line, or the server says try later).
bool SendOne(const fs::path& dir, const Dsn& dsn, const std::string& dsnText) {
    const std::string info = ReadAll(dir / "info.txt");
    const std::string kind = InfoValue(info, "kind");
    Summary           summary;
    if (kind == "hang") {
        summary.kind = "hang";
        summary.value = "the game stopped responding (standalone_hang.log has every thread's stack)";
    } else {
        summary = Summarize(ReadAll(dir / "standalone_crash.log"));
    }
    std::vector<Attachment> attachments;
    for (const Log& l : kLogs) {
        const fs::path p = dir / l.name;
        if (SizeOf(p) > 0) attachments.push_back(Attachment{l.name, Scrub(ReadAll(p))});
    }
    EventInfo e;
    e.eventId = NewEventId();
    e.crashTime = std::atoll(InfoValue(info, "time").c_str());
    e.sentTime = static_cast<int64_t>(std::time(nullptr));
    if (e.crashTime <= 0) e.crashTime = e.sentTime;
    e.release = std::string("openba@") + OPENBA_VERSION;
    e.os = Platform::OsName();
    e.dsn = dsnText;
    const std::string body = BuildEnvelope(e, summary, attachments);

    std::string error;
    const int   status = Platform::HttpPost(EnvelopeUrl(dsn).c_str(), "application/x-sentry-envelope", body, kTimeoutMs,
                                            error);
    const std::string name = dir.filename().string();
    if (status >= 200 && status < 300) {
        Write(dir / "sent", e.eventId + "\n");
        std::fprintf(stderr, "[CrashReport] sent the %s of %s to %s (event %s)\n", summary.kind.c_str(), name.c_str(),
                     dsn.host.c_str(), e.eventId.c_str());
        return true;
    }
    if (status == 0) {
        std::fprintf(stderr, "[CrashReport] could not send the crash of %s: %s; it goes next start\n", name.c_str(),
                     error.c_str());
        return false;
    }
    if (status == 429 || status >= 500) {
        std::fprintf(stderr, "[CrashReport] %s is busy (HTTP %d); the crash of %s goes next start\n", dsn.host.c_str(),
                     status, name.c_str());
        return false;
    }
    // Refused for good (a bad key, a deleted project, too big): marked, so it is not tried again.
    Write(dir / "sent", "refused: HTTP " + std::to_string(status) + "\n");
    std::fprintf(stderr, "[CrashReport] %s refused the crash of %s (HTTP %d); check crashreport.dsn\n",
                 dsn.host.c_str(), name.c_str(), status);
    return true;
}

}  // namespace

void KeepLastCrash() {
    const bool crashed = SizeOf("standalone_crash.log") > 0;
    const bool hung = !crashed && SizeOf("standalone_hang.log") > 0;
    if (!crashed && !hung) return;
    const std::time_t when = static_cast<std::time_t>(WrittenAt(crashed ? "standalone_crash.log" : "standalone_hang.log"));
    char              name[32] = {};
    if (const std::tm* tm = std::localtime(&when)) std::strftime(name, sizeof(name), "%Y%m%d-%H%M%S", tm);
    if (name[0] == '\0') return;
    const fs::path  dir = fs::path(kDir) / name;
    std::error_code ec;
    if (fs::exists(dir, ec)) return;   // kept already
    fs::create_directories(dir, ec);
    if (ec) return;
    for (const Log& l : kLogs) {
        if (SizeOf(l.name) > 0) Write(dir / l.name, ReadCut(l.name, l.head, l.max));
    }
    Write(dir / "info.txt", "time=" + std::to_string(static_cast<long long>(when)) + "\nkind=" +
                                (crashed ? "crash" : "hang") + "\n");
    std::vector<fs::path> dirs = Kept();
    while (dirs.size() > kKeep) {
        fs::remove_all(dirs.front(), ec);
        dirs.erase(dirs.begin());
    }
    s_kept = dir.generic_string();
    s_keptKind = crashed ? "crashed" : "hung";
}

void Start(const std::string& settingsDsn) {
    if (!s_kept.empty())
        std::fprintf(stderr, "[CrashReport] the last run %s; its logs are kept in %s\n", s_keptKind, s_kept.c_str());
    std::vector<fs::path> pending;
    for (const fs::path& d : Kept()) {
        std::error_code ec;
        if (!fs::exists(d / "sent", ec)) pending.push_back(d);
    }
    if (pending.empty()) return;
    const std::string dsnText = !settingsDsn.empty() ? settingsDsn : std::string(OPENBA_CRASH_DSN);
    if (!Ext::Ext().crash_reports) {
        std::fprintf(stderr, "[CrashReport] %zu crash(es) kept in %s, not sent: Send crash reports is off\n",
                     pending.size(), kDir);
        return;
    }
    if (dsnText.empty()) {
        std::fprintf(stderr, "[CrashReport] %zu crash(es) kept in %s, not sent: no crash report address "
                             "(crashreport.dsn in settings.cfg)\n",
                     pending.size(), kDir);
        return;
    }
    Dsn dsn;
    if (!ParseDsn(dsnText, dsn)) {
        std::fprintf(stderr, "[CrashReport] crashreport.dsn is not a Sentry DSN (https://<key>@<host>/<project>); "
                             "nothing sent\n");
        return;
    }
    if (pending.size() > kSendAtOnce) pending.resize(kSendAtOnce);
    try {
        std::thread([pending, dsn, dsnText] {
            for (const fs::path& d : pending)
                if (!SendOne(d, dsn, dsnText)) break;
        }).detach();
    } catch (const std::system_error&) {
        std::fprintf(stderr, "[CrashReport] could not start sending; the crashes go next start\n");
    }
}

}  // namespace CrashReport

// src/ext/auto_update/staging.cpp -- NOT RETAIL. ext.auto_update: the staging folder (staging.h).
#include "ext/auto_update/staging.h"

#include "platform/common/platform_settings.h"   // Platform::ReadWholeFile / WriteFileAtomic

#include <cstdio>

namespace AutoUpdate {
namespace Staging {

namespace {

constexpr size_t kMaxLogBytes = 64 * 1024;

std::string FileOf(const fs::path& dir, const char* name) {
    return Utf8(dir / name);
}

}  // namespace

fs::path Path(const std::string& utf8) {
    return fs::u8path(utf8);
}

std::string Utf8(const fs::path& p) {
    return p.u8string();
}

std::string ReadLine(const fs::path& dir, const char* name) {
    std::string text;
    if (!Platform::ReadWholeFile(FileOf(dir, name).c_str(), text)) return std::string();
    const size_t end = text.find_first_of("\r\n");
    if (end != std::string::npos) text.resize(end);
    return text;
}

bool WriteLine(const fs::path& dir, const char* name, const std::string& value) {
    const std::string body = value + "\n";
    return Platform::WriteFileAtomic(FileOf(dir, name).c_str(), body.data(), body.size());
}

void Remove(const fs::path& dir, const char* name) {
    std::error_code ec;
    fs::remove(dir / name, ec);
}

std::set<std::string> ReadBad(const fs::path& dir) {
    std::set<std::string> out;
    std::string           text;
    if (!Platform::ReadWholeFile(FileOf(dir, "bad").c_str(), text)) return out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.insert(line);
        at = end + 1;
    }
    return out;
}

bool AddBad(const fs::path& dir, const std::string& version) {
    std::set<std::string> bad = ReadBad(dir);
    if (!bad.insert(version).second) return true;
    std::string body;
    for (const std::string& v : bad) body += v + "\n";
    return Platform::WriteFileAtomic(FileOf(dir, "bad").c_str(), body.data(), body.size());
}

StagingState Read(const fs::path& dir, const char* programName, bool versioned) {
    StagingState s;
    std::error_code ec;
    s.ready = ReadLine(dir, "ready");
    if (!s.ready.empty()) s.readyComplete = fs::exists(dir / "staged" / Path(s.ready) / programName, ec);
    s.pending = ReadLine(dir, "pending");
    s.started = ReadLine(dir, "started");
    s.bad = ReadBad(dir);
    if (versioned) {
        s.current = ReadLine(dir, "current");
        if (!s.current.empty()) s.currentComplete = fs::exists(dir / Path(s.current) / programName, ec);
        s.previous = ReadLine(dir, "previous");
    }
    return s;
}

void AppendLog(const fs::path& dir, const std::string& line) {
    const std::string log = Utf8(dir / "update.log");
    std::string       text;
    Platform::ReadWholeFile(log.c_str(), text);
    text += line + "\n";
    if (text.size() > kMaxLogBytes) {
        text.erase(0, text.size() - kMaxLogBytes / 2);   // keep the newer half, from a line start
        const size_t nl = text.find('\n');
        if (nl != std::string::npos) text.erase(0, nl + 1);
    }
    Platform::WriteFileAtomic(log.c_str(), text.data(), text.size());
}

}  // namespace Staging
}  // namespace AutoUpdate

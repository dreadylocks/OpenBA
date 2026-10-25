// ============================================================================
// src/ext/crash_report/report.cpp -- NOT RETAIL. ext.crash_reports: what a crash report says
// ============================================================================
// report.h has the contract. Sentry's envelope: develop.sentry.dev/sdk/data-model/envelopes/.
// ============================================================================

#include "ext/crash_report/report.h"

#include <cxxabi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace CrashReport {

namespace {

bool IsSep(char c) {
    return c == '/' || c == '\\';
}

bool EqualsNoCase(const std::string& s, size_t at, const char* word) {
    const size_t n = std::strlen(word);
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[at + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != word[i]) return false;
    }
    return true;
}

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

bool IsHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// "OpenBA.exe + 0x1234  (base=...)" -> module, offset; the rest is left in `rest`.
bool ModuleAt(const std::string& text, std::string& module, std::string& offset, std::string& rest) {
    const size_t plus = text.find(" + 0x");
    if (plus == std::string::npos) return false;
    module = Trim(text.substr(0, plus));
    size_t i = plus + 5;
    size_t start = i;
    while (i < text.size() && IsHex(text[i])) ++i;
    if (i == start) return false;
    offset = "0x" + text.substr(start, i - start);
    rest = Trim(text.substr(i));
    return !module.empty();
}

// One "  #N <module> + 0x<offset>  (<symbol>+0x<n>)" line (the POSIX handlers), or "(RIP=...)" (Windows),
// or "#N ???  @ <address>".
bool ParseFrame(const std::string& line, Frame& out) {
    std::string t = Trim(line);
    if (t.empty() || t[0] != '#') return false;
    size_t i = 1;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
    if (i == 1) return false;
    t = Trim(t.substr(i));
    out = Frame();
    if (StartsWith(t, "???")) {
        out.module = "???";
        return true;
    }
    std::string rest;
    if (!ModuleAt(t, out.module, out.offset, rest)) return false;
    if (rest.size() > 2 && rest[0] == '(' && !StartsWith(rest, "(RIP=")) {
        std::string sym = rest.substr(1, rest.find(')') == std::string::npos ? std::string::npos : rest.find(')') - 1);
        const size_t plus = sym.rfind("+0x");
        if (plus != std::string::npos) sym = sym.substr(0, plus);
        out.function = sym;
    }
    return true;
}

bool IsHandlerFrame(const Frame& f) {
    static const char* const kNames[] = {"CrashHandler", "PrintStackTrace", "_sigtramp", "__restore_rt", "SignalHandler"};
    for (const char* n : kNames)
        if (f.function.find(n) != std::string::npos) return true;
    return StartsWith(f.module, "libsystem_platform");
}

bool InApp(const Frame& f) {
    return StartsWith(f.module, "OpenBA");
}

std::string FrameName(const Frame& f) {
    if (!f.function.empty()) return Demangle(f.function);
    return f.offset.empty() ? f.module : f.module + "+" + f.offset;
}

void AddValue(std::string& value, const std::string& part) {
    if (part.empty()) return;
    if (!value.empty()) value += "; ";
    value += part;
}

std::string OsName(const std::string& os) {
    if (os == "windows") return "Windows";
    if (os == "macos") return "macOS";
    if (os == "linux") return "Linux";
    return os;
}

// Days since 1970-01-01 -> year, month, day (Howard Hinnant's civil_from_days).
void CivilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

std::string Item(const std::string& header, const std::string& payload) {
    return header + "\n" + payload + "\n";
}

}  // namespace

bool ParseDsn(const std::string& text, Dsn& out) {
    out = Dsn();
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7F) return false;
    }
    size_t rest;
    if (StartsWith(text, "https://")) {
        out.scheme = "https";
        rest = 8;
    } else if (StartsWith(text, "http://")) {
        out.scheme = "http";
        rest = 7;
    } else {
        return false;
    }
    const size_t at = text.find('@', rest);
    const size_t slash = text.find('/', rest);
    if (at == std::string::npos || slash == std::string::npos || at > slash) return false;
    out.publicKey = text.substr(rest, at - rest);
    const size_t colon = out.publicKey.find(':');   // an old DSN's secret: not needed
    if (colon != std::string::npos) out.publicKey.resize(colon);
    out.host = text.substr(at + 1, slash - at - 1);
    const size_t last = text.rfind('/');
    out.projectId = text.substr(last + 1);
    out.path = text.substr(slash, last - slash + 1);
    if (out.publicKey.empty() || out.host.empty() || out.projectId.empty()) return false;
    for (char c : out.projectId)
        if (c == '?' || c == '#') return false;
    return true;
}

std::string EnvelopeUrl(const Dsn& dsn) {
    return dsn.scheme + "://" + dsn.host + dsn.path + "api/" + dsn.projectId + "/envelope/?sentry_key=" + dsn.publicKey +
           "&sentry_version=7&sentry_client=openba-crash-report/1";
}

Summary Summarize(const std::string& crashLog) {
    Summary s;
    bool    inStack = false;
    bool    moduleSeen = false;
    size_t  pos = 0;
    while (pos < crashLog.size()) {
        size_t end = crashLog.find('\n', pos);
        if (end == std::string::npos) end = crashLog.size();
        const std::string line = Trim(crashLog.substr(pos, end - pos));
        pos = end + 1;
        if (StartsWith(line, "===")) {
            inStack = false;
            if (s.kind.empty()) {
                if (line.find("PURE-VIRTUAL") != std::string::npos) s.kind = "pure virtual call";
                else if (line.find("terminate") != std::string::npos) s.kind = "terminate";
                else if (line.find("FAIL-FAST") != std::string::npos) s.kind = "fail-fast";
                else if (line.find("CRASH") != std::string::npos) s.kind = "crash";
            }
            continue;
        }
        if (inStack) {
            Frame f;
            if (ParseFrame(line, f)) {
                s.frames.push_back(f);
                continue;
            }
            inStack = false;
        }
        if (line == "Stack trace:") {
            inStack = true;
        } else if (StartsWith(line, "Exception:")) {
            const std::string what = Trim(line.substr(10));
            AddValue(s.value, what);
            const size_t open = what.find('(');
            if (open != std::string::npos && s.type.empty()) {
                size_t e = open + 1;
                while (e < what.size() && what[e] != ' ' && what[e] != ')') ++e;
                s.type = what.substr(open + 1, e - open - 1);
            }
        } else if (StartsWith(line, "Fault:")) {
            AddValue(s.value, Trim(line.substr(6)));
        } else if (StartsWith(line, "Code:") || StartsWith(line, "Tag:")) {
            AddValue(s.value, line);
        } else if (StartsWith(line, "Module:") && !moduleSeen) {
            moduleSeen = true;
            std::string module, offset, rest;
            if (ModuleAt(Trim(line.substr(7)), module, offset, rest)) AddValue(s.value, "in " + module + " + " + offset);
        }
    }
    while (!s.frames.empty() && IsHandlerFrame(s.frames.front())) s.frames.erase(s.frames.begin());
    if (s.kind.empty()) s.kind = "crash";
    return s;
}

std::string Scrub(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        out += c;
        ++i;
        if (!IsSep(c)) continue;
        size_t word = 0;
        if (EqualsNoCase(text, i, "users") && i + 5 < text.size() && IsSep(text[i + 5])) word = 5;
        else if (EqualsNoCase(text, i, "home") && i + 4 < text.size() && IsSep(text[i + 4])) word = 4;
        if (word == 0) continue;
        out.append(text, i, word + 1);   // "Users/" as written
        i += word + 1;
        // The name runs to the next separator (a Windows name can hold spaces: "Anne Smith"); with none
        // close by on the line, to the first space.
        size_t name = i;
        while (name < text.size() && !IsSep(text[name]) && text[name] != '\n' && text[name] != '\r' &&
               text[name] != '"' && text[name] != '\'' && text[name] != ')' && text[name] != ':')
            ++name;
        if (name >= text.size() || !IsSep(text[name]) || name - i > 64) {
            name = i;
            while (name < text.size() && !IsSep(text[name]) && text[name] != ' ' && text[name] != '\n' &&
                   text[name] != '\r' && text[name] != '"' && text[name] != '\'' && text[name] != ')' &&
                   text[name] != ':')
                ++name;
        }
        if (name > i) {
            out += "<user>";
            i = name;
        }
    }
    return out;
}

std::string JoinCut(const std::string& head, const std::string& tail, uint64_t totalSize) {
    std::string  h = head;
    const size_t headBreak = h.rfind('\n');
    if (headBreak != std::string::npos) h.resize(headBreak + 1);
    std::string  t = tail;
    const size_t tailBreak = t.find('\n');
    if (tailBreak != std::string::npos) t = t.substr(tailBreak + 1);
    const uint64_t kept = h.size() + t.size();
    const uint64_t left = totalSize > kept ? totalSize - kept : 0;
    return h + "[... " + std::to_string(left) + " bytes left out ...]\n" + t;
}

size_t TailBytes(size_t headBytes, size_t maxBytes) {
    const size_t head = headBytes > maxBytes / 2 ? maxBytes / 2 : headBytes;
    const size_t reserve = 96;   // the marker line
    return maxBytes > head + reserve ? maxBytes - head - reserve : 0;
}

std::string HeadAndTail(const std::string& text, size_t headBytes, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    const size_t head = headBytes > maxBytes / 2 ? maxBytes / 2 : headBytes;
    return JoinCut(text.substr(0, head), text.substr(text.size() - TailBytes(headBytes, maxBytes)), text.size());
}

std::string Demangle(const std::string& symbol) {
    if (!StartsWith(symbol, "_Z")) return symbol;
    int   status = 0;
    char* name = abi::__cxa_demangle(symbol.c_str(), nullptr, nullptr, &status);
    if (status != 0 || name == nullptr) return symbol;
    std::string out(name);
    std::free(name);
    return out;
}

std::string JsonEscape(const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    std::string       out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (u < 0x20 || u == 0x7F) {
            out += "\\u00";
            out += kHex[u >> 4];
            out += kHex[u & 15];
        } else if (u >= 0x80) {
            out += '?';   // the event's strings come from the crash log (ASCII); never invalid UTF-8
        } else {
            out += c;
        }
    }
    return out;
}

std::string Rfc3339(int64_t unixSeconds) {
    int64_t days = unixSeconds / 86400;
    int64_t secs = unixSeconds % 86400;
    if (secs < 0) {
        secs += 86400;
        --days;
    }
    int64_t  y = 0;
    unsigned m = 0, d = 0;
    CivilFromDays(days, y, m, d);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02u:%02u:%02uZ", static_cast<long long>(y), m, d,
                  static_cast<unsigned>(secs / 3600), static_cast<unsigned>(secs % 3600 / 60),
                  static_cast<unsigned>(secs % 60));
    return buf;
}

std::string BuildEnvelope(const EventInfo& info, const Summary& summary, const std::vector<Attachment>& attachments) {
    const std::string type = summary.type.empty() ? summary.kind : summary.type;
    const std::string value = summary.value.empty() ? summary.kind : summary.value;

    // Sentry lists frames oldest first; the log has the innermost first.
    std::string frames;
    std::string top;
    for (size_t i = summary.frames.size(); i-- > 0;) {
        const Frame& f = summary.frames[i];
        if (!frames.empty()) frames += ",";
        frames += "{\"function\":\"" + JsonEscape(FrameName(f)) + "\",\"package\":\"" + JsonEscape(f.module) +
                  "\",\"in_app\":" + (InApp(f) ? "true" : "false") + "}";
    }
    for (const Frame& f : summary.frames) {
        if (InApp(f)) {
            top = FrameName(f);
            break;
        }
    }

    std::string event = "{";
    event += "\"event_id\":\"" + JsonEscape(info.eventId) + "\"";
    event += ",\"timestamp\":" + std::to_string(info.crashTime);
    event += ",\"platform\":\"native\",\"level\":\"fatal\",\"logger\":\"openba.crash_report\"";
    event += ",\"release\":\"" + JsonEscape(info.release) + "\",\"environment\":\"release\"";
    event += ",\"tags\":{\"os\":\"" + JsonEscape(info.os) + "\",\"kind\":\"" + JsonEscape(summary.kind) + "\"}";
    event += ",\"contexts\":{\"os\":{\"name\":\"" + JsonEscape(OsName(info.os)) + "\"}}";
    event += ",\"exception\":{\"values\":[{\"type\":\"" + JsonEscape(type) + "\",\"value\":\"" + JsonEscape(value) +
             "\",\"mechanism\":{\"type\":\"crash_log\",\"handled\":false}";
    if (!frames.empty()) event += ",\"stacktrace\":{\"frames\":[" + frames + "]}";
    event += "}]}";
    event += ",\"fingerprint\":[\"openba\",\"" + JsonEscape(summary.kind) + "\",\"" + JsonEscape(type) + "\",\"" +
             JsonEscape(top) + "\"]";
    event += "}";

    std::string out = "{\"event_id\":\"" + JsonEscape(info.eventId) + "\",\"dsn\":\"" + JsonEscape(info.dsn) +
                      "\",\"sent_at\":\"" + Rfc3339(info.sentTime) +
                      "\",\"sdk\":{\"name\":\"openba.crash_report\",\"version\":\"1\"}}\n";
    out += Item("{\"type\":\"event\",\"length\":" + std::to_string(event.size()) + ",\"content_type\":\"application/json\"}",
                event);
    for (const Attachment& a : attachments) {
        out += Item("{\"type\":\"attachment\",\"length\":" + std::to_string(a.data.size()) + ",\"filename\":\"" +
                        JsonEscape(a.filename) + "\",\"content_type\":\"text/plain\"}",
                    a.data);
    }
    return out;
}

}  // namespace CrashReport

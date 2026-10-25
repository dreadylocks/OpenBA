#pragma once
// ============================================================================
// src/ext/crash_report/report.h -- NOT RETAIL. ext.crash_reports: what a crash report says
// ============================================================================
// The pure half of ext.crash_reports (crash_report.h has the files, the settings and the sending):
// a Sentry DSN taken apart, the crash log read for its summary (the fault and the stack the crash
// handlers write, src/diag/<os>/crash_handler_<os>.cpp), user names taken out of paths, a log cut
// to its start and end, and the Sentry envelope (an event plus the logs as attachments). Standard
// C++ only: tests/unit/ext_crash_report_test.cpp links it alone.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CrashReport {

// A Sentry DSN: "https://<public key>@<host>[:port][/<path>]/<project id>". Self-hosted Sentry,
// GlitchTip and Bugsink take the same.
struct Dsn {
    std::string scheme;      // "https" or "http"
    std::string publicKey;
    std::string host;        // with ":port" when the DSN names one
    std::string path;        // "/" or "/<path>/": what comes before the project id
    std::string projectId;
};
bool ParseDsn(const std::string& text, Dsn& out);
// Where an envelope goes: <scheme>://<host><path>api/<project id>/envelope/ with the key in the query.
std::string EnvelopeUrl(const Dsn& dsn);

struct Frame {
    std::string module;      // "OpenBA.exe", "OpenBA-engine", "???"
    std::string offset;      // "0x23AB74" (from the module's base), "" when unknown
    std::string function;    // the symbol the POSIX handlers print (mangled), "" on Windows
};

// What the crash log says. Frames innermost first, as the log lists them, without the crash
// handler's own (the signal trampoline, CrashHandler, PrintStackTrace).
struct Summary {
    std::string        kind;    // "crash", "pure virtual call", "terminate", "fail-fast", "hang"
    std::string        type;    // "SIGSEGV", "EXCEPTION_ACCESS_VIOLATION", ...; "" when the log names none
    std::string        value;   // the fault: "signal 11 (...) address=0x28; read of 0x28; in OpenBA.exe + 0x1234"
    std::vector<Frame> frames;
};
Summary Summarize(const std::string& crashLog);

// The user's name out of paths: "/Users/<name>", "\Users\<name>" and "/home/<name>" become
// ".../<user>" (any case of "Users" / "home"); a name with spaces too, when a separator follows it.
std::string Scrub(const std::string& text);

// A log's start and end joined: `head` cut back to its last line break, `tail` cut after its first,
// and one line between saying how many of the log's `totalSize` bytes were left out.
std::string JoinCut(const std::string& head, const std::string& tail, uint64_t totalSize);
// `text` when it fits in `maxBytes`; otherwise JoinCut of its first ~`headBytes` (at most half) and
// enough of its end to make about `maxBytes`.
std::string HeadAndTail(const std::string& text, size_t headBytes, size_t maxBytes);
// How much of the end HeadAndTail keeps beside `headBytes` (the reader of a file needs it).
size_t TailBytes(size_t headBytes, size_t maxBytes);

// "_ZN6Object12ExternalList4FindE..." -> "Object::ExternalList::Find(...)"; anything else unchanged.
std::string Demangle(const std::string& symbol);

struct Attachment {
    std::string filename;
    std::string data;
};

struct EventInfo {
    std::string eventId;      // 32 lowercase hex digits
    int64_t     crashTime = 0; // seconds since 1970 (UTC): when the crash log was written
    int64_t     sentTime = 0;  // seconds since 1970 (UTC): now
    std::string release;      // "openba@1.1.2"
    std::string os;           // "windows", "macos", "linux"
    std::string dsn;          // the DSN as configured (the envelope header names it)
};

// The envelope (Sentry's "application/x-sentry-envelope"): the header, one event item (level fatal,
// the summary as its exception and stack, a fingerprint of the fault and the innermost frame of our
// own), then each attachment as a text item.
std::string BuildEnvelope(const EventInfo& info, const Summary& summary, const std::vector<Attachment>& attachments);

std::string JsonEscape(const std::string& s);
// "2026-10-25T09:30:00Z"
std::string Rfc3339(int64_t unixSeconds);

}  // namespace CrashReport

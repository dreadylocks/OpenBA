// ============================================================================
// tests/unit/ext_crash_report_test.cpp -- ext.crash_reports: what a crash report says (report.h)
// ============================================================================
// The DSN, the crash log's summary (the POSIX and the Windows handlers' formats), the user's name
// out of paths, the log cut, the envelope. Runs on every platform (no files, no network).
// ============================================================================
#include "unit.h"

#include <string>
#include <vector>

#include "ext/crash_report/report.h"
#include "platform/common/websocket_frames.h"

namespace CR = CrashReport;

namespace {

// The Mac's crash log of 2026-10-24 17:01 (registers cut).
const char kMacLog[] =
    "=== STANDALONE CRASH (ARM64) ===\n"
    "Exception: signal 11 (SIGSEGV (ACCESS_VIOLATION)) address=0x28\n"
    "PC=0000000102A0AB74 SP=000000016D836E30 LR=0000000102A0AB74 FP=000000016D836E80\n"
    "Module: OpenBA-engine + 0x23AB74  (base=0x1027d0000)  _ZNK6Object12ExternalList4FindERKN6System8FilenameEj+0xE8\n"
    "X0=0000000000000000\n"
    "Stack trace:\n"
    "  #0  OpenBA-engine          + 0x30C354    (_ZN12_GLOBAL__N_115PrintStackTraceEv+0x74)\n"
    "  #1  OpenBA-engine          + 0x30C004    (_ZN12_GLOBAL__N_112CrashHandlerEiP9__siginfoPv+0x1D8)\n"
    "  #2  libsystem_platform.dylib + 0x3744      (_sigtramp+0x38)\n"
    "  #3  OpenBA-engine          + 0x23AB74    (_ZNK6Object12ExternalList4FindERKN6System8FilenameEj+0xE8)\n"
    "  #4  OpenBA-engine          + 0xF45A0     (_Z18GameContainer_InitP13GameContainerPKN6Object12ExternalListE+0x2C)\n"
    "  #5  libsystem_pthread.dylib + 0x6C58      (_pthread_start+0x88)\n"
    "=== END CRASH ===\n";

const char kWinLog[] =
    "=== STANDALONE CRASH ===\n"
    "Exception: 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)\n"
    "Address:   00007FF6A1B2C3D4\n"
    "RAX=0000000000000000 RCX=0000000000000000 RDX=0000000000000000 RBX=0000000000000000\n"
    "Module:    OpenBA.exe + 0x1C3D4  (base=00007FF6A1B10000)\n"
    "Fault: read of 0000000000000028\n"
    "Stack trace:\n"
    "  #0  OpenBA.exe             + 0x1C3D4     (RIP=00007FF6A1B2C3D4 RSP=000000A1B2C3D000)\n"
    "  #1  ???                    @ 0000000000001234          (RSP=000000A1B2C3D040)\n"
    "  #2  KERNEL32.DLL           + 0x17344     (RIP=00007FFB12347344 RSP=000000A1B2C3D080)\n"
    "=== END CRASH ===\n";

}  // namespace

UNIT_TEST(crash_report_parses_dsns) {
    CR::Dsn d;
    CHECK(CR::ParseDsn("https://abc123@o4507.ingest.us.sentry.io/4508", d));
    CHECK_STREQ(d.scheme.c_str(), "https");
    CHECK_STREQ(d.publicKey.c_str(), "abc123");
    CHECK_STREQ(d.host.c_str(), "o4507.ingest.us.sentry.io");
    CHECK_STREQ(d.path.c_str(), "/");
    CHECK_STREQ(d.projectId.c_str(), "4508");
    CHECK_STREQ(CR::EnvelopeUrl(d).c_str(),
                "https://o4507.ingest.us.sentry.io/api/4508/envelope/?sentry_key=abc123&sentry_version=7"
                "&sentry_client=openba-crash-report/1");

    CHECK(CR::ParseDsn("http://key:secret@127.0.0.1:9000/glitchtip/7", d));   // self-hosted, an old secret
    CHECK_STREQ(d.publicKey.c_str(), "key");
    CHECK_STREQ(d.host.c_str(), "127.0.0.1:9000");
    CHECK_STREQ(d.path.c_str(), "/glitchtip/");
    CHECK_STREQ(CR::EnvelopeUrl(d).substr(0, 47).c_str(), "http://127.0.0.1:9000/glitchtip/api/7/envelope/");

    CHECK(!CR::ParseDsn("", d));
    CHECK(!CR::ParseDsn("https://o4507.ingest.sentry.io/4508", d));     // no key
    CHECK(!CR::ParseDsn("https://key@host/", d));                     // no project
    CHECK(!CR::ParseDsn("wss://key@host/1", d));
    CHECK(!CR::ParseDsn("https://key@host/1 2", d));
}

UNIT_TEST(crash_report_summarizes_a_posix_crash_log) {
    const CR::Summary s = CR::Summarize(kMacLog);
    CHECK_STREQ(s.kind.c_str(), "crash");
    CHECK_STREQ(s.type.c_str(), "SIGSEGV");
    CHECK(s.value.find("address=0x28") != std::string::npos);
    CHECK(s.value.find("in OpenBA-engine + 0x23AB74") != std::string::npos);
    // The handler's own frames (PrintStackTrace, CrashHandler, _sigtramp) are dropped.
    CHECK_EQ(s.frames.size(), size_t{3});
    CHECK_STREQ(s.frames[0].module.c_str(), "OpenBA-engine");
    CHECK_STREQ(s.frames[0].offset.c_str(), "0x23AB74");
    CHECK_STREQ(s.frames[0].function.c_str(), "_ZNK6Object12ExternalList4FindERKN6System8FilenameEj");
    CHECK_STREQ(s.frames[2].module.c_str(), "libsystem_pthread.dylib");
}

UNIT_TEST(crash_report_summarizes_a_windows_crash_log) {
    const CR::Summary s = CR::Summarize(kWinLog);
    CHECK_STREQ(s.type.c_str(), "EXCEPTION_ACCESS_VIOLATION");
    CHECK(s.value.find("read of 0000000000000028") != std::string::npos);
    CHECK(s.value.find("in OpenBA.exe + 0x1C3D4") != std::string::npos);
    CHECK_EQ(s.frames.size(), size_t{3});
    CHECK_STREQ(s.frames[0].offset.c_str(), "0x1C3D4");
    CHECK(s.frames[0].function.empty());   // "(RIP=...)" is not a symbol
    CHECK_STREQ(s.frames[1].module.c_str(), "???");
    CHECK_STREQ(CR::Summarize("").kind.c_str(), "crash");
}

UNIT_TEST(crash_report_scrubs_user_names) {
    CHECK_STREQ(CR::Scrub("/Users/sgstephans/Documents\\Banished\\settings.cfg").c_str(),
                "/Users/<user>/Documents\\Banished\\settings.cfg");
    CHECK_STREQ(CR::Scrub("C:\\Users\\Anne Smith\\Documents\\x").c_str(), "C:\\Users\\<user>\\Documents\\x");
    CHECK_STREQ(CR::Scrub("home is /home/bob and more words").c_str(), "home is /home/<user> and more words");
    CHECK_STREQ(CR::Scrub("opened C:\\users\\anne\\OneDrive and /home/bob/.local").c_str(),
                "opened C:\\users\\<user>\\OneDrive and /home/<user>/.local");
    CHECK_STREQ(CR::Scrub("no paths here, /usr/lib, Users/x").c_str(), "no paths here, /usr/lib, Users/x");
    CHECK_STREQ(CR::Scrub("/Users/").c_str(), "/Users/");
}

UNIT_TEST(crash_report_cuts_a_long_log_at_line_breaks) {
    std::string text;
    for (int i = 0; i < 1000; ++i) text += "line " + std::to_string(i) + "\n";
    CHECK_STREQ(CR::HeadAndTail(text, 100, text.size()).c_str(), text.c_str());
    const std::string cut = CR::HeadAndTail(text, 100, 400);
    CHECK(cut.size() <= 400);
    CHECK_STREQ(cut.substr(0, 7).c_str(), "line 0\n");
    CHECK(cut.find("bytes left out ...]\n") != std::string::npos);
    CHECK(cut.compare(cut.size() - 9, 9, "line 999\n") == 0);
    const size_t marker = cut.find("[... ");
    CHECK(marker != std::string::npos && cut[marker - 1] == '\n');   // the head ends at a line break
}

UNIT_TEST(crash_report_demangles_symbols) {
    CHECK_STREQ(CR::Demangle("_ZNK6Object12ExternalList4FindERKN6System8FilenameEj").c_str(),
                "Object::ExternalList::Find(System::Filename const&, unsigned int) const");
    CHECK_STREQ(CR::Demangle("_sigtramp").c_str(), "_sigtramp");
}

UNIT_TEST(crash_report_builds_an_envelope) {
    CR::EventInfo e;
    e.eventId = "0123456789abcdef0123456789abcdef";
    e.crashTime = 1761325260;
    e.sentTime = 1761325300;
    e.release = "openba@1.1.2";
    e.os = "macos";
    e.dsn = "https://abc@o1.ingest.sentry.io/2";
    const std::vector<CR::Attachment> files = {{"standalone_crash.log", "a\nb"}, {"standalone_error.log", ""}};
    const std::string env = CR::BuildEnvelope(e, CR::Summarize(kMacLog), files);

    // Header line, then each item's header and exactly `length` bytes, each followed by a newline.
    size_t            pos = env.find('\n');
    const std::string header = env.substr(0, pos);
    CHECK(header.find("\"event_id\":\"0123456789abcdef0123456789abcdef\"") != std::string::npos);
    CHECK(header.find("\"sent_at\":\"2025-10-24T17:01:40Z\"") != std::string::npos);
    CHECK(header.find("\"dsn\":\"https://abc@o1.ingest.sentry.io/2\"") != std::string::npos);
    ++pos;
    std::vector<std::string> types;
    std::vector<std::string> payloads;
    while (pos < env.size()) {
        const size_t      end = env.find('\n', pos);
        const std::string item = env.substr(pos, end - pos);
        const size_t      at = item.find("\"length\":") + 9;
        const size_t      length = static_cast<size_t>(std::stoul(item.substr(at)));
        types.push_back(item.substr(9, item.find('"', 9) - 9));
        payloads.push_back(env.substr(end + 1, length));
        CHECK(env[end + 1 + length] == '\n');
        pos = end + 1 + length + 1;
    }
    CHECK_EQ(types.size(), size_t{3});
    CHECK_STREQ(types[0].c_str(), "event");
    CHECK_STREQ(types[1].c_str(), "attachment");
    CHECK_STREQ(payloads[1].c_str(), "a\nb");
    CHECK(payloads[2].empty());
    const std::string& ev = payloads[0];
    CHECK(ev.find("\"level\":\"fatal\"") != std::string::npos);
    CHECK(ev.find("\"type\":\"SIGSEGV\"") != std::string::npos);
    CHECK(ev.find("\"release\":\"openba@1.1.2\"") != std::string::npos);
    CHECK(ev.find("\"timestamp\":1761325260") != std::string::npos);
    // Oldest frame first; the innermost of ours names the fingerprint.
    CHECK(ev.find("_pthread_start") < ev.find("Object::ExternalList::Find"));
    CHECK(ev.find("\"fingerprint\":[\"openba\",\"crash\",\"SIGSEGV\",\"Object::ExternalList::Find(System::Filename "
                  "const&, unsigned int) const\"]") != std::string::npos);
}

UNIT_TEST(crash_report_json_and_time) {
    CHECK_STREQ(CR::JsonEscape("a\"b\\c\nd\x01\xc3\xa9").c_str(), "a\\\"b\\\\c\\nd\\u0001??");
    CHECK_STREQ(CR::Rfc3339(0).c_str(), "1970-01-01T00:00:00Z");
    CHECK_STREQ(CR::Rfc3339(951782400).c_str(), "2000-02-29T00:00:00Z");
}

UNIT_TEST(http_post_request_and_status) {
    namespace WSF = Platform::WebSocketFrames;
    WSF::Url    u;
    std::string error;
    CHECK(WSF::ParseHttpUrl("https://o1.ingest.sentry.io/api/2/envelope/?sentry_key=k", u, error));
    CHECK(u.secure);
    CHECK_EQ(u.port, uint16_t{443});
    CHECK_STREQ(u.target.c_str(), "/api/2/envelope/?sentry_key=k");
    WSF::Url bad;
    CHECK(!WSF::ParseHttpUrl("ftp://host/x", bad, error));
    CHECK(!WSF::ParseHttpUrl("https://user@host/x", bad, error));
    CHECK(WSF::ParseHttpUrl("http://127.0.0.1:8799/x", u, error));
    CHECK(!u.secure);
    CHECK_STREQ(u.hostHeader.c_str(), "127.0.0.1:8799");
    CHECK_STREQ(WSF::PostRequest(u, "text/plain", 5).c_str(),
                "POST /x HTTP/1.1\r\nHost: 127.0.0.1:8799\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n"
                "Connection: close\r\nUser-Agent: OpenBA/1\r\n\r\n");
    CHECK_EQ(WSF::HttpStatus("HTTP/1.1 200 OK\r\n"), 200);
    CHECK_EQ(WSF::HttpStatus("HTTP/2 429 \r\n"), 429);
    CHECK_EQ(WSF::HttpStatus("SSH-2.0"), 0);
    CHECK_EQ(WSF::HttpStatus("HTTP/1.1 2x0"), 0);
}

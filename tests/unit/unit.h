#pragma once
// Minimal test harness for tests/unit (ENDGAME B). No dependencies.
// tests/unit/README.md describes the build, the macros and the skip mechanism.
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace unit {
struct Test { const char* name; void (*fn)(); Test* next; };
inline Test*& Head() { static Test* head = nullptr; return head; }
inline int& Failures() { static int n = 0; return n; }
// Skips: a test that cannot run on this host (no game install, a compile-time switch off)
// says so with UNIT_SKIP and returns. main.cpp counts them and lists them at the end so a
// skipped test is never mistaken for a passed one; the exit code counts failures only.
struct Skip { const char* name; const char* reason; Skip* next; };
inline Skip*& Skips() { static Skip* head = nullptr; return head; }
inline int& Skipped() { static int n = 0; return n; }
inline const char*& Current() { static const char* name = nullptr; return name; }
inline void RecordSkip(const char* reason) {
    Skip* s = new Skip{Current() ? Current() : "?", reason, nullptr};
    Skip** tail = &Skips();
    while (*tail) tail = &(*tail)->next;
    *tail = s;
    ++Skipped();
    std::printf("skip %s: %s\n", s->name, reason);
}
struct Register { Register(Test* t) { t->next = Head(); Head() = t; } };
}  // namespace unit

#define UNIT_TEST(Name) \
    static void Name(); \
    static unit::Test Name##_test{#Name, &Name, nullptr}; \
    static unit::Register Name##_reg(&Name##_test); \
    static void Name()

// Records the skip (counter + reason), prints `skip <name>: <reason>`, returns from the test.
#define UNIT_SKIP(reason) do { unit::RecordSkip(reason); return; } while (0)

#define CHECK(cond) do { if (!(cond)) { \
    std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++unit::Failures(); } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
    std::printf("  FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); ++unit::Failures(); } } while (0)
// Compare within one function call so temporary strings keep their c_str() alive
// until both arguments have been compared on every compiler.
namespace unit {
inline void CheckStrings(const char* a, const char* b, const char* file, int line,
                         const char* exprA, const char* exprB) {
    if (!((a == nullptr && b == nullptr) || (a && b && std::strcmp(a, b) == 0))) {
        std::printf("  FAIL %s:%d: %s == %s (\"%s\" vs \"%s\")\n", file, line,
                    exprA, exprB, a ? a : "(null)", b ? b : "(null)");
        ++Failures();
    }
}
}  // namespace unit
#define CHECK_STREQ(a, b) ::unit::CheckStrings((a), (b), __FILE__, __LINE__, #a, #b)

// ============================================================================
// tests/unit/cpu_roster_test.cpp -- src/platform/common/cpu/roster: the thread-count rules
// and the `sys.cpu_limit` mask (block CPULIMIT)
// ============================================================================
// (i)   the three roster rules at n = 1..16, with and without the render pipe,
//       against the formulas, and THREADRESEARCH §7.4's table rows n = 4 / 6 / 8
//       exactly
// (ii)  the mask grammar: 0x55, 85, 0, junk
// (iii) the clamp to a system mask (DecideLimit)
// ============================================================================
#include "unit.h"
#include "platform/common/cpu/roster.h"

#include <cstdint>
#include <string>

namespace {

int ClampRef(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

UNIT_TEST(cpu_roster_rules_1_to_16) {
    for (int n = 1; n <= 16; ++n) {
        CHECK_EQ(Cpu::RenderThread(n), n >= 3);
        CHECK_EQ(Cpu::PathWorkers(n, false), ClampRef(n - 2, 1, 4));
        CHECK_EQ(Cpu::PathWorkers(n, true), ClampRef(n - 3, 1, 4));
        CHECK_EQ(Cpu::CullHelpers(n, false), ClampRef(n - 4, 0, 2));
        CHECK_EQ(Cpu::CullHelpers(n, true), ClampRef(n - 5, 0, 2));
        CHECK_EQ(Cpu::ParallelWorkers(n), ClampRef(n - 1, 0, Cpu::kMaxParallelWorkers));
        // Every count stays inside its cap, and a path worker always exists.
        CHECK(Cpu::PathWorkers(n, false) >= 1 && Cpu::PathWorkers(n, false) <= Cpu::kMaxPathWorkers);
        CHECK(Cpu::CullHelpers(n, true) >= 0 && Cpu::CullHelpers(n, true) <= Cpu::kMaxCullHelpers);
    }
    // Small n written out (the fallback rule: n < 3 is the sequential path).
    CHECK_EQ(Cpu::RenderThread(1), false);
    CHECK_EQ(Cpu::RenderThread(2), false);
    CHECK_EQ(Cpu::RenderThread(3), true);
    CHECK_EQ(Cpu::PathWorkers(1, true), 1);
    CHECK_EQ(Cpu::PathWorkers(2, false), 1);
    CHECK_EQ(Cpu::PathWorkers(0, false), 1);   // an unknown count still gets one worker
    CHECK_EQ(Cpu::CullHelpers(0, false), 0);
}

UNIT_TEST(cpu_roster_threadresearch_table) {
    // THREADRESEARCH §7.4: | row | n = 4 | n = 6 | n = 8 |
    // ext.render_pipe: 1 | 1 | 1
    CHECK_EQ(Cpu::RenderThread(4), true);
    CHECK_EQ(Cpu::RenderThread(6), true);
    CHECK_EQ(Cpu::RenderThread(8), true);
    // path workers: 1 (2 with render_pipe off) | 3 (4) | 4 (4)
    CHECK_EQ(Cpu::PathWorkers(4, true), 1);
    CHECK_EQ(Cpu::PathWorkers(4, false), 2);
    CHECK_EQ(Cpu::PathWorkers(6, true), 3);
    CHECK_EQ(Cpu::PathWorkers(6, false), 4);
    CHECK_EQ(Cpu::PathWorkers(8, true), 4);
    CHECK_EQ(Cpu::PathWorkers(8, false), 4);
    // cull helpers (render_pipe on): 0 | 1 | 2
    CHECK_EQ(Cpu::CullHelpers(4, true), 0);
    CHECK_EQ(Cpu::CullHelpers(6, true), 1);
    CHECK_EQ(Cpu::CullHelpers(8, true), 2);
    // and off: clamp(n - 4, 0, 2)
    CHECK_EQ(Cpu::CullHelpers(4, false), 0);
    CHECK_EQ(Cpu::CullHelpers(6, false), 2);
    CHECK_EQ(Cpu::CullHelpers(8, false), 2);
}

UNIT_TEST(cpu_mask_parse) {
    uint64_t m = 0xdead;
    CHECK(Cpu::ParseMask("0x55", &m));
    CHECK_EQ(m, uint64_t{0x55});
    m = 0xdead;
    CHECK(Cpu::ParseMask("85", &m));
    CHECK_EQ(m, uint64_t{85});
    m = 0xdead;
    CHECK(Cpu::ParseMask("0", &m));
    CHECK_EQ(m, uint64_t{0});
    CHECK(Cpu::ParseMask("0X0f", &m));
    CHECK_EQ(m, uint64_t{0xf});
    CHECK(Cpu::ParseMask("  0xFF \r", &m));
    CHECK_EQ(m, uint64_t{0xff});
    CHECK(Cpu::ParseMask("0xffffffffffffffff", &m));
    CHECK_EQ(m, UINT64_MAX);
    CHECK(Cpu::ParseMask("18446744073709551615", &m));
    CHECK_EQ(m, UINT64_MAX);

    // Junk: `m` untouched.
    m = 0x1234;
    CHECK(!Cpu::ParseMask("", &m));
    CHECK(!Cpu::ParseMask("   ", &m));
    CHECK(!Cpu::ParseMask("0x", &m));
    CHECK(!Cpu::ParseMask("0xg1", &m));
    CHECK(!Cpu::ParseMask("abc", &m));
    CHECK(!Cpu::ParseMask("-1", &m));
    CHECK(!Cpu::ParseMask("+5", &m));
    CHECK(!Cpu::ParseMask("12 34", &m));
    CHECK(!Cpu::ParseMask("0x55,", &m));
    CHECK(!Cpu::ParseMask("0x1ffffffffffffffff", &m));    // 17 hex digits
    CHECK(!Cpu::ParseMask("18446744073709551616", &m));   // UINT64_MAX + 1
    CHECK_EQ(m, uint64_t{0x1234});

    CHECK_EQ(Cpu::MaskPopcount(0), 0);
    CHECK_EQ(Cpu::MaskPopcount(0x55), 4);
    CHECK_EQ(Cpu::MaskPopcount(UINT64_MAX), 64);
}

UNIT_TEST(cpu_limit_clamp) {
    const uint64_t sys16 = 0xffff;   // a 16-CPU system

    Cpu::LimitDecision d = Cpu::DecideLimit(false, "", sys16);   // absent
    CHECK(d.limit == Cpu::Limit::None);
    CHECK_EQ(d.apply, uint64_t{0});
    CHECK(!d.junk);

    d = Cpu::DecideLimit(true, "0", sys16);   // 0 = unrestricted
    CHECK(d.limit == Cpu::Limit::None);
    CHECK_EQ(d.apply, uint64_t{0});
    CHECK(!d.junk && !d.empty);

    d = Cpu::DecideLimit(true, "0x55", sys16);   // P4 on SMT-adjacent numbering
    CHECK(d.limit == Cpu::Limit::Applied);
    CHECK_EQ(d.apply, uint64_t{0x55});
    CHECK_EQ(d.requested, uint64_t{0x55});

    d = Cpu::DecideLimit(true, "15", sys16);   // S4 (CPUs 0-3), decimal
    CHECK(d.limit == Cpu::Limit::Applied);
    CHECK_EQ(d.apply, uint64_t{0xf});

    d = Cpu::DecideLimit(true, "0x3000f", sys16);   // bits 16, 17 outside: clamped
    CHECK(d.limit == Cpu::Limit::Clamped);
    CHECK_EQ(d.apply, uint64_t{0xf});
    CHECK_EQ(d.requested, uint64_t{0x3000f});

    d = Cpu::DecideLimit(true, "0xffff0000", sys16);   // nothing inside: ignored
    CHECK(d.limit == Cpu::Limit::None);
    CHECK(d.empty);
    CHECK_EQ(d.apply, uint64_t{0});

    d = Cpu::DecideLimit(true, "four", sys16);   // junk: ignored
    CHECK(d.limit == Cpu::Limit::None);
    CHECK(d.junk);
    CHECK_EQ(d.requested, uint64_t{0});

    d = Cpu::DecideLimit(true, "0xff", 0);   // system mask unknown: nothing to clamp into
    CHECK(d.limit == Cpu::Limit::None);
    CHECK(d.empty);

    CHECK_STREQ(Cpu::LimitName(Cpu::Limit::None), "none");
    CHECK_STREQ(Cpu::LimitName(Cpu::Limit::Applied), "applied");
    CHECK_STREQ(Cpu::LimitName(Cpu::Limit::Clamped), "clamped");
}

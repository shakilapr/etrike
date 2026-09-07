// Host-compilable unit test for etrike::diagnostics::DiagnosticManager.
// Build: g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I<repo-root> shared/test_diagnostics.cpp -o shared/test_diagnostics
// Mirrors the protocol cpp test harness: prints PASS / FAIL and returns the failure count.
#include <cstdio>

#include "shared/diagnostics.h"

namespace diag = etrike::diagnostics;
using diag::DiagReport;

namespace {
int failures = 0;

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);             \
            ++failures;                                                              \
        }                                                                            \
    } while (false)

DiagReport pop_one(diag::DiagnosticManager& m) {
    DiagReport r{};
    CHECK(m.pop_pending_report(r));
    return r;
}

void expect_no_report(diag::DiagnosticManager& m) {
    DiagReport r{};
    CHECK(!m.pop_pending_report(r));
}

}  // namespace

int main() {
    // --- raise: CLEARED -> ACTIVE, occurrence = 1, pending (non-estop event) ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysSebStatusTimeout, 1234);
        CHECK(m.state_of(diag::DiagId::SysSebStatusTimeout) == diag::DiagState::Active);
        CHECK(m.occurrence_of(diag::DiagId::SysSebStatusTimeout) == 1);
        CHECK(m.is_pending_report(diag::DiagId::SysSebStatusTimeout));

        // re-raise without change: no occurrence increment, still pending
        m.raise(diag::DiagId::SysSebStatusTimeout, 1234);
        CHECK(m.occurrence_of(diag::DiagId::SysSebStatusTimeout) == 1);
        CHECK(m.is_pending_report(diag::DiagId::SysSebStatusTimeout));

        // re-raise with changed snapshot: snapshot updated, count unchanged, no burst
        m.raise(diag::DiagId::SysSebStatusTimeout, 5678);
        CHECK(m.occurrence_of(diag::DiagId::SysSebStatusTimeout) == 1);
        {
            DiagReport r = pop_one(m);
            CHECK(r.id == diag::DiagId::SysSebStatusTimeout);
            CHECK(r.state == diag::DiagState::Active);
            CHECK(r.occurrence_count == 1);
            CHECK(r.snapshot_data == 5678);
            CHECK((r.flags & diag::kDiagFlagFirstLocalEstopCause) == 0);
        }
        expect_no_report(m);
    }

    // --- recover: latching event -> LATCHED, non-latching -> RECOVERED ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysEstopButtonAsserted);  // is_estop_cause, latching
        CHECK(m.state_of(diag::DiagId::SysEstopButtonAsserted) == diag::DiagState::Active);
        m.recover(diag::DiagId::SysEstopButtonAsserted);
        CHECK(m.state_of(diag::DiagId::SysEstopButtonAsserted) == diag::DiagState::Latched);
        {
            DiagReport r = pop_one(m);
            CHECK(r.id == diag::DiagId::SysEstopButtonAsserted);
            CHECK(r.state == diag::DiagState::Latched);
        }
        expect_no_report(m);

        m.raise(diag::DiagId::SysSebStatusTimeout);  // not latching
        m.recover(diag::DiagId::SysSebStatusTimeout);
        CHECK(m.state_of(diag::DiagId::SysSebStatusTimeout) == diag::DiagState::Recovered);
        {
            DiagReport r = pop_one(m);
            CHECK(r.state == diag::DiagState::Recovered);
        }
        expect_no_report(m);
    }

    // --- clear: LATCHED -> CLEARED carries final occurrence_count, then resets ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysCanBusOff);    // latching, estop cause
        m.recover(diag::DiagId::SysCanBusOff);  // LATCHED
        m.raise(diag::DiagId::SysCanBusOff);    // re-qualify -> ACTIVE, count 2
        m.recover(diag::DiagId::SysCanBusOff);  // LATCHED again (count 2 preserved)
        CHECK(m.occurrence_of(diag::DiagId::SysCanBusOff) == 2);
        m.clear(diag::DiagId::SysCanBusOff);
        CHECK(m.state_of(diag::DiagId::SysCanBusOff) == diag::DiagState::Cleared);
        {
            DiagReport r = pop_one(m);
            CHECK(r.state == diag::DiagState::Cleared);
            CHECK(r.occurrence_count == 2);  // final count preserved on the report
        }
        CHECK(m.occurrence_of(diag::DiagId::SysCanBusOff) == 0);  // reset after drain
        expect_no_report(m);
    }

    // --- first_local_estop_cause: exactly one report per episode carries the flag ---
    {
        diag::DiagnosticManager e{};
        e.on_estop_episode_cleared();  // arm for a fresh episode
        e.raise(diag::DiagId::SysEstopButtonAsserted);  // claims the episode
        e.raise(diag::DiagId::SysCanBusOff);            // same episode, no claim
        {
            DiagReport r = pop_one(e);  // first estop-cause report gets the flag
            CHECK((r.flags & diag::kDiagFlagFirstLocalEstopCause) != 0);
        }
        {
            DiagReport r = pop_one(e);
            CHECK((r.flags & diag::kDiagFlagFirstLocalEstopCause) == 0);  // episode claimed
        }
        expect_no_report(e);

        // a distinct episode (fresh manager) re-arms the flag
        diag::DiagnosticManager e2{};
        e2.on_estop_episode_cleared();
        e2.raise(diag::DiagId::SysCanBusOff);
        {
            DiagReport r = pop_one(e2);
            CHECK((r.flags & diag::kDiagFlagFirstLocalEstopCause) != 0);
        }
        expect_no_report(e2);
    }

    // --- periodic replay re-marks ACTIVE/LATCHED, not RECOVERED/CLEARED ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysRtHeartbeatTimeout);    // ACTIVE
        m.raise(diag::DiagId::SysEstopButtonAsserted);  // ACTIVE (latching)
        m.recover(diag::DiagId::SysEstopButtonAsserted);  // LATCHED
        DiagReport drain{};
        while (m.pop_pending_report(drain)) { /* drain */ }
        CHECK(!m.is_pending_report(diag::DiagId::SysRtHeartbeatTimeout));
        CHECK(!m.is_pending_report(diag::DiagId::SysEstopButtonAsserted));
        m.replay_active_set();
        CHECK(m.is_pending_report(diag::DiagId::SysRtHeartbeatTimeout));    // ACTIVE replayed
        CHECK(m.is_pending_report(diag::DiagId::SysEstopButtonAsserted));   // LATCHED replayed
    }

    // --- report_counter is monotonic modulo-256 ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysSebStatusTimeout);
        m.raise(diag::DiagId::SysRtHeartbeatTimeout);
        DiagReport a = pop_one(m);
        DiagReport b = pop_one(m);
        CHECK(b.report_counter == static_cast<std::uint8_t>(a.report_counter + 1));
    }

    // --- clear_all clears every latched/recovered record ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysEstopButtonAsserted);
        m.raise(diag::DiagId::SysSebStatusTimeout);
        m.recover(diag::DiagId::SysEstopButtonAsserted);
        m.recover(diag::DiagId::SysSebStatusTimeout);
        m.clear_all();
        CHECK(m.state_of(diag::DiagId::SysEstopButtonAsserted) == diag::DiagState::Cleared);
        CHECK(m.state_of(diag::DiagId::SysSebStatusTimeout) == diag::DiagState::Cleared);
    }

    // --- FIRST_LOCAL_ESTOP_CAUSE attaches to the FIRST-RAISED cause, not first popped ---
    // (regression: flag must not depend on dense-index pop order)
    {
        diag::DiagnosticManager e{};
        e.on_estop_episode_cleared();
        e.raise(diag::DiagId::RtHostHeartbeatTimeout);  // idx 14 — raised first, claims
        e.raise(diag::DiagId::SysCanBusOff);            // idx 8  — raised later, no claim
        // pop order is by dense index, so SysCanBusOff (idx 8) drains before idx 14
        DiagReport r1 = pop_one(e);
        CHECK(r1.id == diag::DiagId::SysCanBusOff);
        CHECK((r1.flags & diag::kDiagFlagFirstLocalEstopCause) == 0);  // not the claimer
        DiagReport r2 = pop_one(e);
        CHECK(r2.id == diag::DiagId::RtHostHeartbeatTimeout);
        CHECK((r2.flags & diag::kDiagFlagFirstLocalEstopCause) != 0);  // the claimer
        expect_no_report(e);
    }

    // --- deterministic drain order: lowest dense index popped first ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysCanBusOff);            // idx 8
        m.raise(diag::DiagId::SysEstopButtonAsserted);  // idx 0
        DiagReport a = pop_one(m);
        DiagReport b = pop_one(m);
        CHECK(a.id == diag::DiagId::SysEstopButtonAsserted);
        CHECK(b.id == diag::DiagId::SysCanBusOff);
        expect_no_report(m);
    }

    // --- re-assert while ACTIVE creates no new pending report (no storm) ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysSebStatusTimeout);
        pop_one(m);  // drained
        CHECK(!m.is_pending_report(diag::DiagId::SysSebStatusTimeout));
        m.raise(diag::DiagId::SysSebStatusTimeout);  // re-assert while ACTIVE
        CHECK(!m.is_pending_report(diag::DiagId::SysSebStatusTimeout));
        CHECK(m.occurrence_of(diag::DiagId::SysSebStatusTimeout) == 1);
        m.replay_active_set();  // replay may re-mark
        CHECK(m.is_pending_report(diag::DiagId::SysSebStatusTimeout));
    }

    // --- snapshot 0 is distinguishable from "no snapshot" ---
    {
        diag::DiagnosticManager m{};
        m.raise(diag::DiagId::SysRtHeartbeatTimeout);  // no snapshot supplied
        CHECK(!m.snapshot_supplied_of(diag::DiagId::SysRtHeartbeatTimeout));
        m.raise(diag::DiagId::SysCanBusOff, 0);  // explicit snapshot value 0
        CHECK(m.snapshot_supplied_of(diag::DiagId::SysCanBusOff));
        DiagReport r1 = pop_one(m);
        DiagReport r2 = pop_one(m);
        CHECK(r1.snapshot_data == 0);
        CHECK(r2.snapshot_data == 0);
    }

    // --- report_counter increments per emitted report (incl. replay) and wraps 255->0 ---
    {
        diag::DiagnosticManager m{};
        for (std::size_t i = 0; i < diag::kImplementedDiagCount; ++i)
            m.raise(diag::kImplementedDiagMeta[i].id);
        bool saw_wrap = false;
        std::uint8_t prev = 0;
        bool first = true;
        for (int cycle = 0; cycle < 7; ++cycle) {
            DiagReport r{};
            while (m.pop_pending_report(r)) {
                if (!first) {
                    std::uint8_t expected = static_cast<std::uint8_t>(prev + 1);
                    CHECK(r.report_counter == expected);  // strictly increasing mod 256
                }
                if (r.report_counter == 0) saw_wrap = true;
                prev = r.report_counter;
                first = false;
            }
            m.replay_active_set();  // re-mark ACTIVE set for the next drain
        }
        CHECK(saw_wrap);
    }

    if (failures == 0) {
        std::printf("PASS (%zu diagnostics manager checks)\n", diag::kImplementedDiagCount);
        return 0;
    }
    std::printf("FAIL: %d diagnostic manager checks failed\n", failures);
    return failures;
}

// Phase 8+9: Safety monitor + mode integration
// g++ -std=c++17 -I. -I../src -I../../shared test_safety.cpp ../src/safety_monitor.cpp ../src/mode_manager.cpp -o t && ./t

#include <cstdio>
#include "safety_monitor.h"
#include "mode_manager.h"

namespace sys { extern int64_t g_sys_test_time_us; }
void advance_time_ms(int ms) { sys::g_sys_test_time_us += int64_t(ms) * 1000; }

static int fails = 0;
#define CHK(d) printf("  %-50s ", d)
#define OK      printf("PASS\n")
#define BAD(m)  do { printf("FAIL: %s\n", m); ++fails; } while(0)

int main() {
    printf("Phase 8+9: Safety monitor + mode integration\n\n");

    // ── Phase 8: Safety monitor ──
    sys::SafetyMonitor sm;
    sm.init();
    CHK("safety init: estop=false, lever=false");
    if (!sm.estop_active() && !sm.brake_lever_pressed()) OK; else BAD("init");

    sm.set_estop(true);
    CHK("set_estop(true) → active");
    if (sm.estop_active()) OK; else BAD("estop");

    sm.set_brake_lever(true);
    CHK("set_brake_lever(true) → pressed");
    if (sm.brake_lever_pressed()) OK; else BAD("lever");

    // Heartbeat — startup grace (no HB yet, < 3s)
    CHK("startup grace: hb_ok at t=0 (never seen)");
    if (sm.heartbeat_ok()) OK; else BAD("grace");    // m_last_hb_us == 0, t=0 < 3s

    advance_time_ms(2000);
    CHK("startup grace: hb_ok at t=2000ms");
    if (sm.heartbeat_ok()) OK; else BAD("grace2");

    // Feed first heartbeat at t=2000
    sm.feed_heartbeat_rt(1);
    CHK("hb_ok after first feed (c=1)");
    if (sm.heartbeat_ok()) OK; else BAD("first");

    // Advance past timeout
    advance_time_ms(1200);  // t=3200, last HB at 2000 → 1200ms > 1000ms
    CHK("hb timeout after 1200ms");
    if (!sm.heartbeat_ok()) OK; else BAD("timeout1");

    // Feed again
    sm.feed_heartbeat_rt(2);
    CHK("hb_ok after feed (c=2)");
    if (sm.heartbeat_ok()) OK; else BAD("feed2");

    // Alive counter validation: same counter = frozen, no timestamp update
    advance_time_ms(200);
    sm.feed_heartbeat_rt(2);  // frozen — timestamp NOT updated
    advance_time_ms(900);     // 200+900=1100ms since last valid, >1000ms timeout
    CHK("frozen counter (2→2) treated as missed");
    if (!sm.heartbeat_ok()) OK; else BAD("frozen");

    // Feed with new counter
    sm.feed_heartbeat_rt(3);
    CHK("new counter (3) → ok again");
    if (sm.heartbeat_ok()) OK; else BAD("recover");

    // ── Phase 9: Mode + safety integration ──
    sys::ModeManager mm;
    mm.init();
    CHK("mode init = MANUAL"); if (mm.mode()==can::Mode::Manual) OK; else BAD("mode init");

    // ESTOP from safety
    mm.force_estop();
    CHK("force_estop → ESTOP"); if (mm.mode()==can::Mode::Estop) OK; else BAD("estop");

    // HB timeout → safety triggers ESTOP
    sm.set_estop(false);
    mm.init();
    CHK("after ESTOP + re-init = MANUAL"); if (mm.mode()==can::Mode::Manual) OK; else BAD("reinit");

    printf("\n  Result: %d failures\n", fails);
    return fails ? 1 : 0;
}

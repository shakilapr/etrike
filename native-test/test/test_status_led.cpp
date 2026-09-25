/*
 * test_status_led.cpp — Unit test suite for Onboard RGB Status LED Visual Language.
 * Validates timing waveforms, gamma breathing, overlay pips, and priority cascades
 * for both RT and SYS evaluators per docs/hardware/rgb-status-led-visual-language.md.
 */

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "status_led.h"
#include "rt_status_led.h"
#include "sys_status_led.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                             \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); } \
    else { g_fail++; printf("  FAIL: %s (%s:%d)\n",      \
               msg, __FILE__, __LINE__); }                \
} while(0)

using namespace shared::led;

// ── Test 1: Palette Lookup & Brightness Scaling ───────────────────────────
static void test_palette_and_scaling() {
    printf("[Test 1: Palette Lookup & Scaling]\n");
    Rgb red = palette_lookup(DomainColor::Red);
    CHECK(red.r == 255 && red.g == 0 && red.b == 0, "Red palette is (255, 0, 0)");

    Rgb orange = palette_lookup(DomainColor::Orange);
    CHECK(orange.r == 255 && orange.g == 35 && orange.b == 0, "Orange palette is (255, 35, 0)");

    Rgb cyan = palette_lookup(DomainColor::Cyan);
    CHECK(cyan.r == 0 && cyan.g == 180 && cyan.b == 255, "Cyan palette is (0, 180, 255)");

    Rgb scaled = scale_rgb(red, 64);
    CHECK(scaled.r == 64 && scaled.g == 0 && scaled.b == 0, "Red scaled to 64 gives (64, 0, 0)");

    Rgb off = render({DomainColor::Off, BaseCadence::Off, OverlayPip::None}, 100);
    CHECK(off.r == 0 && off.g == 0 && off.b == 0, "Off pattern renders (0, 0, 0)");
}

// ── Test 2: Base Cadence Waveforms ────────────────────────────────────────
static void test_base_cadences() {
    printf("[Test 2: Base Cadence Waveforms]\n");

    // Solid
    VisualPattern pat_solid{DomainColor::Red, BaseCadence::Solid, OverlayPip::None};
    Rgb s0 = render(pat_solid, 0);
    Rgb s500 = render(pat_solid, 500);
    CHECK(s0.r == 64 && s500.r == 64, "Solid Red remains continuous over time");

    // Remote ESTOP: 600 ms Red -> 200 ms Yellow (cycle = 800 ms)
    VisualPattern pat_remote{DomainColor::Red, BaseCadence::RemoteEstop, OverlayPip::None};
    Rgb re100 = render(pat_remote, 100);  // Phase 100 < 600 -> Red
    Rgb re599 = render(pat_remote, 599);  // Phase 599 < 600 -> Red
    Rgb re650 = render(pat_remote, 650);  // Phase 650 >= 600 -> Yellow
    Rgb re799 = render(pat_remote, 799);  // Phase 799 >= 600 -> Yellow
    Rgb re850 = render(pat_remote, 850);  // Phase 50 < 600 -> Red (wrap)

    CHECK(re100.r > 0 && re100.g == 0, "Remote ESTOP @ 100ms is Red");
    CHECK(re599.r > 0 && re599.g == 0, "Remote ESTOP @ 599ms is Red");
    CHECK(re650.r > 0 && re650.g > 0,  "Remote ESTOP @ 650ms is Yellow");
    CHECK(re799.r > 0 && re799.g > 0,  "Remote ESTOP @ 799ms is Yellow");
    CHECK(re850.r > 0 && re850.g == 0, "Remote ESTOP @ 850ms wraps back to Red");

    // Fast Blink: 150 ms ON / 150 ms OFF (cycle = 300 ms)
    VisualPattern pat_fast{DomainColor::Orange, BaseCadence::FastBlink, OverlayPip::None};
    Rgb fb50  = render(pat_fast, 50);   // ON
    Rgb fb149 = render(pat_fast, 149);  // ON
    Rgb fb150 = render(pat_fast, 150);  // OFF
    Rgb fb299 = render(pat_fast, 299);  // OFF
    Rgb fb350 = render(pat_fast, 350);  // ON (wrap)

    CHECK(fb50.r > 0,   "FastBlink @ 50ms is ON");
    CHECK(fb149.r > 0,  "FastBlink @ 149ms is ON");
    CHECK(fb150.r == 0, "FastBlink @ 150ms is OFF");
    CHECK(fb299.r == 0, "FastBlink @ 299ms is OFF");
    CHECK(fb350.r > 0,  "FastBlink @ 350ms wraps back to ON");

    // Breathe: 1800 ms cycle (900 ms up, 900 ms down) with floor
    VisualPattern pat_breathe{DomainColor::Green, BaseCadence::Breathe, OverlayPip::None};
    Rgb br_trough = render(pat_breathe, 0);     // Minimum floor
    Rgb br_peak   = render(pat_breathe, 900);   // Peak
    Rgb br_mid_up = render(pat_breathe, 450);   // Intermediate rise
    Rgb br_mid_dn = render(pat_breathe, 1350);  // Intermediate fall

    CHECK(br_trough.g > 0, "Breathe at trough retains non-zero floor (> 0)");
    CHECK(br_peak.g == 64, "Breathe at peak hits maximum scaled brightness (64)");
    CHECK(br_mid_up.g > br_trough.g && br_mid_up.g < br_peak.g, "Breathe rises monotonically in first half");
    CHECK(br_mid_dn.g > br_trough.g && br_mid_dn.g < br_peak.g, "Breathe falls monotonically in second half");
}

// ── Test 3: Temporal Overlay Pips ─────────────────────────────────────────
static void test_overlay_pips() {
    printf("[Test 3: Temporal Overlay Pips]\n");

    // White Tick: 120 ms pulse at end of 1000 ms cycle (phase 880..1000)
    VisualPattern pat_tick{DomainColor::Green, BaseCadence::Solid, OverlayPip::WhiteActivity};
    Rgb tick_base = render(pat_tick, 500);  // Base Green
    Rgb tick_pip  = render(pat_tick, 900);  // Overlay White (900 >= 880)

    CHECK(tick_base.r == 0 && tick_base.g > 0 && tick_base.b == 0, "White tick @ 500ms shows base Green");
    CHECK(tick_pip.r == 64 && tick_pip.g == 64 && tick_pip.b == 64, "White tick @ 900ms pulses White");

    // Diagnostic Pip: 200 ms pulse at end of 1200 ms cycle (phase 1000..1200)
    VisualPattern pat_pip{DomainColor::Cyan, BaseCadence::Breathe, OverlayPip::AmberModified};
    Rgb pip_base = render(pat_pip, 500);   // Base Cyan
    Rgb pip_act  = render(pat_pip, 1100);  // Overlay Amber (1100 >= 1000)

    CHECK(pip_base.r == 0 && pip_base.g > 0 && pip_base.b > 0, "Amber pip @ 500ms shows base Cyan");
    CHECK(pip_act.r > 0 && pip_act.b == 0,                     "Amber pip @ 1100ms pulses Amber (Red+Green, 0 Blue)");
}

// ── Test 4: RT Priority Cascade Evaluation ────────────────────────────────
static void test_rt_cascade() {
    printf("[Test 4: RT Priority Cascade]\n");

    // P1: Bus-Off beats everything
    rt::RtLedInputs in{};
    in.can_or_spi_hw_fault = true;
    in.estop_active = true;
    in.local_estop_cause = true;
    auto p1 = rt::evaluate_rt_led(in);
    CHECK(p1.base == DomainColor::Orange && p1.cadence == BaseCadence::FastBlink, "RT P1: Bus-Off beats ESTOP");

    // P2: Local ESTOP vs Remote ESTOP
    in.can_or_spi_hw_fault = false;
    in.estop_active = true;
    in.local_estop_cause = true;
    auto p2_local = rt::evaluate_rt_led(in);
    CHECK(p2_local.base == DomainColor::Red && p2_local.cadence == BaseCadence::Solid, "RT P2: Local ESTOP is Solid Red");

    in.local_estop_cause = false;
    auto p2_remote = rt::evaluate_rt_led(in);
    CHECK(p2_remote.base == DomainColor::Red && p2_remote.cadence == BaseCadence::RemoteEstop, "RT P2: Remote ESTOP is Red/Yellow step");

    in.seb_emergency_takeover = true;
    auto p2_seb = rt::evaluate_rt_led(in);
    CHECK(p2_seb.base == DomainColor::Red && p2_seb.cadence == BaseCadence::Solid && p2_seb.pip == OverlayPip::BlueAuthority,
          "RT P2: SEB emergency takeover is Red Solid + BlueAuthority pip");

    // P3: Actuator fault / missing
    in.estop_active = false;
    in.seb_emergency_takeover = false;
    in.actuator_fault = true;
    auto p3_fault = rt::evaluate_rt_led(in);
    CHECK(p3_fault.base == DomainColor::Cyan && p3_fault.pip == OverlayPip::RedFault, "RT P3: Actuator fault is Cyan + RedFault");

    in.actuator_fault = false;
    in.actuator_missing = true;
    in.host_cmd_nonzero = true;
    auto p3_miss_cmd = rt::evaluate_rt_led(in);
    CHECK(p3_miss_cmd.base == DomainColor::Cyan && p3_miss_cmd.pip == OverlayPip::AmberModified, "RT P3: Actuator missing with cmd is Cyan + AmberModified");

    // P4: No SYS authority
    in.actuator_missing = false;
    in.no_sys_authority = true;
    auto p4_no_sys = rt::evaluate_rt_led(in);
    CHECK(p4_no_sys.base == DomainColor::White && p4_no_sys.pip == OverlayPip::BlueAuthority, "RT P4: No SYS authority is White + BlueAuthority");

    // P5: Peer missing
    in.no_sys_authority = false;
    in.sys_hb_ok = false;
    auto p5_peer = rt::evaluate_rt_led(in);
    CHECK(p5_peer.base == DomainColor::Yellow && p5_peer.pip == OverlayPip::BlueAuthority, "RT P5: Peer missing with cmd is Yellow + BlueAuthority");

    // P6: Modes
    in.sys_hb_ok = true;
    in.host_hb_ok = true;
    in.mode_auto = false;
    in.host_cmd_nonzero = true;
    auto p6_man_conflict = rt::evaluate_rt_led(in);
    CHECK(p6_man_conflict.base == DomainColor::Purple && p6_man_conflict.pip == OverlayPip::AmberModified, "RT P6: Manual mode receiving Host cmd is Purple + AmberModified");

    in.host_cmd_nonzero = false;
    auto p6_man_idle = rt::evaluate_rt_led(in);
    CHECK(p6_man_idle.base == DomainColor::Purple && p6_man_idle.cadence == BaseCadence::Breathe, "RT P6: Manual standstill is Purple Breathe");

    in.mode_auto = true;
    in.host_cmd_nonzero = true;
    in.safety_clamp_active = false;
    auto p6_auto_drive = rt::evaluate_rt_led(in);
    CHECK(p6_auto_drive.base == DomainColor::Green && p6_auto_drive.cadence == BaseCadence::Solid && p6_auto_drive.pip == OverlayPip::WhiteActivity,
          "RT P6: Auto nominal driving is Green Solid + WhiteActivity");

    in.safety_clamp_active = true;
    auto p6_auto_clamp = rt::evaluate_rt_led(in);
    CHECK(p6_auto_clamp.base == DomainColor::Green && p6_auto_clamp.pip == OverlayPip::AmberModified, "RT P6: Auto safety clamped is Green + AmberModified");

    in.host_cmd_nonzero = false;
    in.spin_in_place_lockout = true;
    auto p6_spin = rt::evaluate_rt_led(in);
    CHECK(p6_spin.base == DomainColor::Green && p6_spin.pip == OverlayPip::AmberModified, "RT P6: Zero-speed spin-in-place lockout is Green + AmberModified");
}

// ── Test 5: SYS Priority Cascade Evaluation ───────────────────────────────
static void test_sys_cascade() {
    printf("[Test 5: SYS Priority Cascade]\n");

    sys::SysLedInputs in{};
    in.twai_bus_off = true;
    in.estop_active = true;
    auto p1 = sys::evaluate_sys_led(in);
    CHECK(p1.base == DomainColor::Orange && p1.cadence == BaseCadence::FastBlink, "SYS P1: Low CAN Bus-Off beats ESTOP");

    in.twai_bus_off = false;
    in.estop_active = true;
    in.local_estop_cause = true;
    in.mtr_ack_retrying = true;
    auto p2_mtr = sys::evaluate_sys_led(in);
    CHECK(p2_mtr.base == DomainColor::Red && p2_mtr.cadence == BaseCadence::Solid && p2_mtr.pip == OverlayPip::WhiteActivity,
          "SYS P2: Local ESTOP + MTR ACK retry is Red Solid + WhiteActivity");

    in.mtr_ack_retrying = false;
    auto p2_solid = sys::evaluate_sys_led(in);
    CHECK(p2_solid.base == DomainColor::Red && p2_solid.cadence == BaseCadence::Solid, "SYS P2: Local ESTOP is Red Solid");

    in.local_estop_cause = false;
    auto p2_remote = sys::evaluate_sys_led(in);
    CHECK(p2_remote.base == DomainColor::Red && p2_remote.cadence == BaseCadence::RemoteEstop, "SYS P2: Remote ESTOP is Red/Yellow step");

    in.estop_active = false;
    in.actuator_fault = true;
    auto p3_fault = sys::evaluate_sys_led(in);
    CHECK(p3_fault.base == DomainColor::Cyan && p3_fault.pip == OverlayPip::RedFault, "SYS P3: Latched actuator fault is Cyan + RedFault");

    in.actuator_fault = false;
    in.actuator_inhibit = true;
    in.drive_cmd_nonzero = true;
    auto p3_inh = sys::evaluate_sys_led(in);
    CHECK(p3_inh.base == DomainColor::Cyan && p3_inh.pip == OverlayPip::AmberModified, "SYS P3: Inhibit + drive request is Cyan + AmberModified");

    in.actuator_inhibit = false;
    in.drive_cmd_nonzero = false;
    in.rt_hb_ok = false;
    auto p4_rt_idle = sys::evaluate_sys_led(in);
    CHECK(p4_rt_idle.base == DomainColor::Yellow && p4_rt_idle.cadence == BaseCadence::Breathe, "SYS P4: RT missing idle is Yellow Breathe");

    in.drive_cmd_nonzero = true;
    auto p4_rt_cmd = sys::evaluate_sys_led(in);
    CHECK(p4_rt_cmd.base == DomainColor::Yellow && p4_rt_cmd.pip == OverlayPip::BlueAuthority, "SYS P4: RT missing with drive is Yellow + BlueAuthority");

    in.rt_hb_ok = true;
    in.rt_cmd_stale = true;
    in.mode_auto = true;
    auto p4_stale = sys::evaluate_sys_led(in);
    CHECK(p4_stale.base == DomainColor::Yellow && p4_stale.pip == OverlayPip::AmberModified, "SYS P4: RT cmd stale in Auto is Yellow + AmberModified");

    in.rt_cmd_stale = false;
    in.mode_auto = false;
    in.drive_cmd_nonzero = true;
    auto p6_man_conflict = sys::evaluate_sys_led(in);
    CHECK(p6_man_conflict.base == DomainColor::Purple && p6_man_conflict.pip == OverlayPip::AmberModified, "SYS P6: Manual mode receiving RT cmd is Purple + AmberModified");

    in.drive_cmd_nonzero = false;
    in.manual_active_input = true;
    auto p6_man_active = sys::evaluate_sys_led(in);
    CHECK(p6_man_active.base == DomainColor::Purple && p6_man_active.cadence == BaseCadence::Solid && p6_man_active.pip == OverlayPip::WhiteActivity,
          "SYS P6: Manual rider throttle/brake is Purple Solid + WhiteActivity");

    in.mode_auto = true;
    in.drive_cmd_nonzero = true;
    in.brake_lever_override = true;
    auto p6_brake_over = sys::evaluate_sys_led(in);
    CHECK(p6_brake_over.base == DomainColor::Green && p6_brake_over.pip == OverlayPip::AmberModified, "SYS P6: Auto brake lever override is Green + AmberModified");
}

int main() {
    printf("=== Onboard Status LED Unit Test Suite ===\n\n");
    test_palette_and_scaling();
    printf("\n");
    test_base_cadences();
    printf("\n");
    test_overlay_pips();
    printf("\n");
    test_rt_cascade();
    printf("\n");
    test_sys_cascade();
    printf("\n");

    printf("===========================================\n");
    printf("Tests passed: %d, Tests failed: %d\n", g_pass, g_fail);
    if (g_fail > 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}

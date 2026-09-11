// Host-driven behavior tests: the real Host (no rm) commands rt-esp32, which
// forwards to the actuators. Verifies vehicle behavior: wheel direction, steering,
// brakes and ESTOP.
//
//   Host --0x300--> RT --0x204--> MTR          (wheel speed + gear)
//   Host --0x303--> RT --0x169--> SES          (steering angle)
//   Host --0x301--> RT --0x205--> SYS --0x7B9--> SEB   (service brake)
//   Host --0x111/0x112--> RT (fwd) --> SYS ; RT --0x7FD--> SYS ; SYS --0x011/0x110/0x113--> MTR
#include <iostream>
#include <cmath>
#include "test_bench.hpp"

namespace testbench {

namespace {
bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Bring the vehicle up to AUTO with a healthy link (Host requests AUTO, operator
// presses START). rm is left inert; this exercises the real Host->RT->SYS path.
void bring_up_auto(TestBench& tb) {
    tb.reset();
    tb.host().request_mode(can::Mode::Auto);
    tb.host().request_power(true);
    tb.run_for_ms(200, 10);
    tb.press_start_button();
    tb.run_for_ms(300, 10);
}
}  // namespace

bool test_host_forward_drive() {
    std::cout << "TEST: HOST forward drive -> wheels turn (gear D)\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().set_gear(can::Gear::D);
    tb.host().send_drive_cmd(1500);
    tb.run_for_ms(1000, 10);

    bool traction = tb.mtr().is_traction_enabled();
    std::cout << "  MTR ready=" << tb.mtr().node_status().ready
              << " gear=" << static_cast<int>(tb.mtr().current_gear())
              << " dac=" << tb.mtr().dac_output()
              << " target=" << tb.mtr().manager().target_speed_mmps()
              << " traction=" << (traction ? "YES" : "NO") << "\n";
    if (!traction || tb.mtr().current_gear() != can::Gear::D) {
        std::cerr << "  FAIL: forward drive did not turn the wheels\n";
        return false;
    }
    return true;
}

bool test_host_reverse_drive() {
    std::cout << "TEST: HOST reverse drive -> wheels turn backwards (gear R)\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().set_gear(can::Gear::R);
    tb.host().send_drive_cmd(-400);
    tb.run_for_ms(1000, 10);

    bool reverse = (tb.mtr().current_gear() == can::Gear::R) && tb.mtr().dac_output() > 0;
    std::cout << "  MTR gear=" << static_cast<int>(tb.mtr().current_gear())
              << " dac=" << tb.mtr().dac_output()
              << " target=" << tb.mtr().manager().target_speed_mmps()
              << " reverse=" << (reverse ? "YES" : "NO") << "\n";
    if (!reverse) { std::cerr << "  FAIL: reverse drive did not engage gear R\n"; return false; }
    return true;
}

bool test_host_steer() {
    std::cout << "TEST: HOST steer cmd -> SES rack angle\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().send_drive_cmd(0);
    tb.host().set_steer_deg(20.0f);
    tb.run_for_ms(800, 10);

    float ang = tb.ses().actual_angle_0_1deg();
    std::cout << "  SES angle=" << ang << " (exp 30200)\n";
    if (!near(ang, 30200.0f, 12.0f)) { std::cerr << "  FAIL: steering did not reach SES\n"; return false; }
    return true;
}

bool test_host_brake() {
    std::cout << "TEST: HOST brake req -> SYS -> SEB stroke\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().send_drive_cmd(500);
    // Host requests 10000 kPa; rt clamps to the SEB limit (5000) and SYS applies
    // Pressure mode raw = 5000/50 = 100 -> 5000 kPa.
    tb.host().set_brake_kpa(10000);
    tb.run_for_ms(1000, 10);

    float kpa = tb.seb().actual_pressure_kpa();
    std::cout << "  SEB pressure=" << kpa << "kPa (exp 5000)\n";
    if (!near(kpa, 5000.0f, 300.0f)) { std::cerr << "  FAIL: brake did not reach SEB\n"; return false; }
    return true;
}

bool test_host_estop() {
    std::cout << "TEST: HOST drive + ESTOP -> wheels stop, brakes full\n";
    TestBench tb;
    bring_up_auto(tb);
    tb.host().set_gear(can::Gear::D);
    tb.host().send_drive_cmd(1500);
    tb.run_for_ms(600, 10);
    bool was_driving = tb.mtr().is_traction_enabled();

    tb.press_estop_button();
    tb.run_for_ms(600, 10);

    bool stopped = !tb.mtr().is_traction_enabled();
    bool braking = near(tb.seb().actual_stroke_mm(), 27.0f, 1.0f);
    bool latched = tb.sys().is_estop_latched();
    std::cout << "  was_driving=" << (was_driving ? "YES" : "NO")
              << "  after ESTOP: traction=" << (tb.mtr().is_traction_enabled() ? "YES" : "NO")
              << " SEB=" << tb.seb().actual_stroke_mm() << "mm"
              << " estop=" << (latched ? "LATCHED" : "NO") << "\n";
    if (!was_driving || !stopped || !braking || !latched) {
        std::cerr << "  FAIL: ESTOP behavior incorrect\n";
        return false;
    }
    return true;
}

}  // namespace testbench

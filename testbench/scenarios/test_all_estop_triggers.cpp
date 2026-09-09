#include <iostream>
#include <cassert>
#include "test_bench.hpp"

namespace testbench {

// ── Trigger 1: Hardware ESTOP Button on SYS ──────────────────────────────
// Tests: Physical button press -> refusal while held -> release -> START reset.
bool test_estop_trigger_01_hw_button() {
    std::cout << "[ESTOP TRIGGER 01] Hardware ESTOP Button on SYS...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Press physical button
    bench.press_estop_button();
    bench.run_for_ms(50);
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());
    assert(bench.mtr().dac_output() == 0);

    // 2. Refusal check: While button is physically depressed, attempt reset
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 3. Clear cause: release physical button
    bench.release_estop_button();
    bench.run_for_ms(50);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(bench.sys().mode() == can::Mode::Manual);
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Refused while held; cleanly reset when released.\n";
    return true;
}

// ── Trigger 2: External 0x001 on Low CAN ──────────────────────────────────
// Tests: Remote 0x001 frame on Low CAN -> refusal while spammed -> reset when clear.
bool test_estop_trigger_02_remote_001_low_can() {
    std::cout << "[ESTOP TRIGGER 02] External 0x001 Frame on Low CAN...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. External node sends 0x001 DLC 0 on Low CAN
    can::Frame fr = can::Frame::standard(can::kIdSafetyEstop, 0);
    bench.low_can().send(NodeId::TEST_HARNESS, fr);
    bench.run_for_ms(50);

    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: Active repeating 0x001 spam
    bench.press_start_button();
    for (int t = 0; t < 70; ++t) {
        bench.low_can().send(NodeId::TEST_HARNESS, fr);
        bench.run_for_ms(10);
    }
    // Must remain latched because spam outside the 500ms grace window re-trips ESTOP
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 3. Clear cause: stop spam and wait past reset grace window (500 ms)
    bench.run_for_ms(600);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Low CAN 0x001 latches and resets cleanly.\n";
    return true;
}

// ── Trigger 3: External 0x001 on High CAN (Gatewayed by RT) ───────────────
// Tests: High CAN 0x001 -> RT forwards to Low CAN -> refusal while active -> reset.
bool test_estop_trigger_03_remote_001_high_can() {
    std::cout << "[ESTOP TRIGGER 03] External 0x001 on High CAN (Gatewayed by RT)...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. External autonomy node sends 0x001 DLC 0 on High CAN
    can::Frame fr = can::Frame::standard(can::kIdSafetyEstop, 0);
    bench.high_can().send(NodeId::HOST, fr);
    bench.run_for_ms(50);

    // Verify RT latched and gatewayed 0x001 onto Low CAN, latching SYS and MTR
    assert(bench.rt().is_estop_latched());
    assert(bench.sys().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: High CAN continues repeating 0x001 past grace window
    bench.press_start_button();
    for (int t = 0; t < 70; ++t) {
        bench.high_can().send(NodeId::HOST, fr);
        bench.run_for_ms(10);
    }
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 3. Clear cause: stop injection and wait past reset grace window
    bench.run_for_ms(600);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: High CAN gatewayed 0x001 latches and resets cleanly.\n";
    return true;
}

// ── Trigger 4: SEB L3 Critical Fault (0x721 error_status = 3) ─────────────
// Tests: SEB reports L3 -> latched fault -> refusal while active -> reset when clear.
bool test_estop_trigger_04_seb_l3_fault() {
    std::cout << "[ESTOP TRIGGER 04] SEB L3 Critical Fault (0x721 error_status=3)...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Inject SEB L3 fault
    bench.seb().inject_l3_fault();
    bench.run_for_ms(60);

    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedSebL3);
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: try reset while error_status == 3
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedSebL3);

    // 3. Clear cause: SEB fault cleared
    bench.seb().clear_fault();
    bench.run_for_ms(50);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!(bench.sys().latched_faults() & sys::kLatchedSebL3));
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: SEB L3 refused while asserted; unlatched upon recovery.\n";
    return true;
}

// ── Trigger 5: Persistent Brake Following Error ───────────────────────────
// Tests: Stroke excursion > 5mm for >= 50ms -> refusal while stuck -> clear -> reset.
bool test_estop_trigger_05_brake_following_error() {
    std::cout << "[ESTOP TRIGGER 05] Persistent Brake Following Error...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Inject stuck actuator at 15 mm (demanded is 0 mm, diff is 15 mm > 5 mm)
    bench.seb().inject_stuck(15.0f);
    bench.run_for_ms(250); // 250ms > 200ms threshold

    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedBrakeFollowing);
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: try reset while actuator is still stuck
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedBrakeFollowing);

    // 3. Clear cause: actuator freed, returns to commanded position
    bench.seb().clear_fault();
    bench.run_for_ms(100);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!(bench.sys().latched_faults() & sys::kLatchedBrakeFollowing));
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Brake following error latched, refused, and recovered.\n";
    return true;
}

// ── Trigger 6: RT Software ESTOP / Autonomy Compute Fault ─────────────────
// Tests: RT originates software ESTOP -> refusal while active -> clear -> reset.
bool test_estop_trigger_06_rt_software_estop() {
    std::cout << "[ESTOP TRIGGER 06] RT Software ESTOP / Autonomy Fault...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. RT triggers internal software ESTOP
    bench.rt().trigger_software_estop();
    bench.run_for_ms(50);

    assert(bench.rt().is_estop_latched());
    assert(bench.sys().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: while RT internal condition is active, press START
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.rt().is_estop_latched());

    // 3. Clear cause: RT internal fault cleared
    bench.rt().clear_software_estop();
    bench.run_for_ms(50);

    // 4. Operator reset: press START on SYS
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: RT software ESTOP recovered through SYS reset sequence.\n";
    return true;
}

// ── Trigger 7: RT Heartbeat Watchdog Timeout at SYS ───────────────────────
// Tests: RT heartbeat dropped -> SYS watchdog trips -> refusal -> restore -> reset.
bool test_estop_trigger_07_rt_heartbeat_timeout() {
    std::cout << "[ESTOP TRIGGER 07] RT Heartbeat Watchdog Timeout at SYS...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(3200); // past 3000 ms startup grace

    // 1. Drop RT heartbeat (0x7FD) on Low CAN (70 frames * 20 ms = 1400 ms > 1000 ms timeout)
    bench.low_can().drop(can::kIdRtHeartbeat, 70);
    bench.run_for_ms(1100); // > 1000 ms timeout

    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: try reset while heartbeat is still lost
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());

    // 3. Clear cause: restore RT heartbeat
    bench.low_can().clear_faults();
    bench.run_for_ms(100); // allow watchdog to refresh

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Heartbeat timeout latched and recovered after link restored.\n";
    return true;
}

// ── Trigger 8: SEB 0x731 L3 Error Info Frame ──────────────────────────────
// Tests: 0x731 error info carries L3 bit -> latches kLatchedSebL3 -> reset works.
bool test_estop_trigger_08_seb_0x731_err_info() {
    std::cout << "[ESTOP TRIGGER 08] SEB 0x731 L3 Error Info Frame...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Send 0x731 with L3 fault (bit 17: MtrStall -> byte 2, bit 1)
    can::Frame fr = can::Frame::standard(can::kIdSebErrInfo, 8);
    fr.data.fill(0);
    fr.data[2] |= (1 << 1); // bit 17
    bench.low_can().send(NodeId::SEB, fr);
    bench.run_for_ms(50);

    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedSebL3);
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal check: keep sending 0x731 L3 error
    bench.low_can().send(NodeId::SEB, fr);
    bench.press_start_button();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());

    // 3. Clear cause: stop 0x731 L3 frame and clear SEB
    bench.seb().clear_fault();
    bench.run_for_ms(100);

    // 4. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!(bench.sys().latched_faults() & sys::kLatchedSebL3));
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: 0x731 L3 error info latches and recovers cleanly.\n";
    return true;
}

// ── Trigger 9: MTR Reported ESTOP via 0x206 Fault Flags ───────────────────
// Tests: MTR reports kMtrFaultEstopActive in 0x206 -> SYS propagates -> reset.
bool test_estop_trigger_09_mtr_reported_estop() {
    std::cout << "[ESTOP TRIGGER 09] MTR Reported ESTOP via 0x206 Fault Flags...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. MTR triggers local ESTOP (e.g. overcurrent / local fault)
    bench.mtr().manager().trigger_estop();
    bench.run_for_ms(50); // MTR sends 0x206 with kMtrFaultEstopActive

    assert(bench.mtr().is_estop_latched());
    assert(bench.sys().is_estop_latched());
    assert(bench.rt().is_estop_latched());

    // 2. Operator reset: press START on SYS
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: MTR 0x206 ESTOP propagated to SYS and cleanly cleared.\n";
    return true;
}

// ── Trigger 10: Multi-Cause Simultaneous ESTOP ────────────────────────────
// Tests: Hardware button + SEB L3 simultaneous -> partial clear refused -> full reset.
bool test_estop_trigger_10_multi_cause_simultaneous() {
    std::cout << "[ESTOP TRIGGER 10] Multi-Cause Simultaneous ESTOP (HW Button + SEB L3)...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Assert BOTH physical button AND SEB L3 fault
    bench.press_estop_button();
    bench.seb().inject_l3_fault();
    bench.run_for_ms(100);

    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedSebL3);
    assert(bench.rt().is_estop_latched());
    assert(bench.mtr().is_estop_latched());

    // 2. Refusal Check A: Release hardware button, but LEAVE SEB L3 active
    bench.release_estop_button();
    bench.run_for_ms(50);
    bench.press_start_button();
    bench.run_for_ms(100);
    // Must remain latched because SEB L3 is still active!
    assert(bench.sys().is_estop_latched());
    assert(bench.sys().latched_faults() & sys::kLatchedSebL3);

    // 3. Refusal Check B: Clear SEB L3, but RE-PRESS hardware button
    bench.seb().clear_fault();
    bench.press_estop_button();
    bench.run_for_ms(50);
    bench.press_start_button();
    bench.run_for_ms(100);
    // Must remain latched because hardware button is active!
    assert(bench.sys().is_estop_latched());

    // 4. Clear ALL causes: Release button AND ensure SEB L3 is clear
    bench.release_estop_button();
    bench.run_for_ms(50);

    // 5. Operator reset: press START
    bench.press_start_button();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(bench.sys().mode() == can::Mode::Manual);
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: Multi-cause refused on partial clear; reset only on full clear.\n";
    return true;
}

// ── Trigger 11: Alternative Reset via MODE Button 3-Second Long-Press ─────
// Tests: Latched ESTOP cleared via 3.0s MODE button hold instead of START button.
bool test_estop_trigger_11_mode_longpress_reset() {
    std::cout << "[ESTOP TRIGGER 11] Alternative Reset via MODE 3-Second Long-Press...\n";
    TestBench bench;
    bench.boot();
    bench.run_for_ms(1500);

    // 1. Trigger ESTOP via SEB L3
    bench.seb().inject_l3_fault();
    bench.run_for_ms(100);
    assert(bench.sys().is_estop_latched());

    // 2. Clear cause
    bench.seb().clear_fault();
    bench.run_for_ms(50);

    // 3. Instead of START button, hold MODE button for 3.0 seconds
    bench.hold_mode_button_3s();
    bench.run_for_ms(200);

    assert(!bench.sys().is_estop_latched());
    assert(bench.sys().mode() == can::Mode::Manual);
    assert(!bench.rt().is_estop_latched());
    assert(!bench.mtr().is_estop_latched());

    std::cout << "  -> PASS: MODE 3-second long-press reset verified successfully.\n";
    return true;
}

} // namespace testbench

#include <iostream>
#include <chrono>

namespace testbench {
    bool test_estop_seb_l3_and_rearm_recovery();
    bool test_01_estop_reset_loop();
    bool test_02_repeated_reset_attempts();
    bool test_03_rt_originated_estop_recovery();
    bool test_04_remote_001_echo();
    bool test_05_persistent_001_spam();
    bool test_06_mtr_feedback_dropout_moving();
    bool test_07_mtr_feedback_dropout_standstill();
    bool test_08_mtr_feedback_recovery();
    bool test_09_mtr_feedback_intermittent_loss();
    bool test_10_command_echo_false_negative();
    // Comprehensive ESTOP Trigger Verification Matrix
    bool test_estop_trigger_01_hw_button();
    bool test_estop_trigger_02_remote_001_low_can();
    bool test_estop_trigger_03_remote_001_high_can();
    bool test_estop_trigger_04_seb_l3_fault();
    bool test_estop_trigger_05_brake_following_error();
    bool test_estop_trigger_06_rt_software_estop();
    bool test_estop_trigger_07_rt_heartbeat_timeout();
    bool test_estop_trigger_08_seb_0x731_err_info();
    bool test_estop_trigger_09_mtr_reported_estop();
    bool test_estop_trigger_10_multi_cause_simultaneous();
    bool test_estop_trigger_11_mode_longpress_reset();

    // Section 3: Concurrent Publishing & Controller Active State Verification
    bool test_estop_clear_with_transient_inflight_publishing();
    bool test_estop_clear_refused_when_continuously_publishing();
    bool test_estop_clear_does_not_mean_controllers_active();

    // Section 4: rm-esp32-t12d operator gateway integration
    bool test_rm_bare_ignites_mtr();
    bool test_rm_sys_mode_seamless();
    bool test_rm_rt_mode_reaches_rt();
    bool test_rm_rt_low_sys_authority();
    bool test_rm_rt_low_sys_011_loss();
    // Section 5: end-to-end signal flow through intermediate controllers
    bool test_signal_flow_bare();
    bool test_signal_flow_sys();
    bool test_signal_flow_rt();
    // Section 6: Host-driven vehicle behavior (no rm): wheels, steer, brakes, ESTOP
    bool test_host_forward_drive();
    bool test_host_reverse_drive();
    bool test_host_steer();
    bool test_host_brake();
    bool test_host_estop();
    // Section 7: ESTOP reset / recovery paths
    bool test_estop_reset_recovery_host();
    bool test_estop_reset_blocked_by_fault();
    bool test_estop_recovery_bare();
    bool test_estop_recovery_rm_sys();
    bool test_estop_recovery_rm_rt();
    // Section 8: safety, fault injection and degraded comms
    bool test_fault_rc_loss_bare();
    bool test_fault_rc_loss_sys();
    bool test_fault_rc_loss_rt();
    bool test_fault_rm_ignores_0x001();
    bool test_fault_mtr_drive_timeout();
    bool test_fault_011_crc_rejected();
    bool test_fault_sys_rt_heartbeat_loss();
    bool test_fault_hot_mode_switch();
    // Section 9: limit clamping and rt host-heartbeat assisted stop
    bool test_limit_steer_clamp_bare();
    bool test_limit_rt_drive_clamp();
    bool test_limit_rt_steer_clamp();
    bool test_status_frames_canonical();
    bool test_timeout_rt_host_heartbeat();
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "========================================================\n";
    std::cout << "  E-TRIKE SYSTEM TEST BENCH (vECU -> Restbus -> HIL)\n";
    std::cout << "  Running Exhaustive ESTOP & Distributed Verification Suite\n";
    std::cout << "========================================================\n\n";

    auto start_time = std::chrono::steady_clock::now();
    bool all_passed = true;

    auto run_test = [&](const char* name, auto fn) {
        try {
            if (!fn()) {
                std::cerr << "FAIL: " << name << "\n\n";
                all_passed = false;
            } else {
                std::cout << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "EXCEPTION in " << name << ": " << e.what() << "\n\n";
            all_passed = false;
        }
    };

    std::cout << "--- SECTION 1: Baseline & Prior Scenarios 1 to 10 ---\n";
    run_test("Baseline ESTOP SEB L3 Recovery", testbench::test_estop_seb_l3_and_rearm_recovery);
    run_test("Test 01: ESTOP Reset-Loop", testbench::test_01_estop_reset_loop);
    run_test("Test 02: Repeated Reset Attempts (20x)", testbench::test_02_repeated_reset_attempts);
    run_test("Test 03: RT-Originated ESTOP Recovery", testbench::test_03_rt_originated_estop_recovery);
    run_test("Test 04: Remote 0x001 Echo", testbench::test_04_remote_001_echo);
    run_test("Test 05: Persistent 0x001 Spam", testbench::test_05_persistent_001_spam);
    run_test("Test 06: MTR Feedback 250ms Dropout Moving", testbench::test_06_mtr_feedback_dropout_moving);
    run_test("Test 07: MTR Feedback 250ms Dropout Standstill", testbench::test_07_mtr_feedback_dropout_standstill);
    run_test("Test 08: MTR Feedback Recovery", testbench::test_08_mtr_feedback_recovery);
    run_test("Test 09: MTR Feedback Intermittent Loss", testbench::test_09_mtr_feedback_intermittent_loss);
    run_test("Test 10: Command-Echo False-Negative", testbench::test_10_command_echo_false_negative);

    std::cout << "--- SECTION 2: All ESTOP Triggers & Reset Matrix ---\n";
    run_test("Trigger 01: Hardware ESTOP Button on SYS", testbench::test_estop_trigger_01_hw_button);
    run_test("Trigger 02: Remote 0x001 on Low CAN", testbench::test_estop_trigger_02_remote_001_low_can);
    run_test("Trigger 03: High CAN 0x001 (Gatewayed by RT)", testbench::test_estop_trigger_03_remote_001_high_can);
    run_test("Trigger 04: SEB L3 Critical Fault (0x721)", testbench::test_estop_trigger_04_seb_l3_fault);
    run_test("Trigger 05: Persistent Brake Following Error", testbench::test_estop_trigger_05_brake_following_error);
    run_test("Trigger 06: RT Software ESTOP / Autonomy Fault", testbench::test_estop_trigger_06_rt_software_estop);
    run_test("Trigger 07: RT Heartbeat Watchdog Timeout at SYS", testbench::test_estop_trigger_07_rt_heartbeat_timeout);
    run_test("Trigger 08: SEB 0x731 L3 Error Info Frame", testbench::test_estop_trigger_08_seb_0x731_err_info);
    run_test("Trigger 09: MTR Reported ESTOP (0x206)", testbench::test_estop_trigger_09_mtr_reported_estop);
    run_test("Trigger 10: Multi-Cause Simultaneous ESTOP", testbench::test_estop_trigger_10_multi_cause_simultaneous);
    run_test("Trigger 11: Alternative Reset via MODE 3s Long-Press", testbench::test_estop_trigger_11_mode_longpress_reset);

    std::cout << "--- SECTION 3: Concurrent Publishing & Active State Invariants ---\n";
    run_test("Test 12: In-Flight / Transient 0x001 Publishing During Clear", testbench::test_estop_clear_with_transient_inflight_publishing);
    run_test("Test 13: Continuous 0x001 Publishing Re-Latches (Refusal)", testbench::test_estop_clear_refused_when_continuously_publishing);
    run_test("Test 14: ESTOP Clear != Active State (Multi-Node Verification)", testbench::test_estop_clear_does_not_mean_controllers_active);

    std::cout << "--- SECTION 4: rm-esp32-t12d Operator Gateway Integration ---\n";
    run_test("RM BARE ignites MTR (0x011 present)", testbench::test_rm_bare_ignites_mtr);
    run_test("RM SYS drives sys-esp32 seamlessly (AUTO)", testbench::test_rm_sys_mode_seamless);
    run_test("RM RT high-bus frames ignored for authority", testbench::test_rm_rt_mode_reaches_rt);
    run_test("RM RT + LOW-bus SYS authority grants motion", testbench::test_rm_rt_low_sys_authority);
    run_test("RM RT LOW-bus 0x011 loss -> rt fail-safe", testbench::test_rm_rt_low_sys_011_loss);

    std::cout << "--- SECTION 5: End-to-End Signal Flow (steer/throttle/brake) ---\n";
    run_test("Signal flow BARE: rm -> SES/SEB/MTR", testbench::test_signal_flow_bare);
    run_test("Signal flow SYS: rm -> SYS -> units", testbench::test_signal_flow_sys);
    run_test("Signal flow RT: rm -> RT -> SYS -> units", testbench::test_signal_flow_rt);

    std::cout << "--- SECTION 6: Host-Driven Vehicle Behavior (no rm) ---\n";
    run_test("HOST forward drive -> wheels turn", testbench::test_host_forward_drive);
    run_test("HOST reverse drive -> wheels reverse", testbench::test_host_reverse_drive);
    run_test("HOST steer -> SES rack angle", testbench::test_host_steer);
    run_test("HOST brake -> SYS -> SEB", testbench::test_host_brake);
    run_test("HOST drive + ESTOP -> stop + full brake", testbench::test_host_estop);

    std::cout << "--- SECTION 7: ESTOP Reset / Recovery Paths ---\n";
    run_test("ESTOP reset/recovery HOST", testbench::test_estop_reset_recovery_host);
    run_test("ESTOP reset refused while fault active", testbench::test_estop_reset_blocked_by_fault);
    run_test("ESTOP recovery rm BARE (0x001)", testbench::test_estop_recovery_bare);
    run_test("ESTOP recovery rm SYS path", testbench::test_estop_recovery_rm_sys);
    run_test("ESTOP recovery rm RT path", testbench::test_estop_recovery_rm_rt);

    std::cout << "--- SECTION 8: Safety, Fault Injection & Degraded Comms ---\n";
    run_test("RC link loss BARE -> safe stop", testbench::test_fault_rc_loss_bare);
    run_test("RC link loss SYS -> safe stop", testbench::test_fault_rc_loss_sys);
    run_test("RC link loss RT -> safe stop", testbench::test_fault_rc_loss_rt);
    run_test("bus 0x001 ESTOP while rm drives", testbench::test_fault_rm_ignores_0x001);
    run_test("MTR 0x204 drive watchdog", testbench::test_fault_mtr_drive_timeout);
    run_test("corrupt 0x011 CRC -> drop authority", testbench::test_fault_011_crc_rejected);
    run_test("sys 0x7FD heartbeat loss -> ESTOP", testbench::test_fault_sys_rt_heartbeat_loss);
    run_test("hot mode switch + 0x7B9 regression", testbench::test_fault_hot_mode_switch);

    std::cout << "--- SECTION 9: Limit Clamping & rt Host-Heartbeat ---\n";
    run_test("steering clamp BARE (29550..30450)", testbench::test_limit_steer_clamp_bare);
    run_test("RT drive/brake clamp (3000/-500/20000)", testbench::test_limit_rt_drive_clamp);
    run_test("RT steer clamp (+/-450)", testbench::test_limit_rt_steer_clamp);
    run_test("SES/SEB status frames decode canonically", testbench::test_status_frames_canonical);
    run_test("rt host-heartbeat -> assisted stop", testbench::test_timeout_rt_host_heartbeat);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    std::cout << "========================================================\n";
    if (all_passed) {
        std::cout << "  RESULT: ALL TESTS PASSED (" << elapsed << " ms execution time)\n";
    } else {
        std::cout << "  RESULT: SOME TESTS FAILED\n";
    }
    std::cout << "========================================================\n";

    return all_passed ? 0 : 1;
}

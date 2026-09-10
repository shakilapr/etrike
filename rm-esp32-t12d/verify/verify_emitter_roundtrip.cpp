// Phase 1 (C++) — End-to-end wire compatibility: rm CanEmitter -> consumer decode.
// Uses the firmware's ACTUAL encode path (can_emitter.h) and the canonical
// consumer decode functions (protocol/compat). Verifies every frame emitted in
// each mode round-trips to the exact signal values the downstream node reads.
//
// Build (from repo root e:\work\etrike):
//   C:\TDM-GCC-64\bin\g++.exe -std=c++17 -Wall -Wextra -Irm-esp32-t12d/src -I. -Ishared ^
//     rm-esp32-t12d/verify/verify_emitter_roundtrip.cpp -o rm-esp32-t12d/verify/verify_emitter_roundtrip.exe
//   rm-esp32-t12d/verify/verify_emitter_roundtrip.exe

#include <cstdint>
#include <cstdio>
#include <vector>
#include <cmath>

#include "protocol/compat/can.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "shared_config.h"
#include "config.h"
#include "rc_decoder.h"
#include "can_emitter.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const char* tag, const char* detail) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s  %s\n", tag, detail); }
    else   { ++g_fail; std::printf("  [FAIL] %s  %s\n", tag, detail); }
}

rm::RcSnapshot make_snap(rm::OperatingMode mode) {
    rm::RcSnapshot snap{};
    snap.signal_valid       = true;
    snap.drive_enable_req  = true;
    snap.park_hold_req     = false;
    snap.gear              = can::Gear::D;
    snap.target_speed_mmps = 2500;
    snap.steering_deg      = 20.0f;
    snap.brake_stroke_mm   = 13.5f;
    snap.op_mode           = mode;
    return snap;
}

void run_mode(rm::OperatingMode mode, const char* mode_name,
              const std::vector<can::Frame>& frames) {
    for (const auto& fr : frames) {
        if (fr.id == 0x169u) {
            can::custom::ses::Command c{};
            auto st = can::custom::ses::decode_command(fr, c);
            check(st == can::gen::CodecStatus::Ok &&
                  c.alignment_enable == true && c.control_enable == true &&
                  c.rolling_counter < 16,
                  "BARE/SYS 0x169 VCU_SES_REQ",
                  "decode OK + alignment/control enabled");
        } else if (fr.id == 0x7B9u) {
            can::custom::seb::Command c{};
            auto st = can::custom::seb::decode_command(fr, c);
            check(st == can::gen::CodecStatus::Ok &&
                  c.alignment_enable && c.control_enable &&
                  c.control_mode == can::custom::seb::ControlMode::Stroke,
                  "BARE/SYS 0x7B9 VCU_SEB_REQ",
                  "decode OK + Stroke mode + alignment/control enabled");
        } else if (fr.id == 0x204u) {
            can::gen::RtDriveCmd c{};
            auto st = can::gen::decode_rt_drive_cmd(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.motor_speed_mmps == 2500 &&
                  c.gear == static_cast<uint8_t>(can::Gear::D),
                  "BARE/SYS 0x204 RT_DRIVE_CMD",
                  "decode OK + speed=2500 gear=D");
        } else if (fr.id == 0x110u) {
            can::gen::SysModeCmd c{};
            auto st = can::gen::decode_sys_mode_cmd(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.mode == 1,
                  "BARE/SYS 0x110 SYS_MODE_CMD", "decode OK + mode=AUTO(1)");
        } else if (fr.id == 0x113u) {
            can::gen::SysPwrCmd c{};
            auto st = can::gen::decode_sys_pwr_cmd(fr, c);
            // power_state is 0 on the first REARM emissions, then 1. Accept both.
            check(st == can::gen::CodecStatus::Ok && (c.power_state == 0 || c.power_state == 1),
                  "BARE/SYS 0x113 SYS_PWR_CMD", "decode OK (power_state 0/1 incl. rearm edge)");
        } else if (fr.id == 0x011u) {
            can::gen::SysSafetySts c{};
            auto st = can::gen::decode_sys_safety_sts(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.estop_active == 0,
                  "BARE/RT 0x011 SYS_SAFETY_STS", "decode OK + estop_active=0 (E2E CRC valid)");
        } else if (fr.id == 0x111u) {
            can::gen::HmiModeReq c{};
            auto st = can::gen::decode_hmi_mode_req(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.req_mode == 1,
                  "SYS/RT 0x111 HMI_MODE_REQ", "decode OK + req_mode=AUTO(1)");
        } else if (fr.id == 0x112u) {
            can::gen::HmiPwrReq c{};
            auto st = can::gen::decode_hmi_pwr_req(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.req_start == 1,
                  "SYS/RT 0x112 HMI_PWR_REQ", "decode OK + req_start=ON(1)");
        } else if (fr.id == 0x7FDu) {
            can::gen::RtHeartbeat c{};
            auto st = can::gen::decode_rt_heartbeat(fr, c);
            check(st == can::gen::CodecStatus::Ok,
                  "SYS 0x7FD RT_HEARTBEAT", "decode OK");
        } else if (fr.id == 0x300u) {
            can::gen::HostDriveCmd c{};
            auto st = can::gen::decode_host_drive_cmd(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.speed_mmps == 2500 &&
                  c.gear == static_cast<uint8_t>(can::Gear::D) &&
                  c.yaw_rate_mrad_s == 0,
                  "RT 0x300 HOST_DRIVE_CMD", "decode OK + speed=2500 gear=D yaw=0");
        } else if (fr.id == 0x301u) {
            can::gen::HostBrakeReq c{};
            auto st = can::gen::decode_host_brake_req(fr, c);
            // rm maps 13.5/27.0 * 20000 = 10000 kPa
            check(st == can::gen::CodecStatus::Ok && c.brake_pressure_kpa == 10000,
                  "RT 0x301 HOST_BRAKE_REQ", "decode OK + pressure=10000 kPa");
        } else if (fr.id == 0x303u) {
            can::gen::HostSteerCmd c{};
            auto st = can::gen::decode_host_steer_cmd(fr, c);
            check(st == can::gen::CodecStatus::Ok && c.steer_angle_0_1deg == 200 &&
                  c.angle_valid == 1,
                  "RT 0x303 HOST_STEER_CMD", "decode OK + angle=200 (20.0deg) valid");
        } else if (fr.id == 0x7FCu) {
            can::gen::HostHeartbeat c{};
            auto st = can::gen::decode_host_heartbeat(fr, c);
            check(st == can::gen::CodecStatus::Ok,
                  "RT 0x7FC HOST_HEARTBEAT", "decode OK");
        } else {
            char buf[32]; std::snprintf(buf, sizeof(buf), "0x%03X", fr.id);
            check(false, buf, "UNEXPECTED FRAME ID");
        }
    }
    (void)mode_name;
}

// Verifies the MTR power REARM edge: the first two emitted 0x113 carry
// power_state=0 and the third carries 1 (mtr-stm32/src/motor_manager.h:166-180).
void test_power_rearm_edge() {
    rm::CanEmitter em;
    std::vector<uint8_t> seq;
    auto snap = make_snap(rm::OperatingMode::Bare);
    for (uint32_t tick : {0u, 10u, 20u}) {
        em.emit_cluster(snap, tick, [&](const can::Frame& f) {
            if (f.id == 0x113u) {
                can::gen::SysPwrCmd c{};
                if (can::gen::decode_sys_pwr_cmd(f, c) == can::gen::CodecStatus::Ok) {
                    seq.push_back(static_cast<uint8_t>(c.power_state));
                }
            }
            return true;
        });
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "0x113 power_state seq=[%u,%u,%u] expect [0,0,1]",
                  seq.size() > 0 ? seq[0] : 9u, seq.size() > 1 ? seq[1] : 9u,
                  seq.size() > 2 ? seq[2] : 9u);
    check(seq.size() == 3 && seq[0] == 0 && seq[1] == 0 && seq[2] == 1,
          "BARE power REARM edge", buf);
}

}  // namespace

int main() {
    std::printf("=== Phase 1 (C++) rm CanEmitter -> consumer decode round-trip ===\n");

    rm::CanEmitter emitter;

    auto collect = [&](rm::OperatingMode m) {
        std::vector<can::Frame> out;
        auto snap = make_snap(m);
        // Emit several 10 ms ticks so the startup power REARM edge completes.
        for (uint32_t tick : {0u, 10u, 20u, 30u}) {
            emitter.emit_cluster(snap, tick, [&](const can::Frame& f) { out.push_back(f); return true; });
        }
        return out;
    };

    std::printf("-- BARE --\n"); run_mode(rm::OperatingMode::Bare, "BARE", collect(rm::OperatingMode::Bare));
    std::printf("-- SYS  --\n"); run_mode(rm::OperatingMode::Sys,  "SYS",  collect(rm::OperatingMode::Sys));
    std::printf("-- RT   --\n"); run_mode(rm::OperatingMode::Rt,   "RT",   collect(rm::OperatingMode::Rt));
    test_power_rearm_edge();

    std::printf("----------------------------------------------------\n");
    std::printf("Round-trip checks: %d pass, %d fail\n", g_pass, g_fail);
    if (g_fail == 0) { std::printf(">>> ALL EMITTER ROUND-TRIP CHECKS PASSED <<<\n"); return 0; }
    std::printf(">>> SOME ROUND-TRIP CHECKS FAILED <<<\n");
    return 1;
}

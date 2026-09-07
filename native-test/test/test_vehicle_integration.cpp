// Whole-Vehicle Multi-Node Integration Suite — SYS + RT + MTR "all active".
//
// Purpose: exercise the *coordinated* behavior that unit tests cannot — the
// vehicle is fully up (all authority streams flowing, AUTO, driving), and then
// a fault + recovery requires the ECUs to send a COMBINATION of commands in the
// correct order. Real production code is exercised on all three nodes:
//
//   SYS  : sys::ModeManager (real) + sys::SafetyMonitor (real) + the
//          sys::resolve_authority clamp that publishes 0x110 / 0x113, plus the
//          latched-ESTOP 0x011 bit the sys can_tx task publishes.
//   RT   : rt::run_safety_checks (real) + rt::g_mtr_health (real MTR-health
//          watchdog, issue #8) + rt::PhysicsModel (real 0x300->0x204).
//   MTR  : mtr::MotorManager (real) consuming SYS authority + RT drive and
//          driving relays/DAC; the asymmetric two-frame 0x011 clear is MTR's.
//
// The SYS/RT FreeRTOS publish loops are replayed (they are inline in main.cpp
// behind IDF/FreeRTOS), but every state machine they call is the real class.
//
// Scenarios (all start from "everything active in AUTO and moving"):
//   S1 boot + all streams + AUTO drive (combined command cadence).
//   S2 ESTOP while driving: SYS latches + clamps authority, MTR cuts. A drive
//      command alone cannot restore motion.
//   S3 premature clear rejected: single 0x011 zero / 0x110 mode change / power
//      re-assert all fail to un-latch MTR.
//   S4 operator reset (START) -> SYS unlatches -> two-frame 0x011 clear -> MTR
//      released into REARM_REQUIRED (still stopped).
//   S5 full REARM: fresh 0x110 + 0x113 OFF->ON (real SYS authority) + fresh
//      0x204 -> motion restores.
//   S6 RT MTR-feedback heartbeat loss in AUTO -> RT zeroes setpoints + max
//      brake (real run_safety_checks) -> confirmed recovery re-enables.
//   S7 SYS heartbeat loss (0x7FE) -> RT fail-safe -> recovery on re-fresh.
//   S8 interleaved stress: while driving AUTO, a brake request + mode toggle +
//      ESTOP all arrive together; safety (ESTOP) must win, then full reset.

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cmath>

#include "protocol/compat/can_protocol.hpp"
#include "protocol/compat/e2e.hpp"
#include "protocol/codecs/ses.hpp"
#include "shared_config.h"

// Real SYS authority
#include "sys-esp32/src/config.h"
#include "sys-esp32/src/mode_manager.h"
#include "sys-esp32/src/safety_monitor.h"
#include "sys-esp32/src/inhibit_state.h"

// Real RT safety / kinematics
#include "rt-esp32/src/config.h"
#include "rt-esp32/src/safety_monitor.h"  // rt::run_safety_checks, rt::g_mtr_health
#include "rt-esp32/src/physics_model.h"
#include "rt-esp32/src/steering_control.h"
#include "system_mode.h"

// Real MTR actuation
#include "mtr-stm32/src/config.h"
#include "mtr-stm32/src/relay_controller.h"
#include "mtr-stm32/src/dac_controller.h"
#include "mtr-stm32/src/motor_manager.h"
#include "stub/stm32g4xx_hal.h"

// ── SYS atomics (inhibit_state.h externs; normally sys main.cpp) ──
namespace sys {
std::atomic<uint32_t> g_inhibit_reasons{0};
std::atomic<uint32_t> g_latched_fault_reasons{0};
}
// ── RT atomics used by run_safety_checks (normally rt main.cpp) ──
std::atomic<int64_t>  g_last_sys_hb_us{0};
std::atomic<int64_t>  g_last_host_hb_us{0};
std::atomic<int32_t>  g_mtr_actual_speed_mmps{0};
std::atomic<int64_t>  g_last_mtr_feedback_us{-1};
std::atomic<int64_t>  g_last_nonzero_cmd_us{-1};
std::atomic<int16_t>  g_last_cmd_angle_0_1deg{INT16_MIN};
std::atomic<int32_t>  g_ses_angle_0_1deg{0};
std::atomic<int32_t>  g_brake_request_kpa{0};
bool g_bench_solo_mode = false;
bool g_bypass_eps_sync = true;    // steering-follow path not under test
bool g_bypass_mtr_absent = false; // MTR health supervision ACTIVE
namespace rt { MtrHealthSupervisor g_mtr_health; }
rt::SteeringControl g_steering{};  // referenced by run_safety_checks (path bypassed)

extern "C" { FDCAN_HandleTypeDef hfdcan1; }  // MTR CanDriver extern

static int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::fprintf(stderr, "  FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)
#define CHECK_EQ(a, b) do { auto _a=(a); auto _b=(b); if (_a==_b) ++g_pass; else { ++g_fail; std::fprintf(stderr, "  FAIL %s:%d (%lld != %lld)\n", __FILE__, __LINE__, (long long)_a, (long long)_b); } } while (0)

namespace {

struct Roll { uint8_t v = 0; uint8_t next() { return v++; } };

// ── The full vehicle harness: real SYS + real MTR + real RT checks ──
struct Vehicle {
    // SYS real authority
    sys::ModeManager   sys_mode;
    sys::SafetyMonitor sys_safety;
    Roll mc, pc, sc, hbc;
    bool hw_estop_pressed = false;

    // MTR real actuation
    mtr::RelayController relays;
    mtr::DacController   dac;
    mtr::MotorManager    mtr{relays, dac};

    // RT real kinematics
    rt::PhysicsModel     physics;

    // Shared clock
    uint32_t now_ms = 100;

    void init() {
        hal_mock::reset();
        mtr.init();
        sys_mode.init();
        sys_safety.init();
        sys::g_inhibit_reasons.store(0);
        sys::g_latched_fault_reasons.store(0);
        g_last_sys_hb_us.store(now_us());
        g_last_host_hb_us.store(now_us());
        g_last_mtr_feedback_us.store(-1);
        g_last_nonzero_cmd_us.store(-1);
        g_brake_request_kpa.store(0);
        g_mtr_actual_speed_mmps.store(0);
        rt::g_mtr_health.reset();
    }

    int64_t now_us() const { return int64_t(now_ms) * 1000; }
    void advance(uint32_t dt_ms) { now_ms += dt_ms; mtr.tick(now_ms); }

    // ── Frame encoders (the "bus") ────────────────────────────────
    void send_sys_mode_cmd() {
        const can::Mode m = sys_mode.mode();
        const bool estop = (m == can::Mode::Estop) || hw_estop_pressed;
        const bool mode_auto = (m == can::Mode::Auto);
        auto auth = sys::resolve_authority(estop, mode_auto, /*pwr_req=*/true);
        can::gen::SysModeCmd msg{};
        msg.mode = auth.mode_auto ? 1u : 0u;
        msg.rolling_counter = mc.next();
        can::Frame f; can::gen::encode_sys_mode_cmd(msg, f);
        mtr.handle_frame(f, now_ms);       // MTR consumes (low bus)
    }
    void send_sys_pwr_cmd() {
        const can::Mode m = sys_mode.mode();
        const bool estop = (m == can::Mode::Estop) || hw_estop_pressed;
        const bool mode_auto = (m == can::Mode::Auto);
        auto auth = sys::resolve_authority(estop, mode_auto, /*pwr_req=*/true);
        can::gen::SysPwrCmd msg{};
        msg.power_state = auth.power_on ? 1u : 0u;
        msg.rolling_counter = pc.next();
        can::Frame f; can::gen::encode_sys_pwr_cmd(msg, f);
        mtr.handle_frame(f, now_ms);       // MTR consumes
    }
    // 0x011 SYS_SAFETY_STS with real E2E CRC-8.
    void send_sys_safety_sts() {
        can::gen::SysSafetySts msg{};
        msg.estop_active = (sys_mode.mode() == can::Mode::Estop) || hw_estop_pressed;
        msg.heartbeat_ok = sys_safety.heartbeat_ok();
        msg.rolling_counter = sc.next();
        msg.e2e_crc = 0;
        can::Frame tmp; can::gen::encode_sys_safety_sts(msg, tmp);
        msg.e2e_crc = static_cast<uint8_t>(can::e2e::sys_safety_sts_crc(tmp.data.data()));
        can::Frame f; can::gen::encode_sys_safety_sts(msg, f);
        mtr.handle_frame(f, now_ms);       // MTR consumes
    }
    // SYS heartbeat 0x7FE feeds RT's sys_hb liveness.
    void send_sys_heartbeat() {
        can::gen::SysHeartbeat hb{};
        hb.alive_ctr = hbc.next();
        hb.estop_active = (sys_mode.mode() == can::Mode::Estop) || hw_estop_pressed;
        hb.mode_auto = (sys_mode.mode() == can::Mode::Auto);
        can::Frame f; can::gen::encode_sys_heartbeat(hb, f);
        // RT observes it.
        g_last_sys_hb_us.store(now_us());
    }
    void send_host_heartbeat() {
        g_last_host_hb_us.store(now_us());
    }

    // Host 0x300 -> RT physics -> 0x204 to MTR.
    struct DriveOut { int32_t speed_mmps; uint8_t gear; };
    DriveOut send_host_drive(int32_t speed_mmps, int32_t yaw_mrad_s) {
        rt::DriveCmd cmd{speed_mmps, yaw_mrad_s};
        rt::ResolvedSetpoint sp{};
        bool ok = physics.resolve(cmd, sp);
        CHECK(ok);
        uint8_t gear = sp.motor_speed_mmps > 0 ? uint8_t(can::Gear::D)
                     : sp.motor_speed_mmps < 0 ? uint8_t(can::Gear::R)
                     : uint8_t(can::Gear::N);
        if (sp.motor_speed_mmps != 0) {
            can::gen::RtDriveCmd dc{sp.motor_speed_mmps, gear};
            can::Frame f; can::gen::encode_rt_drive_cmd(dc, f);
            mtr.handle_frame(f, now_ms);
        }
        // MTR 0x206 feedback to RT health supervisor (simulate echo of cmd).
        can::gen::MtrMotorFbk fbk{};
        fbk.actual_speed_mmps = sp.motor_speed_mmps;
        fbk.gear_state = gear;
        fbk.fault_flags = 0;
        can::Frame ff; can::gen::encode_mtr_motor_fbk(fbk, ff);
        (void)ff;
        g_mtr_actual_speed_mmps.store(sp.motor_speed_mmps);
        g_last_mtr_feedback_us.store(now_us());
        return {sp.motor_speed_mmps, gear};
    }

    // ── Combined SYS publish tick (10 Hz mode task + 5 Hz safety task) ──
    // Replays sys task_safety escalation + task_mode authority + task_can_tx.
    void sys_publish_tick() {
        // task_safety: hardware ESTOP (or safety latch) forces SYS into ESTOP.
        sys_safety.set_estop(hw_estop_pressed);
        if (sys_safety.estop_active() && sys_mode.mode() != can::Mode::Estop) {
            sys_mode.force_estop();
        }
        // task_mode: authority resolver -> 0x110 / 0x113.
        send_sys_mode_cmd();
        send_sys_pwr_cmd();
        // task_can_tx: 0x011 + heartbeat carry the latched estop bit.
        send_sys_safety_sts();
        send_sys_heartbeat();
        mtr.tick(now_ms);
    }

    // ── Bring the whole vehicle to "all active, AUTO, driving" ──
    // 1) SYS boots MANUAL, publishes valid streams (MTR re-acquires).
    // 2) MODE button -> AUTO; power ON. 3) Host drives.
    void drive_to_active_auto(int32_t speed_mmps = 2000) {
        // Boot: SYS publishes a few Manual + power-ON ticks so MTR authorities
        // (0x110/0x113/0x011) become valid.
        for (int i = 0; i < 3; ++i) {
            sys_publish_tick();
            advance(10);
        }
        // Enter AUTO via MODE button (real ModeManager falling-edge on release).
        press_mode_button();
        for (int i = 0; i < 2; ++i) { sys_publish_tick(); advance(10); }
        CHECK(sys_mode.mode() == can::Mode::Auto);

        // Drive (host command stream + authority re-fresh each tick).
        for (int i = 0; i < 5; ++i) {
            send_host_heartbeat();
            send_host_drive(speed_mmps, 0);
            sys_publish_tick();
            advance(10);
        }
    }

    // Press-and-release the MODE button (real ModeManager: change fires on the
    // falling edge, i.e. the release). Flushes any pending debounce first
    // (in real firmware task_mode calls tick() at 10 Hz, so a prior press leaves
    // m_debounce draining over time; here we must drain it explicitly).
    void press_mode_button() {
        for (int i = 0; i < 7; ++i) sys_mode.tick(false, false);  // flush debounce
        bool held = sys_mode.tick(true, false);
        (void)held;
        bool changed = sys_mode.tick(false, false);   // release -> falling edge
        (void)changed;
    }
    // Press-and-release the START button (exits ESTOP -> Manual).
    void press_start_button() {
        for (int i = 0; i < 7; ++i) sys_mode.tick(false, false);  // flush debounce
        sys_mode.tick(false, true);    // press START
        sys_mode.tick(false, false);   // release -> falling edge -> ESTOP->Manual
    }

    // ── RT fail-safe check evaluation (mirrors t_control) ─────────
    // Returns the post-safety command that RT would send on 0x204.
    rt::SafetyResult rt_control_pass() {
        const bool startup_grace = (now_us() < int64_t(shared::kStartupGracePeriodMs) * 1000);
        const uint8_t mode = uint8_t(sys_mode.mode());
        bool estop_pending = false;
        bool seb = false;
        if (mode == uint8_t(can::Mode::Estop)) {
            // RT also latches ESTOP from 0x001 / 0x011 in can_dispatch; for this
            // integration the SYS estop bit is what both RT and MTR must honor.
            estop_pending = true;
        }
        auto r = run_safety_checks(now_us(), startup_grace,
                                       UINT32_MAX, estop_pending, mode, seb);
        return r;
    }
};

}  // anonymous namespace

int main() {
    std::printf("\n=== Whole-Vehicle SYS+RT+MTR Integration Suite ===\n");

    // ── S1: boot -> all streams active -> AUTO -> driving ──────────
    {
        std::printf("\n[S1] boot -> active AUTO drive (combined command cadence)\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        // SYS is AUTO; MTR relay/DAC engaged; RT would not fail-safe.
        CHECK(v.sys_mode.mode() == can::Mode::Auto);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Drive);
        CHECK(v.dac.current_code() > 0);
        CHECK(!v.mtr.is_estop_active());
        auto sr = v.rt_control_pass();
        CHECK(!sr.zero_setpoints);
    }

    // ── S2: ESTOP while driving cuts every node; drive alone can't restore ──
    {
        std::printf("\n[S2] ESTOP while driving: SYS latches, MTR cuts, drive blocked\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Drive);

        // Hardware ESTOP pressed (GPIO): SYS mode -> Estop, authority clamps.
        v.sys_safety.set_estop(true);
        v.hw_estop_pressed = true;
        for (int i = 0; i < 3; ++i) { v.sys_publish_tick(); v.advance(10); }
        CHECK(v.sys_mode.mode() == can::Mode::Estop);

        // MTR received 0x011 estop=1 + authority dropped -> relays off, DAC 0.
        CHECK(v.mtr.is_estop_active());
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Off);
        CHECK_EQ(v.dac.current_code(), 0);

        // A raw drive command (host -> RT -> MTR) cannot restore motion.
        auto out = v.send_host_drive(2000, 0);
        CHECK_EQ(out.speed_mmps, 2000);  // RT still resolves the kinematic setpoint
        v.mtr.tick(v.now_ms);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Off);
        CHECK_EQ(v.dac.current_code(), 0);
    }

    // ── S3: premature clears are rejected ──────────────────────────
    {
        std::printf("\n[S3] premature clears rejected (single zero / mode / power)\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        // Latch ESTOP.
        v.sys_safety.set_estop(true);
        v.hw_estop_pressed = true;
        for (int i = 0; i < 3; ++i) { v.sys_publish_tick(); v.advance(10); }
        CHECK(v.mtr.is_estop_active());

        // (a) SYS clears the HARDWARE button but remains in ESTOP mode
        //     (software-latched) -> 0x011 estop bit stays 1 -> MTR stays latched.
        v.sys_safety.set_estop(false);
        v.hw_estop_pressed = false;
        for (int i = 0; i < 2; ++i) { v.sys_publish_tick(); v.advance(10); }
        CHECK(v.sys_mode.mode() == can::Mode::Estop);  // SW latch persists
        CHECK(v.mtr.is_estop_active());

        // (b) A single 0x011 zero frame must NOT clear MTR (asymmetric).
        // Force SYS out of ESTOP via START button (real ModeManager), then
        // publish EXACTLY ONE zero frame before the next.
        v.press_start_button();
        CHECK(v.sys_mode.mode() == can::Mode::Manual);
        // SYS is now unlatched but only ONE 0x011 zero reaches MTR.
        v.send_sys_mode_cmd();
        v.send_sys_pwr_cmd();
        v.send_sys_safety_sts();        // 1 zero frame
        v.mtr.tick(v.now_ms);
        CHECK(v.mtr.is_estop_active()); // still latched (baseline only)

        // (c) 0x110 MANUAL + fresh power cannot un-latch either (no 2nd zero).
        v.send_sys_mode_cmd();
        v.send_sys_pwr_cmd();
        v.mtr.tick(v.now_ms);
        CHECK(v.mtr.is_estop_active());
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Off);
    }

    // ── S4: operator reset -> two-frame 0x011 clear -> REARM_REQUIRED ──
    {
        std::printf("\n[S4] operator reset: two-frame clear releases latch into REARM\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        v.sys_safety.set_estop(true);
        v.hw_estop_pressed = true;
        for (int i = 0; i < 3; ++i) { v.sys_publish_tick(); v.advance(10); }
        CHECK(v.mtr.is_estop_active());

        // Operator releases hardware + presses START.
        v.sys_safety.set_estop(false);
        v.hw_estop_pressed = false;
        v.press_start_button();
        CHECK(v.sys_mode.mode() == can::Mode::Manual);

        // SYS now publishes two CONSECUTIVE advancing 0x011 zero frames.
        v.sys_publish_tick();           // zero frame #1 (baseline)
        CHECK(v.mtr.is_estop_active()); // not yet
        v.sys_publish_tick();           // zero frame #2 -> authorized clear
        CHECK(!v.mtr.is_estop_active());
        // Released into REARM_REQUIRED -> still stopped even with a drive cmd.
        auto out = v.send_host_drive(1500, 0);
        CHECK_EQ(out.speed_mmps, 1500);
        v.mtr.tick(v.now_ms);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Off);
        CHECK_EQ(v.dac.current_code(), 0);
    }

    // ── S5: full REARM restores AUTO drive ─────────────────────────
    {
        std::printf("\n[S5] REARM (fresh mode + power OFF->ON) restores drive\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        v.sys_safety.set_estop(true);
        v.hw_estop_pressed = true;
        for (int i = 0; i < 3; ++i) { v.sys_publish_tick(); v.advance(10); }
        // Reset -> two-frame clear.
        v.sys_safety.set_estop(false);
        v.hw_estop_pressed = false;
        v.press_start_button();
        v.sys_publish_tick();
        v.sys_publish_tick();
        CHECK(!v.mtr.is_estop_active());

        // REARM: fresh 0x110 (Manual or Auto) then 0x113 OFF -> ON via the
        // authority resolver. To send OFF then ON we toggle SYS mode to Manual
        // (power stays requested true -> resolver ON). Instead, directly issue
        // a power OFF edge then ON edge (SYS would do this on a restart).
        v.send_sys_mode_cmd();                    // fresh mode
        can::gen::SysPwrCmd off{0, v.pc.next()};  // power OFF edge
        can::Frame fo; can::gen::encode_sys_pwr_cmd(off, fo);
        v.mtr.handle_frame(fo, v.now_ms);
        can::gen::SysPwrCmd on{1, v.pc.next()};   // power ON edge
        can::Frame fn; can::gen::encode_sys_pwr_cmd(on, fn);
        v.mtr.handle_frame(fn, v.now_ms);
        v.mtr.tick(v.now_ms);
        // Still Manual: enter AUTO again and drive.
        v.press_mode_button();
        for (int i = 0; i < 2; ++i) { v.sys_publish_tick(); v.advance(10); }
        auto out = v.send_host_drive(1500, 0);
        CHECK_EQ(out.speed_mmps, 1500);
        v.mtr.tick(v.now_ms);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Drive);
        CHECK(v.dac.current_code() > 0);
    }

    // ── S6: RT MTR-feedback (0x206) heartbeat loss in AUTO ────────
    // Real rt::g_mtr_health (issue #8): while everything else stays fresh, a
    // stale 0x206 makes MTR unavailable -> RT zeroes setpoints. Confirmed
    // recovery requires consecutive fresh feedback at control cadence.
    {
        std::printf("\n[S6] RT MTR-feedback loss in AUTO -> failsafe -> confirmed recovery\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);

        // Warm up past the startup grace (3 s) and the MTR AUTO-entry acquire
        // grace (300 ms) with everything fresh so no early trip fires.
        while (v.now_ms < 3600u) {
            v.send_sys_heartbeat();
            v.send_host_heartbeat();
            v.send_host_drive(2000, 0);   // keeps g_last_mtr_feedback_us fresh
            v.sys_publish_tick();
            v.advance(10);
        }
        auto sr0 = v.rt_control_pass();
        CHECK(!sr0.zero_setpoints);       // healthy AUTO: no failsafe

        // MTR feedback stops (0x206 silence) while SYS + Host heartbeats and
        // SYS authority stay fresh -> the *dedicated* MTR-health trip fires.
        bool tripped = false;
        for (int i = 0; i < 40 && !tripped; ++i) {   // up to 400 ms
            v.send_sys_heartbeat();
            v.send_host_heartbeat();
            v.sys_publish_tick();          // no send_host_drive -> no fresh 0x206
            v.advance(10);
            auto sr = v.rt_control_pass();
            if (sr.zero_setpoints && sr.estop_reason == rt::kEstopReasonWatchdog)
                tripped = true;
        }
        CHECK(tripped);

        // Confirmed recovery: kMtrFbkRecoverFrames consecutive fresh 0x206
        // frames at the 10 ms control cadence release the trip.
        bool recovered = false;
        for (int i = 0; i < 10 && !recovered; ++i) {
            v.send_host_drive(2000, 0);    // fresh 0x206 again
            v.sys_publish_tick();
            v.advance(10);
            auto sr = v.rt_control_pass();
            if (!sr.zero_setpoints) recovered = true;
        }
        CHECK(recovered);
    }

    // ── S7: SYS heartbeat (0x7FE) loss -> RT fail-safe -> recovery ──
    {
        std::printf("\n[S7] SYS heartbeat loss -> RT motion prohibited -> recovery\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        while (v.now_ms < 3600u) {
            v.send_sys_heartbeat();
            v.send_host_heartbeat();
            v.send_host_drive(2000, 0);
            v.sys_publish_tick();
            v.advance(10);
        }
        // Stop SYS heartbeats (SYS node wedged) but keep MTR feedback + Host HB
        // fresh so only the SYS-heartbeat path can trip.
        bool tripped = false;
        for (int i = 0; i < 40 && !tripped; ++i) {
            v.send_host_heartbeat();
            v.send_host_drive(2000, 0);
            v.advance(10);                 // no send_sys_heartbeat / publish
            auto sr = v.rt_control_pass();
            if (sr.zero_setpoints) tripped = true;
        }
        CHECK(tripped);

        // Recovery: SYS resumes publishing; RT un-zeroes.
        bool recovered = false;
        for (int i = 0; i < 20 && !recovered; ++i) {
            v.sys_publish_tick();
            v.send_host_heartbeat();
            v.send_host_drive(2000, 0);
            v.advance(10);
            auto sr = v.rt_control_pass();
            if (!sr.zero_setpoints) recovered = true;
        }
        CHECK(recovered);
    }

    // ── S8: interleaved stress — brake req + mode toggle + ESTOP arrive ──
    // together while driving AUTO; the ESTOP must win and the whole reset +
    // rearm path must restore drive (combination that unit tests never try).
    {
        std::printf("\n[S8] interleaved: brake + mode-toggle + ESTOP, then full reset\n");
        Vehicle v;
        v.init();
        v.drive_to_active_auto(2000);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Drive);

        // A service brake request arrives with an ESTOP in the same control
        // tick. RT zeroes; ESTOP dominates over the drive command.
        g_brake_request_kpa.store(1500);
        v.send_host_drive(2000, 0);
        v.sys_safety.set_estop(true);
        v.hw_estop_pressed = true;
        for (int i = 0; i < 3; ++i) { v.sys_publish_tick(); v.advance(10); }
        CHECK(v.sys_mode.mode() == can::Mode::Estop);
        CHECK(v.mtr.is_estop_active());
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Off);
        CHECK_EQ(v.dac.current_code(), 0);
        g_brake_request_kpa.store(0);

        // A MODE button press must NOT clear the ESTOP (ignored in ESTOP).
        v.press_mode_button();
        CHECK(v.sys_mode.mode() == can::Mode::Estop);

        // Full reset + rearm restores AUTO drive.
        v.sys_safety.set_estop(false);
        v.hw_estop_pressed = false;
        v.press_start_button();
        CHECK(v.sys_mode.mode() == can::Mode::Manual);
        v.sys_publish_tick();             // 0x011 zero #1 (baseline)
        CHECK(v.mtr.is_estop_active());
        v.sys_publish_tick();             // zero #2 -> clear
        CHECK(!v.mtr.is_estop_active());
        v.send_sys_mode_cmd();
        can::gen::SysPwrCmd off{0, v.pc.next()};
        can::Frame fo; can::gen::encode_sys_pwr_cmd(off, fo);
        v.mtr.handle_frame(fo, v.now_ms);
        can::gen::SysPwrCmd on{1, v.pc.next()};
        can::Frame fn; can::gen::encode_sys_pwr_cmd(on, fn);
        v.mtr.handle_frame(fn, v.now_ms);
        v.mtr.tick(v.now_ms);
        v.press_mode_button();            // back to AUTO
        for (int i = 0; i < 2; ++i) { v.sys_publish_tick(); v.advance(10); }
        auto out = v.send_host_drive(1500, 0);
        CHECK_EQ(out.speed_mmps, 1500);
        v.mtr.tick(v.now_ms);
        CHECK_EQ((int)v.relays.state(), (int)mtr::RelayController::State::Drive);
        CHECK(v.dac.current_code() > 0);
    }

    std::printf("\n=== %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}


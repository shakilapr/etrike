// RM-ESP32-T12D gateway -> real rt-esp32 RX boundary.
//
// Purpose: the system testbench (testbench/) models rt with RtNode. This test
// instead feeds the *actual* rm::CanEmitter output into the *actual* rt-esp32
// RX router (rt::route_frame, rt-esp32/src/can_rx_router.h) and checks what the
// real firmware accepts/rejects on each bus.
//
// Key architectural fact under test: rt's Host command consumer
// (0x300/0x301/0x303) only accepts frames on the HIGH bus, while rt's SYS
// authority consumers (0x011/0x110) only accept frames on the LOW bus
// (can_rx_router.h:28/36/44 vs :64-84; can_dispatch.h:157). rm is a single-bus
// device (rm-esp32-t12d/src/main.cpp), so its RT-mode emulated SYS authority is
// only authoritative if it physically lands on rt's LOW bus.

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

#include "protocol/compat/can.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "can_rx_router.h"

#include "rm-esp32-t12d/src/config.h"
#include "rm-esp32-t12d/src/rc_decoder.h"
#include "rm-esp32-t12d/src/can_emitter.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; std::fprintf(stderr, "FAIL %s:%d (%s)\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { pass++; } \
    else { fail++; std::fprintf(stderr, "FAIL %s:%d (%lld != %lld)\n", __FILE__, __LINE__, \
        (long long)_a, (long long)_b); } \
} while (0)

namespace {

void fill_common(rm::RcSnapshot& snap) {
    snap.signal_valid       = true;
    snap.drive_enable_req   = true;
    snap.park_hold_req      = false;
    snap.gear               = can::Gear::D;
    snap.target_speed_mmps  = 1500;
    snap.steering_deg       = 12.3f;
    snap.brake_stroke_mm    = 5.0f;
    snap.aux_vra            = 0.5f;
}

std::vector<can::Frame> emit_rm(rm::CanEmitter& em, rm::OperatingMode mode, uint32_t tick) {
    rm::RcSnapshot snap{};
    snap.op_mode = mode;
    fill_common(snap);
    std::vector<can::Frame> out;
    em.emit_cluster(snap, tick, [&](can::Frame& f) -> bool {
        out.push_back(f);
        return true;
    });
    return out;
}

const can::Frame* find_id(const std::vector<can::Frame>& v, uint32_t id) {
    for (const auto& f : v) if (f.id == id) return &f;
    return nullptr;
}

bool has_id(const std::vector<can::Frame>& v, uint32_t id) { return find_id(v, id) != nullptr; }

}  // namespace

int main() {
    std::printf("\n=== RM Gateway -> Real RT RX Router ===\n");

    // ── RT mode: real rm emitter -> real rt::route_frame ────────────────
    {
        std::printf("\n[RT] rm RT cluster -> real rt router\n");
        rm::CanEmitter em;
        em.reset_counters();

        // tick 0 exercises the 10 Hz authority frames and the 2 Hz heartbeat.
        auto frames = emit_rm(em, rm::OperatingMode::Rt, 0);

        CHECK(has_id(frames, can::kIdHostSteerCmd));
        CHECK(has_id(frames, can::kIdHostBrakeReq));
        CHECK(has_id(frames, can::kIdHostDriveCmd));
        CHECK(has_id(frames, can::kIdHmiModeReq));
        CHECK(has_id(frames, can::kIdHmiPwrReq));
        CHECK(has_id(frames, can::kIdSysSafetySts));
        CHECK(has_id(frames, can::kIdSysModeCmd));
        CHECK(has_id(frames, can::kIdHostHeartbeat));

        can::Frame gw_lo{}, gw_hi{};
        can::gen::HostDriveCmd cmd{};
        can::gen::HostSteerCmd steer{};
        int32_t brake_kpa = -1;
        bool estop = false;
        uint8_t mode = 0xFF;
        bool mode_valid = false;
        rt::GatewayQueues q;
        q.gw_tx_low = &gw_lo; q.gw_tx_high = &gw_hi;
        q.cmd = &cmd; q.steer_cmd = &steer; q.brake_req_kpa = &brake_kpa;
        q.estop_flag = &estop; q.mode_from_sys = &mode; q.mode_valid = &mode_valid;

        // Host frames on HIGH are consumed and decoded by the real router.
        const can::Frame* d = find_id(frames, can::kIdHostDriveCmd);
        CHECK(rt::route_frame(*d, true, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ(cmd.speed_mmps, 1500);
        CHECK_EQ((int)cmd.gear, (int)can::Gear::D);

        const can::Frame* b = find_id(frames, can::kIdHostBrakeReq);
        CHECK(rt::route_frame(*b, true, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ(brake_kpa, (int32_t)std::round(5.0f / 27.0f * 20000.0f));

        const can::Frame* s = find_id(frames, can::kIdHostSteerCmd);
        CHECK(rt::route_frame(*s, true, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ(steer.steer_angle_0_1deg, 123);
        CHECK(steer.angle_valid);

        // HMI mode/power requests are transparently forwarded High -> Low.
        gw_lo = can::Frame{};
        CHECK(rt::route_frame(*find_id(frames, can::kIdHmiModeReq), true, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ((int)gw_lo.id, (int)can::kIdHmiModeReq);
        gw_lo = can::Frame{};
        CHECK(rt::route_frame(*find_id(frames, can::kIdHmiPwrReq), true, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ((int)gw_lo.id, (int)can::kIdHmiPwrReq);

        // 0x110 on the HIGH bus is NOT mode authority for real rt (waits LOW).
        // The system testbench RtNode accepts it on either bus, masking this.
        mode = 0xFF; mode_valid = false;
        CHECK(rt::route_frame(*find_id(frames, can::kIdSysModeCmd), true, q) == can::gen::CodecStatus::Ok);
        CHECK(!mode_valid);
        CHECK_EQ((int)mode, 0xFF);

        // 0x110 on the LOW bus (baseline then advancing counter) IS authority.
        const can::Frame* m110 = find_id(frames, can::kIdSysModeCmd);
        can::Frame base = *m110;
        base.data[1] = 0;
        CHECK(rt::route_frame(base, false, q) == can::gen::CodecStatus::Ok);
        can::Frame adv = *m110;
        adv.data[1] = 1;
        CHECK(rt::route_frame(adv, false, q) == can::gen::CodecStatus::Ok);
        CHECK(mode_valid);
        CHECK_EQ((int)mode, 1);

        // 0x011 on the HIGH bus is neither consumed nor forwarded by the router
        // (it is a Low->High forward only, owned by can_dispatch.h:157).
        gw_lo = can::Frame{}; gw_hi = can::Frame{};
        estop = false;
        CHECK(rt::route_frame(*find_id(frames, can::kIdSysSafetySts), true, q) == can::gen::CodecStatus::Ok);
        CHECK(!estop);
        CHECK_EQ((int)gw_lo.id, 0);
        CHECK_EQ((int)gw_hi.id, 0);

        // ...but the same 0x011 on the LOW bus is forwarded Low -> High for Host.
        gw_lo = can::Frame{}; gw_hi = can::Frame{};
        CHECK(rt::route_frame(*find_id(frames, can::kIdSysSafetySts), false, q) == can::gen::CodecStatus::Ok);
        CHECK_EQ((int)gw_hi.id, (int)can::kIdSysSafetySts);
    }

    // ── BARE mode: actuator frames decode on the low bus ────────────────
    {
        std::printf("\n[BARE] rm BARE cluster decode\n");
        rm::CanEmitter em;
        em.reset_counters();
        auto frames = emit_rm(em, rm::OperatingMode::Bare, 0);

        CHECK(has_id(frames, can::kIdVcuSesReq));
        CHECK(has_id(frames, can::kIdVcuSebReq));
        CHECK(has_id(frames, can::kIdRtDriveCmd));
        CHECK(has_id(frames, can::kIdSysModeCmd));
        CHECK(has_id(frames, can::kIdSysPwrCmd));
        CHECK(has_id(frames, can::kIdSysSafetySts));

        can::custom::ses::Command ses{};
        CHECK(can::custom::ses::decode_command(find_id(frames, can::kIdVcuSesReq)->view(), ses)
              == can::gen::CodecStatus::Ok);
        CHECK_EQ(ses.target_angle_raw, 30000 + 123);
        CHECK(ses.control_enable);

        can::custom::seb::Command seb{};
        CHECK(can::custom::seb::decode_command(find_id(frames, can::kIdVcuSebReq)->view(), seb)
              == can::gen::CodecStatus::Ok);
        CHECK_EQ((int)seb.stroke_request_raw, (int)((5.0f + 30.0f) / 0.05f));

        can::gen::RtDriveCmd drive{};
        CHECK(can::decode_frame(*find_id(frames, can::kIdRtDriveCmd), drive) == can::gen::CodecStatus::Ok);
        CHECK_EQ(drive.motor_speed_mmps, 1500);
        CHECK_EQ((int)drive.gear, (int)can::Gear::D);
    }

    // ── SYS mode: brake intent via 0x205, never a 0x7B9 collision ────────
    {
        std::printf("\n[SYS] rm SYS cluster decode\n");
        rm::CanEmitter em;
        em.reset_counters();
        auto frames = emit_rm(em, rm::OperatingMode::Sys, 0);

        CHECK(has_id(frames, can::kIdVcuSesReq));
        CHECK(has_id(frames, can::kIdRtBrakeCmd));
        CHECK(has_id(frames, can::kIdRtDriveCmd));
        CHECK(has_id(frames, can::kIdHmiModeReq));
        CHECK(has_id(frames, can::kIdHmiPwrReq));
        CHECK(has_id(frames, can::kIdRtHeartbeat));
        // rm must NOT emit the SYS-owned 0x7B9 brake command in SYS mode.
        CHECK(!has_id(frames, can::kIdVcuSebReq));

        can::gen::RtBrakeCmd brake{};
        CHECK(can::decode_frame(*find_id(frames, can::kIdRtBrakeCmd), brake) == can::gen::CodecStatus::Ok);
        CHECK_EQ(brake.brake_pressure_kpa, (int32_t)std::round(5.0f / 27.0f * 20000.0f));
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}

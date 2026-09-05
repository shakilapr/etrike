#include <cstdio>
#include <cstdint>

#include "protocol/compat/can_protocol.hpp"
#include "can_rx_router.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond) do { \
    if (cond) { pass++; } \
    else { fail++; std::fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); } \
} while (0)

static rt::GatewayQueues make_queues(can::Frame& low, can::Frame& high,
                                     can::gen::HostDriveCmd& cmd,
                                     int32_t& brake_kpa,
                                     bool& estop,
                                     uint8_t& mode,
                                     uint16_t& steer_angle,
                                     uint8_t& steer_status) {
    rt::GatewayQueues q;
    q.gw_tx_low = &low;
    q.gw_tx_high = &high;
    q.cmd = &cmd;
    q.brake_req_kpa = &brake_kpa;
    q.estop_flag = &estop;
    q.mode_from_sys = &mode;
    q.steer_feedback_angle = &steer_angle;
    q.steer_angle_status = &steer_status;
    return q;
}

int main() {
    std::printf("\n=== RT CAN RX Router ===\n\n");

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        fr.id = can::kIdSafetyEstop;
        rt::route_frame(fr, true, q);

        CHECK(estop);
        CHECK(low.id == 0);
        CHECK(high.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        can::gen::HostSteerCmd steer_cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);
        q.steer_cmd = &steer_cmd;

        can::gen::HostSteerCmd value{-123, true, 0, 42};
        can::encode_frame(value, fr);
        CHECK(rt::route_frame(fr, true, q) == can::gen::CodecStatus::Ok);
        CHECK(steer_cmd.steer_angle_0_1deg == -123);
        CHECK(steer_cmd.angle_valid);
        CHECK(steer_cmd.rolling_counter == 42);
        CHECK(low.id == 0);
        CHECK(high.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        can::gen::HostDriveCmd val{1234, -321, uint8_t(can::Gear::D)};
        can::encode_frame(val, fr);
        rt::route_frame(fr, true, q);

        CHECK(cmd.speed_mmps == 1234);
        CHECK(cmd.yaw_rate_mrad_s == -321);
        CHECK(cmd.gear == uint8_t(can::Gear::D));
        CHECK(low.id == 0);
        CHECK(high.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        // 0x110 SYS_MODE_CMD is DLC2 with a rolling counter; RT supervises it
        // via StreamValidity, so a baseline frame must be followed by an
        // advancing frame before the mode is accepted.
        fr.id = can::kIdSysModeCmd;
        fr.dlc = 2;
        fr.data[0] = uint8_t(can::Mode::Auto);
        fr.data[1] = 0;  // rolling counter (baseline)
        rt::route_frame(fr, false, q);
        CHECK(mode == 0);  // baseline frame not yet authoritative
        CHECK(low.id == 0);
        CHECK(high.id == 0);

        fr.data[1] = 1;  // advancing counter
        rt::route_frame(fr, false, q);
        CHECK(mode == uint8_t(can::Mode::Auto));
        CHECK(low.id == 0);
        CHECK(high.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        fr.id = can::kIdSbwStatus;
        fr.dlc = 8;
        fr.data[0] = 0x01;
        fr.data[2] = 0x30;
        fr.data[3] = 0x75;  // 30000 little-endian
        fr.data[7] = 0xBB;  // XOR(bytes 0-6) ^ 0xFF
        rt::route_frame(fr, false, q);

        CHECK(steer_angle == 30000);
        CHECK(steer_status == 1);
        CHECK(low.id == 0);
        CHECK(high.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        fr.id = can::kIdSysDiagRpt;
        fr.dlc = 8;
        rt::route_frame(fr, false, q);

        CHECK(high.id == can::kIdSysDiagRpt);
        CHECK(low.id == 0);
    }

    {
        can::Frame low{}, high{}, fr{};
        can::gen::HostDriveCmd cmd{};
        int32_t brake_kpa = 0;
        bool estop = false;
        uint8_t mode = 0, steer_status = 0;
        uint16_t steer_angle = 0;
        auto q = make_queues(low, high, cmd, brake_kpa, estop, mode, steer_angle, steer_status);

        can::gen::HostLightCmd val{true, false, true, false};
        can::encode_frame(val, fr);
        rt::route_frame(fr, true, q);

        CHECK(low.id == can::kIdHostLightCmd);
        CHECK(high.id == 0);
    }

    std::printf("\n=== %d pass, %d fail ===\n", pass, fail);
    return fail ? 1 : 0;
}

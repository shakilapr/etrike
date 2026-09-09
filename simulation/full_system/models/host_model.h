#pragma once
// HostModel — the external operator/HMI peer (HIGH bus only).
//
// Sends 0x300 HOST_DRIVE_CMD (10 ms) and 0x7FC HOST_HEARTBEAT (500 ms) using
// the real generated codec. RT is the sole consumer on the HIGH bus.
#include <cstdint>

#include "protocol/compat/can.hpp"
#include "runtime/sim_clock.h"
#include "runtime/virtual_can.h"

namespace sim {

class HostModel {
public:
    void attach(VirtualCanBus& bus) {
        // Host does not consume frames in the vertical slice; subscribing is
        // still useful later for gateway/estop observability.
        bus.subscribe(Bus::High,
                      [](int64_t, const etrike::protocol::Frame&) {});
    }

    void boot(int64_t /*now_us*/) {
        target_speed_mmps_ = 0;
        next_cmd_us_ = 0;
        next_hb_us_ = 0;
        cmd_ctr_ = 0;
    }

    void set_target_speed(int32_t mmps) noexcept { target_speed_mmps_ = mmps; }

    // Publish due frames for quantum `now_us`.
    void publish(int64_t now_us, VirtualCanBus& bus) {
        if (now_us >= next_cmd_us_) {
            next_cmd_us_ = now_us + 10'000;  // HOST_DRIVE_CMD @ 100 Hz
            can::gen::HostDriveCmd cmd{};
            cmd.speed_mmps = target_speed_mmps_;
            cmd.yaw_rate_mrad_s = 0;
            if (cmd.speed_mmps > 0)
                cmd.gear = uint8_t(can::Gear::D);
            else if (cmd.speed_mmps < 0)
                cmd.gear = uint8_t(can::Gear::R);
            else
                cmd.gear = uint8_t(can::Gear::N);
            can::Frame fr;
            if (can::encode_frame(cmd, fr) == can::gen::CodecStatus::Ok) {
                bus.transmit(Bus::High, fr);
                ++cmd_ctr_;
            }
        }
        if (now_us >= next_hb_us_) {
            next_hb_us_ = now_us + 500'000;  // HOST_HEARTBEAT @ 2 Hz
            can::gen::HostHeartbeat hb{};
            hb.alive_ctr = static_cast<uint8_t>(cmd_ctr_ & 0xFFu);
            can::Frame fr;
            if (can::encode_frame(hb, fr) == can::gen::CodecStatus::Ok)
                bus.transmit(Bus::High, fr);
        }
    }

private:
    int32_t target_speed_mmps_ = 0;
    int64_t next_cmd_us_ = 0;
    int64_t next_hb_us_ = 0;
    int     cmd_ctr_ = 0;
};

}  // namespace sim

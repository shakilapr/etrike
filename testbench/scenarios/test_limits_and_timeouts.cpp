// Section 9: actuator limit clamping and the rt host-heartbeat assisted stop.
#include <iostream>
#include <cmath>
#include "can_bus.hpp"
#include "virtual_can_bus.hpp"
#include "protocol/codecs/ses.hpp"
#include "protocol/codecs/seb.hpp"
#include "shared_config.h"
#include "nodes/mtr_node.hpp"
#include "nodes/sys_node.hpp"
#include "nodes/rt_node.hpp"
#include "nodes/seb_model.hpp"
#include "nodes/ses_model.hpp"
#include "nodes/rm_operator_model.hpp"

namespace testbench {

namespace {
bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

bool last_frame(VirtualCanBus& bus, uint32_t id, can::Frame& out) {
    auto frames = bus.find_frames(id);
    if (frames.empty()) return false;
    out = frames.back();
    return true;
}
}  // namespace

// 9.1 BARE steering raw clamp (+/- 45.0 deg == 29550..30450).
bool test_limit_steer_clamp_bare() {
    std::cout << "TEST: steering limit clamp BARE (raw 29550..30450)\n";
    VirtualCanBus bus("LOW_CAN");
    RmOperatorModel rm(bus);
    rm.init();
    rm.set_op_mode(rm::OperatingMode::Bare);

    auto ses_angle = [&](float deg) -> int {
        rm.drive(1500, deg);
        for (uint32_t t = 100; t <= 300; t += 10) { rm.step(t, 10); bus.tick(t, 10); }
        can::Frame f;
        if (!last_frame(bus, 0x169, f)) return 0;
        can::custom::ses::Command c{};
        if (can::custom::ses::decode_command(f, c) != can::gen::CodecStatus::Ok) return 0;
        bus.clear_trace();
        return c.target_angle_raw;
    };

    int right = ses_angle(100.0f);   // clamp to +45.0 -> 30450
    int left  = ses_angle(-100.0f);  // clamp to -45.0 -> 29550
    int zero  = ses_angle(0.0f);     // 30000

    std::cout << "  right=" << right << " (exp 30450)  left=" << left
              << " (exp 29550)  zero=" << zero << " (exp 30000)\n";
    if (right != 30450 || left != 29550 || zero != 30000) {
        std::cerr << "  FAIL: steering clamp incorrect\n";
        return false;
    }
    return true;
}

// 9.2 RT drive speed + brake pressure clamps.
bool test_limit_rt_drive_clamp() {
    std::cout << "TEST: RT drive/brake limit clamp (speed 3000/-500, pressure 20000)\n";
    VirtualCanBus bus("LOW_CAN");
    RmOperatorModel rm(bus);
    rm.init();
    rm.set_op_mode(rm::OperatingMode::Rt);

    auto speed = [&](int32_t spd, can::Gear g) -> int32_t {
        rm.set_gear(g);
        rm.drive(spd, 0.0f, g);
        for (uint32_t t = 100; t <= 300; t += 10) { rm.step(t, 10); bus.tick(t, 10); }
        can::Frame f; can::gen::HostDriveCmd c{};
        int32_t v = -99999;
        if (last_frame(bus, 0x300, f) &&
            can::gen::decode_host_drive_cmd(f, c) == can::gen::CodecStatus::Ok) v = c.speed_mmps;
        bus.clear_trace();
        return v;
    };
    auto pressure = [&](float mm) -> int32_t {
        rm.drive(0, 0.0f, can::Gear::D);
        rm.set_brake_stroke_mm(mm);
        for (uint32_t t = 100; t <= 300; t += 10) { rm.step(t, 10); bus.tick(t, 10); }
        can::Frame f; can::gen::HostBrakeReq c{};
        int32_t v = -1;
        if (last_frame(bus, 0x301, f) &&
            can::gen::decode_host_brake_req(f, c) == can::gen::CodecStatus::Ok) v = c.brake_pressure_kpa;
        bus.clear_trace();
        return v;
    };

    int32_t fwd = speed(9999, can::Gear::D);
    int32_t rev = speed(-9999, can::Gear::R);
    int32_t pfull = pressure(27.0f);
    int32_t pover = pressure(100.0f);

    std::cout << "  fwd=" << fwd << " (exp 3000)  rev=" << rev << " (exp -500)"
              << "  pfull=" << pfull << "  pover=" << pover << " (exp 20000)\n";
    if (fwd != 3000 || rev != -500 || pfull != 20000 || pover != 20000) {
        std::cerr << "  FAIL: RT drive/brake clamp incorrect\n";
        return false;
    }
    return true;
}

// 9.3 RT steer clamp (0.1 deg units, +/-450).
bool test_limit_rt_steer_clamp() {
    std::cout << "TEST: RT steer limit clamp (0.1deg, +/-450)\n";
    VirtualCanBus bus("LOW_CAN");
    RmOperatorModel rm(bus);
    rm.init();
    rm.set_op_mode(rm::OperatingMode::Rt);
    rm.drive(0);

    auto steer = [&](float deg) -> int {
        rm.set_steering_deg(deg);
        for (uint32_t t = 100; t <= 300; t += 10) { rm.step(t, 10); bus.tick(t, 10); }
        can::Frame f; can::gen::HostSteerCmd c{};
        int v = 0;
        if (last_frame(bus, 0x303, f) &&
            can::gen::decode_host_steer_cmd(f, c) == can::gen::CodecStatus::Ok) v = c.steer_angle_0_1deg;
        bus.clear_trace();
        return v;
    };
    int right = steer(100.0f);   // +450
    int left  = steer(-100.0f);  // -450
    std::cout << "  right=" << right << " (exp 450)  left=" << left << " (exp -450)\n";
    if (right != 450 || left != -450) { std::cerr << "  FAIL: RT steer clamp incorrect\n"; return false; }
    return true;
}

// 9.5 SES/SEB status frames must use the canonical bit layout (decodable by
// the frozen-protocol decoders, with valid XOR checksums).
bool test_status_frames_canonical() {
    std::cout << "TEST: SES/SEB status frames decode via canonical decoders\n";
    VirtualCanBus low("LOW_CAN");
    SesModel ses(low);
    SebModel seb(low);
    low.register_node(NodeId::SES, [&](const etrike::protocol::Frame& f) { ses.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SEB, [&](const etrike::protocol::Frame& f) { seb.receive_can("LOW_CAN", f); });
    ses.init();
    seb.init();

    can::custom::ses::Command sc{};
    sc.alignment_enable = true;
    sc.control_enable = true;
    sc.target_angle_raw = 30200;  // +20.0 deg
    sc.target_speed_raw = 328;
    sc.rolling_counter = 0;
    can::Frame sf;
    if (can::custom::ses::encode_command(sc, sf) == can::gen::CodecStatus::Ok) {
        low.send(NodeId::RT, sf);
    }

    can::custom::seb::Command bc{};
    bc.alignment_enable = true;
    bc.control_enable = true;
    bc.control_mode = can::custom::seb::ControlMode::Stroke;
    bc.stroke_request_raw = static_cast<uint16_t>(
        std::round((15.0f - shared::kBrakeStrokeOffset) / shared::kBrakeStrokeScale));
    bc.rolling_counter = 0;
    can::Frame bf;
    if (can::custom::seb::encode_command(bc, bf) == can::gen::CodecStatus::Ok) {
        low.send(NodeId::SYS, bf);
    }

    for (uint32_t t = 100; t <= 1200; t += 10) {
        ses.step(t, 10);
        seb.step(t, 10);
        low.tick(t, 10);
    }

    can::Frame s201, s721;
    if (!last_frame(low, 0x201, s201) || !last_frame(low, 0x721, s721)) {
        std::cerr << "  FAIL: status frames not emitted\n";
        return false;
    }
    can::custom::ses::Status ss{};
    can::custom::seb::Status bs{};
    bool ses_ok = (can::custom::ses::decode_status(s201.view(), ss) == can::gen::CodecStatus::Ok);
    bool seb_ok = (can::custom::seb::decode_status(s721.view(), bs) == can::gen::CodecStatus::Ok);

    std::cout << "  SES decode=" << ses_ok << " angle=" << ss.steering_angle_raw
              << "  SEB decode=" << seb_ok << " stroke_raw=" << bs.stroke_value_raw << "\n";

    if (!ses_ok || !seb_ok) {
        std::cerr << "  FAIL: hand-rolled status layout diverged from canonical codec\n";
        return false;
    }
    if (std::abs(static_cast<int>(ss.steering_angle_raw) - 30200) > 20) {
        std::cerr << "  FAIL: SES status angle wrong\n";
        return false;
    }
    float stroke_mm = static_cast<float>(bs.stroke_value_raw) * shared::kBrakeStrokeScale
                      + shared::kBrakeStrokeOffset;
    if (std::abs(stroke_mm - 15.0f) > 0.5f) {
        std::cerr << "  FAIL: SEB status stroke wrong (" << stroke_mm << " mm)\n";
        return false;
    }
    return true;
}

// 9.4 rt host-heartbeat timeout -> assisted stop (2000 kPa) + zero drive.
bool test_timeout_rt_host_heartbeat() {
    std::cout << "TEST: rt host-heartbeat timeout -> assisted stop (2000 kPa)\n";
    VirtualCanBus high("HIGH_CAN");
    VirtualCanBus low("LOW_CAN");
    RmOperatorModel rm(high);
    RtNode rt(high, low);
    SysNode sys(low);
    MtrNode mtr(low);
    SebModel seb(low);
    high.register_node(NodeId::RM, [&](const etrike::protocol::Frame& f) { rm.receive_can("HIGH_CAN", f); });
    high.register_node(NodeId::RT, [&](const etrike::protocol::Frame& f) { rt.receive_can("HIGH_CAN", f); });
    low.register_node(NodeId::RT,  [&](const etrike::protocol::Frame& f) { rt.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SYS, [&](const etrike::protocol::Frame& f) { sys.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::MTR, [&](const etrike::protocol::Frame& f) { mtr.receive_can("LOW_CAN", f); });
    low.register_node(NodeId::SEB, [&](const etrike::protocol::Frame& f) { seb.receive_can("LOW_CAN", f); });
    rm.init(); rt.init(); sys.init(); mtr.init(); seb.init();
    rm.set_op_mode(rm::OperatingMode::Rt);
    rm.drive(1500);
    sys.press_start_button();
    for (uint32_t t = 100; t <= 1500; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); seb.step(t, 10); rm.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool driving = mtr.is_traction_enabled();

    high.drop(0x7FC, 12);  // rm (Host emulation) is on HIGH; drop ~6 s of heartbeats (> 1500 ms timeout)
    for (uint32_t t = 1510; t <= 3400; t += 10) {
        rt.step(t, 10); sys.step(t, 10); mtr.step(t, 10); seb.step(t, 10); rm.step(t, 10);
        high.tick(t, 10); low.tick(t, 10);
    }
    bool hb_lost = rt.is_host_heartbeat_lost();
    bool stopped = !mtr.is_traction_enabled();
    float seb_mm = seb.actual_stroke_mm();  // 2000/20000*27 = 2.7 mm

    std::cout << "  driving=" << driving << " hb_lost=" << hb_lost
              << " mtr_stopped=" << stopped << " SEB=" << seb_mm << "mm (exp ~2.7)\n";
    if (!driving || !hb_lost || !stopped || !near(seb_mm, 2.7f, 0.6f)) {
        std::cerr << "  FAIL: rt host-heartbeat assisted stop incorrect\n";
        return false;
    }
    return true;
}

}  // namespace testbench

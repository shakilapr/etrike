// Deterministic simulator implementation (see simulator.h).

#include "platform/host/simulator.h"

#include "protocol/adapters.h"
#include "protocol/route_table.h"

namespace rta {

namespace {
constexpr TimeUs kControlCycleUs = 10'000;   // 10 ms (CPU1 control)
}  // namespace

Simulator::Simulator() {
    m_motion.init();
}

void Simulator::inject_host_drive(std::int32_t speed_mmps, std::int32_t yaw_mrad_s,
                                  std::uint8_t gear) {
    Frame frame;
    gen::HostDriveCmd cmd{};
    cmd.speed_mmps = speed_mmps;
    cmd.yaw_rate_mrad_s = yaw_mrad_s;
    cmd.gear = gear;
    if (etrike::protocol::succeeded(gen::encode(cmd, frame))) {
        m_can.transmit(rta::hal::Bus::High, frame);
    }
}

void Simulator::inject_host_heartbeat(std::uint8_t alive) {
    Frame frame;
    gen::HostHeartbeat hb{};
    hb.alive_ctr = alive;
    hb.health_flags = 0x0F;
    if (etrike::protocol::succeeded(gen::encode(hb, frame))) {
        m_can.transmit(rta::hal::Bus::High, frame);
    }
}

void Simulator::inject_mtr(std::int16_t speed_mmps, std::uint8_t gear, std::uint8_t faults) {
    Frame frame;
    gen::MtrMotorFbk fbk{};
    fbk.actual_speed_mmps = speed_mmps;
    fbk.gear_state = gear;
    fbk.fault_flags = faults;
    if (etrike::protocol::succeeded(gen::encode(fbk, frame))) {
        m_can.transmit(rta::hal::Bus::Low, frame);
    }
}

void Simulator::inject_ses(std::int16_t angle_0_1deg, bool aligned) {
    Frame frame = Frame::standard(0x201u, 8u);
    frame.data[0] = aligned ? 0x01u : 0u;
    frame.data[2] = static_cast<std::uint8_t>(angle_0_1deg & 0xFFu);
    frame.data[3] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(angle_0_1deg) >> 8u) & 0xFFu);
    frame.data[6] = 0x03u;
    std::uint8_t cks = 0;
    for (int i = 0; i < 7; ++i) cks ^= frame.data[i];
    frame.data[7] = cks ^ 0xFFu;
    m_can.transmit(rta::hal::Bus::Low, frame);
}

void Simulator::inject_seb(std::uint16_t stroke_raw, bool aligned) {
    Frame frame = Frame::standard(0x721u, 8u);
    frame.data[0] = aligned ? 0x01u : 0u;
    frame.data[2] = static_cast<std::uint8_t>(stroke_raw & 0xFFu);
    frame.data[3] = static_cast<std::uint8_t>((stroke_raw >> 8u) & 0xFFu);
    frame.data[6] = 0x03u;
    std::uint8_t cks = 0;
    for (int i = 0; i < 7; ++i) cks ^= frame.data[i];
    frame.data[7] = cks ^ 0xFFu;
    m_can.transmit(rta::hal::Bus::Low, frame);
}

void Simulator::inject_estop(rta::hal::Bus b) {
    Frame frame = Frame::standard(0x001u, 0u);
    m_can.transmit(b, frame);
}

// CPU0: data-plane executor — decode received frames into typed inputs.
void Simulator::executor_cpu0() {
    etrike::protocol::Frame frame;
    while (m_can.receive(rta::hal::Bus::High, frame)) {
        FrameView view = frame.view();
        DriveDemand d;
        if (decode_host_drive(view, d)) { m_drive_demand = d; }
        auto brake = decode_host_brake(view);
        if (brake.valid) { m_brake_kpa = brake.brake_kpa; }
        ModeRequest mr;
        if (decode_hmi_mode(view, mr)) { m_mode_req = mr; }
        gen::HostHeartbeat hb;
        if (etrike::protocol::succeeded(gen::decode(view, hb))) {
            ++m_host_counter;
            m_host_fresh = true;
        }
    }
    while (m_can.receive(rta::hal::Bus::Low, frame)) {
        FrameView view = frame.view();
        MotorFeedback mf;
        if (decode_mtr_motor(view, mf)) {
            m_motor_fb = mf;
            m_mtr_fresh = true;
        }
        SteeringFeedback sf;
        if (decode_ses_status(view, sf)) { m_steer_fb = sf; }
        BrakeFeedback bf;
        if (decode_seb_status(view, bf)) { m_brake_fb = bf; }
    }
}

// CPU1: safety + motion executor (100 Hz control, 50 Hz brake, 20 Hz safety,
// 10 Hz health).
void Simulator::executor_cpu1() {
    // Control at 100 Hz.
    m_motion.control(m_now, m_drive_demand, m_motor_fb, m_steer_fb, m_brake_fb,
                     m_host_counter, m_host_fresh, m_mtr_fresh, m_obstacle_mm, m_out);

    // Brake at 50 Hz (every 2nd cycle).
    if (++m_cpu1_brake_ticks >= 2) {
        m_cpu1_brake_ticks = 0;
        // Brake FSM already stepped inside control(); here we could re-issue.
    }

    // Safety at 20 Hz (every 5th cycle).
    if (++m_cpu1_safety_ticks >= 5) {
        m_cpu1_safety_ticks = 0;
    }

    // Health at 10 Hz (every 10th cycle).
    if (++m_cpu1_health_ticks >= 10) {
        m_cpu1_health_ticks = 0;
        // Watchdog decision would gate wdt.service() here; for the sim we
        // service when the motion output shows no ESTOP.
        if (!m_out.estop_required) {
            m_wdt.service();
        }
    }

    // Mode via controller (button inputs from GPIO).
    m_motion.mode_tick(m_gpio.read(rta::hal::InputSignal::ModeBtn),
                       m_gpio.read(rta::hal::InputSignal::StartBtn));
}

// CPU2: body executor (lights 50 Hz, mode 100 Hz).
void Simulator::executor_cpu2() {
    if (++m_cpu2_lights_ticks >= 5) {
        m_cpu2_lights_ticks = 0;
        rta::LightState requested;
        requested.head = m_gpio.output(rta::hal::OutputSignal::Headlight);
        BodyController::Output body_out;
        m_body.update(m_motion.mode(), requested, body_out);
        m_gpio.write(rta::hal::OutputSignal::BrakeLight, body_out.lights.brake);
    }
    if (++m_cpu2_mode_ticks >= 10) {
        m_cpu2_mode_ticks = 0;
        m_motion.apply_hmi(m_mode_req);
        m_mode_req.valid = false;  // consume
    }
}

void Simulator::step() {
    // Host freshness decays if no heartbeat seen this cycle.
    m_host_fresh = false;
    m_mtr_fresh = false;

    executor_cpu0();
    executor_cpu1();
    executor_cpu2();

    m_now += kControlCycleUs;
}

}  // namespace rta

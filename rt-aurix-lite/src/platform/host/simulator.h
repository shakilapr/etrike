#pragma once
// Deterministic three-domain system simulation.
//
// Executors call the same app/domain functions the future runtime will
// call, at the architecture's required periods (architecture.md §6.3),
// without pretending they are FreeRTOS tasks. It drives the controllers
// over virtual CAN/GPIO/clock/watchdog and supports fault injection.
//
//   Virtual monotonic clock
//        |
//   +----+----+----+
//   |    |    |    |
//  CPU0  CPU1  CPU2      executors (data-plane / safety+motion / body)
//   |    |    |
//   +- IPC / snapshots -+
//        |
//   virtual CAN_HIGH / CAN_LOW, GPIO, watchdog

#include <cstdint>
#include <functional>

#include "core/time.h"
#include "core/types.h"
#include "app/controllers.h"
#include "platform/host/virtual_can.h"
#include "platform/host/virtual_io.h"

namespace rta {

// Fault-injection scenario selector for the simulator test.
enum class SimScenario {
    Nominal,
    EstopDuringDrive,     // ESTOP arrives while a drive command updates
    CanLowBusOffDuringBrake, // CAN_LOW bus-off during a brake request
    HostHeartbeatFreeze,  // host heartbeat freezes -> assisted stop
    MtrFeedbackFreeze,    // MTR feedback freezes -> zero setpoints
};

// Deterministic simulator: advances a virtual clock and runs the CPU0/1/2
// executors at their configured periods. The test drives the scenario and
// asserts the expected degradation.
class Simulator {
public:
    Simulator();

    // Run the three executors for one control cycle (10 ms) at the current
    // virtual time, then advance the clock. Host frame injection is applied
    // by the caller via the virtual CAN buses before stepping.
    void step();

    // Convenience: run N control cycles.
    void run(std::uint32_t cycles) { for (std::uint32_t i = 0; i < cycles; ++i) step(); }

    // Inject a synthetic host drive command (0x300) on the high bus.
    void inject_host_drive(std::int32_t speed_mmps, std::int32_t yaw_mrad_s, std::uint8_t gear);

    // Inject host heartbeat (0x7FC) on the high bus.
    void inject_host_heartbeat(std::uint8_t alive);

    // Inject MTR motor feedback (0x206) on the low bus.
    void inject_mtr(std::int16_t speed_mmps, std::uint8_t gear, std::uint8_t faults);

    // Inject SES status (0x201) on the low bus (steering feedback).
    void inject_ses(std::int16_t angle_0_1deg, bool aligned);

    // Inject SEB status (0x721) on the low bus (brake feedback).
    void inject_seb(std::uint16_t stroke_raw, bool aligned);

    // Inject an ESTOP frame (0x001) on the given bus.
    void inject_estop(rta::hal::Bus b);

    // Read the latest computed drive/steer/brake outputs (what CPU1 produced).
    const MotionController::Output& motion_output() const { return m_out; }

    // Access virtual buses for fault injection.
    rta::platform::host::VirtualCan& can() { return m_can; }
    rta::platform::host::VirtualGpio& gpio() { return m_gpio; }
    rta::platform::host::VirtualWatchdog& wdt() { return m_wdt; }

    TimeUs now_us() const { return m_now; }

    // Drain received frames into the controllers (CPU0 data-plane executor).
    void executor_cpu0();

private:
    void executor_cpu1();
    void executor_cpu2();

    rta::platform::host::VirtualCan   m_can;
    rta::platform::host::VirtualGpio  m_gpio;
    rta::platform::host::VirtualClock m_clock;
    rta::platform::host::VirtualWatchdog m_wdt;
    MotionController m_motion;
    BodyController   m_body;
    GatewayController m_gateway;

    TimeUs m_now = 0;
    // Frame counts for sub-period executors.
    std::uint32_t m_cpu1_brake_ticks = 0;
    std::uint32_t m_cpu1_safety_ticks = 0;
    std::uint32_t m_cpu1_health_ticks = 0;
    std::uint32_t m_cpu2_lights_ticks = 0;
    std::uint32_t m_cpu2_mode_ticks = 0;

    // Latest decoded inputs (from CPU0 decode).
    DriveDemand    m_drive_demand;
    MotorFeedback  m_motor_fb;
    SteeringFeedback m_steer_fb;
    BrakeFeedback  m_brake_fb;
    ModeRequest    m_mode_req;
    std::int32_t   m_brake_kpa = 0;
    bool           m_host_fresh = true;
    bool           m_mtr_fresh = true;
    std::uint32_t  m_obstacle_mm = 3000;  // clear
    std::uint8_t   m_host_counter = 0;

    MotionController::Output m_out;
};

}  // namespace rta

# Hardware-in-the-loop (HIL) and Synthetic Peers

Developing embedded systems for complex vehicles is challenging when the full physical hardware (actuators, sensors, other ECUs) isn't always available on your desk. To enable rapid development and testing, the E-Trike project uses built-in **bench bypasses** and **synthetic CAN peers**.

---

## 1. System Run Modes

The E-Trike firmware includes a unified configuration mechanism (`SYSTEM_RUN_MODE` in `shared/system_mode.h`) that allows the same binary to behave differently during development.

| Mode | Name | Effect |
|------|------|--------|
| **0** | PRODUCTION | Strict safety. Requires real hardware and physical signals. No software bypasses allowed. |
| **1** | PROTOTYPE | Checks a physical developer override pin (e.g., GPIO 35 jumped to GND) to dynamically enable certain safety bypasses. Allows lab testing on real hardware without flashing a compromised binary. |
| **2** | PURE SIM | Disables cross-ECU heartbeat timeouts and actuator syncs. Used for pure software-in-the-loop testing. |

This ensures that development features don't accidentally make it into a production vehicle, as Mode 0 strictly refuses to start without valid hardware connections.

---

## 2. The Debug Tool and Synthetic Peers

Even with bypasses enabled, safety logic (like the EGAS L2 monitor) requires realistic data on the CAN bus to function correctly. If the physical Motor Controller (MTR) is missing from the bench, the System ECU (SYS) will trigger a timeout and enter ESTOP.

To solve this without modifying the safety logic, the project provides a **Debug Tool**.

The Debug Tool connects to the CAN bus (usually via a CANalyst-II USB adapter) and acts as a "synthetic peer." It injects perfectly formatted, cyclic CAN frames that pretend to be the missing hardware.

### Common Synthetic Peers

- **Synthetic EPS-C (Steering):** Injects `0x201 SES_STATUS` at 100 Hz, reporting that the steering is centered and healthy. This satisfies the RT ECU's steering sync requirements.
- **Synthetic MTR (Motor):** Injects `0x206 MTR_MOTOR_FBK` at 50 Hz. This provides the speed feedback necessary for the SYS ECU's EGAS L2 monitor to function.
- **Synthetic Heartbeats:** Injects `0x7FE SYS_HEARTBEAT` or `0x7FD RT_HEARTBEAT` to prevent the ECUs from entering a cross-node timeout state when tested individually.
- **Synthetic Host (Jetson):** Injects `0x300 HOST_DRIVE_CMD` to command movement without needing the full ROS 2 stack running on a Jetson Orin.

### Dependency Injection via CAN

This concept is effectively dependency injection, but at the physical network layer. The ECU under test does not know whether the `0x206` frame came from a real STM32 microcontroller or from a Python script running on a developer's laptop. 

This enables robust testing of the safety and control state machines in isolation, proving that the software reacts correctly to both healthy and faulty inputs before it is ever connected to a real vehicle.

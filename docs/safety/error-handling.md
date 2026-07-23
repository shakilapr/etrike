# ETrike Error Handling & Safety Architecture

This document outlines the error handling, safety mechanisms, queuing strategies, and recovery behaviors implemented across the RT (Real-Time), SYS (System), and Protocol layers in the ETrike system.

## 1. System Architecture: Controllers & Busses

The error handling architecture spans multiple specialized hardware controllers and communication busses:

* **ESP32-S3 Microcontrollers**: Act as the primary execution hardware for both the `sys-esp32` (System) and `rt-esp32` (Real-Time) layers. These handle high-level logic, routing, and safety monitoring.
* **SES (Steer-by-Wire) Controller**: A dedicated actuator controller responsible for steering torque and angle. Communicates health and fault data over the CAN bus.
* **SEB (Brake-by-Wire) Controller**: A dedicated actuator controller for the braking system.
* **Motor Controller**: Manages traction and speed, reporting diagnostic telemetry.
* **Dual CAN Bus Architecture**: The system utilizes a separated dual CAN bus topology (CAN High and CAN Low). The `rt-esp32` acts as a secure gateway, routing critical data between the busses (e.g., via `gw_tx_low_q` and `gw_tx_high_q`) while isolating faults.

## 2. Asynchronous Task Management & RTOS Queuing

> [!WARNING]
> **The Danger of Non-Real-Time Queues in Autonomous Driving**
> In non-real-time systems (like standard Linux/Python), queues can grow infinitely, consume memory unpredictably, and introduce massive latency. This is highly problematic in autonomous driving because a controller might process "stale" data (e.g., executing a braking command based on where the vehicle was 2 seconds ago). 

FreeRTOS provides bounded storage and deterministic scheduling, but a bounded
FIFO can still contain stale control values. The ETrike transport therefore
uses different policies for state and events:

* **Periodic actuator state**: At most one frame of each actuator command may
  be pending/in flight. New control cycles regenerate current state instead of
  queuing history.
* **Non-blocking CAN submission**: Real-time TX never waits for driver queue
  capacity while holding control or lifecycle resources.
* **Bounded event queues**: Safety events and diagnostics retain bounded queues
  where event ordering matters.
* **ESTOP priority**: `kIdSafetyEstop` uses front-of-queue delivery and is
  latched independently of optional-hardware bypass.

## 3. Fault Detection & Validation

The system evaluates faults primarily through CAN telemetry decoded by the `protocol` layer, with the real-time layers taking definitive safety actions.

### Actuator Faults (SES & SEB)
* **Steer-by-Wire (SES)**: The system monitors the `SES_ErrInfo` frame (0x202) for Level 3 hardware faults, specifically extracting `angle_faults` and `torque_faults`. If the SES controller sets any fault bits, the system immediately flags a `rt::kEstopReasonInternal` and pushes an `ESTOP` to the safety queue.
* **Brake-by-Wire (SEB)**: The `SEB_STATUS` frame (0x721) monitors the `error_status` field.
  > [!TIP]
  > **Signal Integrity Verification:** To prevent spurious ESTOPs triggered by CAN bus noise on the physical wires, SEB error checks (e.g., `error_status == 3`) are strictly evaluated *after* payload checksum validation.

### Telemetry Boundaries
The dispatcher continuously monitors operational boundaries via diagnostic frames (e.g., `SES_Test` 0x6FA, `SEB_Test` 0x6FB) and will trigger warnings for:
* **ECU Temperature**: `> 85.0°C`
* **Motor Current**: `> 30.0 A`
* **Supply Voltage**: `< 10.0 V`

## 4. Core Safety Mechanisms

* **Safety Monitor Subsystem**: A dedicated `SafetyMonitor` object acts as the state machine for transitioning between normal operations, internal fault states, and emergency stops.
* **Multi-Task Watchdog**: The `sys-esp32` layer runs a multi-task software watchdog. Per-task atomic counters (e.g., `g_alive_safety`, `g_alive_motor`) are incremented during their execution loops. A supervisor task monitors these; if any task freezes, the watchdog forces a safe state.
* **Staleness Tracking (Data Freshness)**: Drive and brake consumers enforce
  deadlines derived from their declared cycle times. A timed-out actuator
  command is replaced by its defined safe output and is not refreshed by old
  queued data.
* **Dependency policy**: Developer bypass suppresses faults caused only by
  intentionally absent hardware. It never suppresses physical ESTOP, valid CAN
  ESTOP, explicit ESTOP mode, or local watchdog failures.

## 5. Recovery Procedures

RT prevents the common startup Bus-Off by keeping operational TX closed until a
valid low-bus peer is observed. The controller remains receive-capable and ACKs
the SYS bootstrap traffic.

RT does not automatically recover a Bus-Off controller. ESP-IDF may immediately
resume frames retained in its transmit queue after recovery. Production
therefore remains ESTOP-latched and TX-closed until controlled restart. In
developer bypass, intentionally missing peers produce a degraded/unavailable
state rather than a synthetic ESTOP.

See
[`can-realtime-startup-and-bypass.md`](../communications/can-realtime-startup-and-bypass.md)
for the complete transport, deadline, ACK, and bypass contract.

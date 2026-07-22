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

While standard queues are highly problematic for autonomous driving, the ETrike system utilizes **FreeRTOS** to provide strict real-time queuing guarantees that solve these exact issues:

* **Bounded Sizes (No Infinite Queues)**: Communication channels like `g_safety_evt_q`, `g_can_rx_low_q`, and `g_can_rx_high_q` have strict, pre-allocated maximum sizes. They cannot grow infinitely, preventing unpredictable memory allocation delays.
* **Strict Preemption**: FreeRTOS employs priority-based preemptive scheduling. If a message enters a queue monitored by a high-priority task (e.g., the Safety Monitor), the OS instantly interrupts lower-priority tasks to process the event, guaranteeing low latency.
* **Jumping the Line (LIFO for Emergencies)**: Critical safety events (e.g., `kIdSafetyEstop`) bypass normal FIFO queue logic. They are dispatched using `xQueueSendToFront()` to jump to the front of the line and guarantee immediate processing over normal bus traffic.
* **Deterministic Timeouts**: Queue submissions and reads use strict time limits (e.g., `pdMS_TO_TICKS(10)`). This prevents a faulty or blocked consumer task from permanently locking up the producer task.

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
* **Staleness Tracking (Data Freshness)**: To solve the problem of processing "old" data that got stuck in a queue or dropped by a controller, the `protocol` layer implements `FreshnessTracker` and `CounterTracker` classes. These enforce time-to-live (TTL) on CAN frames. If the SES or SEB stops communicating, the freshness tracker flags a timeout, effectively treating the missing node as a fault.

## 5. Recovery Procedures

The system is designed to recover gracefully from transient hardware issues without requiring full power-cycling.

* **Soft Bus-Off Recovery (TWAI/CAN)**: 
  The `can_driver.h` monitors the Transmit Error Counter (TEC) and Receive Error Counter (REC) of the physical CAN controllers. If the controller enters a bus-off state due to electrical faults, it attempts a "soft recovery" using `twai_initiate_recovery()`. A full driver reinstall is only executed as a last-resort fallback.
* **Logical Recovery Events**: 
  The `FreshnessTracker` and `CounterTracker` support specific `Recovery` events (`FreshnessEvent::Recovery`, `CounterEvent::Recovery`). When a node recovers from a timeout or counter freeze, these events alert the higher-level dispatchers to logically resume operations safely, rather than requiring a hard reset.

# The Testing Architecture

When working on a complex embedded system like the E-Trike, testing isn't as simple as just running `npm test` or `pytest`. Because our code runs on microcontrollers (ESP32, STM32) and interacts with physical hardware (motors, CAN buses), we have to test it in layers. 

It can be confusing to see "native tests," "simulation," and "HIL" all mixed together. This note explains what each testing level does and *why* we need it.

---

## 1. Schema & Golden Vector Tests (The Contract)

Before any code is compiled for an ECU, we test the **protocol itself**.

- **What it is:** Tests that verify the `yaml` CAN dictionaries compile successfully, and that the "Golden Vectors" (raw hex payloads) match our expected decoded values.
- **Where it runs:** On your computer (host) in Python/C++ during CI.
- **What it proves:** That the RT ECU (C++) and Jetson (Python) agree on what a "Drive Command" looks like on the wire.
- **What it DOES NOT prove:** That the ECU actually sends the message on time, or that the motor moves.

## 2. Native Component Tests (The Logic)

We want to test our core logic (like physics calculations, PID controllers, and the Mode State Machine) extremely fast, without waiting 30 seconds to flash an ESP32.

- **What it is:** PlatformIO's `[env:native]` environment. It compiles the C++ codebase for your laptop's CPU (Windows/Mac/Linux) instead of the ESP32. It uses "Shadow HAL" headers to mock out the hardware-specific stuff (like reading a physical GPIO pin).
- **Where it runs:** On your computer (`pio test -e native`).
- **What it proves:** That the inverse bicycle model calculates the correct steering angle, or that the Mode FSM correctly drops back to MANUAL if a timeout occurs.
- **What it DOES NOT prove:** That the FreeRTOS scheduler handles interrupts correctly, or that the SPI bus to the CAN controller is working.

## 3. Simulation & Replays (The Network)

Sometimes we need to test how the system reacts to network traffic without setting up a full physical bench.

- **What it is:** Software that creates "Synthetic Peers" (fake ECUs) or replays recorded CAN traffic from a real test drive. 
- **Where it runs:** On your computer, often communicating via Virtual CAN (vcan) or sending mock data to the backend tools.
- **What it proves:** That the Control UI displays the correct speed when it receives a `0x206` feedback message, or that the logging system can handle a burst of CAN frames without crashing.
- **What it DOES NOT prove:** Real-world transmission behavior or physical hardware latency.

## 4. Hardware-in-the-Loop / HIL (The Reality)

Eventually, code has to run on the real microcontrollers. 

- **What it is:** The firmware is compiled using `[env:bench]` or `[env:vehicle]` and flashed onto the actual ESP32/STM32 boards. A test script on a laptop talks to the boards over a physical CAN-USB adapter.
- **Where it runs:** On the physical MCU sitting on your desk (or in the vehicle).
- **What it proves:** That the FreeRTOS tasks run at the correct 100Hz periods, that the CAN transceiver actually transmits voltage to the wire, and that physical I/O (like pressing the physical ESTOP button) works.
- **What it DOES NOT prove:** Untested electrical extremes (e.g. voltage spikes when the real 72V motor kicks in).

---

## Summary

When you write a new feature, ask yourself where it should be tested:
1. Is it a change to the CAN layout? **Add a Golden Vector.**
2. Is it a math/logic change? **Write a Native Test.**
3. Is it a UI/Backend change? **Use Simulation/Replays.**
4. Is it a hardware/timing change? **It requires a HIL/Bench test.**

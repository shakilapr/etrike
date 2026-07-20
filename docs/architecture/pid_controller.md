# RT ESP32 PID Controller

This document outlines the architecture, configuration, and behavior of the PID speed controller within the RT (Real-Time) ESP32 node.

## Overview
The PID controller in the e-trike is designed specifically to maintain the **longitudinal speed** of the vehicle. It is a single-input, single-output (SISO) control loop that ensures the main drive motor reaches and maintains the target speed requested by the Host, regardless of terrain, wind resistance, or other load changes.

**Crucially**, the PID controller is entirely decoupled from the steering system. It does not monitor individual left/right wheel speeds, nor does it attempt to correct vehicle drift or yaw. It operates under a "bicycle model" assumption where it controls the average forward velocity of the vehicle.

## File Locations
- **Core Logic:** `rt-esp32/src/pid_controller.h`
- **System Integration:** `rt-esp32/src/speed_controller.h`
- **Configuration:** `rt-esp32/src/build_config.h`
- **Execution:** `rt-esp32/src/main.cpp`

## Inputs and Outputs

### Inputs
The PID loop executes at **100 Hz** (every 10ms) and takes the following inputs:
1. **Setpoint (`desired_mmps`):** The target speed requested by the Host, after passing through kinematics and obstacle-avoidance limiters.
2. **Measured Speed (`measured_mmps`):** The actual speed of the vehicle. Depending on the `ETRIKE_RT_SPEED_FEEDBACK_SOURCE` build configuration, this comes from:
   - A physical rear motor encoder (`RtEncoder`).
   - A mathematical plant estimator (`Calculated`).
3. **Time Step (`dt`):** The elapsed time since the last calculation (nominally `0.01s`).

*(Note: The PID algorithm does not receive or process data from the `rear left`, `rear right`, or `front wheel` encoders.)*

### Output
The PID produces a final speed correction:
1. **Raw Effort:** The core algorithm calculates a unitless effort fraction (`-1.0` to `+1.0`).
2. **Scaled Output:** The `SpeedController` scales this fraction by the trike's maximum forward speed (`shared::kMaxSpeedFwdMmps`), resulting in a speed correction in `mm/s`.

In **Active** mode, this correction is algebraically added to the original setpoint (feedforward) before being sent to the motor controller (`MTR`) over the CAN bus.

## Algorithmic Features
The PID implementation (`pid_controller.h`) includes several advanced features for stable vehicle control:

1. **Derivative-on-Measurement:** Instead of taking the derivative of the error, the controller takes the derivative of the measured plant output. This prevents a massive spike in the D-term ("derivative kick") when the Host commands a sudden step-change in speed.
2. **D-Term Low-Pass Filter:** An optional exponential moving average filter (`d_filter_alpha`) smooths out noise from the speed sensors before applying the derivative gain.
3. **Conditional Integration (Anti-Windup):** If the controller's output is saturated (e.g., commanding maximum acceleration) and the error is still pushing in the same direction, the controller stops accumulating the integral (I-term). This prevents the I-term from growing uncontrollably when the motor cannot physically reach the target speed.
4. **Setpoint-Change I-Reset:** If the target speed changes by a large amount in a single step (threshold > `500 mm/s`), the integral term is instantly reset to zero to prevent overshoot.
5. **Encoder Safety Guard:** In `SpeedController::update_shadow_pid`, if the measured speed reads exactly `0` (which could indicate a broken encoder wire), the PID is reset and outputs `0` correction, preventing an I-term runaway acceleration scenario.

## Operational Modes
The PID's behavior is set at compile-time via the `ETRIKE_RT_PID_MODE` flag in `platformio.ini` (validated in `build_config.h`):

- **`0` (Disabled):** The PID logic is bypassed. No calculations occur, and no output is generated.
- **`1` (Shadow):** The PID calculates the required correction and reports it via the `0x220 RT_PID_RPT` CAN telemetry message at 10Hz. The output is **discarded** and does not affect the actual drive commands. Used primarily for tuning and verification.
- **`2` (Active):** The PID correction is actively injected into the drive setpoint to enforce closed-loop speed control. *(Note: Active mode strictly requires `RtEncoder` or `Calculated` feedback sources; it rejects basic `MTR` feedback for safety reasons).*

## System Separation (Speed vs. Steering)
The RT ESP32 node executes commands blindly.
- **Speed (PID):** Corrects the longitudinal velocity to match the setpoint.
- **Steering:** The kinematics resolver calculates the required physical steering angle based on the Host's requested yaw rate and speed.
- **Path Correction:** If the vehicle drifts off its intended path (e.g., due to tire slip or a bump), the RT ECU **will not** auto-correct the steering. Path correction must be detected by the Host's higher-level autonomy stack, which will then issue a new `yaw_rate` command to the RT node.

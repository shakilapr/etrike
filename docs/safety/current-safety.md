# System Safety and Defense-in-Depth Logic

This document compiles the comprehensive safety logic and defensive programming strategies implemented in the E-Trike firmware codebase (`rt-esp32` and `sys-esp32`). The core philosophy is that high-level autonomy software (Host/Jetson) is treated as untrusted regarding physical constraints, and the low-level microcontrollers act as authoritative safety gatekeepers.

---

## 1. Actuator Limits and Clamping

### Hardware Abstraction vs. Mechanical Limits
The internal CAN protocol allows wide-range commands (e.g., 20 MPa brake pressure, unbounded steering yaw rates). The firmware intercepts and clamps these to the specific physical limitations of the trike.

*   **Brake Pressure Clamping (`sys-esp32/src/brake_control.h`):** 
    Converts standard 0–20,000 kPa requests to the actuator's raw 8-bit scale and explicitly clamps it to the physical SEB limit (5 MPa / raw `100`). This prevents integer overflow errors and codec rejections.
    ```cpp
    int32_t raw = (brake_kpa + 25) / 50; 
    out.pressure_request_raw = uint8_t(raw > shared::kSebMaxPressureRaw ? shared::kSebMaxPressureRaw : raw);
    ```

*   **Steering Mechanical Limits (`rt-esp32/src/direct_resolver.cpp`):**
    Although the EPS-C actuator allows commands up to ±780°, the E-trike's mechanical rack hits hard stops at ±45°. The kinematics resolver translates target yaw into an angle and strictly clamps it to `kSteerLimitMdeg` (±45000 mdeg) to prevent motor stall and tie-rod snapping.

*   **Motor Speed Clamping (`rt-esp32/src/direct_resolver.cpp`):**
    Requested speeds are clamped to the hardcoded safe operational speeds of the trike (`3000 mm/s` forward, `500 mm/s` reverse) defined in `shared_config.h`.

### Dynamic Steering Slew Rates (`rt-esp32/src/steering_control.h`)
To prevent the vehicle from rolling over during sharp autonomous turns at high speeds, the turning speed of the steering actuator is dynamically clamped based on the current vehicle speed.
```cpp
float speed_kmh = std::abs(m_speed_mmps) * 3.6f / 1000.0f;
float rate_deg_s = kSteerRateMinDegS + (speed_kmh - 2.0f) * (kSteerRateRangeDegS / kAngleClampSpeedRange);
out.target_speed_raw = static_cast<uint16_t>(std::clamp(rate_deg_s, 125.0f, 525.0f));
```

---

## 2. Emergency Stop (ESTOP) Handling

### Context-Aware ESTOP State Machine (`rt-esp32/src/steering_control.h`)
The steering controller executes two different safe-stop strategies depending on the source of the ESTOP:
1.  **Hardware ESTOP (`ESTOP_RAMP_TO_ZERO`):** Triggered by a button press. The steering safely and steadily ramps back to 0° (center) at a controlled rate (20°/s) to ensure the vehicle drives straight while coming to a halt.
2.  **Obstacle ESTOP (`ESTOP_HOLD_THEN_SILENT`):** Holds the current steering angle for 500ms to maintain vehicle trajectory stability during sudden hard braking, then goes silent to release the rack.

### Dynamic Hold Limits (Anti-Rollover)
If an obstacle triggers an ESTOP while the trike is turning sharply at high speed, holding that sharp angle while slamming on the brakes risks a rollover. The firmware dynamically calculates a safe hold angle limit (e.g., max 5° at top speed) and clamps the hold position.
```cpp
float max_deg = compute_dynamic_limit(speed);
int16_t max_raw = static_cast<int16_t>(max_deg * 10.0f);  
m_estop_hold_angle = std::clamp(m_active_angle, int16_t(-max_raw), max_raw);
```

---

## 3. Mode-Gated Execution and Arbitration

### Steering Actuator Release (`rt-esp32/src/main.cpp`)
In `MANUAL` mode, the RT node completely stops transmitting `0x169 VCU_SES_REQ` commands. This intentional silence forces the EPS-C actuator to fall back into a "standalone" (free-spin) state, releasing motor resistance so the rider can physically turn the handlebars without the actuator fighting them.
```cpp
// Only block in MANUAL — EPS-C runs standalone, RT must not command.
if (g_mode_current.load() != uint8_t(can::Mode::Manual)) { /* transmit steering */ }
```

### Redundant Brake Takeover (Deadman Switch)
In `AUTO` mode, the RT node has authority over the brake actuator. However, the SYS node continuously monitors the RT node's heartbeat. If the RT node crashes or its CAN messages stop arriving, SYS breaks arbitration and immediately resumes sending its own brake commands to stop the vehicle.

### Driver Override Priority (`sys-esp32/src/brake_control.h`)
The firmware enforces a strict priority chain: Physical `ESTOP` > `Driver Brake Lever` > `CAN Autonomy Request`. If the Jetson is driving autonomously but the rider pulls the physical brake lever, the SYS node immediately abandons the Jetson's pressure request and commands physical stroke mode.

---

## 4. Communication Integrity

### Listen-Before-Speaking
When initializing, the controllers enter a `LISTEN_SYNC` state. They wait to receive a valid status frame (`0x721` for brake, `0x201` for steering) and verify the actuator's `alignment_status` bit before they transition to the `ACTIVE` state and begin transmitting commands.

### Following-Error Monitoring
The firmware constantly compares the commanded setpoint against the actual physical feedback reported by the actuators:
*   **Steering:** During an ESTOP centering ramp, if the actual physical angle lags the commanded angle by more than 5° for 1000ms, the system assumes the physical linkage is jammed, triggers a `STEER_FAULT`, and ceases commands.
*   **Brake:** In manual stroke mode, if the commanded stroke differs from the actual stroke by >3mm for >100ms, a `g_brake_fault_active` flag is raised.

### Redundant Brake Light OR-Logic
To ensure the rear brake lights activate reliably during any deceleration event, they are triggered by a 3-input OR condition:
1. Physical lever is pulled.
2. Jetson explicitly requests brake lights via CAN `0x302`.
3. The brake actuator's physical stroke extends >0.5mm.

### Watchdogs and Heartbeats
System stability relies on frozen-counter checks. Every task iteration updates an atomic counter. A 1Hz diagnostics task checks all counters; if any task (e.g., safety, brake, dispatch) is stale for >200ms, the system logs an error and triggers the physical hardware Watchdog Timer (TPS3850) to reset the MCU.

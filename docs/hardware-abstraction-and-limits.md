# Hardware Abstraction and Safety Clamping (Defense-in-Depth)

This document details the architectural separation between the high-level autonomy stack (Jetson/Host) and the low-level physical actuators (SEB, EPS-C, MTR). It explains the design philosophy behind the wide-range internal CAN messages versus the strictly clamped physical actuator limits, and how the intermediate microcontrollers (RT, SYS) enforce mechanical safety.

## 1. Architectural Philosophy

The E-Trike's control architecture is built on a two-tier model:
- **High-Level Autonomy (Host / RT):** Operates on standardized SI units (kPa, mrad/s, mm/s) with wide theoretical ceilings. This ensures the autonomy software remains hardware-agnostic and does not need to be rewritten if a stronger actuator or heavier vehicle chassis is adopted in the future.
- **Low-Level Execution (SYS / RT / Actuators):** The intermediate microcontrollers act as "hardware gateways." They receive the unbounded high-level commands, translate them into proprietary vendor protocols, and strictly clamp the outputs to the physical and mechanical limitations of the *currently installed* hardware.

This "Defense-in-Depth" approach ensures that even if a high-level algorithm goes rogue (e.g., requesting infinite speed or maximum steering angle at top speed), the low-level firmware intercepts and caps the request before any physical damage or loss of vehicle control can occur.

---

## 2. Brake System (SEB)

### The Discrepancy
- **Internal CAN Bus:** Messages like `0x301 HOST_BRAKE_REQ` and `0x205 RT_BRAKE_CMD` define the `BrakePressure` signal as a 32-bit integer ranging from **0 to 20,000 kPa (20 MPa)**. This matches the standard hydraulic pressure ceilings for large automotive applications.
- **Physical Hardware:** The currently installed SEB (Steer-by-Wire / Brake-by-Wire) electro-hydraulic unit has a strict maximum operating pressure of **5,000 kPa (5.0 MPa)**, which translates to a vendor raw value of `100`.

### The Safety Clamp
If the Jetson commands 20,000 kPa, the SYS gateway safely manages it. In `sys-esp32/src/brake_control.h`, the SYS microcontroller converts the physical kPa request into the actuator's raw 8-bit scale and explicitly clamps it to the SEB's maximum limit *before* generating the frame.

```cpp
// Conversion: 0.05 MPa per bit (kPa / 50)
int32_t raw = (brake_kpa + 25) / 50; 
out.pressure_request_raw = uint8_t(raw > shared::kSebMaxPressureRaw ? shared::kSebMaxPressureRaw : raw);
```
**Why it matters:** Without this clamp, a 20,000 kPa request would cause an integer overflow when cast to an 8-bit unsigned integer, leading to unpredictable braking (e.g., dropping to 0 or being rejected entirely by the protocol codec). Clamping guarantees maximum physical braking effort without data corruption.

---

## 3. Steering System (EPS-C)

### The Discrepancy
- **Internal CAN Bus:** The Jetson commands turning effort via `0x300 HOST_DRIVE_CMD` using `HOST_YawRate`, ranging from **-3000 to +3000 mrad/s**.
- **Physical Actuator Limits:** The EPS-C actuator's CAN protocol can theoretically accept commands up to **±780°** (raw `±7800`). 
- **Mechanical Trike Limits:** The physical steering rack on the E-Trike bottoms out mechanically at approximately **±35° to ±45°**. 

### The Safety Clamp
Commanding the EPS-C to turn to 780° when the rack stops at 45° would cause the steering motor to stall, eventually burning out the driver board or snapping the mechanical tie-rods. 
The RT microcontroller's kinematics resolver (`rt-esp32/src/direct_resolver.cpp`) scales the Jetson's yaw rate into a target angle, and explicitly clamps it to the safe mechanical limit of **±45°** (`±45000 mdeg`).

```cpp
constexpr int32_t kSteerLimitMdeg = 45000;
const int32_t raw_steer = cmd.yaw_rate_mrad_s * kYawToSteerScale;
out.steer_angle_mdeg = std::clamp(raw_steer, -kSteerLimitMdeg, kSteerLimitMdeg);
```

### Dynamic Slew Rate and Rollover Prevention
To further protect the vehicle, the RT firmware (`rt-esp32/src/steering_control.h`) dynamically clamps the **steering speed (slew rate)** based on the vehicle's current speed. 
At low speeds, the actuator is allowed to swing rapidly (up to 525°/s). At higher speeds, the turning rate is severely clamped (down to 125°/s) to prevent the vehicle from rolling over due to a sharp command.

---

## 4. Motor Drive System (MTR)

### The Discrepancy
- **Internal CAN Bus:** Both `0x300 HOST_DRIVE_CMD` and `0x204 RT_DRIVE_CMD` support a 32-bit speed request ranging from **-500 to +3000 mm/s**.
- **Operational Safety Limits:** The physical motor and motor controller may be capable of driving the trike much faster, but the system must be constrained to safe pedestrian/low-speed autonomous vehicle speeds.

### The Safety Clamp
Before the target speed is forwarded to the motor MCU, the RT kinematics resolver intercepts the target speed and clamps it to the hardcoded vehicle parameters (`kMaxSpeedFwdMmps` = 3000 mm/s, `kMaxSpeedRevMmps` = 500 mm/s) defined in `shared_config.h`.

```cpp
const int32_t max_fwd = static_cast<int32_t>(shared::kMaxSpeedFwdMmps);
const int32_t max_rev = static_cast<int32_t>(shared::kMaxSpeedRevMmps);
out.motor_speed_mmps = std::clamp(cmd.speed_mmps, -max_rev, max_fwd);
```

---

## 5. Dynamic Safety Holds (ESTOP Context)

During an obstacle-triggered ESTOP (where the vehicle slams on the brakes due to a detected object), the steering system must freeze its current position for 500ms before relaxing. 
However, if the vehicle is traveling at maximum speed and the wheels are currently turned 45°, holding that sharp angle while applying max brake pressure will almost certainly cause a rollover.

To handle this, the RT node dynamically clamps the **ESTOP Hold Angle** based on a physical limit calculation (`compute_dynamic_limit()`).

```cpp
// At high speed the dynamic limit may be as low as 5° — holding an
// angle beyond that during hard braking risks rollover.
float max_deg = compute_dynamic_limit(static_cast<float>(std::abs(m_speed_mmps)));
int16_t max_raw = static_cast<int16_t>(max_deg * 10.0f);  
m_estop_hold_angle = std::clamp(m_active_angle, int16_t(-max_raw), max_raw);
```
Even if the steering was actively at 45°, an ESTOP at high speed will immediately clamp the hold position down to a safe angle (e.g., 5°) to keep the vehicle stable during emergency deceleration.

---

## Summary

The E-Trike software stack relies on **hardware abstraction** to remain flexible for future upgrades, but heavily utilizes **firmware-level safety clamping** at the RT and SYS nodes to ensure that the physical vehicle cannot be driven beyond its mechanical, electrical, or dynamic stability limits, regardless of what the higher-level autonomy algorithms request.

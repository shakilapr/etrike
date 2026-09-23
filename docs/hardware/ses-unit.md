# SES (Steer-by-Wire) Actuator Technical Reference & Integration Guide

**Subsystem**: SES (Steering Actuator / Steer-by-Wire)  
**Target Controllers**: `rt-esp32`, `sys-esp32`, `rm-esp32-t12d`, `control-toolkit`, Hardware Simulator  
**Bus Architecture**: Low-CAN (500 kbps, 11-bit Standard Identifiers)  
**Document Status**: Official Engineering Reference  

---

## 1. System Overview & Actuator Role

The **SES (Steer-by-Wire)** unit is an intelligent electromechanical steering actuator that controls the front steering rack via CAN commands. It integrates an internal Brushless DC (BLDC) motor, an absolute angular position encoder, and a closed-loop microcontroller running an internal PID position loop.

```
                      +------------------------------------------------+
                      |               Low-CAN (500 kbps)              |
                      +-------+--------------------------------+-------+
                              |                                ^
           0x169 VCU_SES_REQ  |                                | 0x201 SES_STATUS
           (50 Hz / 20 ms)    |                                | (100 Hz / 10 ms)
                              v                                |
                      +------------------------------------------------+
                      |         SES (Steer-by-Wire Actuator)           |
                      |                                                |
                      |  [ Internal PID ] <───> [ Absolute Encoder ]   |
                      |          │                                     |
                      |          v                                     |
                      |  [ BLDC Actuator ] ───> [ Steering Linkage ]   |
                      +------------------------------------------------+
```

### Key Operating Parameters
| Parameter | Value | Notes |
|:---|:---|:---|
| **CAN Bus** | Low-CAN | Dedicated actuator bus |
| **Bitrate** | 500 kbps | Standard 11-bit Base Frame Format |
| **Command ID** | `0x169` (`VCU_SES_REQ`) | Transmitted by active controller (RT / RM / SYS) |
| **Command Rate** | 20 ms (50 Hz) | Actuator enters fault/timeout if silent > 100 ms |
| **Feedback ID** | `0x201` (`SES_STATUS`) | Actuator transmits continuously |
| **Feedback Rate** | 10 ms (100 Hz) | Center status, active mode, measured angle, errors |
| **Byte Ordering** | **Motorola Big-Endian** | MSB first for multi-byte values (Angle, Slew) |
| **Mechanical Range** | $\pm 45.0^\circ$ rack travel | Full actuator encoder span is $\pm 700.0^\circ$ |
| **Software Clamp** | $\pm 40.0^\circ$ ($[-400, +400]$ counts) | Enforced by RT / SYS to prevent end-stop collisions |

---

## 2. CAN Wire Protocol Specifications

### 2.1 Command Frame: `0x169 VCU_SES_REQ` (50 Hz / 20 ms Period)

DLC: 8 Bytes. Transmitted periodically by the VCU/RT controller.

| Byte | Bits | Field Name | Type | Scale | Offset | Raw Range | Physical / Description |
|:---:|:---:|:---|:---:|:---:|:---:|:---:|:---|
| **0** | `0` | `VCU_SES_Alignment_Enable` | bool | 1 | 0 | `0..1` | `0` = Normal operation; `1` = Factory zero-calibration |
| **0** | `1` | `VCU_SES_Control_Enable` | bool | 1 | 0 | `0..1` | **$0 \to 1$ Rising Edge required** to enter Angle Control Mode |
| **0** | `2..7`| Reserved | — | — | — | `0` | Must be `0` |
| **1** | `0..7`| `VCU_SES_Tgt_StrAngle [15:8]` | int16 | 0.1 | -3000 | `23000..37000` | Target Steering Angle MSB (**Big-Endian**) |
| **2** | `0..7`| `VCU_SES_Tgt_StrAngle [7:0]` | int16 | 0.1 | -3000 | `23000..37000` | Target Steering Angle LSB (**Big-Endian**) |
| **3** | `0..7`| `VCU_SES_Tgt_StrAngleSpd [15:8]`| uint16| 1 | 0 | `125..525` | Maximum Slew Rate MSB (deg/s, **Big-Endian**) |
| **4** | `0..7`| `VCU_SES_Tgt_StrAngleSpd [7:0]` | uint16| 1 | 0 | `125..525` | Maximum Slew Rate LSB (Nominal: 328 deg/s = `0x0148`) |
| **5** | `0` | `VCU_SES_RollCnt_Enable` | bool | 1 | 0 | `1` | **Must be set to `1`** |
| **5** | `1` | `VCU_SES_CheckSum_Enable`| bool | 1 | 0 | `1` | **Must be set to `1`** |
| **5** | `2..3`| Reserved | — | — | — | `0` | Must be `0` |
| **5** | `4..7`| `VCU_SES_RollCnt` | uint4 | 1 | 0 | `0..15` | Rolling counter: increments 1 each frame ($0 \to 15 \to 0$) |
| **6** | `0..7`| `VCU_Veh_Spd_Value` | uint8 | 1 | 0 | `0..255` | Vehicle speed in km/h. **Must be $\ge 5$ km/h during drive** |
| **7** | `0..7`| `VCU_SES_CheckSum` | uint8 | 1 | 0 | `0..255` | **8-bit Additive Sum**: $\left(\sum_{i=0}^6 \text{Byte}_i\right) \ \& \ \text{0xFF}$ |

#### Angle Encoding Formula:
$$\text{RawValue} = \text{round}\left(\text{Angle}_{\text{deg}} \times 10.0\right) + 30000$$

- **Center / Neutral ($0.0^\circ$)**: $\text{Raw} = 30000$ (`0x7530`) $\implies$ Byte 1 = `0x75`, Byte 2 = `0x30`
- **Right Turn ($+15.0^\circ$)**: $\text{Raw} = 30150$ (`0x75C6`) $\implies$ Byte 1 = `0x75`, Byte 2 = `0xC6`
- **Left Turn ($-15.0^\circ$)**: $\text{Raw} = 29850$ (`0x749A`) $\implies$ Byte 1 = `0x74`, Byte 2 = `0x9A`
- **Max Right Clamp ($+40.0^\circ$)**: $\text{Raw} = 30400$ (`0x76C0`) $\implies$ Byte 1 = `0x76`, Byte 2 = `0xC0`
- **Max Left Clamp ($-40.0^\circ$)**: $\text{Raw} = 29600$ (`0x73A0`) $\implies$ Byte 1 = `0x73`, Byte 2 = `0xA0`

---

### 2.2 Status Frame: `0x201 SES_STATUS` (100 Hz / 10 ms Period)

DLC: 8 Bytes. Transmitted periodically by the SES internal controller.

| Byte | Bits | Field Name | Type | Scale | Offset | Description |
|:---:|:---:|:---|:---:|:---:|:---:|:---|
| **0** | `0` | `SES_INF_Angle_Status` | bool | 1 | 0 | Center Finding Status: `0` = Searching / Initializing, `1` = Aligned / Found |
| **0** | `1..2`| `SES_Control_Mode_Status`| uint2 | 1 | 0 | Active Mode: `0` = Manual / Assist Mode, **`1` = Automatic / Angle Control Mode** |
| **0** | `3..5`| Reserved / Internal | — | — | — | Reserved |
| **0** | `6..7`| `SES_Error_Status` | uint2 | 1 | 0 | Fault Level: `0` = OK, `1` = L1 Warning, `2` = L2 Minor Fault, `3` = L3 Critical Fault |
| **1** | `0..7`| `SES_StrAngle [15:8]` | uint16| 0.1 | -3000 | Measured Steering Angle MSB (**Big-Endian**) |
| **2** | `0..7`| `SES_StrAngle [7:0]` | uint16| 0.1 | -3000 | Measured Steering Angle LSB (**Big-Endian**) |
| **3** | `0..7`| `SES_Tgt_StrAngleSpd [15:8]`| uint16| 1 | 0 | Measured Turning Speed MSB (deg/s, **Big-Endian**) |
| **4** | `0..7`| `SES_Tgt_StrAngleSpd [7:0]` | uint16| 1 | 0 | Measured Turning Speed LSB (deg/s, **Big-Endian**) |
| **5** | `0..7`| `EPS_SteeringWheel_Torq` | uint8 | 0.1 | -12.1 | Rack / Handwheel torque (Nm). Formula: $(\text{Raw} \times 0.1) - 12.1$ |
| **6** | `0` | `SES_RollCnt_Enable_Status` | bool | 1 | 0 | Echo of rolling counter enable (`1`) |
| **6** | `1` | `SES_CheckSum_Enable_Status`| bool | 1 | 0 | Echo of checksum enable (`1`) |
| **6** | `2..3`| Reserved | — | — | — | Reserved |
| **6** | `4..7`| `SES_RollCnt_Status` | uint4 | 1 | 0 | Echoed rolling counter |
| **7** | `0..7`| `SES_CheckSum_Status` | uint8 | 1 | 0 | Echoed 8-bit additive checksum |

#### Angle Decoding Formula:
$$\text{Angle}_{\text{deg}} = \frac{\left((\text{Byte}_1 \ll 8) \mid \text{Byte}_2\right) - 30000}{10.0}$$

---

## 3. Mandatory Protocol Implementation Rules

Failure to adhere to these four specific rules will result in the SES actuator ignoring all target angle commands:

### Rule 1: The Rising-Edge Trigger Requirement
The SES boots by default into **Assist Mode** (`SES_Control_Mode_Status = 0`). To transition into **Angle Control Mode** (`SES_Control_Mode_Status = 1`), the internal state machine **strictly requires a $0 \to 1$ rising edge** on `VCU_SES_Control_Enable` (Byte 0, Bit 1).

- **The Static-High Trap**: If a controller boots and immediately transmits `Control_Enable = 1` in the first frame and never pulls it low, the actuator **never detects a low-to-high edge**. It will remain stuck in Assist Mode indefinitely.
- **The Required Sequence**:
  1. Emit `Control_Enable = 0` for at least 100–200 ms (5–10 frames @ 50 Hz).
  2. Transition `Control_Enable = 1` on the subsequent frame.
  3. Verify `SES_Control_Mode_Status == 1` via `0x201` feedback.

### Rule 2: 8-Bit Additive Sum Checksum
Byte 7 must contain an 8-bit truncated additive sum of Bytes 0 through 6:

$$\text{Byte}_7 = \left(\sum_{i=0}^6 \text{Byte}_i\right) \pmod{256}$$

> [!CAUTION]
> **Do NOT use XOR checksums**. Many standard CAN stacks mistakenly use $\text{XOR}(\text{Bytes } 0..6) \oplus \text{0xFF}$. If an XOR checksum is transmitted, the SES actuator drops 100% of received frames without acknowledging or moving.

### Rule 3: Zero-Speed Holding Interlock
When the target steering angle returns to $0.0^\circ$ (center), if `VCU_Veh_Spd_Value` (Byte 6) is reported as $0\text{ km/h}$, the actuator's power-saving logic turns off holding current to the motor.  
To ensure the actuator maintains stiffness and holds the wheels centered while the vehicle is active:
- Set `VCU_Veh_Spd_Value = 10` (or $\ge 5\text{ km/h}$) whenever vehicle drive is active/armed.
- Set `VCU_Veh_Spd_Value = 0` only when parked/disarmed.

### Rule 4: Slew Rate Limits
`VCU_SES_Tgt_StrAngleSpd` defines the maximum angular velocity of the rack.
- Valid range: $125 \text{ to } 525^\circ/\text{s}$.
- Values below 125 or above 525 cause command rejection or clamp faults.
- Recommended nominal operating value: **$328^\circ/\text{s}$** (`0x0148`).

---

## 4. Operational State Machine & Hot-Plug Recovery

```
                      +──────────────────────────+
                      │       POWER ON / BOOT    │
                      │  Actuator in Assist (0)  │
                      +─────────────┬────────────+
                                    │
                                    v
                      +──────────────────────────+
                      │    PHASE 1: DISARM GATE  │
                      │  Control_Enable = 0      │
                      │  Duration: 200 ms (10 fr)│
                      +─────────────┬────────────+
                                    │
                                    v
                      +──────────────────────────+
                      │    PHASE 2: RISING EDGE  │
                      │  Control_Enable = 1      │
                      │  Target = Current Angle  │
                      +─────────────┬────────────+
                                    │
                  0x201 Mode == 1   │   0x201 Mode != 1 (Timeout > 500ms)
               ┌────────────────────┴────────────────────┐
               v                                         v
+──────────────────────────+               +──────────────────────────+
│    PHASE 3: CLOSED LOOP  │               │      RE-ARM RETRY        │
│   Active Angle Tracking  │               │  Control_Enable = 0      │
│   Follows Jetson / Remote│               │  Return to Phase 1       │
+──────────────┬───────────+               +──────────────────────────+
               │
               │ Wire Disconnected (0x201 silent > 250ms)
               │   OR Mode Drops to 0 while Armed
               v
+──────────────────────────+
│  HOT-PLUG AUTO-REARM     │
│  Actuator resets to 0    │
│  Hold Enable=0 for 200ms │ ──> Re-arm on link restore (Phase 1)
+──────────────────────────+
```

### Hot-Plug / Physical Wire Reconnection Behavior
If the CAN harness or power supply to the SES actuator is disconnected and reconnected while the controller continues running:
1. The SES microcontroller reboots into **Assist Mode** (`Mode = 0`).
2. If the controller continues sending `Control_Enable = 1`, the reconnected actuator will **never engage** because no rising edge occurs.
3. **The Firmware Watchdog Solution**:
   - The controller must monitor `0x201` frame arrival timestamps.
   - If `0x201` is absent for $> 250\text{ ms}$ and subsequently reappears, OR if `SES_Control_Mode_Status` reports `0` while drive is active:
   - The controller immediately initiates a **200 ms disarm cycle** (`Control_Enable = 0`), followed by re-asserting `Control_Enable = 1`. This automatically and seamlessly recovers the steering link without requiring an ECU reboot.

---

## 5. RT & SYS Safety Interlocks

The real-time controller (`rt-esp32`) must enforce the following safety gates prior to transmitting `0x169`:

### 5.1 "Listen Before Speaking" Cold-Start Handshake
Never command $0.0^\circ$ immediately on boot if the vehicle's wheels are physically turned. An abrupt snap from $+30^\circ$ to $0^\circ$ can damage mechanical linkages or cause physical injury.
1. On boot, listen for `0x201 SES_STATUS`.
2. Extract the current physical steering angle $\theta_{\text{actual}}$.
3. Initialize the controller's internal target angle to $\theta_{\text{actual}}$.
4. The first commanded frame after enabling must command $\theta_{\text{target}} = \theta_{\text{actual}}$ (zero initial movement).

### 5.2 Mechanical Hard-Stop Protection Clamps
- Physical rack limits: $\pm 45.0^\circ$
- Firmware software clamp: **$\pm 40.0^\circ$**
  ```cpp
  constexpr float kMaxSteerAngleDeg = 40.0f;
  target_angle_deg = std::clamp(target_angle_deg, -kMaxSteerAngleDeg, kMaxSteerAngleDeg);
  ```

### 5.3 Dynamic Speed-Dependent Steering Envelope
To prevent vehicle rollover at high speeds, maximum steering deflection must be restricted based on measured vehicle velocity:

| Vehicle Speed | Maximum Allowed Steering Angle |
|:---|:---|
| $\le 2\text{ km/h}$ | $\pm 40.0^\circ$ |
| $10\text{ km/h}$ | $\pm 20.0^\circ$ |
| $\ge 25\text{ km/h}$ | $\pm 5.0^\circ$ |

```cpp
float compute_safe_steer_limit(float speed_kmh) {
    if (speed_kmh <= 2.0f)  return 40.0f;
    if (speed_kmh >= 25.0f) return 5.0f;
    // Linear interpolation between (2 km/h, 40 deg) and (25 km/h, 5 deg)
    return 40.0f - ((speed_kmh - 2.0f) / 23.0f) * 35.0f;
}
```

### 5.4 Following Error Watchdog
If the physical actuator fails to follow the commanded trajectory (e.g. rack jam, tie-rod damage, or power loss):
- Condition: $|\theta_{\text{target}} - \theta_{\text{measured}}| > 5.0^\circ$ persisting for $> 300\text{ ms}$.
- Action: Assert system `ESTOP`, cease propulsion motor command (`0x204`), and transition actuator to safe state.

---

## 6. Zero-Allocation C++ Codec & Driver Implementation

The following reference implementation provides production-ready encode, decode, and auto-rearm state handling suitable for FreeRTOS on `rt-esp32`, `sys-esp32`, or `rm-esp32-t12d`.

### 6.1 Codec Header (`ses_protocol.hpp`)

```cpp
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace etrike::ses {

constexpr uint32_t kCommandId = 0x169;
constexpr uint32_t kStatusId  = 0x201;
constexpr int32_t  kAngleOffset = 30000;
constexpr float    kMaxAngleDeg = 40.0f;

struct Command {
    bool     align_enable{false};
    bool     control_enable{false};
    float    target_angle_deg{0.0f};
    uint16_t target_slew_deg_s{328}; // Nominal 328 deg/s
    uint8_t  rolling_counter{0};
    uint8_t  vehicle_speed_kmh{10};  // >= 5 km/h prevents zero-deg motor shutoff
};

struct Status {
    bool    aligned{false};          // Center Finding status (1 = Aligned)
    uint8_t mode{0};                 // 0 = Manual/Assist, 1 = Auto/Angle Control
    uint8_t error_level{0};          // 0 = OK, 1 = L1, 2 = L2, 3 = L3
    float   actual_angle_deg{0.0f};
    uint16_t turning_speed_deg_s{0};
    float   torque_nm{0.0f};
    uint8_t rolling_counter{0};
    uint8_t checksum{0};
};

// 8-bit additive sum checksum: Sum(bytes 0..6) & 0xFF
inline uint8_t calc_sum8(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

// Encode 0x169 VCU_SES_REQ
inline void encode_command(const Command& cmd, uint8_t out[8]) {
    out[0] = (cmd.align_enable ? 0x01 : 0x00) | (cmd.control_enable ? 0x02 : 0x00);
    
    // Clamp angle to safe mechanical boundary
    float clamped_deg = std::clamp(cmd.target_angle_deg, -kMaxAngleDeg, kMaxAngleDeg);
    int32_t raw_angle = static_cast<int32_t>(std::round(clamped_deg * 10.0f)) + kAngleOffset;
    raw_angle = std::clamp<int32_t>(raw_angle, 23000, 37000);

    // Big-Endian packing for Angle (Bytes 1-2)
    out[1] = static_cast<uint8_t>((raw_angle >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>(raw_angle & 0xFF);

    // Big-Endian packing for Slew Rate (Bytes 3-4)
    uint16_t slew = std::clamp<uint16_t>(cmd.target_slew_deg_s, 125, 525);
    out[3] = static_cast<uint8_t>((slew >> 8) & 0xFF);
    out[4] = static_cast<uint8_t>(slew & 0xFF);

    // Security byte: RollCnt_En (bit 0), Checksum_En (bit 1), RollCnt (bits 4..7)
    out[5] = 0x03 | (static_cast<uint8_t>(cmd.rolling_counter & 0x0F) << 4);

    // Vehicle Speed
    out[6] = cmd.vehicle_speed_kmh;

    // Checksum: Additive sum over bytes 0..6
    out[7] = calc_sum8(out, 7);
}

// Decode 0x201 SES_STATUS
inline bool decode_status(const uint8_t in[8], Status& out) {
    // Verify checksum
    if (calc_sum8(in, 7) != in[7]) {
        return false;
    }

    out.aligned     = (in[0] & 0x01) != 0;
    out.mode        = (in[0] >> 1) & 0x03;
    out.error_level = (in[0] >> 6) & 0x03;

    // Big-Endian decoding for Measured Angle
    uint16_t raw_angle = (static_cast<uint16_t>(in[1]) << 8) | in[2];
    out.actual_angle_deg = static_cast<float>(static_cast<int32_t>(raw_angle) - kAngleOffset) / 10.0f;

    // Turning speed
    out.turning_speed_deg_s = (static_cast<uint16_t>(in[3]) << 8) | in[4];

    // Rack torque: scale 0.1, offset -12.1 Nm
    out.torque_nm = (static_cast<float>(in[5]) * 0.1f) - 12.1f;

    out.rolling_counter = (in[6] >> 4) & 0x0F;
    out.checksum        = in[7];
    return true;
}

} // namespace etrike::ses
```

### 6.2 State Machine Controller with Hot-Plug Recovery

```cpp
class SesController {
public:
    void update_feedback(uint8_t mode, uint32_t now_ms) {
        last_status_ms_ = now_ms;
        ses_mode_ = mode;
        
        // If actuator unexpectedly dropped to Assist Mode while we are armed, trigger auto-rearm
        if (armed_ && mode == 0 && rearm_countdown_ticks_ == 0) {
            rearm_countdown_ticks_ = 20; // 200 ms disarm pulse
        }
    }

    void step_10ms(bool drive_armed, float target_deg, uint32_t now_ms, uint8_t out_frame[8]) {
        armed_ = drive_armed;

        // Hot-plug link loss check (> 250 ms timeout)
        if (now_ms - last_status_ms_ > 250) {
            link_lost_ = true;
            rearm_countdown_ticks_ = 20; // Pre-load disarm pulse so edge fires upon link restore
        } else if (link_lost_) {
            link_lost_ = false; // Wire reconnected!
        }

        // Handle re-arm countdown
        if (rearm_countdown_ticks_ > 0) {
            --rearm_countdown_ticks_;
        }

        etrike::ses::Command cmd;
        cmd.align_enable = false;
        // Rising edge gate: only 1 when armed AND disarm pulse has finished
        cmd.control_enable = drive_armed && (rearm_countdown_ticks_ == 0);
        cmd.target_angle_deg = target_deg;
        cmd.target_slew_deg_s = 328;
        cmd.rolling_counter = roll_cnt_;
        roll_cnt_ = (roll_cnt_ + 1) & 0x0F;
        // Speed >= 5 km/h during drive prevents 0 deg motor shutdown
        cmd.vehicle_speed_kmh = drive_armed ? 10 : 0;

        etrike::ses::encode_command(cmd, out_frame);
    }

private:
    uint32_t last_status_ms_{0};
    uint32_t rearm_countdown_ticks_{20}; // Initial 200 ms low pulse on boot
    uint8_t  roll_cnt_{0};
    uint8_t  ses_mode_{0};
    bool     armed_{false};
    bool     link_lost_{true};
};
```

---

## 7. Diagnostics & Troubleshooting Matrix

| Observed Symptom | Underlying Root Cause | Verification & Remediation Step |
|:---|:---|:---|
| **Actuator does not move; `0x201` reports `Mode = 0` (Assist)** | Static high signal on `Control_Enable` without prior low edge | Inspect CAN sniffer trace for initial frames. Ensure `Control_Enable = 0` is transmitted for $\ge 100\text{ ms}$ before setting to `1`. |
| **Actuator completely ignores commands; `0x201` counter does not increment** | Invalid Checksum Algorithm (e.g. using XOR8 instead of Additive Sum) | Verify Byte 7 of `0x169`. Sum bytes 0 through 6: `(B0+B1+B2+B3+B4+B5+B6) & 0xFF`. If not matching Byte 7, actuator discards frame. |
| **Actuator turns to commanded angle, but turns limp/shuts down when returning to $0.0^\circ$** | Note 11 Zero-Speed Interlock | Check Byte 6 of `0x169`. If Byte 6 is `0` km/h at $0^\circ$, motor shuts off. Set Byte 6 to `10` ($10\text{ km/h}$) whenever vehicle is active. |
| **Actuator snaps violently to the mechanical limit upon boot** | Endianness swapped or angle offset incorrect | Ensure Motorola Big-Endian packing (`Byte 1 = MSB`, `Byte 2 = LSB`). Base neutral raw value is **`30000` (`0x7530`)**, not `3000` or `0`. |
| **Actuator responds on initial boot, but becomes permanently unresponsive after unplugging and replugging CAN cable** | Actuator reset to Mode 0 upon power/link recovery; no new rising edge provided | Implement the Hot-Plug Auto-Rearm watchdog (§4). Drop `Control_Enable` to 0 for 200 ms when `0x201` is reacquired. |
| **`0x201` reports `Error_Status = 2` or `3`** | Slew rate out of boundary or physical rack obstruction | Verify `VCU_SES_Tgt_StrAngleSpd` is within $125 \text{ to } 525^\circ/\text{s}$. Check mechanical linkages for mechanical jamming. |

# SYNTREE EPS-C — Steer-by-Wire Unit

CAN-controlled steering actuator. Factory-programmed IDs (not reconfigurable).

---

## 1. CAN Interface

| Parameter | Value |
|-----------|-------|
| Bus | Low-level CAN (500 kbit/s) |
| Command ID | `0x169` VCU_SES_REQ (customized from factory default `0x200`) |
| Status ID | `0x201` SES_STATUS |
| Command rate | 20 ms (50 Hz) — **continuous transmission required** |
| Status rate | 10 ms (100 Hz) |
| Endianness | Motorola LSB (little-endian) |
| DLC (both) | 8 |

> **ID note**: Factory command ID `0x200` is the factory default, customized to `0x169`. `RT_DRIVE_CMD` is placed at `0x204` to avoid collision.

---

## 2. Command Frame — 0x169 VCU_SES_REQ

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SES_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = enable calibration |
| `VCU_SES_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = enable active control (rising-edge trigger, no separate Control_Mode signal) |
| (reserved) | 2 | 6 | — | — | — | — | — | — | Bits 2–7 — not enumerated in CSV |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `VCU_SES_Tgt_StrAngle` | 16 | 16 | i16 | 0.1 | -3000 | -700 | 700 | deg | Target angle. Negative = left, positive = right. Raw 30000→0°. Note: offset -3000 is unusual — physical scale is 0.1°/bit centered at 30000 raw for 0°. |
| `VCU_SES_Tgt_StrAngleSpd` | 32 | 16 | u16 | 1 | 0 | 125 | 525 | deg/s | Maximum turning speed / slew rate |
| `VCU_SES_RollCnt_Enable` | 40 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| `VCU_SES_CheckSum_Enable` | 41 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| (reserved) | 42 | 2 | — | — | — | — | — | — | |
| `VCU_SES_RollCnt` | 44 | 4 | u8 | 1 | 0 | 0 | 15 | — | Rolling counter. Increment every frame. |
| `VCU_Veh_Spd_Value` | 48 | 8 | u8 | 1 | 0 | 0 | 255 | km/h | Vehicle speed |
| `VCU_SES_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Sum of bytes 0–6, low byte only |

### Byte layout

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | Align[0]+CtrlEn[1]+rsvd[2-7] | rsvd | angle [7:0] | angle [15:8] | speed [7:0] | speed[11:8](lower nibble)+security(upper nibble) | veh_spd | checksum |

### Unit conversion (internal mdeg ↔ SYNTREE raw, offset -3000)

The CSV defines an offset of -3000 for `VCU_SES_Tgt_StrAngle`, meaning 0° corresponds to a raw value of 30000.

```
SYNTREE raw = (internal_angle_mdeg / 100) + 30000    (45500 mdeg → 455 + 30000 = 30455 raw → 45.5°)
internal_mdeg = (SYNTREE raw - 30000) × 100          (30455 raw → 455 × 100 = 45500 mdeg → 45.5°)
```

---

## 3. Status Frame — 0x201 SES_STATUS

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SES_INF_Angle_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Center Finding Status. 0=Center Finding, 1=Found. |
| `SES_Control_Mode_Status` | 1 | 2 | u8 | 1 | 0 | 0 | 3 | enum | Control Mode Feedback. 0=Manual, 1=Automatic. |
| (unaccounted) | 3 | 3 | — | — | — | — | — | — | Bits 3–5 — not enumerated in CSV |
| `SES_Error_Status` | 6 | 2 | u8 | 1 | 0 | 0 | 3 | enum | Error Status. 0=Normal, 1=L1, 2=L2, 3=L3. |
| (unaccounted) | — | 8 | — | — | — | — | — | — | Byte 1 — not enumerated in CSV |
| `SES_StrAngle` | 16 | 16 | u16 | 0.1 | -3000 | -700 | 700 | ° | Steering Angle. Unsigned per CSV. Raw 30000→0°. |
| `SES_Tgt_StrAngleSpd` | 32 | 16 | i16 | 0.5 | 0 | 0 | 1480 | °/s | Target Angle Speed feedback. Overlaps Torq at byte 5. |
| `EPS_SteeringWheel_Torq` | 40 | 8 | u8 | 0.1 | -12.1 | -12 | 12 | Nm | Steering Wheel Torque. Init 0x79 (121=0 Nm). Overlaps StrAngleSpd[15:8]. |
| `SES_RollCnt_Enable_Status` | 48 | 1 | bool | 1 | 0 | 0 | 1 | — | Life Signal Enable Feedback. 0=Invalid, 1=Valid. |
| `SES_CheckSum_Enable_Status` | 49 | 1 | bool | 1 | 0 | 0 | 1 | — | Checksum Enable Feedback. |
| (unaccounted) | 50 | 2 | — | — | — | — | — | — | |
| `SES_RollCnt_Status` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Life Signal Feedback. Rolling counter 0–15. |
| `SES_CheckSum_Status` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum Feedback = XOR(bytes 0–6) ^ 0xFF. |

### Byte layout

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | AngleSts[0]+ModeSts[1:2]+(gap)+Error[6:7] | (unacc.) | StrAngle [7:0] | StrAngle [15:8] | Speed [7:0] | Speed[15:8] / Torq [7:0] (overlap) | RollCntEn[0]+CksEn[1]+(gap)+RollCnt[4:7] | CksSum_Stat |

---

## 4. Control Mode — Angle Control

Command the steering rack to a specific angle. The EPS-C's internal PID drives the motor until the built-in angle sensor matches the target.

**Slew rate (`VCU_SES_Tgt_StrAngleSpd`)** is a required second dimension — unlike the brake, every steering movement must specify a maximum turning speed in degrees/second. Low slew rate = smooth, controlled turns. High slew rate = aggressive snap (risk of traction loss or rack damage).

**RT implementation:** Slew rate is speed-dependent. At low vehicle speed → low slew rate for comfort. At high speed → fast slew rate for responsiveness, but within the dynamic angle clamp (§5.2).

---

## 5. Safety Mechanisms (RT ESP32-S3)

These are implemented in the RT firmware, not in the EPS-C unit itself.

### 5.1 Software Hard-Stops

The EPS-C accepts commands up to ±780° (raw ±7800). The physical trike steering rack maxes out around ±35–45°. Commanding beyond the mechanical limit will stall the motor, potentially burning out the driver or snapping a tie-rod.

| Parameter | Value | Description |
|-----------|-------|-------------|
| `kSteerHardLimitDeg` | 40.0 | Software clamp, inside physical end-stops |
| Enforcement | RT `control_task` | Any Jetson command exceeding ±40° is clamped before `0x169` TX |

### 5.2 Dynamic Angle Clamp (Rollover Prevention)

A 40° turn at 2 km/h is safe. A 40° turn at 25 km/h will flip the trike. Maximum allowable steering angle is inversely proportional to vehicle speed.

| Speed | Max angle |
|-------|-----------|
| 2 km/h (~555 mm/s) | ±40° |
| 25 km/h (~6944 mm/s) | ±5° |
| Interpolation | Linear between these points |

Enforcement: RT `control_task` clamps resolved angle to `max_angle = f(RT_PidMeasured)` before CAN TX. This overrides Jetson regardless of what `/cmd_vel` requests.

### 5.3 Following Error Detection

If the physical wheel gets stuck (rock jam, linkage failure) while the EPS-C tries to turn:

| Parameter | Value | Description |
|-----------|-------|-------------|
| Threshold | 5° | `|commanded − SES_StrAngle|` |
| Duration | 300 ms | Must persist this long before trigger |
| Action | `mode_set(ESTOP)` | System-wide emergency stop |

RT compares the last commanded angle (from `0x169`) against the feedback angle (from `0x201 SES_StrAngle`) every control tick.

---

## 6. Security Bytes

### Rolling Counter

Same as SEB: 4-bit value (0–15), increment every frame. Two consecutive frames with same counter → EPS-C rejects, triggers fault.

### Checksum

Same algorithm as SEB: `XOR(bytes[0..6]) ^ 0xFF`. Placed in Byte 7.

### Security Enable Bits (Byte 5) — Unique to EPS-C

Unlike the SEB, the EPS-C requires explicit enable bits to activate its security checks:

- `roll_cnt_enable` (Byte 5, bit 0): **Must be 1**
- `checksum_enable` (Byte 5, bit 1): **Must be 1**

If either is 0, the unit may ignore perfectly valid rolling counter and checksum values.

---

## 7. Boot Sequence — "Listen Before Speaking"

The EPS-C has an absolute encoder that retains calibration across power cycles. No physical homing sweep is required on every boot. However, a strict software handshake is mandatory.

```
State machine:

STEER_BOOT_WAIT:
  - 500 ms delay after power-on
  - DO NOT transmit any 0x169 frames
  - → STEER_LISTEN_SYNC

STEER_LISTEN_SYNC:
  - Wait for 0x201 SES_STATUS frame
  - Read SES_StrAngle (current physical angle)
  - CRITICAL: Set active_target_angle = current_physical_angle
    (If trike was parked with wheels at 15°, first command must be 15° — not 0°)
  - Wait for SES_INF_Angle_Status == 1 (aligned)
  - → STEER_ACTIVE

STEER_ACTIVE:
  - Transmit 0x169 at 50 Hz continuously
  - First frame commands exactly the current angle (no movement)
  - Then follow Jetson targets with dynamic clamp applied
  - Monitor following error → ESTOP on fault

STEER_FAULT:
  - Stop transmitting 0x169
  - EPS-C will timeout-fault (lock or limp — verify behavior with SYNTREE spec)
```

**Critical rules:**
1. Never apply power while VCU is already sending commands — causes angle sensor detection error.
2. Never command 0° on boot if the wheels are physically turned — the resulting snap can damage the rack or cause injury.
3. First frame after sync must command the current angle (stay where you are).
4. `SES_INF_Angle_Status` must be 1 before AUTO mode engages. Drive motor locked out until aligned.

---

## 8. One-Time Calibration

- **Required:** Only when the EPS-C is first mated to a new mechanical assembly.
- **Procedure:** Trike stationary, `VCU_SES_Alignment_Enable = 1`. Motor physically finds end-stops or index pulse to establish center.
- **After calibration:** ECU is permanently paired to that hardware. Not interchangeable between vehicles.
- **Power cycles:** Calibration state retained. No daily homing needed.
- **Boot-time:** Only the software sync handshake (§7) is required — no mechanical movement.

---

## 9. C++ Implementation Guide

```cpp
#include <stdint.h>
#include <string.h>

// Factory default — NOT reconfigurable
#define CAN_ID_VCU_SES_REQ 0x169  // customized from factory default 0x200

typedef struct {
    uint8_t align_enable    : 1;   // Byte0,b0
    uint8_t control_enable  : 1;   // Byte0,b1 — rising-edge trigger, no separate Control_Mode
    uint8_t reserved_0      : 6;   // Byte0,b2-7

    uint8_t reserved_1;            // Byte1 — not enumerated in CSV

    // Signed i16, scale 0.1 deg/bit, offset -3000, min -700, max 700.
    // Example: 0° → 30000 (raw), 45.5° right → 30455 (raw)
    int16_t target_angle;

    uint16_t target_speed;         // Bytes4-5, u16, scale 1 deg/s, 125-525

    // Byte 5 lower nibble: target_speed[11:8]
    // Byte 5 upper nibble: security
    uint8_t roll_cnt_enable  : 1;  // Byte5,b40 — MUST be 1
    uint8_t checksum_enable  : 1;  // Byte5,b41 — MUST be 1
    uint8_t reserved_2       : 2;  // Byte5,b42-43
    uint8_t rolling_counter  : 4;  // Byte5,b44-47, 0-15

    uint8_t vehicle_speed;         // Byte6, u8, 0-255 km/h
    uint8_t checksum;              // Byte7, sum(bytes[0..6]) & 0xFF
} __attribute__((packed)) VCU_SES_Req_t;


void ses_send_command(float target_angle_deg, uint16_t speed_limit_deg_s) {
    static uint8_t roll_cnt = 0;
    VCU_SES_Req_t payload;
    memset(&payload, 0, sizeof(payload));

    // 1. Enables (no separate Control_Mode — control_enable rising-edge triggers angle control)
    payload.align_enable   = 1;
    payload.control_enable = 1;

    // 2. Security enables — MUST be 1
    payload.roll_cnt_enable = 1;
    payload.checksum_enable = 1;

    // 3. Angle target (internal mdeg → SYNTREE raw with offset -3000)
    //    internal_angle_mdeg / 100 = target_angle_deg × 10, then add 30000 offset
    payload.target_angle = (int16_t)(target_angle_deg * 10.0f + 30000);

    // 4. Slew rate (16-bit, 125-525)
    payload.target_speed = speed_limit_deg_s;

    // 5. Rolling counter
    payload.rolling_counter = roll_cnt;
    roll_cnt = (roll_cnt + 1) & 0x0F;

    // 6. Vehicle speed
    payload.vehicle_speed = 0;  // set from vehicle state

    // 7. Checksum (sum of bytes 0-6, low byte only)
    uint8_t* raw = (uint8_t*)&payload;
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++) sum += raw[i];
    payload.checksum = sum & 0xFF;

    // 8. Transmit
    // CAN_Write(CAN_ID_VCU_SES_REQ, 8, raw);
}
```

---

## 10. Error Handling

| Condition | EPS-C Response | Controller Action |
|-----------|---------------|-------------------|
| Rolling counter repeats | Reject frame, fault | Reset counter, re-sync |
| Checksum mismatch | Discard frame | Re-transmit with correct checksum |
| Comm timeout (>20 ms no frame) | Internal comm fault | Restart boot sequence |
| Security enables = 0 | May ignore frame unpredictably | Firmware bug — enforce in struct |
| `SES_Error_Status > 0` | Degraded mode | Log, report, ESTOP if L2/L3 |
| Following error >5° for 300 ms | — | RT triggers system ESTOP |
| Sync timeout (no `0x201` for 2s) | — | Remain in STEER_FAULT, MANUAL only |
| EPS-C timeout-fault behavior | TBD — lock, center, or freewheel | Verify against SYNTREE spec |

---

## 11. Integration Notes

- **Controlled by:** RT ESP32-S3 (low-level CAN)
- **Mode:** Angle (1) — only supported mode
- **No daily homing.** Absolute encoder retains calibration across power cycles.
- **Not reconfigurable.** Factory CAN IDs are fixed. EPS-C command `0x169` (customized from factory default `0x200`). `RT_DRIVE_CMD` is at `0x204` to avoid collision.
- **MANUAL mode:** RT does not send `0x169`. EPS-C operates standalone from rider steering wheel input.
- **AUTO mode:** RT sends `0x169` at 50 Hz with resolved angle + dynamic clamp + slew rate.
- **ESTOP:** RT stops sending `0x169`. EPS-C timeout-faults (behavior TBD).

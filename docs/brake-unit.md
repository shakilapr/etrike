# SYNTREE SEB — Electro-Hydraulic Brake Unit

CAN-controlled brake actuator. Factory-programmed IDs (not reconfigurable).

---

## 1. CAN Interface

| Parameter | Value |
|-----------|-------|
| Bus | Low-level CAN (500 kbit/s) |
| Command ID | `0x720` VCU_SEB_REQ |
| Status ID | `0x721` SEB_STATUS |
| Command rate | 20 ms (50 Hz) — **continuous transmission required** |
| Status rate | 10 ms (100 Hz) |
| Endianness | Motorola LSB (little-endian) |
| DLC (both) | 8 |

---

## 2. Command Frame — 0x720 VCU_SEB_REQ

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SEB_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Calibration enable |
| `VCU_SEB_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Active control enable |
| `VCU_SEB_Control_Mode` | 2 | 2 | u8 | 1 | 0 | 0 | 2 | enum | 0=None, 1=Stroke, 2=Pressure |
| `VCU_SEB_AutoBrake` | 4 | 1 | bool | 1 | 0 | 0 | 1 | — | Auto-brake trigger |
| (reserved) | 5 | 3 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `VCU_SEB_Stroke_Value_Req` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Requested stroke position |
| `VCU_SEB_Pre_Value_Req` | 32 | 16 | u16 | — | — | — | — | MPa | Requested pressure (TBD: verify exact scale against spec) |
| (reserved) | 48 | 4 | — | — | — | — | — | — | |
| `VCU_SEB_RollCnt` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Rolling counter. Increment every frame. |
| `VCU_SEB_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | XOR of bytes 0–6, then `^ 0xFF` |

### Byte layout

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | ctrl bits | rsvd | stroke [7:0] | stroke [15:8] | press [7:0] | press [15:8] | rsvd(4)+RollCnt(4) | checksum |

### Stroke conversion

```
raw_value = (physical_mm + 30.0) / 0.05
physical_mm = raw_value × 0.05 − 30.0
```

| Physical | Raw | Use case |
|----------|-----|----------|
| −5 mm | 500 | Minimum |
| 0 mm | 600 | Released (no brake) |
| 15 mm | 900 | Manual lever pressed |
| 27 mm | 1140 | ESTOP — full brake |

---

## 3. Status Frame — 0x721 SEB_STATUS

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SEB_Alignment_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = aligned |
| `SEB_Control_Enable_Status` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Control enabled |
| `SEB_Control_Mode_Status` | 2 | 2 | u8 | 1 | 0 | 0 | 2 | enum | Current active mode |
| `SEB_Error_Status` | 4 | 2 | u8 | 1 | 0 | 0 | 3 | enum | 0=Normal, 1=L1, 2=L2, 3=L3 |
| (reserved) | 6 | 2 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `SEB_Stroke_Value` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Actual measured stroke |
| `SEB_Pressure_Value` | 32 | 16 | u16 | — | — | — | — | MPa | Actual hydraulic pressure |
| `SEB_RollCnt_Status` | 48 | 4 | u8 | 1 | 0 | 0 | 15 | — | Echoes received rolling counter |
| (reserved) | 52 | 4 | — | — | — | — | — | — | |
| `SEB_CheckSum_Status` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum verification status |

### Byte layout

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | status bits | rsvd | stroke [7:0] | stroke [15:8] | press [7:0] | press [15:8] | rsvd(4)+RollCnt(4) | cksum_stat |

---

## 4. Control Modes

### Mode 1 — Stroke Control (Position-Based)

Command the actuator's internal pushrod to move a specific distance in millimeters. Best for mimicking human pedal travel or simple proportional control.

- 0 mm = no brake
- 15 mm = moderate braking
- 27 mm = full lock

**Current usage in E-Trike:** SYS uses Stroke Mode exclusively. The only brake triggers are the physical lever (binary) and ESTOP (binary) — both map to fixed stroke positions. No closed-loop pressure control needed.

### Mode 2 — Pressure Control (Force-Based)

Command a specific hydraulic pressure in the brake fluid lines (MPa). The unit's internal PID moves the motor to maintain that exact pressure.

**Planned for AUTO mode** once the brake arbitration gap is closed. Benefits:
- Compensates for brake pad wear
- Compensates for fluid temperature expansion
- Safer for autonomous deceleration — maintains commanded deceleration regardless of mechanical variance

---

## 5. Security Bytes

### Rolling Counter

4-bit value (0–15) in Byte 6, bits 4–7. **Must increment every frame.** If the SEB receives two consecutive frames with the same counter value, it assumes the controller is frozen and rejects the command.

```
roll_cnt = (roll_cnt + 1) & 0x0F;  // increment, wrap at 15
```

### Checksum

8-bit XOR of bytes 0–6, then XOR with `0xFF`. Placed in Byte 7.

```cpp
uint8_t checksum = 0;
for (int i = 0; i < 7; i++) checksum ^= raw_bytes[i];
checksum ^= 0xFF;
```

> **Verify against SYNTREE spec** — some units use a different constant or a shift operation.

---

## 6. Boot Sequence — "Listen Before Speaking"

The SEB requires a strict startup handshake. Violating this causes sensor detection errors.

```
State machine:

BRAKE_BOOT_WAIT:
  - 500 ms delay after power-on
  - DO NOT transmit any 0x720 frames
  - → BRAKE_LISTEN_SYNC

BRAKE_LISTEN_SYNC:
  - Wait for 0x721 SEB_STATUS frame
  - Read SEB_Stroke_Value (current physical position)
  - Set initial command target = current stroke (hold position — do not move)
  - Wait for SEB_Alignment_Status == 1
  - → BRAKE_ACTIVE

BRAKE_ACTIVE:
  - Transmit 0x720 at 50 Hz continuously
  - First frame commands exactly the current position (no movement)
  - Rolling counter + checksum on every frame
  - → BRAKE_FAULT on error

BRAKE_FAULT:
  - Stop transmitting
  - Log error
```

**Critical rules:**
1. Never apply power while VCU is already sending commands — causes angle sensor detection error.
2. Never command a position until the current position is known.
3. First frame after sync must command the current position (no movement).

---

## 7. C++ Implementation Guide

```cpp
#include <stdint.h>
#include <string.h>

#define CAN_ID_VCU_SEB_REQ 0x720

typedef struct {
    uint8_t align_enable   : 1;
    uint8_t control_enable : 1;
    uint8_t control_mode   : 2;   // 0=None, 1=Stroke, 2=Pressure
    uint8_t auto_brake     : 1;
    uint8_t reserved_0     : 3;

    uint8_t reserved_1;

    // Physical = (raw × 0.05) − 30
    // Example: 0 mm → raw = (0 + 30) / 0.05 = 600
    uint16_t stroke_req;

    uint16_t pressure_req;   // TBD: verify exact bit-length in spec

    uint8_t reserved_2     : 4;
    uint8_t rolling_counter : 4;   // Increment 0–15 every frame

    uint8_t checksum;              // XOR(bytes[0..6]) ^ 0xFF
} __attribute__((packed)) VCU_SEB_Req_t;


void seb_send_command(float target_stroke_mm) {
    static uint8_t roll_cnt = 0;
    VCU_SEB_Req_t payload;
    memset(&payload, 0, sizeof(payload));

    // 1. Enable + Mode
    payload.align_enable  = 1;
    payload.control_enable = 1;
    payload.control_mode   = 1;   // Stroke Mode

    // 2. Stroke target (apply scale + offset)
    payload.stroke_req = (uint16_t)((target_stroke_mm + 30.0f) / 0.05f);

    // 3. Rolling counter
    payload.rolling_counter = roll_cnt;
    roll_cnt = (roll_cnt + 1) & 0x0F;

    // 4. Checksum
    uint8_t* raw = (uint8_t*)&payload;
    uint8_t cksum = 0;
    for (int i = 0; i < 7; i++) cksum ^= raw[i];
    payload.checksum = cksum ^ 0xFF;

    // 5. Transmit
    // CAN_Write(CAN_ID_VCU_SEB_REQ, 8, raw);
}
```

---

## 8. Error Handling

| Condition | SEB Response | Controller Action |
|-----------|-------------|-------------------|
| Rolling counter repeats | Reject frame, trigger fault | Reset counter, re-sync |
| Checksum mismatch | Discard frame | Re-transmit with correct checksum |
| Comm timeout (>20 ms no frame) | Internal fault | Restart boot sequence |
| `SEB_Error_Status > 0` | Unit enters degraded mode | Log, report via `0x011` |
| Sync timeout (no `0x721` for 2s) | — | Remain in BRAKE_FAULT |

---

## 9. Integration Notes

- **Controlled by:** SYS ESP32-S3 (low-level CAN)
- **Current mode:** Stroke (1) — lever press → 15 mm, ESTOP → 27 mm, released → 0 mm
- **Planned mode:** Pressure (2) — when RT brake arbitration path to SYS is implemented (§12.1 in architecture.md)
- **No daily homing required.** One-time zero-calibration when first mated to mechanical assembly. Calibration retained across power cycles.
- **Not reconfigurable.** CAN IDs are factory-programmed. `RT_DRIVE_SETPOINT` placed at `0x202` to avoid collision with EPS-C `0x200`.

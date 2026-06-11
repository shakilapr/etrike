# CAN Signal Dictionary — E-Trike

Two physical CAN buses at 500 kbit/s. All multi-byte fields big-endian (MSB first). RT bridges selected IDs between buses (same ID, same payload).

---

## 1. Low-Level CAN Bus

Nodes: RT ESP32-S3, SYS ESP32-S3, Brake CAN module, Steering CAN module, DC-DC converter (72V→12V).

---

### 0x001 — SAFETY_ESTOP

| Property | Value |
|----------|-------|
| **Sender** | Any (RT, SYS) |
| **Receiver(s)** | All nodes on low-level |
| **DLC** | 0 |
| **Period** | On event |
| **Priority** | Highest (ID 0x001 wins all arbitration) |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| (none) | — | 0 | — | — | — | — | — | — | Presence of this frame = emergency stop |

**Behavior**: Recipient sets mode to ESTOP immediately. Motor stop, brake engage, steering disable, DCDC off.

---

### 0x010 — SYS_BRAKE_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | Brake CAN module |
| **DLC** | 1 |
| **Period** | On state change |
| **Priority** | Very high |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_BrakeEngage` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = release, 1 = engage |

---

### 0x011 — SYS_SAFETY_STATUS

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | RT (forwards to Jetson on high-level) |
| **DLC** | 2 |
| **Period** | 5 Hz (200 ms) |
| **Priority** | Very high |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_EstopActive` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = not active, 1 = ESTOP active |
| `SYS_HeartbeatOk` | 8 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = RT heartbeat lost, 1 = RT heartbeat OK |

---

### 0x012 — SYS_DCDC_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | DC-DC converter (72V→12V) |
| **DLC** | 1 |
| **Period** | On state change |
| **Priority** | Very high |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_DcdcEnable` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = converter OFF (12V rail dead), 1 = converter ON |

**Behavior**: ESTOP → OFF. All other modes → ON. The 12V accessory power relay (GPIO27) is a secondary cut (defense-in-depth).

---

### 0x110 — SYS_MODE_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 1 |
| **Period** | On state change |
| **Priority** | High |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_Mode` | 0 | 8 | u8 | 1 | 0 | 0 | 2 | enum | 0 = MANUAL, 1 = AUTO, 2 = ESTOP |

---

### 0x120 — SYS_THROTTLE_POS

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | RT (forwards to Jetson on high-level) |
| **DLC** | 2 |
| **Period** | 100 Hz (10 ms) |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_ThrottleSpeed` | 0 | 16 | i16 | 1 | 0 | 0 | 3000 | mm/s | ADC-mapped throttle position. In MANUAL = rider input; in AUTO = telemetry only |

---

### 0x200 — RT_DRIVE_SETPOINT

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 5 |
| **Period** | 100 Hz (10 ms) |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `RT_MotorSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s | Rear motor target speed (negative = reverse) |
| `RT_Gear` | 32 | 8 | u8 | 1 | 0 | 0 | 3 | enum | 0 = N, 1 = D, 2 = S, 3 = R |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 | 4 |
|------|---|---|---|---|---|
| Content | `RT_MotorSpeed` [31:24] | `RT_MotorSpeed` [23:16] | `RT_MotorSpeed` [15:8] | `RT_MotorSpeed` [7:0] | `RT_Gear` |

---

### 0x230 — RT_STEER_CMD

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | Steering CAN module (drive-by-wire) |
| **DLC** | 4 |
| **Period** | 100 Hz (10 ms) | AUTO mode only |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `RT_SteerAngle` | 0 | 32 | i32 | 1 | 0 | -45000 | 45000 | mdeg | Front steer angle (+right, -left). 0 = straight. |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 |
|------|---|---|---|---|
| Content | `RT_SteerAngle` [31:24] | [23:16] | [15:8] | [7:0] |

**Mode behavior**:
- MANUAL: RT does not send. Steering module operates standalone.
- AUTO: RT sends at 100 Hz.
- ESTOP: RT stops sending. Module should center/lock.

---

### 0x302 — HOST_LIGHT_CMD (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 (forwarded from Jetson on high-level) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 1 |
| **Period** | On change |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `HOST_LeftTurn` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Left turn signal |
| `HOST_RightTurn` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Right turn signal |
| `HOST_BrakeLight` | 2 | 1 | bool | 1 | 0 | 0 | 1 | — | Brake light |
| `HOST_Headlight` | 3 | 1 | bool | 1 | 0 | 0 | 1 | — | Headlight |
| (reserved) | 4 | 4 | — | — | — | — | — | — | Reserved, set to 0 |

---

### 0x600 — SYS_DIAG

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | RT (forwards to Jetson on high-level) |
| **DLC** | 8 |
| **Period** | 1 Hz (1000 ms) |
| **Priority** | Lowest |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `SYS_DiagMode` | 0 | 8 | u8 | 1 | 0 | 0 | 2 | enum | Current mode (0=M, 1=A, 2=ESTOP) |
| `SYS_DiagBrakeEngaged` | 8 | 8 | u8 | 1 | 0 | 0 | 1 | — | Brake actuator state |
| `SYS_DiagHeartbeatOk` | 16 | 8 | u8 | 1 | 0 | 0 | 1 | — | RT heartbeat status |
| `SYS_DiagEstopActive` | 24 | 8 | u8 | 1 | 0 | 0 | 1 | — | ESTOP input state |
| `SYS_DiagFreeHeapKb` | 32 | 16 | u16 | 1 | 0 | 0 | 65535 | KiB | ESP32 free heap |
| `SYS_DiagTec` | 48 | 8 | u8 | 1 | 0 | 0 | 255 | — | TWAI transmit error counter |
| `SYS_DiagRec` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | TWAI receive error counter |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | mode | brake | hb_ok | estop | heap [15:8] | heap [7:0] | tec | rec |

---

### 0x7FF — HEARTBEAT (low-level)

| Property | Value |
|----------|-------|
| **Sender** | RT, SYS |
| **Receiver(s)** | RT, SYS |
| **DLC** | 0 |
| **Period** | 2 Hz (500 ms) |
| **Priority** | Lowest |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| (none) | — | 0 | — | — | — | — | — | — | Presence = node alive. SYS monitors RT HB; RT monitors SYS HB. |

**Timeout**: 1500 ms (3 missed heartbeats). In AUTO, RT HB timeout triggers ESTOP on SYS.

---

## 2. High-Level CAN Bus

Nodes: Jetson Orin NX, RT ESP32-S3 (MCP2515 SPI).

---

### 0x001 — SAFETY_ESTOP (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson or RT (forwarded from low-level) |
| **Receiver(s)** | Jetson, RT |
| **DLC** | 0 |
| **Period** | On event |
| **Priority** | Highest |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| (none) | — | 0 | — | — | — | — | — | — | Bridged by RT between buses |

---

### 0x011 — SYS_SAFETY_STATUS (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 (forwarded from low-level) |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 2 |
| **Period** | 5 Hz (200 ms) |
| **Priority** | Very high |

Signal layout identical to low-level `0x011` (see §1). Payload forwarded transparently.

---

### 0x120 — SYS_THROTTLE_POS (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 (forwarded from low-level) |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 2 |
| **Period** | 100 Hz (10 ms) |
| **Priority** | Medium |

Signal layout identical to low-level `0x120` (see §1). Payload forwarded transparently.

---

### 0x210 — RT_STATE_REPORT

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 3 |
| **Period** | 10 Hz (100 ms) |
| **Priority** | Low |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `RT_Mode` | 0 | 8 | u8 | 1 | 0 | 0 | 2 | enum | 0 = MANUAL, 1 = AUTO, 2 = ESTOP |
| `RT_SteerValid` | 8 | 8 | u8 | 1 | 0 | 0 | 1 | bool | Steering actively controlled (AUTO, speed > threshold) |
| `RT_Reversing` | 16 | 8 | u8 | 1 | 0 | 0 | 1 | bool | Motor direction is reverse |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 |
|------|---|---|---|
| Content | `RT_Mode` | `RT_SteerValid` | `RT_Reversing` |

---

### 0x220 — RT_PID_FEEDBACK

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 6 |
| **Period** | 10 Hz (100 ms) |
| **Priority** | Low |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `RT_PidSetpoint` | 0 | 16 | i16 | 1 | 0 | -500 | 3000 | mm/s | PID speed setpoint |
| `RT_PidMeasured` | 16 | 16 | i16 | 1 | 0 | -500 | 3000 | mm/s | Encoder-measured speed |
| `RT_PidOutput` | 32 | 16 | i16 | 1 | 0 | -32768 | 32767 | — | PID controller output (raw) |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 |
|------|---|---|---|---|---|---|
| Content | setpoint [15:8] | setpoint [7:0] | measured [15:8] | measured [7:0] | output [15:8] | output [7:0] |

---

### 0x300 — HOST_DRIVE_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson Orin NX |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 8 |
| **Period** | ≤100 Hz (≥10 ms) | AUTO mode only |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `HOST_DriveSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s | Forward velocity (`linear.x × 1000`) |
| `HOST_YawRate` | 32 | 32 | i32 | 1 | 0 | -3000 | 3000 | mrad/s | Yaw rate (`angular.z × 1000`) |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | speed [31:24] | speed [23:16] | speed [15:8] | speed [7:0] | yaw [31:24] | yaw [23:16] | yaw [15:8] | yaw [7:0] |

**ROS 2 conversion**:
```
speed_mmps     = (int32_t)(cmd_vel.linear.x  * 1000.0)
yaw_rate_mrad_s = (int32_t)(cmd_vel.angular.z * 1000.0)
```

---

### 0x301 — HOST_BRAKE_REQUEST

| Property | Value |
|----------|-------|
| **Sender** | Jetson Orin NX |
| **Receiver(s)** | RT ESP32-S3 |
| **DLC** | 4 |
| **Period** | On demand |
| **Priority** | Medium |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `HOST_BrakePressure` | 0 | 32 | i32 | 1 | 0 | 0 | — | kPa | Desired brake pressure. 0 = release. RT arbitrates: max(RT_computed, HOST_request). |

**Byte layout** (big-endian):

| Byte | 0 | 1 | 2 | 3 |
|------|---|---|---|---|
| Content | `HOST_BrakePressure` [31:24] | [23:16] | [15:8] | [7:0] |

> **Design gap**: RT-derived brake pressure has no CAN path to SYS. This frame is consumed by RT but the arbitrated result is not forwarded.

---

### 0x302 — HOST_LIGHT_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson Orin NX |
| **Receiver(s)** | RT ESP32-S3 (forwards to SYS on low-level) |
| **DLC** | 1 |
| **Period** | On change |
| **Priority** | Medium |

Signal layout identical to low-level `0x302` (see §1). Payload forwarded transparently by RT.

---

### 0x400 — RT_OBSTACLE_DIST

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 4 |
| **Period** | 10 Hz (100 ms) |
| **Priority** | Low |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| `RT_ObstacleDistance` | 0 | 32 | u32 | 1 | 0 | 0 | 4294967295 | mm | 0 = no reading, UINT32_MAX = timeout/error |

---

### 0x600 — SYS_DIAG (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 (forwarded from low-level) |
| **Receiver(s)** | Jetson Orin NX |
| **DLC** | 8 |
| **Period** | 1 Hz (1000 ms) |
| **Priority** | Lowest |

Signal layout identical to low-level `0x600` (see §1). Payload forwarded transparently.

---

### 0x7FF — HEARTBEAT (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson, RT |
| **Receiver(s)** | Jetson, RT |
| **DLC** | 0 |
| **Period** | 2 Hz (500 ms) |
| **Priority** | Lowest |

| Signal | Start bit | Length | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|--------|------|-------|--------|-----|-----|------|-------------|
| (none) | — | 0 | — | — | — | — | — | — | RT monitors Jetson HB. Jetson monitors RT HB. |

**Timeout**: 500 ms (command staleness, RT watchdog). If Jetson HB lost, RT sends zero setpoints on low-level (controlled stop).

---

## 3. CAN ID summary (both buses)

### Low-level bus

| ID | Name | Sender | Receiver(s) | DLC | Rate |
|----|------|--------|-------------|-----|------|
| `0x001` | SAFETY_ESTOP | RT, SYS | All | 0 | Event |
| `0x010` | SYS_BRAKE_CMD | SYS | Brake module | 1 | Change |
| `0x011` | SYS_SAFETY_STATUS | SYS | RT (→Jetson) | 2 | 5 Hz |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC converter | 1 | Change |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | Change |
| `0x120` | SYS_THROTTLE_POS | SYS | RT (→Jetson) | 2 | 100 Hz |
| `0x200` | RT_DRIVE_SETPOINT | RT | SYS | 5 | 100 Hz |
| `0x230` | RT_STEER_CMD | RT | Steering module | 4 | 100 Hz |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | Change |
| `0x600` | SYS_DIAG | SYS | RT (→Jetson) | 8 | 1 Hz |
| `0x7FF` | HEARTBEAT | RT, SYS | RT, SYS | 0 | 2 Hz |

### High-level bus

| ID | Name | Sender | Receiver(s) | DLC | Rate |
|----|------|--------|-------------|-----|------|
| `0x001` | SAFETY_ESTOP | Jetson, RT | Jetson, RT | 0 | Event |
| `0x011` | SYS_SAFETY_STATUS | RT (fwd) | Jetson | 2 | 5 Hz |
| `0x120` | SYS_THROTTLE_POS | RT (fwd) | Jetson | 2 | 100 Hz |
| `0x210` | RT_STATE_REPORT | RT | Jetson | 3 | 10 Hz |
| `0x220` | RT_PID_FEEDBACK | RT | Jetson | 6 | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQUEST | Jetson | RT | 4 | Demand |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→SYS) | 1 | Change |
| `0x400` | RT_OBSTACLE_DIST | RT | Jetson | 4 | 10 Hz |
| `0x600` | SYS_DIAG | RT (fwd) | Jetson | 8 | 1 Hz |
| `0x7FF` | HEARTBEAT | Jetson, RT | Jetson, RT | 0 | 2 Hz |

---

## 4. Forwarding rules (RT gateway)

| Direction | CAN IDs | Notes |
|-----------|---------|-------|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` | Transparent (same ID, same payload) |
| High → Low | `0x001`, `0x302` | Transparent (same ID, same payload) |
| Not forwarded | `0x300`, `0x301` | Consumed by RT only |
| Not forwarded | `0x200`, `0x230` | Generated by RT on low-level |
| Not forwarded | `0x210`, `0x220`, `0x400` | Generated by RT on high-level |
| Not forwarded | `0x010`, `0x012`, `0x110` | Low-level only |
| Not forwarded | `0x7FF` | Independent heartbeats per bus |

---

## 5. Priority groups

| Priority | CAN ID range | IDs on both buses |
|----------|-------------|-------------------|
| Highest | `0x001` | SAFETY_ESTOP |
| Very high | `0x010`–`0x01F` | BRAKE_CMD, SAFETY_STATUS, DCDC_CMD |
| High | `0x100`–`0x11F` | MODE_CMD |
| Medium | `0x120`–`0x3FF` | THROTTLE_POS, DRIVE_SETPOINT, STEER_CMD, DRIVE_CMD, BRAKE_REQUEST, LIGHT_CMD |
| Low | `0x400`–`0x5FF` | OBSTACLE_DIST, STATE_REPORT, PID_FEEDBACK |
| Lowest | `0x600`–`0x7FF` | DIAG, HEARTBEAT |

Lower CAN ID = higher arbitration priority on the bus. Safety-critical frames occupy the `0x00X` block.

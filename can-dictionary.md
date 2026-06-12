# CAN Signal Dictionary — E-Trike

Two physical CAN buses at 500 kbit/s. All fields big-endian (MSB first) unless noted.

---

## 1. Low-Level CAN Bus

Nodes: RT ESP32-S3, SYS ESP32-S3, SYNTREE EPS-C (steering), SYNTREE SEB (brake), DC-DC converter.

---

### 0x001 — SAFETY_ESTOP

| Property | Value |
|----------|-------|
| **Sender** | Any (RT, SYS) |
| **Receiver(s)** | All nodes |
| **DLC** | 0 |
| **Period** | On event |

Presence of this frame = emergency stop. Motor stop, brake engage, steering disable, DCDC off.

---

### 0x011 — SYS_SAFETY_STS

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 2 |
| **Period** | 5 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_EstopActive` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — |
| `SYS_HeartbeatOk` | 8 | 8 | u8 | 1 | 0 | 0 | 1 | — | 0 = RT alive counter frozen >1000ms, 1 = alive counter incrementing |

---

### 0x012 — SYS_DCDC_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | DC-DC converter (72V→12V) |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_DcdcEnable` | 0 | 8 | u8 | 1 | 0 | 0 | 1 | — |

ESTOP → 0 (off). All other modes → 1 (on).

---

### 0x110 — SYS_MODE_CMD

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_Mode` | 0 | 8 | u8 | 1 | 0 | 0 | 2 | enum (0=M, 1=A, 2=ESTOP) |

---

### 0x120 — SYS_THROTTLE_STS

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 2 |
| **Period** | 100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `SYS_ThrottleSpeed` | 0 | 16 | i16 | 1 | 0 | 0 | 3000 | mm/s |

---

### 0x202 — RT_DRIVE_CMD

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 5 |
| **Period** | 100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `RT_MotorSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s |
| `RT_Gear` | 32 | 8 | u8 | 1 | 0 | 0 | 3 | enum (0=N,1=D,2=S,3=R) |

Byte layout (big-endian): Byte 0-3 = speed [31:0], Byte 4 = gear.

> Placed at `0x202` to avoid collision with EPS-C factory command at `0x200`. SYNTREE units are preprogrammed and cannot be reconfigured.

---

### 0x203 — RT_BRAKE_CMD

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 4 |
| **Period** | 50 Hz (20 ms) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `RT_BrakePressure` | 0 | 32 | i32 | 1 | 0 | 0 | 20000 | kPa |

Byte layout (big-endian): Bytes 0-3 = brake pressure [kPa].

RT max-select: `brake_kpa = max(rt_obstacle, jetson_0x301)`. SYS converts: `seb_raw = (uint8_t)(kpa * 0.02f)` (verified SYNTREE spec: `VCU_SEB_Pre_Value_Req` is u8, scale 0.05 MPa/bit, range 0–5 MPa). When `0x203 > 0`, SYS switches SEB to Pressure Mode (mode=2). When `0x203 == 0`, falls back to Stroke Mode for lever/ESTOP triggers.


### 0x201 — SES_STATUS (SYNTREE EPS-C Feedback)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE EPS-C (steering module) |
| **Receiver(s)** | RT |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SES_INF_Angle_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Alignment/homing status. 1 = aligned. |
| `SES_Control_Mode_Status` | 1 | 2 | u8 | 1 | 0 | 0 | 2 | enum | Current active mode |
| `SES_Error_Status` | 4 | 2 | u8 | 1 | 0 | 0 | 3 | enum | 0=Normal, 1=L1, 2=L2, 3=L3 |
| (reserved) | 6 | 2 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `SES_StrAngle` | 16 | 16 | i16 | 0.1 | 0 | -780 | 780 | deg | Actual measured steering angle. Negative = left. |
| (reserved) | — | 16 | — | — | — | — | — | — | Bytes 4–5 |
| `EPS_SteeringWheel_Torq` | 40 | 8 | u8 | 1 | 0 | — | — | Nm | Resistance torque |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | status bits | rsvd | `SES_StrAngle` [7:0] | `SES_StrAngle` [15:8] | rsvd | `EPS_Torq` | rsvd | rsvd |

**RT usage**: Boot sync — read `SES_StrAngle` as initial command target. Active — compare against commanded angle for following error detection. `SES_INF_Angle_Status` must be 1 before AUTO engages.

**Internal conversion**: `internal_angle_mdeg = SES_StrAngle_raw × 100` (raw 455 → 45500 mdeg → 45.5°)

---

### 0x200 — VCU_SES_REQ (SYNTREE EPS-C Command)

| Property | Value |
|----------|-------|
| **Sender** | RT ESP32-S3 |
| **Receiver(s)** | SYNTREE EPS-C (steering module) |
| **DLC** | 8 |
| **Period** | 20 ms (50 Hz) — **continuous, every frame** |
| **Endianness** | Motorola LSB (little-endian) |
| **Note** | Factory default `0x200`. SYNTREE unit is preprogrammed and not reconfigurable. `RT_DRIVE_CMD` placed at `0x202` to avoid collision. |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SES_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = enable calibration |
| `VCU_SES_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = enable active control |
| `VCU_SES_Control_Mode` | 2 | 2 | u8 | 1 | 0 | 0 | 2 | enum | 0=None, 1=Angle Mode |
| (reserved) | 4 | 4 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `VCU_SES_Tgt_StrAngle` | 16 | 16 | i16 | 0.1 | 0 | -780 | 780 | deg | Target angle. Negative = left. |
| `VCU_SES_Tgt_StrAngleSpd` | 32 | 8 | u8 | 1 | 0 | 0 | 255 | deg/s | Max turning speed / slew rate |
| `roll_cnt_enable` | 40 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| `checksum_enable` | 41 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| (reserved) | 42 | 6 | — | — | — | — | — | — | |
| (reserved) | 48 | 4 | — | — | — | — | — | — | Byte 6, bits 0–3 |
| `VCU_SES_RollCnt` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Rolling counter. Increment every frame. |
| `VCU_SES_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | XOR of bytes 0–6, then `^ 0xFF` |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | ctrl bits | rsvd | angle [7:0] | angle [15:8] | speed | sec enables | rsvd(4) + RollCnt(4) | checksum |

**Internal conversion**: `VCU_SES_Tgt_StrAngle_raw = internal_angle_mdeg / 100` (45500 mdeg → 455 raw)

**Security**: If `roll_cnt_enable=0` or `checksum_enable=0`, unit may reject frames. Both must be 1. Checksum algorithm: `XOR(bytes[0..6]) ^ 0xFF` (verify exact formula against SYNTREE spec).

**Slew rate**: Speed-dependent. RT computes `VCU_SES_Tgt_StrAngleSpd` based on speed to ensure smooth steering. Lower speed → lower slew rate for comfort; higher speed → higher slew rate for responsiveness (within dynamic clamp).

---

### 0x302 — HOST_LIGHT_CMD (forwarded)

| Property | Value |
|----------|-------|
| **Sender** | RT (fwd from Jetson) |
| **Receiver(s)** | SYS |
| **DLC** | 1 |
| **Period** | On change |

| Signal | Start bit | Len | Type |
|--------|-----------|-----|------|
| `HOST_LeftTurn` | 0 | 1 | bool |
| `HOST_RightTurn` | 1 | 1 | bool |
| `HOST_BrakeLight` | 2 | 1 | bool |
| `HOST_Headlight` | 3 | 1 | bool |
| (reserved) | 4 | 4 | — |

---

### 0x600 — SYS_DIAG_RPT

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT (→ Jetson) |
| **DLC** | 8 |
| **Period** | 1 Hz |

| Signal | Start bit | Len | Type |
|--------|-----------|-----|------|
| `SYS_DiagMode` | 0 | 8 | u8 |
| `SYS_DiagBrakeEngaged` | 8 | 8 | u8 |
| `SYS_DiagHeartbeatOk` | 16 | 8 | u8 |
| `SYS_DiagEstopActive` | 24 | 8 | u8 |
| `SYS_DiagFreeHeapKb` | 32 | 16 | u16 |
| `SYS_DiagTec` | 48 | 8 | u8 |
| `SYS_DiagRec` | 56 | 8 | u8 |

Byte layout (big-endian): Byte 0=mode, 1=brake, 2=hb_ok, 3=estop, 4-5=heap, 6=tec, 7=rec.

---

### 0x720 — VCU_SEB_REQ (SYNTREE SEB Brake Command)

| Property | Value |
|----------|-------|
| **Sender** | SYS ESP32-S3 |
| **Receiver(s)** | SYNTREE SEB (brake module) |
| **DLC** | 8 |
| **Period** | 20 ms (50 Hz) — **continuous, every frame** |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `VCU_SEB_Alignment_Enable` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | Calibration enable |
| `VCU_SEB_Control_Enable` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Active control enable |
| `VCU_SEB_Control_Mode` | 2 | 2 | u8 | 1 | 0 | 0 | 2 | enum | 0=None, 1=Stroke, 2=Pressure |
| `VCU_SEB_AutoBrake` | 4 | 1 | bool | 1 | 0 | 0 | 1 | — | Auto-brake trigger |
| (reserved) | 5 | 3 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `VCU_SEB_Stroke_Value_Req` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Requested stroke position |
| `VCU_SEB_Pre_Value_Req` | 32 | 8 | u8 | 0.05 | 0 | 0 | 5 | MPa | Requested pressure. Raw = kPa × 0.02 |
| (reserved) | 40 | 8 | — | — | — | — | — | — | Byte 5 |
| `VCU_SEB_RollCnt_Enable` | 48 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| `VCU_SEB_CheckSum_Enable` | 49 | 1 | bool | 1 | 0 | 0 | 1 | — | **Must be 1** |
| (reserved) | 50 | 2 | — | — | — | — | — | — | |
| `VCU_SEB_RollCnt` | 52 | 4 | u8 | 1 | 0 | 0 | 15 | — | Rolling counter. Increment every frame. |
| `VCU_SEB_CheckSum` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | XOR of bytes 0–6, then `^ 0xFF` |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | ctrl bits | rsvd | stroke [7:0] | stroke [15:8] | press [7:0] | rsvd | rsvd(2)+RollCntEn(1)+CksEn(1)+RollCnt(4) | checksum |

**Stroke conversion**: `raw = (physical_mm + 30.0) / 0.05`

| Physical | Raw | Use case |
|----------|-----|----------|
| -5 mm | 500 | Min |
| 0 mm | 600 | Released |
| 15 mm | 900 | Manual lever pressed |
| 27 mm | 1140 | ESTOP full brake |

**Security**: Rolling counter must increment 0→15 every frame. Same value twice → SEB rejects (assumes frozen controller). Checksum = `XOR(bytes[0..6]) ^ 0xFF` (verify against SYNTREE spec).

**Mode 1 (Stroke)**: Command a specific pushrod position in mm. Best for mimicking pedal travel.
**Mode 2 (Pressure)**: Command hydraulic pressure in MPa. SEB's internal PID maintains target. Best for autonomous deceleration control (compensates for pad wear, temperature).

---

### 0x721 — SEB_STATUS (SYNTREE SEB Brake Feedback)

| Property | Value |
|----------|-------|
| **Sender** | SYNTREE SEB (brake module) |
| **Receiver(s)** | SYS ESP32-S3 |
| **DLC** | 8 |
| **Period** | 10 ms (100 Hz) |
| **Endianness** | Motorola LSB (little-endian) |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit | Description |
|--------|-----------|-----|------|-------|--------|-----|-----|------|-------------|
| `SEB_Alignment_Status` | 0 | 1 | bool | 1 | 0 | 0 | 1 | — | 1 = aligned |
| `SEB_Control_Enable_Status` | 1 | 1 | bool | 1 | 0 | 0 | 1 | — | Control enabled |
| `SEB_Control_Mode_Status` | 2 | 2 | u8 | 1 | 0 | 0 | 2 | enum | Current mode |
| `SEB_Error_Status` | 4 | 2 | u8 | 1 | 0 | 0 | 3 | enum | 0=Normal, 1=L1, 2=L2, 3=L3 |
| (reserved) | 6 | 2 | — | — | — | — | — | — | |
| (reserved) | — | 8 | — | — | — | — | — | — | Byte 1 |
| `SEB_Stroke_Value` | 16 | 16 | u16 | 0.05 | -30 | -5 | 27 | mm | Actual measured stroke |
| `SEB_Pressure_Value` | 32 | 8 | u8 | 0.05 | 0 | 0 | 5 | MPa | Actual hydraulic pressure |
| (reserved) | 40 | 8 | — | — | — | — | — | — | Byte 5 |
| `SEB_RollCnt_Status` | 48 | 4 | u8 | 1 | 0 | 0 | 15 | — | Echoes received rolling counter |
| (reserved) | 52 | 4 | — | — | — | — | — | — | |
| `SEB_CheckSum_Status` | 56 | 8 | u8 | 1 | 0 | 0 | 255 | — | Checksum status |

**Byte layout** (little-endian):

| Byte | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|------|---|---|---|---|---|---|---|---|
| Content | status bits | rsvd | stroke [7:0] | stroke [15:8] | press [7:0] | rsvd | rsvd(4)+RollCnt(4) | cksum_stat |

**SYS usage**: Boot sync — read `SEB_Stroke_Value` as initial command target. Active — confirm `SEB_Alignment_Status == 1`. `SEB_Error_Status > 0` → log and report via `0x011`.

---

### 0x7FD — RT_HEARTBEAT (low-level)

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | SYS |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1000ms (2 missed frames) → SYS triggers ESTOP (AUTO only) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung RT. |

`0x202` staleness check at 200ms provides faster detection. RT sends `0x7FD` independently on both buses (per-bus, NOT bridged; separate counters per bus).

---

### 0x7FE — SYS_HEARTBEAT (low-level)

| Property | Value |
|----------|-------|
| **Sender** | SYS |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1000ms (2 missed frames) → RT sends CAN `0x001` ESTOP (AUTO only) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung SYS. |

SYS heartbeat never leaves low bus. Startup grace period: 3 seconds (both heartbeat monitors).

---

## 2. High-Level CAN Bus

Nodes: Jetson Orin NX, RT ESP32-S3 (MCP2515 SPI).

---

### 0x001 — SAFETY_ESTOP (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson or RT (fwd from low) |
| **Receiver(s)** | Jetson, RT |
| **DLC** | 0 |
| **Period** | On event |

Bridged by RT between buses.

---

### 0x011 — SYS_SAFETY_STS (forwarded)

Forwarded from low-level by RT. Same payload layout as §1 `0x011`.

---

### 0x120 — SYS_THROTTLE_STS (forwarded)

Forwarded from low-level by RT. Same payload layout as §1 `0x120`.

---

### 0x210 — RT_STATE_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 3 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Min | Max | Unit |
|--------|-----------|-----|------|-----|-----|------|
| `RT_Mode` | 0 | 8 | u8 (enum) | 0 | 2 | — |
| `RT_SteerValid` | 8 | 8 | u8 (bool) | 0 | 1 | — |
| `RT_Reversing` | 16 | 8 | u8 (bool) | 0 | 1 | — |

Byte layout (big-endian): Byte 0=mode, 1=steer_valid, 2=reversing.

---

### 0x220 — RT_PID_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 6 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Unit |
|--------|-----------|-----|------|------|
| `RT_PidSetpoint` | 0 | 16 | i16 | mm/s |
| `RT_PidMeasured` | 16 | 16 | i16 | mm/s |
| `RT_PidOutput` | 32 | 16 | i16 | — |

Byte layout (big-endian): Bytes 0-1=sp, 2-3=meas, 4-5=out.

---

### 0x300 — HOST_DRIVE_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 8 |
| **Period** | ≤100 Hz |

| Signal | Start bit | Len | Type | Scale | Offset | Min | Max | Unit |
|--------|-----------|-----|------|-------|--------|-----|-----|------|
| `HOST_DriveSpeed` | 0 | 32 | i32 | 1 | 0 | -500 | 3000 | mm/s |
| `HOST_YawRate` | 32 | 24 | i24 | 1 | 0 | -3000 | 3000 | mrad/s |
| `HOST_Gear` | 56 | 8 | u8 | 1 | 0 | 0 | 3 | enum (0=N,1=D,2=S,3=R) |

Byte layout (big-endian): Bytes 0-3=speed, 4-6=yaw[23:0], 7=gear.

ROS 2 conversion: `speed_mmps = linear.x × 1000`, `yaw_rate_mrad_s = angular.z × 1000`.

---

### 0x301 — HOST_BRAKE_REQ

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 4 |
| **Period** | On demand |

| Signal | Start bit | Len | Type | Unit |
|--------|-----------|-----|------|------|
| `HOST_BrakePressure` | 0 | 32 | i32 | kPa |

Byte layout (big-endian): Bytes 0-3. RT arbitrates: max(RT_computed, HOST_request). **Gap**: result not yet forwarded to SYS.

---

### 0x302 — HOST_LIGHT_CMD

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT (→ SYS) |
| **DLC** | 1 |
| **Period** | On change |

Layout identical to low-level `0x302`. RT forwards transparently.

---

### 0x400 — RT_OBSTACLE_RPT

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 4 |
| **Period** | 10 Hz |

| Signal | Start bit | Len | Type | Min | Max | Unit |
|--------|-----------|-----|------|-----|-----|------|
| `RT_ObstacleDistance` | 0 | 32 | u32 | 0 | 2³²−1 | mm |

UINT32_MAX = no reading / timeout.

---

### 0x600 — SYS_DIAG_RPT (forwarded)

Forwarded from low-level by RT. Same layout as §1 `0x600`.

---

### 0x7FD — RT_HEARTBEAT (high-level)

| Property | Value |
|----------|-------|
| **Sender** | RT |
| **Receiver(s)** | Jetson |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1500ms (3 missed frames) → Jetson stops publishing `/cmd_vel` |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung RT. |

RT sends `0x7FD` independently on both buses (per-bus, NOT bridged).

---

### 0x7FC — JETSON_HEARTBEAT (high-level)

| Property | Value |
|----------|-------|
| **Sender** | Jetson |
| **Receiver(s)** | RT |
| **DLC** | 1 |
| **Period** | 2 Hz (500 ms) |
| **Timeout** | 1500ms (3 missed frames) → RT zeroes `0x202` + stops `0x200` (controlled stop) |

| Signal | Start bit | Len | Type | Description |
|--------|-----------|-----|------|-------------|
| `alive_ctr` | 0 | 8 | u8 | Increments every frame (wraps at 255). Frozen = hung Jetson. |

Jetson is QM, not safety-critical. Heartbeat loss triggers controlled stop, not ESTOP.

---

## 3. CAN ID Summary

### Low-level bus

| ID | Name | Sender | Receiver | DLC | Rate |
|----|------|--------|----------|-----|------|
| `0x001` | SAFETY_ESTOP | RT, SYS | All | 0 | Event |
| `0x011` | SYS_SAFETY_STS | SYS | RT (→Jetson) | 2 | 5 Hz |
| `0x012` | SYS_DCDC_CMD | SYS | DC-DC | 1 | Change |
| `0x110` | SYS_MODE_CMD | SYS | RT | 1 | Change |
| `0x120` | SYS_THROTTLE_STS | SYS | RT (→Jetson) | 2 | 100 Hz |
| `0x200` | VCU_SES_REQ | RT | EPS-C | 8 | **50 Hz** |
| `0x201` | SES_STATUS | EPS-C | RT | 8 | 100 Hz |
| `0x202` | RT_DRIVE_CMD | RT | SYS | 5 | 100 Hz |
| `0x203` | RT_BRAKE_CMD | RT | SYS | 4 | **50 Hz** |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | SYS | 1 | Change |
| `0x600` | SYS_DIAG_RPT | SYS | RT (→Jetson) | 8 | 1 Hz |
| `0x720` | VCU_SEB_REQ | SYS | SEB | 8 | **50 Hz** |
| `0x721` | SEB_STATUS | SEB | SYS | 8 | 100 Hz |
| `0x7FD` | RT_HEARTBEAT | RT | SYS | 1 | 2 Hz |
| `0x7FE` | SYS_HEARTBEAT | SYS | RT | 1 | 2 Hz |

### High-level bus

| ID | Name | Sender | Receiver | DLC | Rate |
|----|------|--------|----------|-----|------|
| `0x001` | SAFETY_ESTOP | Jetson, RT | Jetson, RT | 0 | Event |
| `0x011` | SYS_SAFETY_STS | RT (fwd) | Jetson | 2 | 5 Hz |
| `0x120` | SYS_THROTTLE_STS | RT (fwd) | Jetson | 2 | 100 Hz |
| `0x210` | RT_STATE_RPT | RT | Jetson | 3 | 10 Hz |
| `0x220` | RT_PID_RPT | RT | Jetson | 6 | 10 Hz |
| `0x300` | HOST_DRIVE_CMD | Jetson | RT | 8 | ≤100 Hz |
| `0x301` | HOST_BRAKE_REQ | Jetson | RT | 4 | Demand |
| `0x302` | HOST_LIGHT_CMD | Jetson | RT (→SYS) | 1 | Change |
| `0x400` | RT_OBSTACLE_RPT | RT | Jetson | 4 | 10 Hz |
| `0x600` | SYS_DIAG_RPT | RT (fwd) | Jetson | 8 | 1 Hz |
| `0x7FD` | RT_HEARTBEAT | RT | Jetson | 1 | 2 Hz |
| `0x7FC` | JETSON_HEARTBEAT | Jetson | RT | 1 | 2 Hz |

---

## 4. Forwarding Rules (RT Gateway)

RT is the only dual-bus node. Every CAN message falls into exactly one of three categories:

### Category 1: Transparent forward (same ID, same payload)

| Direction | IDs |
|-----------|-----|
| Low → High | `0x001`, `0x011`, `0x120`, `0x600` |
| High → Low | `0x001`, `0x302` |

### Category 2: Consumed by RT → different message generated

| Inbound | Bus | Outbound | Bus |
|---------|-----|----------|-----|
| `0x300` HOST_DRIVE_CMD | High | `0x202` RT_DRIVE_CMD + `0x200` VCU_SES_REQ | Low |
| `0x301` HOST_BRAKE_REQ | High | `0x203` RT_BRAKE_CMD | Low |

### Category 3: Bus-local (never forwarded, never regenerated)

| Bus | IDs |
|-----|-----|
| Low only | `0x012`, `0x110`, `0x200`, `0x202`, `0x203`, `0x720` |
| Low only | `0x201`, `0x721` (SYNTREE feedback) |
| High only | `0x210`, `0x220`, `0x400` (RT telemetry) |
| Both independent | `0x7FD`, `0x7FE`, `0x7FC` (per-node heartbeat — NOT bridged) |

---

## 5. Priority Groups

| Priority | ID Range | IDs |
|----------|----------|-----|
| Highest | `0x001` | ESTOP |
| Very High | `0x010`–`0x01F` | SAFETY_STATUS, DCDC_CMD |
| High | `0x100`–`0x11F` | MODE_CMD |
| Medium | `0x120`–`0x3FF` | THROTTLE, DRIVE, SES_STATUS/REQ, DRIVE_CMD, BRAKE_REQ, LIGHT_CMD |
| Low | `0x400`–`0x5FF` | OBSTACLE, STATE_REPORT, PID_FEEDBACK |
| Lowest | `0x600`–`0x7FF` | DIAG, SEB_REQ/STATUS, HEARTBEAT (`0x7FC`–`0x7FE`) |

> Lower CAN ID = higher bus arbitration priority. Safety-critical frames occupy `0x00X`. SYNTREE IDs (`0x2XX`, `0x7XX`) are in medium/lowest ranges per manufacturer assignment.

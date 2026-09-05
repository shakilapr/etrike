# CAN Network Documentation — high (Bus)
**Description:** Signal reference generated from canonical protocol contracts

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 19
- **Total Signal Definitions:** 70

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Message Dictionary
### 0x001 — SAFETY_ESTOP (Bus: high)
- **Sender:** Any
- **Receivers:** Host, RT
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x011 — SYS_SAFETY_STS (Bus: high)
- **Sender:** SYS
- **Receivers:** Host
- **DLC:** 5 bytes
- **Cycle:** 200 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `estop_active` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 1 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `light_left` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_right` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_brake` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_head` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |
| `rolling_counter` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `e2e_crc` | 4 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x111 — HMI_MODE_REQ (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** Mode requests are produced by Host (acting as the logical HMI role) and forwarded by RT to SYS, which remains the sole mode authority.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x112 — HMI_PWR_REQ (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_start` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: high)
- **Sender:** MTR
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x121 — RT_MOTION_RPT (Bus: high)
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |
| `yaw_rate_mrad_s` | 2 | 0 | 24 | signed | 1 | [-3000, 3000] | - |  |
| `gear` | 5 | 0 | 8 | unsigned | 1 | [0, 3] | - |  (Values: 0=N, 1=D, 2=S, 3=R) |
| `speed_valid` | 6 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `yaw_rate_valid` | 6 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `gear_valid` | 6 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `reserved` | 6 | 3 | 5 | unsigned | 1 | [0, 31] | - |  |
| `rolling_counter` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x206 — MTR_MOTOR_FBK (Bus: high)
- **Sender:** MTR
- **Receivers:** RT, SYS, Host
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `actual_speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |
| `gear_state` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `fault_flags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x210 — RT_STATE_RPT (Bus: high)
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 6 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  (Values: 0=MANUAL, 1=AUTO, 2=ESTOP) |
| `safety_state` | 1 | 0 | 2 | unsigned | 1 | [0, 2] | - |  |
| `estop_reason` | 1 | 4 | 4 | unsigned | 1 | [0, 7] | - |  |
| `reversing` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `rx_overflow` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `task_health` | 4 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `steer_state` | 5 | 0 | 8 | unsigned | 1 | [0, 5] | - |  |

### 0x220 — RT_PID_RPT (Bus: high)
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 6 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_setpoint` | 0 | 0 | 16 | signed | 1 | [-32768, 32767] | - |  |
| `speed_measured` | 2 | 0 | 16 | signed | 1 | [-32768, 32767] | - |  |
| `pid_output` | 4 | 0 | 16 | signed | 1 | [-32768, 32767] | - |  |

### 0x300 — HOST_DRIVE_CMD (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | - |  |
| `yaw_rate_mrad_s` | 4 | 0 | 24 | signed | 1 | [-3000, 3000] | - |  |
| `gear` | 7 | 0 | 8 | unsigned | 1 | [0, 3] | - |  (Values: 0=N, 1=D, 2=S, 3=R) |

### 0x301 — HOST_BRAKE_REQ (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 0 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `brake_pressure_kpa` | 0 | 0 | 32 | signed | 1 | [0, 20000] | - |  |

### 0x302 — HOST_LIGHT_CMD (Bus: high)
- **Sender:** Host
- **Receivers:** RT, SYS
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `left_turn` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `right_turn` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `brake_light` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `headlight` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

### 0x303 — HOST_STEER_CMD (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `steer_angle_0_1deg` | 0 | 0 | 16 | signed | 1 | [-450, 450] | - |  |
| `angle_valid` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `reserved` | 2 | 1 | 7 | unsigned | 1 | [0, 127] | - |  |
| `rolling_counter` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x310 — STEER_DIAG (Bus: high)
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `angle_0_1deg` | 0 | 0 | 16 | unsigned | x0.1 + -3000 | [-3000, 3553.5] | - |  |
| `fault` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `motor_current` | 3 | 0 | 16 | unsigned | x0.01 | [0, 655.35] | - |  |
| `ecu_temp` | 5 | 0 | 16 | unsigned | x0.1 | [0, 6553.5] | - |  |
| `reserved` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x311 — BRAKE_DIAG (Bus: high)
- **Sender:** RT
- **Receivers:** Host
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `pressure_raw` | 0 | 0 | 16 | unsigned | x0.05 | [0, 3276.75] | - |  |
| `fault` | 2 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `motor_current` | 3 | 0 | 16 | unsigned | x0.01 | [0, 655.35] | - |  |
| `ecu_temp` | 5 | 0 | 16 | unsigned | x0.1 | [0, 6553.5] | - |  |
| `reserved` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x400 — HOST_OBSTACLE_DIST (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `distance_mm` | 0 | 0 | 32 | unsigned | 1 | [0, 4.29497e+09] | - |  (Values: 4294967295=clear) |

### 0x600 — SYS_DIAG_RPT (Bus: high)
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  (Values: 0=MANUAL, 1=AUTO, 2=ESTOP) |
| `brake_engaged` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `brake_fault` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `rx_overflow` | 2 | 1 | 6 | unsigned | 1 | [0, 63] | - |  |
| `estop_active` | 3 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `free_heap_kb` | 4 | 0 | 16 | unsigned | 1 | [0, 65535] | - |  |
| `tec` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `rec` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x7FC — HOST_HEARTBEAT (Bus: high)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `health_flags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x7FD — RT_HEARTBEAT (Bus: high)
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `health_flags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

---

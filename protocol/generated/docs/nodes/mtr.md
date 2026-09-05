# CAN Network Documentation — MTR (Node)
**Description:** Signal reference generated from canonical protocol contracts

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 7
- **Total Signal Definitions:** 22

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Message Dictionary
### 0x001 — SAFETY_ESTOP (Bus: low)
- **Sender:** Any
- **Receivers:** SYS, MTR, DCDC, RT
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x011 — SYS_SAFETY_STS (Bus: low)
- **Sender:** SYS
- **Receivers:** RT, MTR
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

### 0x110 — SYS_MODE_CMD (Bus: low)
- **Sender:** SYS
- **Receivers:** RT, MTR
- **DLC:** 2 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x113 — SYS_PWR_CMD (Bus: low)
- **Sender:** SYS
- **Receivers:** MTR
- **DLC:** 2 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `power_state` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: low)
- **Sender:** MTR
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: high)
- **Sender:** MTR
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x204 — RT_DRIVE_CMD (Bus: low)
- **Sender:** RT
- **Receivers:** SYS, MTR
- **DLC:** 5 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `motor_speed_mmps` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | - |  |
| `gear` | 4 | 0 | 8 | unsigned | 1 | [0, 3] | - |  (Values: 0=N, 1=D, 2=S, 3=R) |

### 0x206 — MTR_MOTOR_FBK (Bus: low)
- **Sender:** MTR
- **Receivers:** RT, SYS, Host
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `actual_speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |
| `gear_state` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `fault_flags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

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

---

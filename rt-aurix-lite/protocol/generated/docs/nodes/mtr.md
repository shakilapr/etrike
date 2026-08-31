# CAN Network Documentation — MTR (Node)
**Description:** Signal reference generated from the RT-AURIX-Lite protocol subset

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 6
- **Total Signal Definitions:** 13

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
- **Receivers:** RT, Host, MTR
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x001 — SAFETY_ESTOP (Bus: low)
- **Sender:** Any
- **Receivers:** RT, Host, MTR
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x110 — RTA_MODE_CMD (Bus: low)
- **Sender:** RT
- **Receivers:** MTR
- **DLC:** 1 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: low)
- **Sender:** MTR
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: high)
- **Sender:** MTR
- **Receivers:** Host
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x204 — RTA_DRIVE_CMD (Bus: low)
- **Sender:** RT
- **Receivers:** MTR
- **DLC:** 5 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `motor_speed_mmps` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | - |  |
| `gear` | 4 | 0 | 8 | unsigned | 1 | [0, 3] | - |  (Values: 0=N, 1=D, 2=S, 3=R) |

### 0x206 — MTR_MOTOR_FBK (Bus: low)
- **Sender:** MTR
- **Receivers:** RT
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `actual_speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |
| `gear_state` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `fault_flags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x206 — MTR_MOTOR_FBK (Bus: high)
- **Sender:** MTR
- **Receivers:** Host
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `actual_speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |
| `gear_state` | 2 | 0 | 8 | unsigned | 1 | [0, 3] | - |  |
| `fault_flags` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x7FD — RTA_HEARTBEAT (Bus: low)
- **Sender:** RT
- **Receivers:** MTR
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `health_flags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

---

# CAN Network Documentation — SYS (Node)
**Description:** Signal reference generated from canonical protocol contracts

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 18
- **Total Signal Definitions:** 83

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
- **Receivers:** SYS, Host, MTR, DCDC
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x001 — SAFETY_ESTOP (Bus: low)
- **Sender:** Any
- **Receivers:** SYS, Host, MTR, DCDC
- **DLC:** 0 bytes
- **Cycle:** 0 ms (0 = event-based)

*No payload (DLC=0 event frame)*

### 0x011 — SYS_SAFETY_STS (Bus: low)
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 3 bytes
- **Cycle:** 200 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `estop_active` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 1 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `light_left` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_right` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_brake` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_head` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

### 0x011 — SYS_SAFETY_STS (Bus: high)
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 3 bytes
- **Cycle:** 200 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `estop_active` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 1 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `light_left` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_right` | 2 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_brake` | 2 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `light_head` | 2 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

### 0x110 — SYS_MODE_CMD (Bus: low)
- **Sender:** SYS
- **Receivers:** RT, MTR
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |

### 0x111 — HMI_MODE_REQ (Bus: high)
- **Sender:** HMI
- **Receivers:** SYS, Host
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x111 — HMI_MODE_REQ (Bus: low)
- **Sender:** HMI
- **Receivers:** SYS, Host
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x112 — HMI_PWR_REQ (Bus: high)
- **Sender:** HMI
- **Receivers:** SYS
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_start` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x112 — HMI_PWR_REQ (Bus: low)
- **Sender:** HMI
- **Receivers:** SYS
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_start` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x204 — RT_DRIVE_CMD (Bus: low)
- **Sender:** RT
- **Receivers:** SYS, MTR
- **DLC:** 5 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `motor_speed_mmps` | 0 | 0 | 32 | signed | 1 | [-500, 3000] | - |  |
| `gear` | 4 | 0 | 8 | unsigned | 1 | [0, 3] | - |  (Values: 0=N, 1=D, 2=S, 3=R) |

### 0x205 — RT_BRAKE_CMD (Bus: low)
- **Sender:** RT
- **Receivers:** SYS
- **DLC:** 4 bytes
- **Cycle:** 20 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `brake_pressure_kpa` | 0 | 0 | 32 | signed | 1 | [0, 20000] | - |  |

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

### 0x210 — RT_STATE_RPT (Bus: low)
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

### 0x302 — HOST_LIGHT_CMD (Bus: low)
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

### 0x600 — SYS_DIAG_RPT (Bus: low)
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |
| `brake_engaged` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `brake_fault` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `rx_overflow` | 2 | 1 | 6 | unsigned | 1 | [0, 63] | - |  |
| `estop_active` | 3 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `free_heap_kb` | 4 | 0 | 16 | unsigned | 1 | [0, 65535] | - |  |
| `tec` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `rec` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x600 — SYS_DIAG_RPT (Bus: high)
- **Sender:** SYS
- **Receivers:** RT, Host
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `mode` | 0 | 0 | 8 | unsigned | 1 | [0, 2] | - |  |
| `brake_engaged` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `brake_fault` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `heartbeat_ok` | 2 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `rx_overflow` | 2 | 1 | 6 | unsigned | 1 | [0, 63] | - |  |
| `estop_active` | 3 | 0 | 8 | unsigned | 1 | [0, 1] | - |  |
| `free_heap_kb` | 4 | 0 | 16 | unsigned | 1 | [0, 65535] | - |  |
| `tec` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `rec` | 7 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x6FB — SEB_TEST (Bus: low)
- **Sender:** SEB
- **Receivers:** SYS, RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x721 — SEB_STATUS (Bus: low)
- **Sender:** SEB
- **Receivers:** SYS, RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x731 — SEB_ERR_INFO (Bus: low)
- **Sender:** SEB
- **Receivers:** SYS
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x741 — SEB_VERSION (Bus: low)
- **Sender:** SEB
- **Receivers:** SYS
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x7B9 — VCU_SEB_REQ (Bus: low)
- **Sender:** SYS
- **Receivers:** SEB
- **DLC:** 8 bytes
- **Cycle:** 20 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x7FD — RT_HEARTBEAT (Bus: high)
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `health_flags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x7FD — RT_HEARTBEAT (Bus: low)
- **Sender:** RT
- **Receivers:** Host, SYS
- **DLC:** 2 bytes
- **Cycle:** 500 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `health_flags` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x7FE — SYS_HEARTBEAT (Bus: low)
- **Sender:** SYS
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `alive_ctr` | 0 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `heartbeat_ok` | 1 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `estop_active` | 1 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `mode_auto` | 1 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `can_ok` | 1 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |
| `task_safety_ok` | 1 | 4 | 1 | unsigned | 1 | [0, 1] | - |  |
| `task_brake_ok` | 1 | 5 | 1 | unsigned | 1 | [0, 1] | - |  |
| `task_dispatch_ok` | 1 | 6 | 1 | unsigned | 1 | [0, 1] | - |  |
| `task_can_tx_ok` | 1 | 7 | 1 | unsigned | 1 | [0, 1] | - |  |

---

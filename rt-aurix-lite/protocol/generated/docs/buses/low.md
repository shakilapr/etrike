# CAN Network Documentation — low (Bus)
**Description:** Signal reference generated from the RT-AURIX-Lite protocol subset

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 19
- **Total Signal Definitions:** 17

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

### 0x111 — HMI_MODE_REQ (Bus: low)
- **Sender:** HMI
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** Mode requests may be produced directly by HMI or by Host/Jetson; RT remains the sole mode authority.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x112 — HMI_PWR_REQ (Bus: low)
- **Sender:** HMI
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_start` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

### 0x120 — SYS_THROTTLE_STS (Bus: low)
- **Sender:** MTR
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 10 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `speed_mmps` | 0 | 0 | 16 | signed | 1 | [-500, 3000] | - |  |

### 0x169 — VCU_SES_REQ (Bus: low)
- **Sender:** RT
- **Receivers:** EPS_C
- **DLC:** 8 bytes
- **Cycle:** 20 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x201 — SES_STATUS (Bus: low)
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x202 — SES_ERR_INFO (Bus: low)
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x203 — SES_VERSION (Bus: low)
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

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

### 0x302 — HOST_LIGHT_CMD (Bus: low)
- **Sender:** Host
- **Receivers:** RT
- **DLC:** 1 bytes
- **Cycle:** 0 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `left_turn` | 0 | 0 | 1 | unsigned | 1 | [0, 1] | - |  |
| `right_turn` | 0 | 1 | 1 | unsigned | 1 | [0, 1] | - |  |
| `brake_light` | 0 | 2 | 1 | unsigned | 1 | [0, 1] | - |  |
| `headlight` | 0 | 3 | 1 | unsigned | 1 | [0, 1] | - |  |

### 0x6FA — SES_TEST (Bus: low)
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x6FB — SEB_TEST (Bus: low)
- **Sender:** SEB
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x721 — SEB_STATUS (Bus: low)
- **Sender:** SEB
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x731 — SEB_ERR_INFO (Bus: low)
- **Sender:** SEB
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x741 — SEB_VERSION (Bus: low)
- **Sender:** SEB
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 1000 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

### 0x7B9 — VCU_SEB_REQ (Bus: low)
- **Sender:** RT
- **Receivers:** SEB
- **DLC:** 8 bytes
- **Cycle:** 20 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

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

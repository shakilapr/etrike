# CAN Network Documentation — HMI (Node)
**Description:** Signal reference generated from the RT-AURIX-Lite protocol subset

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 2
- **Total Signal Definitions:** 8

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Message Dictionary
### 0x111 — HMI_MODE_REQ (Bus: high)
- **Sender:** HMI
- **Receivers:** RT, Host
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)
- **Description:** Mode requests may be produced directly by HMI or by Host/Jetson; RT remains the sole mode authority.

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_mode` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=MANUAL, 1=AUTO) |
| `rolling_counter` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |

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

### 0x112 — HMI_PWR_REQ (Bus: high)
- **Sender:** HMI
- **Receivers:** RT
- **DLC:** 2 bytes
- **Cycle:** 1000 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `req_start` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=OFF, 1=ON) |
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

---

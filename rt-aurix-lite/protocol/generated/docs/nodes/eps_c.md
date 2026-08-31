# CAN Network Documentation — EPS_C (Node)
**Description:** Signal reference generated from the RT-AURIX-Lite protocol subset

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 5
- **Total Signal Definitions:** 0

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Message Dictionary
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

### 0x6FA — SES_TEST (Bus: low)
- **Sender:** EPS_C
- **Receivers:** RT
- **DLC:** 8 bytes
- **Cycle:** 10 ms (0 = event-based)

*Opaque payload or unsupported layout kind: opaque*

---

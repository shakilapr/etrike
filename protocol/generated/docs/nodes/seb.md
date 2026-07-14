# CAN Network Documentation — SEB (Node)
**Description:** Signal reference generated from canonical protocol contracts

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

---

# CAN Network Documentation — PWT (Node)
**Description:** Signal reference generated from canonical protocol contracts

*(Note: This file is fully auto-generated from the YAML configurations. Do not edit manually.)*

## Summary Statistics
- **Unique CAN Message IDs:** 1
- **Total Signal Definitions:** 8

---

## Type Notation
| Notation | Meaning |
|---|---|
| `signed` / `unsigned` | Signed / Unsigned integer |
| `enum` | Enumeration (value map provided) |
| `DLC=0` | Zero-length CAN frame (event signal, no payload) |

## Message Dictionary
### 0x10262B27 — PWT_DCDC_CMD (Bus: powertrain)
- **Sender:** PWT
- **Receivers:** DCDC
- **DLC:** 8 bytes
- **Cycle:** 100 ms (0 = event-based)

| Signal Name | Byte | Bit | Size | Type | Scale | Range | Unit | Description |
|---|---|---|---|---|---|---|---|---|
| `control` | 0 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=disabled, 1=enabled) |
| `reserved_1` | 1 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reserved_2` | 2 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reserved_3` | 3 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reserved_4` | 4 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reserved_5` | 5 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reserved_6` | 6 | 0 | 8 | unsigned | 1 | [0, 255] | - |  |
| `reset_control` | 7 | 0 | 8 | unsigned | 1 | [0, 1] | - |  (Values: 0=no_reset, 1=reset) |

---

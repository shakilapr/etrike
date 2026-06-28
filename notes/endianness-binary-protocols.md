# Endianness & Binary Protocols

When a CAN frame carries a 16-bit or 32-bit value (speed, angle, pressure), the sender and receiver must agree on *byte order*. Getting it wrong silently swaps bytes — your 3000 mm/s becomes 768 mm/s, or worse, a negative number.

The E-Trike uses **big-endian (MSB first)** for multi-byte CAN fields — except steer-by-wire actuators which use **little-endian (LSB first)** per their factory protocol. The `can-dictionary.md` marks endianness on every signal.

---

## 1. Big-Endian vs Little-Endian

**Big-endian (MSB first):** The most significant byte comes first in memory.

```
Value: 0x12345678 (32-bit)
Memory:  [12] [34] [56] [78]
          ↑                 ↑
        low addr         high addr
```

**Little-endian (LSB first):** The least significant byte comes first.

```
Value: 0x12345678 (32-bit)
Memory:  [78] [56] [34] [12]
          ↑                 ↑
        low addr         high addr
```

**How to remember:** Big-endian is "human readable" — the bytes appear in the order you'd write the number. Little-endian is "computer efficient" — the low byte is at the low address, making addition and carry propagation faster in hardware.

### Practical example

```cpp
uint32_t speed = 0x00000BB8;  // 3000 in decimal, 0x0BB8 in hex

// Big-endian on CAN bus: [00] [00] [0B] [B8]
// Little-endian on CAN bus: [B8] [0B] [00] [00]
```

If the receiver expects big-endian but gets little-endian, it reads `0xB80B0000` = 3,087,433,728. Completely wrong, no obvious error.

---

## 2. Bit Numbering — Motorola vs Intel Format

CAN signal descriptions specify a **start bit** and **length**. But "start bit" means different things to different conventions:

| Convention | Start bit meaning | Used by |
|-----------|-------------------|---------|
| **Motorola (big-endian)** | MSB of the signal | Classical CAN DBs, AUTOSAR |
| **Intel (little-endian)** | LSB of the signal | Many modern tools |

For example, a 16-bit signal at start bit 0, length 16:

```
Motorola: bit 0 = MSB of signal → signal bits are [0..15] = [MSB..LSB]
Intel:    bit 0 = LSB of signal → signal bits are [0..15] = [LSB..MSB]
```

**The E-Trike convention:** All signals use big-endian (Motorola) unless marked otherwise. steer-by-wire protocols use "Motorola LSB" — Motorola bit numbering but little-endian byte order. See the signal tables in `can-dictionary.md`.

---

## 3. Why CAN Uses Big-Endian

CAN itself is big-endian at the bit level: the most significant bit of the CAN ID is transmitted first during arbitration. This is why lower IDs (more leading zeros in the MSB) win arbitration — the first dominant bit (0) from a competing node overrides a recessive bit (1) from another, and the lower-ID frame continues.

Most automotive standards (J1939, CANopen) follow this convention and define multi-byte signals as big-endian. The E-Trike does the same for consistency — except where steer-by-wire factory protocols dictate little-endian.

---

## 4. C/C++ Struct Packing

Embedded firmware often maps a C struct directly onto CAN payload bytes:

```cpp
// Big-endian: RT_DRIVE_CMD (0x204), DLC=5
struct __attribute__((packed)) RtDriveCmd {
    uint8_t  speed_byte3;  // MSB of speed
    uint8_t  speed_byte2;
    uint8_t  speed_byte1;
    uint8_t  speed_byte0;  // LSB of speed
    uint8_t  gear;
};

// Convert struct to uint32_t speed:
int32_t speed = (cmd.speed_byte3 << 24) |
                (cmd.speed_byte2 << 16) |
                (cmd.speed_byte1 << 8)  |
                cmd.speed_byte0;
```

**Three rules for safe struct mapping:**

1. **Always `__attribute__((packed))`** — prevents the compiler from inserting padding bytes that shift everything.
2. **Always verify with `static_assert(sizeof(Struct) == DLC)`** — catches compiler quirks.
3. **Never use bitfields for CAN protocols** — the C standard doesn't specify bitfield ordering, so the same code on different compilers (GCC vs Clang vs IAR) produces different byte layouts.

Bad (non-portable):
```cpp
struct Bad {
    uint8_t mode : 2;     // ← ordering is compiler-defined!
    uint8_t flags : 6;
};
```

Good (portable):
```cpp
struct Good {
    uint8_t byte0;        // ← explicit bit operations extract fields
};
uint8_t mode = byte0 & 0x03;
uint8_t flags = (byte0 >> 2) & 0x3F;
```

---

## 5. Endianness Conversion

When your CPU's native endianness differs from the protocol:

```cpp
// Host-to-network (big-endian) — for standard CAN signals
uint32_t htonl(uint32_t host) {
    return ((host & 0xFF) << 24) |
           ((host & 0xFF00) << 8) |
           ((host & 0xFF0000) >> 8) |
           ((host & 0xFF000000) >> 24);
}

// For steer-by-wire (little-endian), just memcpy if the CPU is little-endian (ESP32 is).
// If your CPU is big-endian, reverse.
```

The ESP32 (Xtensa LX7) is **little-endian**. So:
- For E-Trike big-endian CAN signals: reverse the bytes before sending.
- For steer-by-wire little-endian signals: use native byte order — no conversion needed.

---

## 6. Common Gotchas

| Gotcha | What happens | How to catch |
|--------|-------------|-------------|
| **Forgot `packed` attribute** | Compiler inserts padding, shifts fields by 1–3 bytes | `static_assert(sizeof(MyStruct) == expected_dlc, "wrong size")` |
| **Bitfield across compilers** | GCC and Clang order bitfields differently | Use explicit bit masks, never bitfields for protocols |
| **Mixed endianness on one bus** | Some signals correct, others silently wrong | Every signal in can-dictionary.md has an endianness column |
| **Sign extension on narrow types** | Casting `uint8_t` to `int16_t` sign-extends bit 7 | Use explicit masks: `value = (int16_t)((raw_high << 8) | raw_low)` — but watch for the `int16_t` cast after OR, not before |
| **Endianness of the CAN ID itself** | Mismatch between `0x200` in code and on the bus | The CAN controller handles ID endianness. Just use the numeric value. |

---

*See also: [[can-protocol]] for CAN frame structure, `can-dictionary.md` for bit-level layouts of every signal, `docs/steering-unit.md` for steer-by-wire little-endian layout.*

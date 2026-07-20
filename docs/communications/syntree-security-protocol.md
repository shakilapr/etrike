# steer-by-wire CAN Security Protocol — Rolling Counter + Checksum

Actuator units require two security bytes in every command frame: a **rolling counter** and an **XOR checksum**. If either is wrong, the actuator silently discards the frame — no error response, no fault flag, just ignored.

This is a **liveness check**, not encryption. It proves the controller hasn't crashed and is still computing fresh frames (not stuck in a loop replaying the same buffer).

---

## Why steer-by-wire requires this

CAN is a broadcast bus. Without security bytes, a crashed controller could:

1. **Replay the last valid frame forever.** The CAN peripheral's TX mailbox auto-retransmits on error. If the CPU hangs but the peripheral keeps running, the actuator sees a valid-looking 50 Hz stream with plausible angle values — and never detects the fault.
2. **Send stale data after a watchdog reset.** On reboot, the firmware might reinitialize and immediately resume transmitting the last command from before the crash, before it has reacquired sensor data or mode state.

Rolling counter + checksum breaks both scenarios: a crashed CPU stops incrementing the counter (actuator detects liveness loss), and a reboot resets the counter (actuator detects discontinuity).

---

## Frame layout (both EPS-C and SEB)

Both actuators use the same 8-byte security scheme in bytes 5–7:

| Byte | Field | Description |
|------|-------|-------------|
| 5 | Security enables | Bit 0 = rolling counter enable, Bit 1 = checksum enable. Always `0x03` (both enabled). |
| 6 | Rolling counter (low nibble) + reserved (high nibble) | 4-bit counter, increments 0→15→0 every frame. High nibble = 0. |
| 7 | Checksum | `XOR(bytes 0..6) ^ 0xFF` |

### Byte 5 — security enables

```
Bit 0: rolling_counter_enable   (1 = enabled)
Bit 1: checksum_enable          (1 = enabled)
Bits 2-7: reserved (0)

Value: 0x03
```

Both must be set to 1. If either is 0, the frame is rejected regardless of content.

### Byte 6 — rolling counter

```
Bits 0-3: rolling_counter (0–15)
Bits 4-7: reserved (0)

After each transmission: counter = (counter + 1) & 0x0F
```

The actuator tracks the last received counter. It expects `counter = (previous + 1) & 0x0F`. If the counter doesn't increment (stuck) or jumps backward (reboot), the actuator may fault — exact behavior is unit-specific.

### Byte 7 — checksum

```
uint8_t checksum = 0;
for (int i = 0; i < 7; i++) {
    checksum ^= frame_bytes[i];
}
checksum ^= 0xFF;
```

Or equivalently: XOR all 7 bytes together, then invert all bits.

---

## C++ implementation

```cpp
struct steer-by-wireSecurityFrame {
    uint8_t bytes[8];

    void set_security_enables() {
        bytes[5] = 0x03;  // both rolling counter and checksum enabled
    }

    void set_rolling_counter(uint8_t counter) {
        bytes[6] = (counter & 0x0F);  // low nibble only
    }

    void compute_checksum() {
        uint8_t cks = 0;
        for (int i = 0; i < 7; i++) {
            cks ^= bytes[i];
        }
        bytes[7] = cks ^ 0xFF;
    }

    void finalize(uint8_t counter) {
        set_security_enables();
        set_rolling_counter(counter);
        compute_checksum();
    }
};

// Usage in 50 Hz transmit loop
static uint8_t rolling_counter = 0;
steer-by-wireSecurityFrame cmd;

// EPS-C steering command
cmd.bytes[0] = 0x01;           // control mode = Angle
cmd.bytes[1] = angle_raw & 0xFF;      // angle low byte
cmd.bytes[2] = (angle_raw >> 8) & 0xFF; // angle high byte
cmd.bytes[3] = angle_speed;     // slew rate
cmd.bytes[4] = 0x00;            // reserved
cmd.finalize(rolling_counter);
rolling_counter = (rolling_counter + 1) & 0x0F;

twai_transmit(&cmd, portMAX_DELAY);
```

---

## Verification by the actuator

When EPS-C or SEB receives a frame:

1. Check byte 5 bits 0 and 1 — both must be `1`. If not → discard silently.
2. Compute `XOR(bytes 0..6) ^ 0xFF` — must match byte 7. If not → discard silently.
3. Check byte 6 low nibble — must be `(previous + 1) & 0x0F`. If stuck or wrong → may fault (unit-specific).
4. If all checks pass → accept the command and actuate.

---

## Failure modes

| Failure | Cause | Actuator behavior |
|---------|-------|-------------------|
| Checksum mismatch | Corrupted frame (bus noise, bad termination) or software bug (wrong XOR) | Frame silently discarded. Rolling counter NOT incremented. |
| Counter stuck | Controller CPU hung, CAN peripheral replaying TX mailbox | Actuator detects no counter change. May trigger comm-fault after N repeated frames (unit-specific timeout, typically 5–10 frames = 100–200 ms). |
| Counter jump | Controller rebooted — counter reset to 0 while actuator expected next value | Actuator may fault or re-sync depending on unit firmware. Boot LBS ensures sync. |
| Security enables = 0 | Software bug (forgot to set byte 5) or endianness error | All frames silently discarded. Actuator appears unresponsive. |

---

## Debugging silent discards

If the actuator seems unresponsive despite valid-looking CAN traffic:

1. Verify byte 5 = `0x03` on every frame.
2. Verify checksum with a CAN analyzer that can compute XOR across bytes.
3. Verify the rolling counter increments monotonically (0, 1, 2, ..., 15, 0, 1, ...).
4. Check that byte ordering matches steer-by-wire's little-endian expectation.

The most common bug: forgetting to call `finalize()` or calling it before setting all payload bytes (so bytes 0–4 are zero when checksum is computed, then payload is written afterward, invalidating the checksum).

---

## Why not CRC?

steer-by-wire chose XOR + inversion over a proper CRC:

- XOR is computationally trivial (no lookup table, no polynomial division).
- Combined with the rolling counter, it covers both integrity (corruption detection) and liveness (stuck-controller detection).
- A standalone CRC only proves the frame wasn't corrupted in transit, not that the controller is still alive.

The downside: XOR catches single-bit errors but is weaker against multi-bit errors than CRC-8 or CRC-16. steer-by-wire's design trades stronger error detection for simplicity and the addition of liveness checking via the counter.

---

*Primary reference: [[emergency-system]] for how rolling counter failure modes fit into the 8-layer safety system and trigger ESTOP.*

*See also: [[listen-before-speaking]] for the boot sequence that must complete before security bytes matter, [[architecture]] §7.6 for EPS-C protocol, §8.6 for SEB protocol, [[can-dictionary]] for bit-level frame layouts.*

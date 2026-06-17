# Analog Interfacing Basics

Most embedded systems bridge digital logic (MCU) and analog reality (voltages, currents, sensors, actuators). The E-Trike does this in several places: reading a 0–5 V throttle grip via ADC, generating a 0–5 V motor speed signal via DAC, and sensing 72 V gear lines through optoisolators.

This note covers the fundamental concepts behind each interface.

---

## 1. ADC — Analog-to-Digital Conversion

An **ADC** converts a continuous voltage into a discrete digital number. The ESP32-S3 has a built-in 12-bit SAR ADC with multiple channels.

### Resolution

A 12-bit ADC divides the input range into 2¹² = 4096 steps. Each step represents:

```
step_size = V_ref / 2^bits = 3.3V / 4096 ≈ 0.8 mV
```

| Bits | Steps | Step size at 3.3V | Step size at 5.0V |
|------|-------|-------------------|-------------------|
| 8 | 256 | 12.9 mV | 19.5 mV |
| 10 | 1024 | 3.2 mV | 4.9 mV |
| **12** | **4096** | **0.8 mV** | **1.2 mV** |
| 16 | 65536 | 0.05 mV | 0.08 mV |

For a throttle grip that moves from 0–5 V, 12 bits gives ~1.2 mV resolution — far more than a human hand can position (and more than the grip potentiometer's own noise floor).

### Voltage dividers

The ESP32 ADC is 3.3 V max. The throttle signal is 0–5 V. A **voltage divider** scales it down:

```
5V input ──┬── R1 (e.g., 10kΩ) ──┬── ADC pin (0–3.3V)
           │                      │
           │                      ├── R2 (e.g., 20kΩ)
           │                      │
           └──────────────────────┴── GND

V_out = V_in × R2 / (R1 + R2) = 5V × 20k / (10k + 20k) = 3.33V
```

Choose R1, R2 so that max input voltage → just under V_ref. Keep total resistance (R1 + R2) above 10 kΩ to limit current draw, but below 100 kΩ to keep the ADC's sampling capacitor charging fast.

### Sampling and noise

SAR ADCs work by charging an internal sampling capacitor, then comparing it against a reference. If the source impedance is too high, the capacitor doesn't fully charge before the comparison starts → reading is low.

- Add a small capacitor (100 nF) from ADC pin to GND — acts as a charge reservoir.
- Average multiple readings in software: `value = (value * 3 + new_reading) / 4` (exponential moving average).
- A reading of 0 or 4095 that persists is likely a fault (shorted to GND or VCC).

---

## 2. DAC — Digital-to-Analog Conversion

A **DAC** does the reverse: converts a digital number into a continuous voltage. The E-Trike uses the MCP4725, a 12-bit I2C DAC, to generate the 0–5 V signal that controls motor speed.

### Why a dedicated DAC instead of PWM?

| Approach | How it works | Pros | Cons |
|----------|-------------|------|------|
| **PWM + RC filter** | MCU outputs a square wave; low-pass filter smooths it to an average voltage | Free (built-in peripheral) | Ripple, slow settling, sensitive to component tolerance |
| **I2C/SPI DAC** | Dedicated IC outputs true analog voltage | Clean output, <10 µs settling, no ripple | Additional BOM cost (~$1) |

For a safety-critical throttle signal, the DAC's clean output is worth the cost. PWM ripple on a throttle line can cause the motor controller to oscillate between drive and coast, producing surging.

### MCP4725 basics

```
ESP32 I2C (SDA=15, SCL=16) ──► MCP4725 (addr 0x60) ──► 0–5V analog ──► Motor controller
                                   │
                                  VCC = 5V (not 3.3V — the DAC's output range equals its supply)
```

12-bit value → output voltage:
```
V_out = (digital_value / 4095) × VCC
```

So `digital_value = 2047` → ~2.5 V at VCC=5V.

**I2C write sequence:**
```
START | ADDR+W | ACK | DATA_H | ACK | DATA_L | ACK | STOP
```

The MCP4725 expects two data bytes: the upper 8 bits of the 12-bit value in the first byte, the lower 4 bits in the upper nibble of the second byte.

```cpp
void dac_write(uint16_t value_12bit) {
    uint8_t data[2];
    data[0] = (value_12bit >> 4) & 0xFF;    // upper 8 bits
    data[1] = (value_12bit & 0x0F) << 4;     // lower 4 bits → upper nibble
    i2c_write(I2C_ADDR, data, 2);
}
```

---

## 3. Optoisolators — Galvanic Isolation for Digital Signals

An **optoisolator** (optocoupler) transfers a signal using light, providing electrical isolation between two voltage domains. No current flows between input and output — the signal crosses an optical gap.

The E-Trike uses TLP281 optoisolators to sense 72 V gear selector lines from a 3.3 V ESP32 GPIO.

### How it works

```
72V domain                             3.3V domain
──────────                             ──────────

72V gear wire ──► R_limit ──► LED ──► GND_72V
                                │
                           light │  (optical barrier, 2.5 kV isolation)
                                │
                                ▼
                          Phototransistor
                                │
ESP32 GPIO ◄── R_pullup ────────┤
                                │
                              GND_3V3
```

- **72 V present** → LED emits light → phototransistor conducts → GPIO reads LOW.
- **72 V absent** → LED off → phototransistor open → pull-up resistor pulls GPIO HIGH.

Note the **logic inversion**: HIGH on GPIO = "no gear signal." This is normal for optoisolator circuits — handle the inversion in software.

### Key optoisolator parameters

| Parameter | Meaning | TLP281 typical |
|-----------|---------|----------------|
| Isolation voltage | Max voltage between input and output | 2500 Vrms |
| CTR (Current Transfer Ratio) | Output current / input current, as % | 100–600% @ 5 mA |
| Input forward voltage | Voltage drop across LED | ~1.15 V @ 5 mA |
| Rise/fall time | Speed of signal transition | 2–5 µs (fast enough for 50 Hz gear sensing) |

### Sizing the input resistor

The resistor on the 72 V side limits LED current:
```
R = (V_in - V_f) / I_f = (72V - 1.15V) / 0.005A ≈ 14.2 kΩ
```

Use a resistor with adequate power rating: P = I²R = (0.005)² × 14200 = 0.36 W → use a 0.5 W or 1 W resistor.

---

## 4. Relays — Mechanical Isolation for Power Signals

When you need to switch a high-voltage signal ON/OFF from a low-voltage GPIO, a **mechanical relay** provides both isolation and power handling.

```
ESP32 GPIO ──► R_base ──► NPN transistor base
                            │
                            ├── Collector ──► Relay coil ──► 12V
                            │
                            └── Emitter ──► GND

Relay contacts (electrically isolated from coil):
  COM ──► NO (normally open) ──► Load (e.g., 72V gear input on motor controller)
```

**Why relays for gear outputs?**

| Property | Relay | MOSFET |
|----------|-------|--------|
| Galvanic isolation | ✓ (coil and contacts are separate) | ✗ (single silicon die) |
| Fail-safe on power loss | ✓ (contacts open) | ✗ (body diode may conduct) |
| 72 VDC switching | Simple | Requires gate drive with bootstrap |
| Switching speed | ~5–10 ms | <1 µs |
| Audible feedback | Click confirms operation | Silent |

For gear selection (switching once every few seconds at most), relay speed is irrelevant — and the audible click is a useful diagnostic.

---

## 5. Protection Circuits

Any line that connects to a high-voltage domain needs protection.

### Fuse (overcurrent)

A fast-blow fuse in series with every 72 V line. If the line shorts to chassis, the fuse blows before the wire melts or the battery is damaged. Size the fuse slightly above the maximum normal load. For gear signaling (microamps into high-impedance input), 1 A is conservative — any current above 1 A is definitely a short.

### TVS diode (overvoltage transient)

A **Transient Voltage Suppression (TVS)** diode clamps voltage spikes. Place between the signal line and GND:

```
72V line ──┬── Fuse ──┬── Load
           │           │
           │           ├── TVS diode ── GND
           │           │
           └───────────┘
```

Choose a TVS with:
- **Standoff voltage > normal operating voltage** (diode doesn't conduct normally).
- **Clamping voltage < destruction voltage of protected components**.
- **Bidirectional** for lines that can see both positive and negative transients.

The E-Trike uses SMCJ90CA: 90 V standoff (above 72 V nominal), 146 V clamping, 1500 W peak. The 'C' suffix = bidirectional; 'A' suffix = 5% tolerance.

---

## 6. Common Pitfalls

| Pitfall | What happens | Fix |
|---------|-------------|-----|
| **Missing voltage divider** | 5 V signal fed directly to 3.3 V ADC pin → pin damage | Always check max input voltage against V_ref |
| **High source impedance** | ADC reading droops — sampling capacitor doesn't charge | Add parallel capacitor (100 nF) at ADC pin, or use a buffer op-amp |
| **DAC powered by MCU's 3.3V** | Output max is 3.3 V, but motor controller expects 0–5 V | DAC VCC = 5 V rail, not 3.3 V |
| **No TVS on 72 V lines** | Load dump or inductive kick destroys transceiver/opto | TVS + fuse on every high-voltage line |
| **Forgetting logic inversion with optos** | GPIO HIGH when signal is present — opposite of expected | Account for inversion in software; document in pin table |
| **Relay coil without flyback diode** | Inductive kick on turn-off destroys transistor | Diode across relay coil (cathode to VCC) |

---

*See also: [[high-voltage-isolation]] for the E-Trike's specific 72V isolation design, [[external-watchdog]] for hardware watchdog, `architecture.md` §8.6 for throttle/gear control, §8.8 for pin assignments.*

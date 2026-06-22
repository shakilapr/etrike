# High-Voltage Isolation & Protection

The E-Trike has a **72 V traction battery** and a **12 V accessory rail**. The ESP32-S3 MCUs run at **3.3 V**. These voltage domains must never meet — a single fault connecting 72 V to an ESP32 GPIO destroys the MCU and potentially everything on the CAN bus.

We use three strategies: **galvanic isolation** for input sensing, **mechanical relay isolation** for output switching, and **protection circuits** on every line that carries high voltage.

---

## Voltage domains

```
72 V Traction Battery
  ├── Motor controller (72 V direct)
  ├── Gear selector lines (72 V signaling)
  ├── DC-DC converter (72V → 12V)
  │
  └─12 V Accessory Rail
      ├── Signal lights (12 V)
      ├── Headlight (12 V)
      ├── Mode indicator LEDs (12 V)
      ├── Brake light (12 V)
      └── ESP32-S3 (via 3.3 V LDO regulator)
          └── CAN bus (3.3 V logic, 5 V transceiver)
```

---

## Galvanic isolation — TLP281 optoisolators (input)

**Problem:** The gear selector uses 72 V lines. When a gear position is selected, 72 V appears on the corresponding wire. The ESP32 GPIOs are 3.3 V tolerant only.

**Solution:** TLP281 optoisolators provide galvanic (optical) isolation between the 72 V domain and the 3.3 V domain.

```
72 V gear wire ──┬── R1 (current limit) ──┬── TLP281 LED (anode)
                 │                         │
                 └── R2 (voltage divider) ─┴── TLP281 LED (cathode) ── GND_72V

                                                  │  Optical barrier
                                                  │  (4 kV isolation)

ESP32 GPIO ◀── R3 (pull-up to 3.3V) ──┬── TLP281 phototransistor (collector)
                                       │
                                      GND_3V3
```

**How it works:**
1. 72 V present → LED emits light → phototransistor conducts → GPIO reads LOW.
2. 72 V absent → LED off → phototransistor open → GPIO reads HIGH (pull-up).

Note the logic inversion: HIGH on GPIO means "no gear signal." This is handled in the `gear` task's FSM.

**Key properties of TLP281:**
- Isolation voltage: 2500 Vrms minimum (far exceeds 72 VDC requirement).
- CTR (Current Transfer Ratio): 100–600% at 5 mA LED current.
- The input resistor network is sized so that 72 V produces ~5 mA through the LED (within the device's absolute maximum rating and optimal CTR range).

---

## Mechanical relay isolation (output)

**Problem:** SYS ESP32 must switch 72 V to the motor controller's gear inputs (D, S, R lines). These lines carry full traction voltage.

**Solution:** Mechanical relays driven by ESP32 GPIOs via a transistor driver.

```
ESP32 GPIO ── R1 ── NPN transistor base
                     │
                     ├── Collector ── Relay coil ── 12V
                     │
                     └── Emitter ── GND

Relay contacts (NO):
  72V bus ──┬── Common ── NO contact ──┬── Gear input on motor controller
             │                          │
             └── 1A fuse ───────────────┘
```

**Why relays instead of MOSFETs:**
- Galvanic isolation: relay coil and contacts are physically separated.
- Fail-safe: when coil is de-energized (ESP32 off, crashed, or ESTOP), contacts open → no gear signal.
- 72 VDC switching doesn't require complex gate drive or bootstrap circuits.
- Relay coil runs on 12 V (via transistor), not 72 V.

**Why not SSRs (solid-state relays):**
- Mechanical relays are simpler to debug (audible click).
- Lower cost for low-frequency switching (gear changes are rare, not PWM).
- No leakage current in OFF state.

---

## Protection circuits — 1A fuse + SMCJ90CA TVS

Every line that carries or connects to 72 V has a protection circuit:

```
72V source ──┬── 1A fast-blow fuse ──┬── Load (relay contact, gear wire, etc.)
              │                       │
              │                       ├── SMCJ90CA TVS diode ── GND_72V
              │                       │
              └───────────────────────┘
```

### 1A fast-blow fuse

- **Purpose:** Overcurrent protection. If a gear line shorts to chassis or another wire, the fuse blows before the wire melts or the battery is damaged.
- **Rating:** 1A fast-blow. Gear signaling draws microamps (into the motor controller's high-impedance input) — any current above 1A indicates a short.
- **One per line:** Each gear line (D, S, R) has its own fuse. A short on one gear doesn't take down the others.

### SMCJ90CA bidirectional TVS diode

- **Purpose:** Transient voltage suppression. Protects against:
  - **Load dump:** When the motor controller suddenly disconnects from the battery under load, the wiring inductance generates a high-voltage spike.
  - **ESD:** Electrostatic discharge during handling or from road static.
  - **Switching transients:** Relay contact bounce and motor controller PWM noise.
- **Rating:** SMCJ90CA — 90 V standoff, 146 V clamping, 1500 W peak pulse power.
  - Standoff voltage (90 V) > nominal bus voltage (72 V) — diode doesn't conduct in normal operation.
  - Clamping voltage (146 V) < breakdown voltage of adjacent components.
- **Bidirectional:** Protects against both positive and negative transients. The 'C' suffix = bidirectional. The 'A' suffix = 5% tolerance on breakdown voltage.

### Why both?

- The **fuse** protects against sustained overcurrent (shorts, wiring faults).
- The **TVS** protects against transient overvoltage (spikes, load dump, ESD).
- Together they cover the two major electrical fault categories. Neither alone is sufficient.

---

## 12 V accessory protection

The 12 V rail powers signal lights, headlights, and the ESP32's LDO regulator. It's downstream of the DC-DC converter (72V→12V).

Protection on the 12 V rail is less stringent (lower voltage, lower energy) but still present:
- **Reverse polarity protection:** A series Schottky diode on the ESP32 power input prevents damage if the 12 V wiring is accidentally reversed.
- **Bulk capacitance:** 470 µF electrolytic on the 12 V rail at the ESP32 input smooths voltage dips when lights switch on/off.

---

## Physical wiring rules

1. **72 V wiring is orange.** 12 V wiring is red. Ground is black. CAN is yellow/green twisted pair. Color coding prevents accidental cross-connection.
2. **72 V terminals are insulated boot-style.** No exposed metal. All connectors are latching (not friction-fit).
3. **Fuses are accessible without disassembly.** Mounted on a terminal block with a clear cover.
4. **The 72 V battery has a service disconnect** (Anderson connector or equivalent) that physically breaks both positive and negative. Pull it before any wiring work.

---

## Why this matters for the CAN bus

The CAN bus shares a common ground reference. If 72 V leaks onto the CAN ground, every transceiver on the bus sees 72 V common-mode and fails — potentially all at once. Galvanic isolation of the high-voltage signals prevents this. The SN65HVD230 transceivers are rated for ±25 V common-mode; 72 V would destroy them instantly.

---

*Primary reference: [[emergency-system]] for ESTOP electrical fault handling, emergency response matrix, and testing procedures.*

*See also: [[defense-in-depth-safety]] for ESTOP layers including motor/gear shutdown, [[architecture]] §5 for responsibility split, §8.6 for gear output details.*

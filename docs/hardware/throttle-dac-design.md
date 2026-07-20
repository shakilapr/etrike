# Throttle DAC Design — MCP4725 I2C DAC

Why MCP4725, not a digital potentiometer, not PWM.

---

## 1. What the motor controller expects

The motor controller's throttle input is a **0–5V analog DC voltage**, same as what the physical throttle grip produces. The throttle grip is a potentiometer wired as a voltage divider — twist produces a steady voltage proportional to angle.

MCP4725 mimics this by outputting a programmable DC voltage:
```
SYS ADC reads grip voltage → SYS DAC recreates voltage → Motor controller
```

---

## 2. Why not X9C103S (digital potentiometer)

X9C103S is a digital potentiometer — it varies a **resistance** (0 to 10kΩ), not a voltage.

| Property | X9C103S | MCP4725 |
|----------|---------|---------|
| Output type | Variable resistor | Voltage source |
| Resolution | 100 steps (~7-bit) | 4096 steps (12-bit) |
| Interface | Up/down pulses (relative) | I2C (set absolute value) |
| Power-on state | Unknown wiper position | Known: outputs 0V until configured |
| Set absolute value | No — must cycle to endpoint first | Yes — write 12-bit value |
| External parts needed | Voltage reference, buffer op-amp | None (direct voltage output) |
| Throttle feel | Steppy (100 discrete levels) | Smooth (4096 levels) |

To use X9C103S, you'd need to:
1. Wire it as voltage divider with external reference
2. On boot, pulse 100 steps down to calibrate to zero (slow, wears the wiper)
3. Accept only 100 throttle positions — noticeable steps to the rider

To use MCP4725:
1. Write `code = speed/3000 × 4095` via I2C
2. Output pin produces that voltage
3. Done.

---

## 3. Why not PWM from ESP32

ESP32's LEDC peripheral can produce PWM. With an RC low-pass filter, PWM approximates a DC voltage. But:

| Property | PWM + RC filter | MCP4725 DAC |
|----------|-----------------|-------------|
| Ripple | Inherent — filter cutoff trades ripple vs response time | Near-zero (true DC) |
| Resolution | ESP32 LEDC: up to 16-bit, but effective bits drop with ripple | True 12-bit |
| Response time | Filter delay (e.g., 1 kHz PWM needs ~10ms to settle) | I2C write latency only (~100µs) |
| Output impedance | High (from RC filter) | Low (op-amp buffered) |
| Failure mode | PWM stuck HIGH → RC charges to 3.3V → motor sees voltage | I2C timeout → DAC holds last value or drops to 0 depending on config |

PWM+filter *can* work. But the MCP4725 is purpose-built for this — cleaner signal, simpler failure analysis.

---

## 4. Voltage domain bridging (3.3V vs 5V)

ESP32 I2C operates at 3.3V. MCP4725 needs to output 0–5V to the motor controller.

```
ESP32-S3 (3.3V)              MCP4725 (5V domain)           Motor Controller
┌──────────────┐            ┌──────────────────┐          ┌───────────────┐
│              │            │                  │          │               │
│  I2C SDA ────┼────────────►  SDA             │          │               │
│  I2C SCL ────┼────────────►  SCL          VOUT├──────────► 0-5V throttle │
│              │            │                  │          │               │
│  3.3V        │            │  VDD = 5V        │          │               │
│              │            │  GND             │          │               │
└──────────────┘            └──────────────────┘          └───────────────┘
```

**Problem:** MCP4725 at VDD=5V has VIH ≈ 0.7×5V = 3.5V. ESP32's 3.3V I2C HIGH may not reliably cross this threshold.

**Option A: Power MCP4725 at 5V, level-shift I2C**
```
ESP32 ──► BSS138 MOSFET level shifter ──► MCP4725 VDD=5V ──► VOUT=0-5V
```
Clean 0-5V output. Needs shifter module (2× BSS138 + 4× 10k pull-ups) — cheap, standard.

**Option B: Power MCP4725 at 3.3V, amplify output**
```
ESP32 ──► MCP4725 VDD=3.3V ──► VOUT=0-3.3V ──► op-amp ×1.52 gain ──► 0-5V
```
Safe I2C levels. Adds op-amp. MCP4725 datasheet allows VDD down to 2.7V.

**Option C: Motor controller accepts 3.3V?**
Some controllers treat anything above ~3V as "full throttle." If so, no level shifting needed. Only way to know: test it. See parallel task WO-14 (motor controller analog input characterization).

**Recommended until proven otherwise:** Option A (5V MCP4725 + I2C level shifter). Most reliable, standard pattern, well-documented.

---

## 5. I2C details

| Parameter | Value |
|-----------|-------|
| Device | MCP4725A0T-E/CH |
| Address | 0x60 (7-bit) |
| SDA | GPIO15 |
| SCL | GPIO16 |
| VDD | 5V (via Option A level shifter) or 3.3V (via Option B) |
| Resolution | 12-bit (0–4095) |
| Output range | 0V to VDD |
| Write command | I2C fast mode (400 kHz): 3 bytes (cmd + dataH + dataL) |

Mapping: `DAC_code = abs(speed_mmps) / kThrottleMaxSpeedMmps × 4095`

---

## 6. Failure modes

| Failure | MCP4725 behavior | System effect |
|---------|-----------------|---------------|
| I2C bus hung | Holds last written value | Motor stays at last speed — stale setpoint check catches this (>200ms → zero) |
| MCP4725 power lost | VOUT drops to 0V | Motor stops — safe |
| ESP32 hung, WDT resets | I2C pins go Hi-Z, pull-ups bring SDA/SCL HIGH | MCP4725 holds last value until next I2C write. WDT reset clears ESP32 → ESP32 re-inits DAC to 0 |
| ESTOP triggered | Software writes 0 to DAC | 0V output → motor stops |

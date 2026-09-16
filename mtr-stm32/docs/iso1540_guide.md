# ISO154x / ISO1540 Practical Guide

If by **“ISO154x”** you mean the small breakout whose PCB is literally marked **ISO154x**, that is the Adafruit board built around TI’s **ISO1540 bidirectional I²C isolator**. This guide covers the TI ISO1540/ISO1541 family, hardware design considerations, electrical traps, timing limits, and alternatives.

---

## 1. What it actually does

The ISO1540 sits in the middle of an I²C bus:

```text
MCU / controller                           isolated device side
     │
 SDA ├────────┐                       ┌──────── SDA
 SCL ├────────┤     ISO1540           ├──────── SCL
3V3  ├── VCC1 │  ║ isolation ║       │ VCC2 ── 3V3/5V isolated supply
GND1 └── GND1 │  ║  barrier   ║       │ GND2 ── isolated ground
              └───────────────────────┘
             SIDE 1                 SIDE 2
```

It provides galvanic isolation for both SDA and SCL while preserving I²C's open-drain bidirectional behaviour. The two supply domains can also use different voltages, so it can simultaneously perform **3.3 V ↔ 5 V I²C level translation**.

The isolator itself does **not generate isolated power**. For real galvanic isolation, Side 2 needs a supply isolated from Side 1 (such as an isolated DC-DC converter or transformer-based supply).

---

## 2. ISO1540 versus ISO1541

| Feature                      |             ISO1540 |              ISO1541 |
| ---------------------------- | ------------------: | -------------------: |
| SDA                          |       Bidirectional |        Bidirectional |
| SCL                          |   **Bidirectional** | Side 1 → Side 2 only |
| Clock stretching from target |             **Yes** |               **No** |
| Multi-controller bus         |            Suitable |         Generally no |
| Max device rate              |               1 MHz |                1 MHz |
| Supply                       | 3.0–5.5 V each side |  3.0–5.5 V each side |
| Package                      |              SOIC-8 |               SOIC-8 |

Clock stretching is an important distinction. ISO1540 supports it because SCL is bidirectional; ISO1541 does not.

For most modern I²C peripherals, **ISO1540 is the safer choice** because you do not have to assume the target will never stretch SCL.

---

## 3. Important electrical limits

These are the numbers that matter much more than the headline "1 MHz".

| Parameter               |                                  Side 1 |        Side 2 |
| ----------------------- | --------------------------------------: | ------------: |
| Recommended VCC         |                               3.0–5.5 V |     3.0–5.5 V |
| Maximum bus capacitance |                               **40 pF** |    **400 pF** |
| Output sink capability  |                              **3.5 mA** |     **35 mA** |
| LOW output              |                         **0.57–0.80 V** |        ≤0.4 V |
| Input LOW threshold     |  roughly 0.48–0.66 V internal threshold | 0.3–0.4 × VCC |
| Input capacitance       |                                   ~7 pF |         ~7 pF |
| Maximum I²C rate        |                                   1 MHz |         1 MHz |
| Operating temperature   |                 −40°C to +125°C         |               |
| CMTI                    | 25 kV/µs min, 50 kV/µs typical          |               |

This asymmetry is intentional:

```text
SIDE 1
controller / low-capacitance node
≤40 pF
weak sink
unusual ~0.7 V LOW

          │ isolation │

SIDE 2
main I²C bus
≤400 pF
much stronger sink
normal ≤0.4 V LOW
```

Side 1 is designed as the **low-capacitance node** (MCU side) and Side 2 as the **fully loaded I²C bus** (peripheral side):

```text
MCU/controller → Side 1
I²C peripherals → Side 2
```

---

## 4. The most important ISO1540 trap: Side 1 is not really 0 V when LOW

This is probably the single most common ISO1540 compatibility problem.

When a LOW originates on Side 2 and travels back to Side 1:

```text
Normal I²C LOW:
0V ────────── approximately

ISO1540 Side-1 returned LOW:
      ┌──────── ~0.57–0.80 V
──────┘
```

That elevated LOW is intentional. It prevents the two internally bidirectional channels from seeing each other's LOW and permanently latching the bus.

The problem appears when your connected device expects something like:

```text
Device VIL(max) = 0.4 V
ISO1540 VOL1(max) = 0.8 V

0.8 V > 0.4 V

FAIL
```

For example, when interfacing with devices where target VIL is 0.4 V, the target could fail to ACK because the isolator returns approximately 0.8 V.

### Before connecting anything to Side 1

Check its datasheet:

```text
VIL(max) of receiving device > 0.8 V
```

is the comfortable condition.

Something specified below 0.8 V deserves serious attention.

Do **not** assume changing the pull-up resistor will fix this. Changing pull-up resistance does not eliminate the inherent Side-1 VOL behaviour.

If this causes incompatibility, options include putting that device on Side 2 or adding an appropriate I²C buffer such as a TCA980x/TCA9517-class device.

---

## 5. Pull-ups: do not blindly trust the onboard 10 kΩ

The Adafruit ISO1540 breakout contains approximately **10 kΩ pull-ups on SDA and SCL on both sides**.

That makes simple low-capacitance 100-kHz setups convenient.

But it does **not mean 10 kΩ is correct for every bus**.

I²C lines rise through:

```text
pull-up resistor
       │
VCC ── R ───── SDA/SCL
                 │
                 Cbus
                 │
                GND
```

Approximate 30–70% rise time:

```text
tr ≈ 0.8473 × Rpullup × Cbus
```

I²C specifications limit rise time approximately to:

| Mode           |   Clock | Maximum rise time |
| -------------- | ------: | ----------------: |
| Standard       | 100 kHz |           1000 ns |
| Fast           | 400 kHz |            300 ns |
| Fast-mode Plus |   1 MHz |            120 ns |

### With only 40 pF

```text
tr = 0.8473 × 10,000 × 40 pF
   ≈ 339 ns
```

Already slightly slower than Fast-mode's 300 ns limit.

### With 100 pF

```text
≈ 847 ns
```

Fine for 100 kHz; nowhere near Fast-mode.

### With 400 pF

```text
≈ 3.39 µs
```

Far too slow even for normal 100-kHz compliant timing.

This explains why simply reading **"supports 1 MHz"** on the product page can be misleading. The IC itself can operate at 1 MHz, but your **actual RC bus may not**.

---

## 6. Lower pull-up limit matters too

You cannot simply keep lowering the resistance. Side 1 may sink only about **3.5 mA**.

### 3.3 V Side 1

```text
Rmin ≈ 3.3V / 3.5mA
     ≈ 943 Ω
```

### 5 V Side 1

```text
Rmin ≈ 5V / 3.5mA
     ≈ 1.43 kΩ
```

Side 2 can tolerate substantially more current because its sink capability is 35 mA. Pull-ups must be selected so these current limits are not exceeded.

So a 1 kΩ pull-up to 5 V on Side 1 is **not appropriate**, because about 5 mA would have to be sunk.

---

## 7. A practical pull-up design method

For each side separately:

```text
1. Estimate Cbus.
2. Pick your I²C mode.
3. Calculate Rmax from rise time.
4. Calculate Rmin from sink current.
5. Select an available resistance:
   
       Rmin < Rpullup < Rmax

6. Measure SDA and SCL with an oscilloscope.
```

Example: 100-pF Side 2 running 400 kHz.

```text
Rmax ≈ tr / (0.8473 × C)
     ≈ 300ns / (0.8473 × 100pF)
     ≈ 3.54 kΩ
```

Therefore:
- `10 kΩ`: too weak
- `4.7 kΩ`: still theoretically too weak
- `3.3 kΩ`: plausible
- `2.2 kΩ`: stronger margin

Actual system capacitance must be measured or conservatively estimated.

---

## 8. Watch for hidden parallel pull-ups

A real modular system might look like:

```text
ISO1540 board     10k
Sensor A board    10k
Sensor B board    10k
Sensor C board    4.7k
```

These are all in parallel:

```text
1/Reffective = 1/10k + 1/10k + 1/10k + 1/4.7k
Reffective ≈ 1.95 kΩ
```

That is completely different from "a 10-kΩ bus." This can be helpful for rise time but can eventually exceed the Side-1 3.5-mA sink specification. Always calculate **effective resistance**, not the printed resistor on one board.

---

## 9. Powering the module correctly

For real isolation:

```text
        MCU DOMAIN                     SENSOR DOMAIN

MCU 3V3 ── VCC1                VCC2 ── isolated 3.3/5V
MCU GND ── GND1                GND2 ── isolated GND

            NO GROUND CONNECTION
            BETWEEN GND1/GND2
```

Each side needs its own power. Connecting `GND1 ───────── GND2` defeats galvanic ground isolation. The signals will still pass, and voltage translation can still work, but the system is no longer galvanically isolated.

---

## 10. Very important: power sequencing

A dangerous case is:

```text
VCC1 = OFF
but
SDA1/SCL1 are externally pulled to 3.3V
```

The signal pins have an absolute maximum of roughly:

```text
Vpin ≤ VCC + 0.5 V
```

Therefore when `VCC = 0V`, you cannot safely leave `SDA = 3.3V` / `SCL = 3.3V`. This condition can damage the ISO1540.

The safer sequence is:

```text
Power isolator side
        ↓
Allow supply to stabilize
        ↓
Connect / enable SDA + SCL pull-ups
```

This also matters when hot-plugging cables.

---

## 11. ISO1540 is not genuinely hot-swap designed

For hot plugging the ISO1540, power/ground contacts must effectively connect before SDA/SCL, for example by using staggered/recessed signal contacts.

The newer **ISO1640** is designed to be hot-swappable and is a pin-to-pin upgrade to ISO1540.

---

## 12. Decoupling is unusually important here

Requirements:

```text
VCC1 ── 0.1 µF ── GND1
VCC2 ── 0.1 µF ── GND2
```

The capacitor should be placed **2 mm maximum from the supply pin**.

If decoupling capacitors are too far away from the IC, the isolator will not receive the transient current it needs, causing intermittent transmission errors or bus lockups.

Recommended layout:

```text
VCC pin ─┬─ 0.1uF ─ via → ground plane
         │
         └─ supply trace
```

rather than:

```text
VCC ───────── long trace ───────── 0.1uF
```

---

## 13. Side 1 should remain physically small

Limit:

```text
Cside1 ≤ 40 pF
```

Therefore Side 1 should typically be:

```text
MCU ── short PCB trace ── ISO1540
```

Not across long cables or connector chains. Put the heavily loaded portion on Side 2.

---

## 14. Long cables are still a bad I²C environment

The isolator fixes problems such as ground loops, ground-potential differences, and common-mode currents.

It does **not magically turn I²C into a long-distance differential bus**. Ordinary I²C buses are restricted by the 400-pF limit. For multi-meter noisy wiring, use a proper differential physical layer (CAN, RS-485) or differential I²C extender (e.g. PCA9615).

---

## 15. Software: the ISO1540 has no address

The ISO1540 has no I²C address, no registers, and requires no driver:

```text
MCU ── normal I²C bus ── ISO1540 ── normal target
```

The target retains its normal address. Firmware communicates with target sensors/DACs transparently.

---

## 16. Arduino Code Example

Start conservatively at 100 kHz:

```cpp
#include <Wire.h>

void setup() {
    Serial.begin(115200);

    Wire.begin();
    Wire.setClock(100000);

    Serial.println("I2C started through ISO1540");
}

void loop() {
}
```

I²C address scanner:

```cpp
#include <Wire.h>

void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);

    delay(100);

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);

        if (Wire.endTransmission() == 0) {
            Serial.print("Found 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
        }
    }
}

void loop() {
}
```

---

## 17. ESP32 Arduino Example

```cpp
#include <Wire.h>
#include <Adafruit_INA219.h>

constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;

Adafruit_INA219 ina219;

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);

    if (!ina219.begin()) {
        Serial.println("INA219 not detected");
    }
}

void loop() {
}
```

---

## 18. ESP-IDF Example

```c
i2c_master_bus_config_t bus_config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_21,
    .scl_io_num = GPIO_NUM_22,
    .clk_source = I2C_CLK_SRC_DEFAULT,
};

i2c_master_bus_handle_t bus_handle;

ESP_ERROR_CHECK(
    i2c_new_master_bus(&bus_config, &bus_handle)
);
```

---

## 19. CircuitPython Example

```python
import board
import busio

i2c = busio.I2C(
    board.SCL,
    board.SDA,
    frequency=100000
)

while not i2c.try_lock():
    pass

print([hex(x) for x in i2c.scan()])

i2c.unlock()
```

---

## 20. Raspberry Pi / Linux Example

```bash
i2cdetect -y 1
```

Python:

```python
from smbus2 import SMBus

ADDRESS = 0x40

with SMBus(1) as bus:
    value = bus.read_byte_data(ADDRESS, 0x00)
    print(value)
```

---

## 21. STM32 HAL Example

```c
HAL_I2C_Master_Transmit(
    &hi2c1,
    DEVICE_ADDR << 1,
    data,
    sizeof(data),
    HAL_MAX_DELAY
);
```

---

## 22. Reference Hardware Implementations

Because the ISO1540 is transparent to software, the key references are hardware designs:
- Adafruit ISO1540 PCB breakout design
- MikroElektronika Click ISO1540 board implementation
- PSMonitor isolated measurement system (Arduino + ISO1540 + INA260)
- lwz180 isolated-I²C project
- KXKM Batterie Parallelator architecture (ESP-IDF + ISO1540 + INA237)

*(Note: Ensure device frequency is treated as maximum 1 MHz per TI specification).*

---

## 23. Common Failure Modes and Solutions

| Symptom | Cause / Finding | Fix |
|---|---|---|
| Slave does not ACK | Side-1 LOW can reach ~0.8 V; target VIL only 0.4 V | Reconsider side placement or add compatible buffer. |
| Works after adding capacitor to SDA | Supply decoupling layout inadequate | Put 0.1-µF bypass directly beside VCC/GND pins (≤2 mm). |
| Slow / no comms with 10 kΩ pull-up | RC rise time too slow for bus capacitance | Calculate pull-up from capacitance and mode. |
| Works directly but fails through isolator | Logic thresholds incompatible | Compare device VIL/VIH against ISO1540 VOL/thresholds. |
| Bus fails after rapid transactions | Level incompatibility or missing pull-up on one side | Verify logic compatibility and confirm pull-ups on **both** sides. |
| Problems during plugging / unplugging | ISO1540 not designed for hot-swap | Power first, signals later, or use ISO1640. |
| Device at VCC=0 while SDA/SCL high | I/O exceeds VCC + 0.5 V absolute max | Remove signal voltage until side is powered. |
| Long-cable instability | High capacitance/noise/transients remain | Keep I²C short or use differential transceivers. |
| Clock-stretching device fails with ISO1541 | ISO1541 SCL is unidirectional | Use ISO1540. |
| Attempt to isolate RS-485 | Wrong electrical interface | Use an isolated RS-485 transceiver. |
| Attempt to isolate UART | Bidirectional open-drain conflicts with push-pull UART | Use a digital/UART isolator. |

---

## 24. Interface Restrictions

ISO1540 is strictly an **open-drain I²C isolator**. Do not use it for:
- Differential buses (RS-485, CAN)
- Push-pull digital signals (UART, SPI, PWM)

---

## 25. High-Voltage Isolation Considerations

The ISO1540 IC itself has substantial ratings (2500 V RMS withstand, 4242 V peak transient). However, a complete system requires considering:
- PCB creepage and clearance distances
- Connector spacing and board contamination
- Working voltage and pollution degree
- Enclosure and power-supply isolation

The rating printed on an IC datasheet does not make an unrated breakout board mains-safe on its own.

---

## 26. Supply Current Requirements

At ~3.3 V to 5 V, the ISO1540 draws several milliamps per side (worst-case up to ~7 mA per side depending on bus state and traffic). Ensure the isolated DC-DC converter has sufficient headroom for:
- ISO1540 Side 2 supply current
- Target sensors / DAC current
- Pull-up resistor sinking current

---

## 27. Power-Up Recovery & UVLO

The ISO1540 has an internal Undervoltage Lockout (UVLO) threshold (1.7–2.9 V) with up to ~151 µs recovery time.

Always provide a short power-up delay before issuing transactions:

```cpp
Wire.begin();
delay(5); // Allow isolator and target supply to settle
sensor.begin();
```

---

## 28. ISO1640 Comparison

TI identifies the **ISO1640** as the newer pin-to-pin upgrade:

| Feature | ISO1540 | ISO1640 |
|---|---:|---:|
| Generation | Older | Newer |
| Hot swappable | No dedicated support | **Yes** |
| Max speed | 1 MHz | **1.7 MHz** |
| Side-1 capacitance | 40 pF | **80 pF** |
| Side-2 capacitance | 400 pF | 400 pF |
| Side-2 VCC minimum | 3.0 V | **2.25 V** |
| CMTI typical | ~50 kV/µs | **~100 kV/µs** |
| Pin compatibility | — | Pin-to-pin upgrade available |

---

## 29. ESP32 / STM32 Isolated Sensor Interconnect

```text
                 SIDE 1             SIDE 2

Controller                        Isolated Sensor / DAC
3.3V ─────────── VCC1    VCC2 ─── 3.3V / 5.0V ISO
GND  ─────────── GND1    GND2 ─── GND ISO
SDA  ─────────── SDA1    SDA2 ──── SDA
SCL  ─────────── SCL1    SCL2 ──── SCL

      0.1uF                     0.1uF
VCC1 ──||──GND1          VCC2 ──||──GND2

         NO electrical connection
         between GND1 and GND2
```

---

## 30. Design & Pre-Flight Verification Checklist

1. **Part Verification**: Ensure ISO1540 (bidirectional SCL) is fitted if clock stretching or multi-controller is possible.
2. **Side Orientation**: Controller on Side 1 (low capacitance), peripheral bus on Side 2 (high drive).
3. **Logic Low Levels**: Verify Side-1 receiving pins tolerate $V_{OL1} \approx 0.8\text{ V}$.
4. **Supply Isolation**: Ensure true galvanic separation between GND1 and GND2 (no shared ground loops via debuggers, shields, or power supplies).
5. **Decoupling Layout**: Place 100 nF ceramic capacitors within 2 mm of VCC1/GND1 and VCC2/GND2.
6. **Pull-Up Sizing**: Calculate effective parallel resistance on each side to ensure $R_{min} < R_{pullup} < R_{max}$ and rise times meet the selected I²C frequency mode.
7. **Capacitance Budget**: Maintain Side 1 $< 40\text{ pF}$ (keep traces short on-board) and Side 2 $< 400\text{ pF}$.
8. **Power Sequencing**: Prevent unpowered signal biasing ($V_{pin} \le V_{CC} + 0.5\text{ V}$).
9. **Startup Delay**: Include $\ge 5\text{ ms}$ delay after power rails stabilize before initiating I²C transactions.
10. **Signal Integrity**: Verify rise time, logic levels, overshoot, and ACK pulses with an oscilloscope on both sides during bring-up.

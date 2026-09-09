# ESP32 to RadioLink T12D / R16F SBUS Wiring Reference

Pin-to-pin wiring guide for connecting the **RadioLink R16F V1.0** receiver to the **ESP32** / **ESP32-S3** microcontroller running [`rm-esp32-t12d`](file:///e:/work/etrike/rm-esp32-t12d) firmware, plus Low-CAN bus transceiver interconnects.

---

## 1. System Architecture & Topology

```
                  ┌─────────────────────────────────────────┐
                  │          RadioLink T12D                 │
                  │   (FHSS V2.1, 16 Channels, 4096-res)    │
                  └───────────────────┬─────────────────────┘
                                      │ 2.4 GHz FHSS
                                      ▼
                  ┌─────────────────────────────────────────┐
                  │        RadioLink R16F V1.0              │
                  │        (PWM + SBUS Mode)                │
                  │    [RED LED = ON, BLUE LED = ON]        │
                  └───────────────────┬─────────────────────┘
                                      │
            ┌─────────────────────────┼─────────────────────────┐
            │ CH16 Signal (S.BUS)     │ +5V Power               │ Common GND
            │ 100k, 8E2, Inverted     │ (Regulated 5V Rail)     │
            ▼                         ▼                         ▼
   ┌─────────────────┐       ┌─────────────────┐       ┌─────────────────┐
   │ ESP32 GPIO 16   │       │ Regulated +5V   │       │ ESP32 GND       │
   │ (UART1 RX)      │       │ (DO NOT use EXT)│       │ (Common Ground) │
   └────────┬────────┘       └─────────────────┘       └────────┬────────┘
            │                                                   │
            │           ESP32 / ESP32-S3 Microcontroller        │
            │           Running rm-esp32-t12d Firmware          │
            │                                                   │
            └───────────┬───────────────────────────┬───────────┘
                        │ GPIO 5 (CAN TX)           │ GPIO 4 (CAN RX)
                        ▼                           ▼
            ┌───────────────────────────────────────────────────┐
            │        SN65HVD230 CAN Transceiver (3.3V)          │
            └─────────────────────┬───────┬─────────────────────┘
                                  │       │
                            CAN_H │       │ CAN_L (500 kbit/s Low-CAN)
                                  ▼       ▼
                       Actuator Bus (SES / SEB / MTR)
```

---

## 2. RadioLink R16F V1.0 Receiver Pinout

Looking at the R16F receiver with the servo pin headers facing you:
- **Top row**: Signal (light-colored / white / yellow wire)
- **Middle row**: Power Positive `+` (red wire, 3–12 V input, 50 mA @ 5V)
- **Bottom row**: Ground `−` (dark-colored / black / brown wire)

```
        ┌───────────────────────────────────────────────────┐
        │  [16 S.BUS]  [15]  [14] ... [2]  [1]     [EXT]    │
 Signal │    (●)       (●)   (●)      (●)  (●)      +  -    │  Top
     +5V│    (●)       (●)   (●)      (●)  (●)     (●)(●)   │  Middle
    GND │    (●)       (●)   (●)      (●)  (●)              │  Bottom
        └───────────────────────────────────────────────────┘
              ▲
              │
         Use CH16 for SBUS
```

> [!CAUTION]
> **DO NOT USE THE EXT CONNECTOR FOR POWER**:
> The 2-pin connector labeled **EXT (+ −)** on the right side of the R16F is strictly for telemetry battery voltage sensing (up to 36 V / 8S LiPo). RadioLink explicitly warns that feeding power into EXT can destroy the receiver and will not power it. Power the receiver exclusively through the servo header pins (`+` and `−`).

---

## 3. Microcontroller Wiring Table

### 3.1 Receiver to ESP32 Connections

| R16F Pin | ESP32 Classic Pin | ESP32-S3 Pin | Function / Net | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **CH16 Signal** (Top) | **GPIO 16** | **GPIO 16** | SBUS Serial RX | Inverted UART1 RX @ 100,000 baud, 8E2 |
| **CH16 +** (Middle) | **5V / VBUS** | **5V / VBUS** | Receiver Power | Clean regulated 5.0 V rail (~50 mA) |
| **CH16 −** (Bottom) | **GND** | **GND** | Ground | Must share common GND with ESP32 |

### 3.2 CAN Transceiver (SN65HVD230 / WCMCU-230) Connections

| Transceiver Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **3V3 / VCC** | **3V3** | Logic Power | 3.3 V clean logic rail |
| **GND** | **GND** | Ground | Common system ground |
| **CTX / TXD** | **GPIO 5** | CAN TX | ESP32 TWAI Transmit |
| **CRX / RXD** | **GPIO 4** | CAN RX | ESP32 TWAI Receive |
| **CAN_H** | **Low-CAN Backbone** | CAN High | Differential bus (+120 Ω termination at bus ends) |
| **CAN_L** | **Low-CAN Backbone** | CAN Low | Differential bus (+120 Ω termination at bus ends) |

---

## 4. Signal Voltage Level & Inversion

### 4.1 On-Chip Hardware Inversion
SBUS protocol uses an inverted UART signal (idle low, start bit high).
The `rm-esp32-t12d` firmware natively configures the ESP32 internal UART hardware inverter:
```cpp
uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV);
```
**No external transistor or inverter chip is needed.**

### 4.2 Electrical High Level Verification & Resistor Divider
Before plugging the signal wire directly into the ESP32:
1. Power the R16F with 5 V.
2. Measure the voltage between **CH16 Signal** and **GND** with a multimeter or oscilloscope:
   - If the high level is **$\approx 3.3\,\text{V}$**: Direct connection to GPIO 16 is safe.
   - If the high level is **$\approx 5.0\,\text{V}$**: Add a resistor divider to reduce 5 V down to $3.33\,\text{V}$ to protect the 3.3 V ESP32 input:

```text
R16F SBUS (5V Signal)
          │
        10 kΩ
          │
          ├────────► ESP32 GPIO 16 (UART RX)
          │
        20 kΩ
          │
         GND
```

The voltage reduction formula:
$$5.0\,\text{V} \times \frac{20\,\text{k}\Omega}{10\,\text{k}\Omega + 20\,\text{k}\Omega} = 3.33\,\text{V}$$

A dedicated 5-V-to-3.3-V logic buffer level shifter can also be used.

---

## 5. Protocol Specification & Reference Decoder

### 5.1 Protocol Parameters
```text
Baud       100000
Data       8 bits
Parity     Even
Stop bits  2
Signal     Inverted (Idle Low, Start Bit High)
Frame      25 bytes
Channels   16 × 11-bit (Packed into bytes 1..22)
```

The hardware inversion is handled internally by ESP32 UART (`uart_set_line_inverse`), so no external transistor is needed.

### 5.2 Standalone Reference SBUS Decoder (Arduino-ESP32 / Diagnostic)

```cpp
#include <Arduino.h>

HardwareSerial SBUS(1);

constexpr int SBUS_RX = 16;

uint8_t frame[25];
uint8_t indexPos = 0;

uint16_t channels[16];

void decodeSBUS(const uint8_t *f)
{
    // Channel data occupies bytes 1..22.
    // 16 channels × 11 bits = 176 bits.

    for (int ch = 0; ch < 16; ch++) {
        int bitIndex  = ch * 11;
        int byteIndex = 1 + bitIndex / 8;
        int shift     = bitIndex % 8;

        uint32_t value =
            ((uint32_t)f[byteIndex]) |
            ((uint32_t)f[byteIndex + 1] << 8) |
            ((uint32_t)f[byteIndex + 2] << 16);

        channels[ch] = (value >> shift) & 0x07FF;
    }

    bool frameLost = f[23] & 0x04;
    bool failsafe  = f[23] & 0x08;

    Serial.printf(
        "CH1=%u CH2=%u CH3=%u CH4=%u  lost=%d failsafe=%d\n",
        channels[0],
        channels[1],
        channels[2],
        channels[3],
        frameLost,
        failsafe
    );
}

void setup()
{
    Serial.begin(115200);

    // baud, format, RX, TX, invert
    SBUS.begin(
        100000,
        SERIAL_8E2,
        SBUS_RX,
        -1,
        true
    );

    Serial.println("R16F SBUS reader started");
}

void loop()
{
    while (SBUS.available()) {

        uint8_t b = SBUS.read();

        // Wait for SBUS start byte
        if (indexPos == 0 && b != 0x0F)
            continue;

        frame[indexPos++] = b;

        if (indexPos == 25) {

            // Typical SBUS end byte
            if (frame[0] == 0x0F) {
                decodeSBUS(frame);
            }

            indexPos = 0;
        }
    }
}
```

### 5.3 Typical Raw SBUS Ranges
```text
Low       ~172
Center    ~992
High      ~1811
```

---

## 6. RadioLink Configuration & LED Modes

### 5.1 Configure T12D Transmitter
1. Turn on the T12D transmitter.
2. Open **Receiver Settings** $\longrightarrow$ **RF SETTINGS**.
3. Set:
   - **MODULE SELECTION**: `Internal`
   - **PROTOCOL**: `FHSS V2.1` (16 channels, 4096 PWM resolution).

### 5.2 Binding R16F to T12D
1. Keep transmitter and receiver roughly **60 cm apart** (too close can cause RF desensitization/blocking).
2. Power up the R16F receiver.
3. Hold the **ID SET** button on the R16F for $>1$ second.
4. The receiver LED will flash rapidly. Release the button.
5. Wait for the LED to turn solid. The T12D display will show the RF signal strength indicator.

### 5.3 Put R16F into SBUS Mode
With the receiver powered and bound:
- **Short-press** the R16F ID SET button once.
- Check LED indicators:
  - **RED LED = ON**
  - **BLUE LED = ON**
- This indicates **PWM + SBUS mode** is active. Channel 16 is now outputting 100 kbaud SBUS frames.

---

## 6. Verification & Telemetry Output

Once wired and powered, monitor the serial telemetry log at 115200 baud:

```powershell
pio run -d rm-esp32-t12d -e vehicle -t upload -t monitor
```

### Expected Startup Banner:
```text
I (310) rm_t12d: =================================================
I (310) rm_t12d:   RM-ESP32-T12D Receiver Gateway (RadioLink SBUS)
I (310) rm_t12d:   Version: v0.8.0-alpha-rm-t12d
I (310) rm_t12d: =================================================
I (320) can: TWAI TX=21 RX=22 @ 500 kbit/s
I (330) rc_rx: Initialized SBUS UART1 on RX GPIO 16 (100k, 8E2, inverted)
I (340) rm_t12d: All tasks created successfully. RM-ESP32-T12D operational.
```

### Serial Output Strategy (Change-Driven + 2 Hz Decimated Summary):
To prevent UART TX buffer overflow and latency at 50 Hz, the firmware uses an optimal dual-rate display:
1. **Immediate Delta Trigger**: Logs instantly whenever steering ($\ge 1.0^\circ$), brake ($\ge 0.5\text{ mm}$), speed ($\ge 50\text{ mm/s}$), gear, or ignition changes.
2. **Periodic 2 Hz Pulse**: Logs a single consolidated summary every 500 ms when controls are steady.

```text
I (1450) can_tx: [CAN TX] 0x169 SES: raw=30045 (+4.5°) | 0x7B9 SEB: raw=600 (0.0mm) | 0x204 MTR: +1250mm/s [D] | 0x113 PWR: ON | 0x110 MODE: Auto
I (1950) can_tx: [CAN TX] 0x169 SES: raw=30000 (+0.0°) | 0x7B9 SEB: raw=600 (0.0mm) | 0x204 MTR: +0mm/s [N] | 0x113 PWR: OFF | 0x110 MODE: Manual
```

Under Fail-Safe or ESTOP:
```text
W (2300) can_tx: [CAN TX | ESTOP] 0x001 SAFETY_ESTOP | 0x169 SES: raw=30000 (0.0°) | 0x7B9 SEB: raw=1140 (27.0mm) | 0x204 MTR: 0mm/s [N] | 0x113 PWR: OFF
```

---

## 7. Critical T12D & R16F Operational Constraints

| Constraint | Physical / Protocol Cause | Required Robot Action |
| :--- | :--- | :--- |
| **12 Channels Max** | T12D transmitter generates 12 channels; R16F SBUS slots 13–16 are unused. | Design vehicle logic around CH1–CH12 only. |
| **FHSS V2.1 Mandatory** | FHSS V1 and V2 protocols limit R16F to 8 channels. | Freeze T12D RF setting to `FHSS V2.1`. |
| **Rebind on Protocol Change** | Changing RF mode breaks existing binding. | Rebind transmitter and receiver after any RF menu change. |
| **Mode Toggle Button Trap** | Short-pressing the R16F ID SET button changes mode. | Confirm **RED LED = ON** and **BLUE LED = ON** (PWM + SBUS). |
| **RF Blocking (< 60 cm)** | Placing T12D within 60 cm of R16F causes signal desensitization and link loss. | Maintain $>1.0\text{ m}$ separation during bench testing. |
| **Switch Jitter & Calibration** | Inherent ~0.25 µs jitter; switches never produce exact integer counts. | Decode switches using threshold hysteresis bands, not exact values. |
| **Model Memory Drift** | Selecting wrong model memory changes channel reverses, curves, and mixes. | Create and lock a dedicated `ROBOT` model profile on the T12D. |
| **No Direct Actuator Drive** | RC values must never directly feed motor/brake hardware. | Pass all inputs through plausibility, deadman, and arbitration layers. |

---

## 8. Pre-Drive Commissioning Checklist

Before powering high-voltage motor drivers or operating the vehicle:

- [ ] **Receiver Power**: Verify R16F is powered from clean 5.0 V regulated rail (NOT from raw traction battery, and NOT via EXT port).
- [ ] **Common Ground**: Solid GND reference between R16F and ESP32.
- [ ] **LED Status**: R16F shows solid **RED + BLUE LEDs** (PWM + SBUS).
- [ ] **Transmitter Model**: T12D active model verified as `ROBOT`.
- [ ] **RF Protocol**: T12D confirmed on `Internal Module` $\rightarrow$ `FHSS V2.1`.
- [ ] **Startup Switch Check**: All switches in safe state before power (Ignition UP=OFF, Gear MID=N, ESTOP UP=Run).
- [ ] **Failsafe Test**: Turn off T12D transmitter on bench: verify serial prints `[CAN TX | ESTOP]`, brake clamps to `27.0 mm`, and `0x001 SAFETY_ESTOP` is emitted.
- [ ] **Recovery Test**: Turn T12D back on, cycle SWB to OFF, SWC to Neutral: verify ESTOP clears.

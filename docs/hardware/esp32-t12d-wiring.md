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
                        │ GPIO 21 (CAN TX)          │ GPIO 22 (CAN RX)
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
| **CTX / TXD** | **GPIO 21** | CAN TX | ESP32 TWAI Transmit |
| **CRX / RXD** | **GPIO 22** | CAN RX | ESP32 TWAI Receive |
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

### 4.2 Electrical High Level Verification
Before plugging the signal wire directly into the ESP32:
1. Power the R16F with 5 V.
2. Measure the voltage between **CH16 Signal** and **GND** with a multimeter / oscilloscope:
   - If the high level is **$\approx 3.3\,\text{V}$**: Direct connection to GPIO 16 is safe.
   - If the high level is **$\approx 5.0\,\text{V}$**: Insert a simple resistor divider to protect the 3.3 V ESP32 input:
     ```
     R16F CH16 Signal ────[ 2.2 kΩ ]────┬────► ESP32 GPIO 16
                                        │
                                     [ 3.3 kΩ ]
                                        │
                                       GND
     ```

---

## 5. RadioLink Configuration & LED Modes

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

Expected startup logs:
```text
I (310) rm_t12d: =================================================
I (310) rm_t12d:   RM-ESP32-T12D Receiver Gateway (RadioLink SBUS)
I (310) rm_t12d:   Version: v0.8.0-alpha-rm-t12d
I (310) rm_t12d: =================================================
I (320) can: TWAI TX=21 RX=22 @ 500 kbit/s
I (330) rc_rx: Initialized SBUS UART1 on RX GPIO 16 (100k, 8E2, inverted)
I (340) rm_t12d: All tasks created successfully. RM-ESP32-T12D operational.
```

When transmitting:
```text
I (1340) rm_t12d: STATUS | Valid=1 FS=0 Lost=0 Ign=1 Gear=D Steer=0.0 deg Brk=0.0 mm Throt=0% VRA=1.00 | CH[1..8]=[1500,1500,1000,1500,2000,2000,2000,1000]us | CAN ok=120 fail=0
```

### Safety Test:
Turn off the T12D transmitter while monitoring:
- `FS=1` (hardware failsafe bit asserted).
- Firmware immediately clamps brake to `27.0 mm`, centers steering, zeros throttle, and broadcasts `0x001 SAFETY_ESTOP`.

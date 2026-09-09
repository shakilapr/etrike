# RM-ESP32-T12D — Receiver Module Gateway (RadioLink T12D / R16F SBUS)

Gateway firmware for the **RadioLink T12D** RC transmitter and **RadioLink R16F V1.0** receiver running on the **ESP32** / **ESP32-S3** microcontroller over single-wire **SBUS**.

---

## 1. Overview & Architecture

The `rm-esp32-t12d` node is a specialized receiver gateway for the RadioLink T12D transmitter (FHSS V2.1 protocol) paired with the R16F V1.0 receiver.

Unlike the legacy PWM multi-wire interface, this gateway decodes **all 16 RC channels**, the **hardware failsafe bit**, and **frame loss indicator** via a **single signal wire** connected to receiver channel 16 (CH16 / S.BUS) using the ESP32 hardware UART peripheral with native hardware line inversion (100,000 baud, 8E2).

It translates these inputs into canonical vehicle control frames on the **Low CAN bus (500 kbit/s, Classic CAN 2.0A)**:

- **Steering Control (`0x169 VCU_SES_REQ` at 50 Hz)**:
  - Right Stick Horizontal (CH1) $\longrightarrow$ proportional steering angle $\pm 45.0^\circ$ to SES / SES.
  - Formatted with vendor center offset $30000$ (linear range $29550\dots 30450$ at $0.1^\circ$/LSB).
  - Center deadband of $\pm 30\,\mu\text{s}$ holds $0.0^\circ$ and eliminates hand tremor.
  - Active when Ignition is ON and gear is in Drive (D) or Reverse (R).
- **Braking Control (`0x7B9 VCU_SEB_REQ` at 50 Hz)**:
  - Right Stick Vertical (CH2) $\longrightarrow$ proportional stroke request ($0.0\dots 27.0\text{ mm}$, raw $600\dots 1140$) to SEB.
  - Deadband threshold at $1520\,\mu\text{s}$; pushing forward progressively engages caliper stroke up to emergency max $27.0\text{ mm}$.
- **Motor Control & Direct Bypass (`0x204 RT_DRIVE_CMD` at 50 Hz)**:
  - Left Stick Vertical (CH3) $\longrightarrow$ proportional speed setpoint.
  - Forward Drive (D): ramps $0\dots 3000\text{ mm/s}$ ($10.8\text{ km/h}$).
  - Reverse (R): ramps $0\dots 500\text{ mm/s}$ ($1.8\text{ km/h}$).
  - **Brake-Over-Throttle Interlock**: when brake stroke exceeds $> 5.0\text{ mm}$, motor setpoint is immediately forced to $0\text{ mm/s}$.
- **Ignition Switch (`0x113 SYS_PWR_CMD` at 10 Hz)**:
  - **SWB** (2-position toggle, CH5) $\longrightarrow$ authoritative vehicle power state (OFF / ON).
- **Gear Selector (`0x110 SYS_MODE_CMD` & `0x204 RT_DRIVE_CMD` at 10 Hz / 50 Hz)**:
  - **SWC** (3-position toggle, CH6) $\longrightarrow$ **Reverse** (UP), **Neutral / Park** (MID), **Drive** (DOWN).
- **Auxiliary Controls & Telemetry**:
  - **SWA** (CH7): 2-position switch (Autonomy takeover / mode override).
  - **SWD** (CH8): 2-position switch (Auxiliary / fast emergency trigger).
  - **VRA** (CH9): Rotary potentiometer (Speed governor / throttle limit trim).
  - **VRB** (CH10): Rotary potentiometer (Brake sensitivity trim).
  - **CH11–CH16**: Reserved / expansion channels.
- **Hardware Fail-Safe & Deadman ESTOP (`0x001 SAFETY_ESTOP`)**:
  - Receiver hardware failsafe bit (Byte 23 Bit 3 in SBUS frame) or RF signal loss $>100\text{ ms}$ immediately:
    1. Snaps steering to $0.0^\circ$ (raw $30000$).
    2. Applies full emergency brake clamp ($27.0\text{ mm}$, raw $1140$).
    3. Zeros motor speed setpoint ($0\text{ mm/s}$).
    4. Broadcasts a zero-length `0x001 SAFETY_ESTOP` frame.

```
RadioLink T12D  ─────── 2.4 GHz FHSS V2.1 ───────►  RadioLink R16F V1.0
(16 Channels)                                       (CH16 / S.BUS Pin)
                                                             │
                                                    Single Signal Wire
                                                    (100 kbaud, 8E2, Inverted)
                                                             ▼
                                                    ESP32 UART1 RX (GPIO 16)
                                                             │
                                                    ┌────────┴────────┐
                                                    │  sbus_parser    │
                                                    │ (16-ch unpack,  │
                                                    │ failsafe check) │
                                                    └────────┬────────┘
                                                             │
                                                    ┌────────┴────────┐
                                                    │   rc_decoder    │
                                                    │  (snap states)  │
                                                    └────────┬────────┘
                                                             │
                                                    ┌────────┴────────┐
                                                    │   can_driver    │
                                                    │  (50 Hz Low-CAN)│
                                                    └────────┬────────┘
                                                             ▼
                                                    Low CAN Bus (500 kbps)
                                              0x169 SES | 0x7B9 SEB | 0x204 MTR
```

---

## 2. Hardware Wiring & Pinout

Only **three connections** are required between the R16F receiver and the ESP32:

| R16F Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **CH16 Signal** (Top) | **GPIO 16** | SBUS Serial RX | Inverted UART1 RX (100 kbps, 8E2) |
| **CH16 +5V** (Middle) | **5V Power Rail** | Receiver Power | 3–12 V supply range (~50 mA @ 5V). Do NOT use EXT port to power receiver! |
| **CH16 GND** (Bottom) | **GND** | Ground | Common reference ground |

### CAN Bus Wiring:
| Net | ESP32 GPIO | Direction | Connected To |
| :--- | :--- | :--- | :--- |
| **CAN TX** | **GPIO 21** | Output | Low CAN Transceiver TXD |
| **CAN RX** | **GPIO 22** | Input | Low CAN Transceiver RXD |

> [!CAUTION]
> **Electrical Signal Level**:
> While the R16F accepts 3–12 V power, verify that the signal pin high level on CH16 is 3.3 V safe for the ESP32 GPIO. If CH16 outputs 5 V logic, place a simple 2-resistor voltage divider (e.g., 2.2 kΩ / 3.3 kΩ) or level shifter between CH16 Signal and GPIO 16.

---

## 3. RadioLink T12D & R16F Setup Guide

### 3.1 Configure RadioLink T12D
1. Turn on T12D transmitter.
2. Navigate to: `Receiver Settings` $\longrightarrow$ `RF SETTINGS`.
3. Set:
   - **MODULE SELECTION**: `Internal`
   - **PROTOCOL**: `FHSS V2.1` (enables 16 channels and 4096-level PWM resolution).

### 3.2 Bind Transmitter to R16F
1. Place transmitter and receiver ~60 cm apart.
2. Power on the R16F receiver.
3. Press and hold the **ID SET** button on the R16F for $>1$ second.
4. The receiver LED will flash rapidly. Release the button.
5. Once bound, the LED remains solid.

### 3.3 Enable SBUS Output Mode on R16F
1. With receiver powered, **short press** the ID SET button once.
2. Verify LEDs:
   - **RED LED**: ON
   - **BLUE LED**: ON
   - **Interpretation**: Red + Blue ON indicates **PWM + SBUS mode**. CH16 is now the SBUS serial output.

---

## 4. Building & Flashing

```powershell
cd rm-esp32-t12d

# Build vehicle firmware
pio run -e vehicle

# Build bench firmware (emulated SYS safety authority)
pio run -e bench

# Flash to ESP32
pio run -e vehicle -t upload

# Monitor telemetry serial log (115200 baud)
pio run -e vehicle -t upload -t monitor
```

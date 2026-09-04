# RM-ESP32 — Receiver Module Gateway

Gateway firmware for the **FlySky FS-i6** RC Receiver running on the classic **ESP32** microcontroller.

---

## 1. Overview & Capabilities

The `rm-esp32` node decodes 6 channels of high-resolution PWM timing pulses from the FlySky FS-i6 RC receiver via the ESP32 hardware **RMT** peripheral, and translates them into canonical vehicle commands on the **Low-CAN bus (500 kbit/s, Classic CAN 2.0A)**:

- **Steering Control (`0x169 VCU_SES_REQ`)**:
  - Right gimbal horizontal axis $\longrightarrow$ proportional steering angle $\pm 45.0^\circ$ to EPS-C / SES.
  - Formatted with vendor center offset $30000$ (linear range $29550\dots 30450$ at $0.1^\circ$/LSB).
  - Active when Ignition is ON and gear is in Drive or Reverse.
  - Center deadband of $\pm 30\,\mu\text{s}$ holds $0.0^\circ$ (raw `30000`) and suppresses hand jitter.
- **Braking Control (`0x7B9 VCU_SEB_REQ`)**:
  - Left gimbal vertical axis $\longrightarrow$ proportional stroke request ($0\dots 27\text{ mm}$, raw $600\dots 1140$) to SEB.
- **Motor Control & Standalone Bypass (`0x204 RT_DRIVE_CMD`)**:
  - Enables direct vehicle driving without Host, RT, or SYS connected.
  - In Drive (D) or Reverse (R), Left Stick throttle maps directly to motor speed setpoint ($0\dots 3000\text{ mm/s}$ Drive, $0\dots 500\text{ mm/s}$ Reverse) and emits canonical `0x204 RT_DRIVE_CMD`.
- **Ignition Switch (`0x112 HMI_PWR_REQ`)**:
  - **SWB** (2-position toggle) $\longrightarrow$ Ignition ON / OFF commands broadcast at 1 Hz.
- **Gear Selector (`0x111 HMI_MODE_REQ`)**:
  - **SWC** (3-position toggle) $\longrightarrow$ **Reverse** ($1000\,\mu\text{s}$), **Neutral / Park** ($1500\,\mu\text{s}$), **Drive** ($2000\,\mu\text{s}$).
- **Dual-Stick Proportional Control**:
  - **Right Stick Horizontal (CH1 / GPIO 18)** $\longrightarrow$ Steering ($\pm 45.0^\circ$ rack limit, CAN `0x169`: $29550\dots 30450$).
  - **Right Stick Vertical (CH2 / GPIO 19)** $\longrightarrow$ Brake stroke ($0.0\dots 27.0\text{ mm}$, CAN `0x7B9`: $600\dots 1140$).
  - **Left Stick Vertical (CH3 / GPIO 14)** $\longrightarrow$ Proportional throttle ($0\dots 100\%$, CAN `0x204`).
- **Safety Deadman & ESTOP (`0x001 SAFETY_ESTOP`)**:
  - If RC pulses drop or disconnect for $>100\text{ ms}$, the node immediately snaps steering to $0.0^\circ$ (raw `30000`), applies maximum emergency brake stroke ($27.0\text{ mm}$, raw `1140`), forces motor speed to 0, and broadcasts a zero-length `SAFETY_ESTOP` frame.

---

## 2. Hardware Pinout & Wiring

The `rm-esp32` node decodes 6 channels of high-resolution PWM timing pulses from the FlySky FS-i6 receiver:

| Pin / Net | Function | Direction | Connected To / Vehicle Action |
| :--- | :--- | :--- | :--- |
| **GPIO 21** | CAN TX | Output | TWAI Transceiver TXD |
| **GPIO 22** | CAN RX | Input | TWAI Transceiver RXD |
| **GPIO 18** | RC CH1 | Input | Steering Right Gimbal Horizontal ($\pm 45.0^\circ$, raw $29550\dots 30450$) |
| **GPIO 19** | RC CH2 | Input | Brake Right Gimbal Vertical ($0\dots 27\text{ mm}$, raw $600\dots 1140$) |
| **GPIO 14** | RC CH3 | Input | Throttle Left Gimbal Vertical ($0\dots 100\%$ speed) |
| **GPIO 32** | RC CH4 | Input | Spare Left Gimbal Horizontal |
| **GPIO 13** | RC CH5 | Input | SWB 2-Position Switch (Ignition ON / OFF) |
| **GPIO 4**  | RC CH6 | Input | SWC 3-Position Switch (Gear R / N / D) |

---

## 3. Codebase Structure

```
rm-esp32/
├── CMakeLists.txt         # ESP-IDF CMake registration
├── platformio.ini         # PlatformIO configuration (-std=gnu++17)
├── sdkconfig.defaults     # FreeRTOS 1000 Hz, TWAI ISR in IRAM
├── new-architecture.md    # Full technical architecture specification
├── architecture.md        # Legacy RMT_14 reference documentation
├── src/
│   ├── config.h           # GPIO definitions, FlySky FS-i6 timing constants
│   ├── rc_decoder.h       # Pure signal decoding logic (free of hardware deps)
│   ├── rc_receiver.h/.cpp # 6-channel RMT pulse measurement driver
│   ├── can_driver.h/.cpp  # Handle-based TWAI driver with bus-off recovery
│   ├── freertos_hooks.cpp # Stack overflow & heap exhaustion hooks
│   └── main.cpp           # FreeRTOS tasks (50 Hz capture, 50 Hz CAN TX)
└── test/
    └── test_rm_receiver.cpp # Native unit test suite (49 assertions)
```

---

## 4. Building & Flashing

### PlatformIO CLI
```bash
cd rm-esp32
pio run -e vehicle -t upload
```

### Run Native Host Tests
```bash
g++ -static -std=c++17 -I .. -I ../shared -o test_rm_runner.exe test/test_rm_receiver.cpp
./test_rm_runner.exe
```

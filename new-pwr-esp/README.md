# new-pwr-esp

Standalone power supply keepalive firmware for **ESP32-WROOM-32UE** with **WCMCU-230** CAN transceiver.

## Overview

This firmware runs a dedicated 10 Hz / 100 ms keepalive loop transmitting the 29-bit extended CAN frame required by the DCDC power supply unit:

```cpp
// ── Power Supply / DCDC Keepalive (10 Hz / 100 ms, 29-bit Extended CAN 0x10262B27) ──
if (tick_10ms_count % 10 == 0) {
    can::Frame dcdc_cmd = can::Frame::extended_frame(0x10262B27u, 8);
    dcdc_cmd.data = {0xFD, 0x78, 0x00, 0xC8, 0x00, 0xFF, 0xFF, 0xFC};
    send_can_frame(dcdc_cmd);
}
```

## Hardware & Connections

- **Controller:** ESP32-WROOM-32UE (`esp32dev`)
- **Transceiver:** WCMCU-230 (SN65HVD230 3.3V CAN transceiver)
- **CAN Bitrate:** 500 kbps (default)
- **Pins:**
  - ESP32 silkscreen **`TX`** (GPIO 1) $\rightarrow$ WCMCU-230 **`CTX`**
  - ESP32 silkscreen **`RX`** (GPIO 3) $\rightarrow$ WCMCU-230 **`CRX`**
  - ESP32 **`3V3`** $\rightarrow$ WCMCU-230 **`3V3`**
  - ESP32 **`GND`** $\rightarrow$ WCMCU-230 **`GND`**

> Complete wiring reference is documented in [`wiring.md`](wiring.md).

## Building & Flashing

Using PlatformIO:

```bash
# Build firmware (default: 500 kbps on printed TX/RX pins)
pio run -d new-pwr-esp

# Upload to ESP32 (specify COM port if needed)
pio run -d new-pwr-esp -t upload --upload-port COMx

# Build bench self-test mode (transmits without waiting for external ACK)
pio run -d new-pwr-esp -e pwr_bench_self_test -t upload --upload-port COMx
```

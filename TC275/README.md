# AURIX TC275 CAN Motor Controller Firmware

Automotive-grade CAN motor controller firmware for the **Infineon AURIX TC275** (LQFP-176) microcontroller.

## Hardware

| Component | Part | Function |
|---|---|---|
| MCU | Infineon AURIX TC275 (LQFP-176) | Triple-core 200MHz TriCore CPU |
| CAN Transceiver | TLE9251VSJ | CAN FD, automotive-grade |
| DAC | DAC8562 + TXB0104 level shifter | 16-bit analog voltage reference (0–5V) |
| Channel Selector | SN74HC139 2-to-4 decoder | Selects 1 of 4 opto-isolated high-side motor channels |

## Firmware Architecture

```
CPU0 (Master)    — CAN receive + DAC control + 74HC139 GPIO
CPU1             — CAN watchdog (500ms timeout)
CPU2 (Safety)    — Hardware interlock: disables all outputs on timeout
```

## CAN Protocol

- **CAN ID:** `0x100`
- **Baud rate:** 500 kbps
- **Frame format (8 bytes):**

| Byte | Content |
|---|---|
| 0 | Channel select (0–3) |
| 1 | DAC value HIGH byte |
| 2 | DAC value LOW byte |
| 3–7 | Reserved |

**Example:** `{0x02, 0x80, 0x00}` → Channel 2 ON, DAC = 0x8000 = 2.5V

## Build Instructions

1. Open **AURIX Development Studio (ADS)**
2. `File → Import → Existing Projects into Workspace`
3. Select this folder
4. Build: `Project → Build All`
5. Flash: `Run → Debug Configurations → AURIX miniWiggler`

## Pin Map (TC275 LQFP-176)

| Signal | TC275 Port | Pin# |
|---|---|---|
| QSPI SCLK | P10.2 | 170 |
| QSPI MOSI | P10.3 | 171 |
| QSPI CS (SYNC) | P10.5 | 173 |
| CAN TX | P20.8 | 126 |
| CAN RX | P20.7 | 125 |
| CAN STBY | P20.6 | 124 |
| HC139 ENABLE | P00.7 | 18 |
| HC139 A | P00.6 | 17 |
| HC139 B | P00.5 | 16 |
| LED (Heartbeat) | P00.1 | 4 |

## License

Copyright © 2026. All rights reserved.

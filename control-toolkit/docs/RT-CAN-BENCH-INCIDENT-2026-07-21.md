# RT CAN bench incident - 2026-07-21

## Hardware confirmed

- RT serial: COM10; SYS serial: COM6.
- CANalyst-II mapping: CH0 = high, CH1 = low, both 500 kbit/s.
- RT high controller: MCP2515 with 8.000 MHz crystal.
- SPI: SCK GPIO15, MOSI GPIO16, MISO GPIO17, CS GPIO18, INT GPIO47.
- The old GPIO36-40 SPI map is invalid for ESP32-S3 N16R8 because GPIO33-37 are used by octal PSRAM.

## High bus failure

SPI initially returned CANSTAT 0x00 until MCP power/wiring was corrected. Afterward CANSTAT was 0x80 and register read/write tests passed, but the physical high bus stayed quiet in the API. RT reported TEC up to 248 and REC around 128-135.

Two firmware defects remained:

1. Firmware configured MCP INT on GPIO7 while the installed wiring used GPIO47. GPIO47 is not an ESP32-S3 strapping pin.
2. The 8 MHz / 500 kbit/s MCP timing used CNF1=0x00, CNF2=0x91, CNF3=0x08. CNF3 bit 3 is WAKFIL, not PHSEG2. This tuple has 7 TQ per bit and runs at approximately 571.4 kbit/s.

The corrected timing is CNF1=0x00, CNF2=0x91, CNF3=0x01: Sync=1 TQ, PropSeg=2 TQ, PHSEG1=3 TQ, PHSEG2=2 TQ, total 8 TQ at 250 ns = 500 kbit/s.

## High bus fix and verification

Changed:

- `rt-esp32/src/can_driver_mcp2515.h`: `kCnf3_500k` from `0x08` to `0x01`.
- `rt-esp32/src/config.h`: `kMcpIntGpio` from GPIO7 to GPIO47.

After rebuilding and flashing RT vehicle firmware:

- API high channel changed from quiet to active.
- CANalyst CH0 RX increased continuously.
- Physical RT_STATE_RPT `0x210` was live at approximately 9.96 Hz.
- Physical RT_HEARTBEAT `0x7FD` was received at 2 Hz.
- A zero-speed HOST_DRIVE_CMD was scheduled through `/api/v1/injections` at 100 ms. RT booted with INT=47, reported no high-CAN errors, and did not report command-stale while the injection was active. The injection job was canceled afterward.

Conclusion: RT high CAN transmit and receive pass through the physical bus and Control Toolkit API.

## Current low-bus test

RT vehicle low-bus output currently fails:

- CH1 remains active and receives SYS smoke `0x200` at approximately 5 Hz.
- RT low IDs `0x204` RT_DRIVE_CMD, `0x210` RT_STATE_RPT, and `0x7FD` RT_HEARTBEAT are stale by approximately 20 minutes.
- RT serial reports low bus-off, TEC=128, and repeated soft-recovery attempts.
- Earlier A/B testing exchanged the RT and SYS low transceiver modules. SYS became stable and RT stopped transmitting, so the fault followed the module.

Conclusion: RT cannot currently deliver low-level commands. Replace or reseat the low-CAN transceiver now connected to RT, then repeat the CH1 API test for `0x204`, `0x210`, and `0x7FD`.
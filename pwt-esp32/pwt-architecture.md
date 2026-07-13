# PWT ESP32-S3 - Powertrain CAN Node

## Status

The current PWT firmware operates **one 250 kbit/s powertrain CAN bus only**.
It sends the DC-DC converter command defined in `can_powertrain.yaml` and toggles an
external watchdog. It is not a gateway and has no low-level CAN interface.

ESP32-S3 has one built-in TWAI controller. It cannot connect the 500 kbit/s
low-level bus and the 250 kbit/s powertrain bus simultaneously without an
additional CAN controller/transceiver.

Do not connect a 250 kbit/s DC-DC converter to the 500 kbit/s low-level bus.

## Current Hardware

| Signal | GPIO | Direction | Connected To |
|---|---:|---|---|
| Powertrain CAN TX | 7 | Output | 3.3 V CAN transceiver TXD |
| Powertrain CAN RX | 6 | Input | 3.3 V CAN transceiver RXD |
| External watchdog | 21 | Output | TPS3850 WDI |

The CAN transceiver must be a 3.3 V logic device. Follow normal CAN topology:
twisted pair, a shared ground reference, and exactly two 120 ohm terminators at
the physical bus ends.

## Current Firmware Behavior

| Function | Behavior |
|---|---|
| CAN controller | Built-in TWAI0 on GPIO7 TX and GPIO6 RX at 250 kbit/s |
| DC-DC command | Extended ID `0x10262B27`, DLC 8, transmitted every 100 ms |
| DC-DC state | Local build owner; `PWT_DCDC_DEFAULT_ENABLED` selects boot state (default enabled) |
| Watchdog | GPIO21 toggled at 20 Hz |
| Low CAN forwarding | Not implemented |
| ESTOP forwarding | Not implemented |
| PWT heartbeat | Not implemented |
| Motor telemetry forwarding | Not implemented |

The nonexistent low-bus `0x012` command has been retired. PWT does not wait for
SYS input: loss of the powertrain connection is detected through consecutive
transmit failures and TWAI TEC/REC state. The first failure and each 50-failure
aggregate are logged, followed by one recovery event, so a 100 ms failure cannot
flood the log. The configured local state is transmitted every 100 ms when the
bus is available.

## Required Hardware For A Gateway

Choose one before implementing any low-to-powertrain forwarding:

1. Add an external CAN controller and 3.3 V transceiver for either the low or powertrain bus.
2. Use an MCU with two independently usable CAN controllers.
3. Remove the gateway requirement and keep PWT as a standalone powertrain node.

The bridge implementation must use two independent driver instances, validate
bitrate and frame formats per bus, rate-limit ESTOP forwarding, and prevent
forwarding loops. The two buses must never be physically joined.

## Build

```text
cd pwt-esp32
pio run -e vehicle
```

The PlatformIO target `esp32-s3-devkitc-1` resolves to the N8 board variant:
8 MB flash and no PSRAM. Select a different board definition only when the
physical module has been verified.

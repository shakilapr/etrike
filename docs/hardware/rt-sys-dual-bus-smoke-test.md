# RT/SYS Dual-Bus Smoke Test

This is the required pre-production CAN check for the current RT/SYS bench.
It proves that RT can transmit on both physical CAN buses at the same time and
that SYS is present on the low bus.

## Current bench identity

| Role | USB-UART | Adapter | USB serial |
|---|---|---|---|
| SYS | COM6 | CH343 | `5A7A060077` |
| RT | COM10 | CH343 | `5A7A059756` |

Control Toolkit physical mapping:

| Logical bus | CANalyst-II channel | Bitrate |
|---|---:|---:|
| High | CH0 | 500 kbit/s |
| Low | CH1 | 500 kbit/s |

## Wiring under test

RT low CAN uses the ESP32-S3 TWAI controller:

- GPIO5 -> low-CAN transceiver CTX/TXD
- GPIO4 <- low-CAN transceiver CRX/RXD

RT high CAN uses MCP2515 over SPI:

- GPIO15 -> SCK
- GPIO16 -> MOSI/SI
- GPIO17 <- MISO/SO
- GPIO18 -> CS
- GPIO47 <- INT
- MCP2515 crystal: 8 MHz

SYS low CAN uses GPIO5 TX and GPIO4 RX. All test traffic is standard CAN at
500 kbit/s.

Do not use the obsolete GPIO36/37/38/39/40 MCP2515 pin map on the ESP32-S3
N16R8; GPIO33-37 are occupied by octal PSRAM. GPIO47 is the verified MCP2515
interrupt input and is not a strapping pin.

## Firmware image

Source: `can-test/src/dual_bus_smoke.cpp`

| Board/image | Bus | CAN ID | Payload prefix | Rate |
|---|---|---:|---|---:|
| RT `dual_rt` | Low | `0x100` | `RT` | 5 Hz |
| RT `dual_rt` | High | `0x110` | `RH` | 5 Hz |
| SYS `dual_sys` | Low | `0x200` | `SY` | 5 Hz |

The RT high controller is configured with the verified 8 MHz / 500 kbit/s
MCP2515 timing: `CNF1=0x00`, `CNF2=0x91`, `CNF3=0x01`.

## Run procedure

1. Connect CANalyst-II CH0 to the high bus and CH1 to the low bus. Set both
   channels to 500 kbit/s and start the Control Toolkit in physical `bench_test`
   mode.
2. Flash the RT dual-bus image:

   ```powershell
   cd E:\work\etrike\can-test
   pio run -e dual_rt -t upload --upload-port COM10
   ```

3. Flash (or retain) the SYS low-bus smoke image:

   ```powershell
   cd E:\work\etrike\can-test
   pio run -e dual_sys -t upload --upload-port COM6
   ```

4. Inspect `GET /api/v1/state` and the channel counters in
   `GET /api/v1/status`.

## Pass criteria

- CH0/high is active and receives RT `0x110` at approximately 5 Hz.
- CH1/low is active and receives RT `0x100` and SYS `0x200`, each at
  approximately 5 Hz.
- Neither channel reports an adapter transport error, RX overflow, or invalid
  frame count.

The API may label the three smoke IDs `unknown_id` or `invalid`. That is an
expected catalog-decoding result, not a physical CAN failure: the frames must
be marked `source: physical` and remain live.

## Verified result

The test was run successfully with RT on COM10 and SYS on COM6:

| Frame | Observed rate | Result |
|---|---:|---|
| RT low `0x100` | ~4.96 Hz | PASS |
| RT high `0x110` | ~5.01 Hz | PASS |
| SYS low `0x200` | ~5.06 Hz | PASS |

Both CANalyst-II channels were active with zero RX overflow and zero adapter
transport errors.

## Restore production firmware

Only after the above pass, flash the vehicle images:

```powershell
cd E:\work\etrike\rt-esp32
pio run -e vehicle -t upload --upload-port COM10

cd E:\work\etrike\sys-esp32
pio run -e vehicle -t upload --upload-port COM6
```

Keep the RT production settings: low TWAI TX=GPIO5/RX=GPIO4, MCP2515
INT=GPIO47, and 8 MHz MCP2515 timing with `CNF3=0x01`.

## Production validation — 2026-07-22

After restoring the RT and SYS `vehicle` images, both low-CAN drivers were
migrated from deprecated `driver/twai.h` to the handle-based `esp_twai` API.
The reason is specific and observable: ESP-IDF 5.5's deprecated transmit path
always supplies an eight-byte HAL buffer, and the SJA1000 HAL substitutes that
buffer length when the requested DLC is zero. This changed `SAFETY_ESTOP`
`0x001` from DLC 0 to DLC 8 on the wire.

Production images were built and hash-verified while flashing:

| Image | Port | MAC | Result |
|---|---|---|---|
| RT `vehicle` | COM10 | `80:b5:4e:c7:d0:34` | PASS |
| SYS `vehicle` | COM6 | `80:b5:4e:c5:b9:4c` | PASS |

The physical Control Toolkit API then showed:

- Low and High `SAFETY_ESTOP 0x001`: `validation_status=ok`.
- Raw history on both buses: `dlc=0`, empty `data_hex`, `source=physical`.
- RT heartbeat live on Low and High; SYS heartbeat live on Low.
- SYS diagnostics live with TEC=0 and REC=0.
- Three samples two seconds apart retained the same healthy result.

ESTOP remaining active is expected while the hardware bypass/input is not
released. The defect fixed here was the invalid frame shape, not the asserted
safety state.

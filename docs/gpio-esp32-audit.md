# GPIO and ESP Hardware Audit

Audited 2026-07-13 against the executable pin maps in `rt-esp32/src/config.h`,
`sys-esp32/src/config.h`, and `pwt-esp32/src/config.h`. This file records
deployment blockers; source configuration takes precedence over older tables.

## Blocking Before Vehicle Power

| Severity | Finding | Evidence | Required action |
|---|---|---|---|
| Critical | The documented NC ESTOP circuit is electrically inverted. A pull-up with an NC contact to GND reads LOW while healthy, which firmware treats as ESTOP; a broken wire reads HIGH and clears ESTOP. | `docs/wiring.md` previously specified both NC-to-GND and pull-up; `task_safety` treats LOW as active. | Wire the NC contact from `3.3 V` to SYS GPIO1 and MTR's ESTOP input. Fit a 10 kOhm external pull-down at each MCU input. A pressed switch or open circuit then reads LOW and triggers ESTOP. This is now the firmware pull direction. |
| Critical | The standard MCP2515 plus TJA1050 module is powered at 5 V, so MCP2515 MISO and INT can drive 5 V into ESP32-S3 GPIO17 and GPIO47. ESP32-S3 GPIOs are not 5 V tolerant. | `docs/wiring.md` specifies 5 V module power; RT uses GPIO15/16/17/18/47. | Use bidirectional level translation for every SPI/INT line, or use a 3.3 V MCP2515 plus 3.3 V CAN transceiver module. Do not directly connect a 5 V module to the ESP32-S3. |
| Critical | MTR cannot be used as a vehicle actuator ECU. Its CubeMX clock/GPIO/I2C/ADC/CAN initialization is commented out, its ESTOP reader always returns false, and CAN pin/transceiver mapping is TBD. | `mtr-stm32/src/main.cpp:90-93`, `mtr-stm32/src/main.cpp:419-426`, `docs/wiring.md:339-351`. | Implement and hardware-test the CubeMX configuration, a direct ESTOP GPIO/interrupt, and CAN transceiver pins before connecting the DAC or gear relay outputs. |
| Critical | PWT cannot bridge low CAN and powertrain CAN on ESP32-S3: the chip has one TWAI controller, while the PWT design requires two. Current firmware only operates the 250 kbit/s bus. | `pwt-esp32/src/main.cpp`, `pwt-esp32/src/can_driver.h`, `pwt-esp32/pwt-architecture.md`. | Add an external CAN controller/transceiver for one bus, use a two-TWAI MCU, or remove PWT gateway functionality. Do not connect a 250 kbit/s DC-DC node to the 500 kbit/s low bus. |
| High | ~~The SYS bench-only throttle input is impossible as currently mapped.~~ | `sys-esp32/src/throttle_input.h`, `sys-esp32/src/config.h`. | **Resolved:** The legacy bench throttle code (`SYS_OWNS_MOTOR`) has been permanently removed. |
| High | A 5 V MCP4725 cannot safely share I2C lines with a 3.3 V ESP32-S3 through 5 V pull-ups; 3.3 V highs may also fail its 0.7 x VDD VIH specification. | SYS/MTR DAC configuration and MCP4725 VCC=5 V wiring. | Pull SDA/SCL up to 3.3 V only if confirmed by the exact DAC's VIH specification, otherwise fit a bidirectional I2C level shifter. Never expose ESP GPIOs to 5 V. |
| High | The PlatformIO board target is `esp32-s3-devkitc-1`, which resolves to N8: 8 MB quad flash and no PSRAM. Multiple architecture documents claim 8 MB PSRAM. | SYS/PWT hardware build output; `platformio.ini`. | Select the actual board/module definition and correct the documentation. Do not allocate or wire PSRAM-dependent designs based on the current target. |

## Corrected Firmware Behavior

- SYS configures connected buttons and switches with defined pulls and latches implemented output drivers LOW before enabling them.
- SYS GPIO1 uses a pull-down consistent with an NC, active-low, open-circuit-is-ESTOP input. The external pull-down remains mandatory for a vehicle harness.
- The SYS bench DAC receives an explicit zero write at initialization.
- MTR's MCP4725 address is `0x61` for A0 tied to VCC, matching the harness specification; its init path also writes zero.

## Pin Map Notes

- RT high CAN firmware mapping is SCK=GPIO15, MOSI=GPIO16, MISO=GPIO17, CS=GPIO18, INT=GPIO47. Older documents that specify GPIO36-41 or reuse one GPIO for two SPI signals are invalid.
- RT/SYS TWAI GPIO5 TX and GPIO4 RX are valid ESP32-S3 matrix pins. The SN65HVD230 must run from 3.3 V.
- SYS GPIO39-41 are usable GPIOs but overlap conventional JTAG signals. They cannot be used with external JTAG debugging unless the debugger or those functions are remapped or disconnected.
- GPIO0, GPIO3, GPIO45, and GPIO46 are ESP32-S3 strapping pins. Do not move safety or actuator wiring to them without a boot-level analysis.
- Relay coils, lamps, and 72 V interfaces require transistor/opto drivers, flyback suppression, suitable fusing, and a common low-voltage ground where isolation is not deliberate. No ESP32 GPIO can drive those loads directly.

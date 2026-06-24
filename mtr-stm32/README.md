# MTR STM32 — Motor Controller (EGAS Level 1)

Dedicated STM32F103 motor actuation board. Per ISO 26262 EGAS 3-level concept, MTR owns all motor-related I/O for safety isolation.

See [`architecture.md`](../architecture.md) for full system design.

## Responsibilities

- MCP4725 I2C DAC (0–5V throttle output)
- ADC throttle grip position sensing
- TLP281 optoisolator gear sense inputs (72V)
- Relay gear outputs (72V)
- ESTOP button GPIO (Level 3 direct hardware kill)
- CAN bus (low-level, bxCAN1)

## Build

```bash
cd mtr-stm32
pio run              # build
pio run -t upload    # flash (ST-Link)
pio device monitor   # serial console
```

Target: `genericSTM32F103C8` | Framework: `stm32cube` | FreeRTOS

## Pin Map

| Function | Pin | STM32 |
|----------|-----|-------|
| CAN1 RX  | PB8 | GPIO 24 |
| CAN1 TX  | PB9 | GPIO 25 |
| ADC1 IN0 | PA0 | ADC ch0 |
| I2C1 SDA | PB7 | GPIO 23 |
| I2C1 SCL | PB6 | GPIO 22 |
| ESTOP    | PA1 | GPIO 1 |
| Gear D sense | PB0 | GPIO 16 |
| Gear S sense | PB1 | GPIO 17 |
| Gear R sense | PB2 | GPIO 18 |
| Gear D out   | PA3 | GPIO 3 |
| Gear S out   | PA4 | GPIO 4 |
| Gear R out   | PA5 | GPIO 5 |

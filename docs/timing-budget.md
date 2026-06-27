# Timing Budget

Task execution times are estimates based on the ESP32-S3 at 240 MHz and STM32 at 72 MHz. CAN bus load calculated at 500 kbit/s.

## FreeRTOS Task Timing — RT ESP32-S3

| Task | Prio | Rate | WCET (est.) | CPU % | Deadline |
|------|------|------|-------------|-------|----------|
| rx_low | 5 | event-driven | ~50µs (TWAI read) | <1% | — |
| rx_high | 5 | event-driven | ~80µs (SPI burst 13B) | <1% | — |
| dispatch | 4 | event-driven | ~100µs (route_frame) | <1% | — |
| control | 4 | 100 Hz (10ms) | ~500µs (physics+safety) | 5% | 10ms |
| tx_low | 3 | 200 Hz polling | ~200µs (frame build+send) | 4% | 5ms |
| tx_high | 3 | 100 Hz polling | ~150µs (frame build+SPI) | 1.5% | 10ms |
| watchdog | 1 | 10 Hz (100ms) | ~50µs | <1% | 100ms |
| heartbeat | 1 | 2 Hz (500ms) | ~50µs | <1% | 500ms |

**Total CPU utilization (RT): ~12%** — well within ESP32-S3 capacity.

## FreeRTOS Task Timing — SYS ESP32-S3

| Task | Prio | Rate | WCET (est.) | CPU % | Deadline |
|------|------|------|-------------|-------|----------|
| can_rx | 5 | event-driven | ~30µs (TWAI read) | <1% | — |
| safety | 5 | 20 Hz (50ms) | ~200µs (checks) | <1% | 50ms |
| dispatch | 4 | event-driven | ~150µs (switch 10 IDs) | <1% | — |
| motor | 4 | 100 Hz (10ms) | ~100µs (DAC write) | 1% | 10ms |
| mode | 4 | 10 Hz (100ms) | ~50µs | <1% | 100ms |
| throttle | 3 | 100 Hz (10ms) | ~50µs (ADC read) | <1% | 10ms |
| brake | 3 | 50 Hz (20ms) | ~300µs (SEB build+checksum) | 1.5% | 20ms |
| gear | 3 | 50 Hz (20ms) | ~50µs (GPIO read) | <1% | 20ms |
| lights | 3 | 20 Hz (50ms) | ~50µs | <1% | 50ms |
| dcdc | 3 | 5 Hz (200ms) | ~50µs | <1% | 200ms |
| can_tx | 2 | 200 Hz polling | ~100µs (frame build) | 2% | 5ms |
| indicator | 2 | 5 Hz | ~50µs | <1% | 200ms |
| power | 2 | 5 Hz | ~50µs | <1% | 200ms |
| diag | 1 | 1 Hz (1s) | ~100µs | <1% | 1s |
| hb | 1 | 10 Hz (100ms) | ~50µs | <1% | 100ms |

**Total CPU utilization (SYS): ~6%** — well within capacity.

## CAN Bus Load

### Low Bus (500 kbit/s)

| ID | Name | DLC | Rate (Hz) | Frames/s | Bus Time (µs) | Load |
|----|------|-----|-----------|---------|---------------|------|
| 0x011 | SYS_SAFETY_STS | 3 | 5 | 5 | 720 | 0.36% |
| 0x012 | SYS_DCDC_CMD | 1 | 5 | 5 | 540 | 0.27% |
| 0x110 | SYS_MODE_CMD | 1 | event | — | — | — |
| 0x120 | SYS_THROTTLE_STS | 2 | 100 | 100 | 12,600 | 6.3% |
| 0x169 | VCU_SES_REQ | 8 | 50 | 50 | 11,800 | 5.9% |
| 0x201 | SES_STATUS | 8 | 100 | 100 | 23,600 | 11.8% |
| 0x202 | SES_ErrInfo | 8 | 10 | 10 | 2,360 | 1.2% |
| 0x203 | SES_Version | 8 | 1 | 1 | 236 | 0.1% |
| 0x204 | RT_DRIVE_CMD | 5 | 100 | 100 | 18,200 | 9.1% |
| 0x205 | RT_BRAKE_CMD | 4 | 50 | 50 | 8,250 | 4.1% |
| 0x206 | MTR_MOTOR_FBK | 4 | 50 | 50 | 8,250 | 4.1% |
| 0x302 | HOST_LIGHT_CMD | 1 | event | — | — | — |
| 0x600 | SYS_DIAG_RPT | 8 | 1 | 1 | 236 | 0.1% |
| 0x6FA | SES_Test | 8 | 100 | 100 | 23,600 | 11.8% |
| 0x6FB | SEB_Test | 8 | 100 | 100 | 23,600 | 11.8% |
| 0x721 | SEB_STATUS | 8 | 100 | 100 | 23,600 | 11.8% |
| 0x731 | SEB_ErrInfo | 8 | 10 | 10 | 2,360 | 1.2% |
| 0x741 | SEB_Version | 8 | 1 | 1 | 236 | 0.1% |
| 0x7B9 | VCU_SEB_REQ | 8 | 50 | 50 | 11,800 | 5.9% |
| 0x7FD | RT_HEARTBEAT | 1 | 2 | 2 | 216 | 0.1% |
| 0x7FE | SYS_HEARTBEAT | 1 | 10 | 10 | 1,080 | 0.5% |

**Low bus total: ~86% of 500 kbit/s** (primarily from 100 Hz SYNTREE telemetry frames). 0x6FA and 0x6FB alone contribute ~24%. These are lowest-priority diagnostic frames — under bus saturation they would be the first to drop.

### High Bus (500 kbit/s)

| ID | Name | DLC | Rate (Hz) | Frames/s | Load |
|----|------|-----|-----------|---------|------|
| 0x001 | SAFETY_ESTOP | 0 | event | — | — |
| 0x011 | SYS_SAFETY_STS | 3 | 5 | 5 | 0.4% |
| 0x120 | SYS_THROTTLE_STS | 2 | 100 | 100 | 6.3% |
| 0x206 | MTR_MOTOR_FBK | 4 | 50 | 50 | 4.1% |
| 0x210 | RT_STATE_RPT | 4 | 10 | 10 | 0.8% |
| 0x220 | RT_PID_RPT | 6 | 10 | 10 | 1.0% |
| 0x300 | HOST_DRIVE_CMD | 8 | 100 | 100 | 13.0% |
| 0x301 | HOST_BRAKE_REQ | 4 | event | — | — |
| 0x302 | HOST_LIGHT_CMD | 1 | event | — | — |
| 0x310 | STEER_DIAG | 8 | 10 | 10 | 1.3% |
| 0x311 | BRAKE_DIAG | 8 | 10 | 10 | 1.3% |
| 0x400 | HOST_OBSTACLE_DIST | 4 | 10 | 10 | 0.8% |
| 0x600 | SYS_DIAG_RPT | 8 | 1 | 1 | 0.1% |
| 0x7FC | HOST_HEARTBEAT | 1 | 2 | 2 | 0.1% |
| 0x7FD | RT_HEARTBEAT | 1 | 2 | 2 | 0.1% |

**High bus total: ~29% of 500 kbit/s** — well within limits.

## ESTOP Latency Budget

| Stage | Latency | Notes |
|-------|---------|-------|
| Button press → GPIO edge | <1ms | Mechanical bounce ~5ms, debounced in ISR |
| GPIO edge → ESTOP flag set | <100µs | ISR writes atomic |
| ESTOP flag → CAN frame queued | <10ms | t_control runs at 100 Hz |
| CAN frame on bus | <260µs | DLC=0 frame, highest priority (ID 0x001) |
| CAN frame → MTR receives | <2ms | MTR CAN polling at 2ms intervals |
| MTR ESTOP → DAC=0V | <10ms | MTR task_control at 100 Hz |
| DAC=0V → motor throttle off | <1ms | Analog RC filter on MCP4725 output |

**Total worst-case ESTOP latency: ~28ms** (button→motor kill).
Hardware ESTOP GPIO path (Level 3): **~5ms** (direct wire, no CAN dependency).

## Power Budget

| Node | MCU | CAN | Actuators | Total (est.) |
|------|-----|-----|-----------|-------------|
| RT ESP32-S3 | 200mA @ 3.3V | 100mA (MCP2515+TJA1050) | — | ~1.0W |
| SYS ESP32-S3 | 200mA @ 3.3V | 100mA (WCMCU-230) | 500mA (relays, lights, DCDC enable) | ~2.6W |
| MTR STM32 | 50mA @ 3.3V | 50mA (bxCAN+transceiver) | 10mA (MCP4725 DAC) | ~0.4W |
| PWT ESP32-S3 | 200mA @ 3.3V | 100mA (DC-DC bus) | — | ~1.0W |

**Total system: ~5W** (excluding Jetson Orin, SYNTREE actuators, motor controller).

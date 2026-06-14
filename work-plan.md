# E-Trike Coding Plan

44 phases. Each phase delivers compilable, testable code. Test harnesses are host-based (g++) where hardware isn't needed, ESP32-based where it is.

---

## Phase 1: Shared CAN protocol header
**Files:** `shared/can_protocol.h`
- All CAN ID constants (`#define CAN_ID_SAFETY_ESTOP 0x001` …)
- All payload structs with `__attribute__((packed))`
- All enums: `SysMode`, `Gear`, `SteerState`, `BrakeState`
- Big-endian serialize/deserialize helpers for i16/i32/u32
- **Test:** `g++ -std=c++17 -I. -c` — compiles. Struct sizes match DLC.

## Phase 2: RT config header
**Files:** `rt-esp32/src/config.h`
- GPIO pins, timing constants, PID params (stubbed), CAN IDs from shared header
- **Test:** Compiles against ESP-IDF. No undefined symbols.

## Phase 3: SYS config header
**Files:** `sys-esp32/src/config.h`
- GPIO pins, timing constants, CAN IDs, brake stroke values
- **Test:** Compiles against ESP-IDF.

## Phase 4: SYS CAN driver
**Files:** `sys-esp32/src/can_driver.cpp`, `can_driver.h`
- `can_init()`: TWAI general config, 500 kbit/s, normal mode, acceptance filter (accept all)
- `can_send(id, dlc, data)`: `twai_transmit()` wrapper with 0 timeout
- `can_receive(frame, timeout_ms)`: `twai_receive()` wrapper
- **Test:** ESP32 sends frame → logic analyzer verifies on CAN_H/CAN_L. Loopback with another ESP32.

## Phase 5: SYS main.cpp skeleton
**Files:** `sys-esp32/src/main.cpp`
- `app_main()`: init CAN, create all 15 tasks (empty stubs), start scheduler
- Each task: `while(1) { vTaskDelay(pdMS_TO_TICKS(period_ms)); }` — compiles, links, runs
- **Test:** ESP32 boots. Serial shows "Ready". 15 tasks appear in `vTaskList()`.

## Phase 6: SYS heartbeat task
**Files:** `sys-esp32/src/heartbeat.cpp`, `heartbeat.h`
- `heartbeat_task`: every 500ms, send `0x7FE` with `alive_ctr++ & 0xFF`, DLC=1
- **Test:** Logic analyzer shows frame every 500ms ±10ms. Counter increments correctly.

## Phase 7: SYS mode manager — button debounce
**Files:** `sys-esp32/src/mode_manager.cpp`, `mode_manager.h`
- `mode_manager_init()`: configure GPIO11 (MODE), GPIO32 (START) as inputs with pull-ups
- `mode_task` at 10 Hz: read both GPIOs, falling-edge detect, 500ms debounce
- `mode_get()`, `mode_set(SysMode m)`: ESTOP override enforces can't-leave-ESTOP via MODE button
- Internal `std::atomic<int>` for `g_mode`
- **Test (host):** `g++ test_mode.cpp mode_manager.cpp` — mock GPIO reads, verify state transitions. ESTOP→MODE button ignored. START→MANUAL works.

## Phase 8: SYS safety monitor
**Files:** `sys-esp32/src/safety_monitor.cpp`, `safety_monitor.h`
- `safety_init()`: configure GPIO1 (ESTOP, NC, pull-up), GPIO2 (brake lever, pull-up)
- `safety_task` at 20 Hz: read GPIOs, check heartbeat timeout (startup grace 3s, then 1000ms)
- `safety_estop_active()`, `safety_brake_lever_pressed()`, `safety_heartbeat_ok()`
- **Test (host):** Inject GPIO states, verify ESTOP detection within 50ms. Inject heartbeat timestamps, verify timeout detection. Startup grace returns true for 3s.

## Phase 9: SYS mode state machine — integration
**Files:** update `mode_manager.cpp`, `safety_monitor.cpp`, `main.cpp`
- Wire mode_manager + safety_monitor together: ESTOP button → `mode_set(Estop)`
- MODE button toggles MANUAL↔AUTO (ignored in ESTOP)
- START button: ESTOP→MANUAL, no effect otherwise
- CAN `0x110 SYS_MODE_CMD` sent on every mode change
- **Test (ESP32):** Press ESTOP → mode=Estop, `0x110` shows mode=2. Press START → mode=Manual. Press MODE → mode=Auto. Press MODE again → mode=Manual.

## Phase 10: SYS throttle ADC
**Files:** `sys-esp32/src/throttle_input.cpp`, `throttle_input.h`
- `throttle_init()`: ADC1_CH5, 12-bit, 0–3.3V range (voltage divider 5V→3.3V assumed external)
- `throttle_task` at 100 Hz: `adc1_get_raw()`, dead zone (200), map to mm/s (0–3000)
- Store to `std::atomic<int16_t> g_throttle_speed_mmps`
- CAN `0x120 SYS_THROTTLE_STS` sent every tick
- **Test (host):** Mock `adc1_get_raw()` return values. Verify dead zone, linear mapping, CAN payload.

## Phase 11: SYS MCP4725 DAC driver
**Files:** `sys-esp32/src/mcp4725_dac.cpp`, `mcp4725_dac.h`
- `dac_init()`: I2C master init (GPIO15=SDA, GPIO16=SCL), verify device at addr 0x60
- `dac_write(uint16_t value)`: 12-bit value (0–4095) → 0–5V output
- `dac_set_voltage_mv(uint16_t mv)`: convenience wrapper with clamping
- **Test (ESP32):** Write 0 → measure 0V on MCP4725 VOUT. Write 2048 → ~2.5V. Write 4095 → ~5V.

## Phase 12: SYS motor driver — combined throttle + gear
**Files:** `sys-esp32/src/motor_driver.cpp`, `motor_driver.h`
- `motor_init()`: calls dac_init(), configures gear output GPIOs
- `motor_task` at 100 Hz: reads `setpoint_queue` (AUTO) or `g_throttle_speed_mmps` (MANUAL)
- MANUAL: ADC speed → MCP4725 (pass-through)
- AUTO: `setpoint.speed` → `abs(speed)/3000 × 4095` → MCP4725
- ESTOP: MCP4725 = 0
- **Test (host):** Mock ADC, mock queue. Verify DAC values per mode. Mock ESTOP → DAC=0.

## Phase 13: SYS gear control
**Files:** `sys-esp32/src/gear_control.cpp`, `gear_control.h`
- `gear_init()`: GPIO12-14 IN (TLP281), GPIO33-35 OUT (relays)
- `gear_task` at 50 Hz:
  - MANUAL: read TLP281 GPIOs → mirror to relay GPIOs
  - AUTO: use gear from `ActuatorSetpoint` → energize correct relay
  - ESTOP: all relays OFF
  - Gear sense conflict (multiple HIGH) → treat as N (fail-safe)
- **Test (host):** Inject gear sense patterns, verify relay outputs. AUTO gear from queue. ESTOP → all LOW.

## Phase 14: SYS CAN receive — dispatch task
**Files:** `sys-esp32/src/can_rx_router.cpp`, `can_rx_router.h`
- `dispatch_task`: blocks on `can_rx_queue` (16 deep), routes by CAN ID
- `0x202 RT_DRIVE_CMD` → `xQueueOverwrite(setpoint_queue)`
- `0x203 RT_BRAKE_CMD` → `g_brake_pressure_kpa` (atomic)
- `0x302 HOST_LIGHT_CMD` → `g_light_state` (atomic)
- `0x001 SAFETY_ESTOP` → `mode_set(Estop)`
- `0x721 SEB_STATUS` → brake feedback handler
- `0x7FD RT_HEARTBEAT` → `safety_feed_heartbeat_rt()`
- **Test (host):** Inject CAN frames into queue. Verify routing to correct destination. ESTOP frame → mode changes.

## Phase 15: SYS CAN send — telemetry TX task
**Files:** `sys-esp32/src/can_tx.cpp`, `can_tx.h`
- `can_tx_task` at 5 Hz: assemble and send `0x011 SYS_SAFETY_STS`
- `throttle_task` already sends `0x120` (phase 10)
- `diag_task` at 1 Hz: assemble and send `0x600 SYS_DIAG_RPT`
- `hb_task` already sends `0x7FE` (phase 6)
- **Test (ESP32):** All four CAN IDs appear on bus at correct rates. Payloads match expected values.

## Phase 16: SYS DC-DC control
**Files:** `sys-esp32/src/dcdc_control.cpp`, `dcdc_control.h`
- `dcdc_task` at 5 Hz: send `0x012 SYS_DCDC_CMD`
- ESTOP → enable=0, all other modes → enable=1
- Send only on state change (not every tick)
- **Test (host):** Cycle modes. Verify CAN frames only on transition. ESTOP → 0, MANUAL → 1.

## Phase 17: SYS brake control — SEB protocol
**Files:** `sys-esp32/src/brake_control.cpp`, `brake_control.h`
- `BrakeState` state machine: BOOT_WAIT (500ms) → LISTEN_SYNC (await `0x721`) → ACTIVE
- `brake_task` at 50 Hz:
  - Build `0x720 VCU_SEB_REQ` with control enables, mode, stroke value
  - Rolling counter increment 0→15 every frame
  - Checksum = XOR(bytes[0..6]) ^ 0xFF
  - MANUAL: lever pressed → 15mm, released → 0mm
  - AUTO: `g_brake_pressure_kpa > 0` → Pressure Mode (map kPa→MPa), else Stroke Mode with lever override
  - ESTOP: max stroke (27mm)
- **Test (host):** Build command frames, verify checksum algorithm. Roll cnt wraps at 15. Verify stroke→raw conversion: 0mm→600, 15mm→900, 27mm→1140.

## Phase 18: SYS signal lights — outputs + OR logic
**Files:** `sys-esp32/src/light_control.cpp`, `light_control.h`
- `lights_init()`: GPIO18/19/21/22 OUT (relay lamps), GPIO3/6/7 IN (handlebar switches)
- `lights_task` at 20 Hz:
  - Left/right turn blink: 500ms on, 500ms off, toggle behavior
  - Both pressed → hazard flashers (sync blink)
  - Headlight: toggle on each press (MANUAL) or follow `g_light_state.headlight` (AUTO)
  - Brake light OR logic: `lever_pressed OR mode==Estop OR g_light_state.brake_light`
  - ESTOP: brake light ON, all others OFF
- **Test (host):** Inject switch states + mode + light_state. Verify GPIO outputs match expected.

## Phase 19: SYS mode indicator bulbs + 12V relay
**Files:** `sys-esp32/src/indicator_control.cpp`, `indicator_control.h`, `power_control.cpp`, `power_control.h`
- `indicator_task` at 5 Hz: AUTO→GPIO25, MANUAL→GPIO26, both OFF in ESTOP
- `power_task` at 5 Hz: GPIO27 ON in MANUAL/AUTO, OFF in ESTOP
- **Test (host):** Verify outputs per mode. ESTOP → both bulbs OFF, 12V relay OFF.

## Phase 20: SYS external watchdog
**Files:** `sys-esp32/src/watchdog.cpp`, `watchdog.h`
- `safety_task` toggles GPIO23 (WDT) every iteration (50ms)
- **Test (ESP32):** Stop `safety_task` → verify TPS3850 pulls MR LOW → ESP32 resets within 100ms.

## Phase 21: SYS startup sequence
**Files:** update `sys-esp32/src/main.cpp`
- Wire all 15 tasks with correct init order:
  1. CAN driver 2. mode_manager 3. safety_monitor 4. throttle+MCP4725 5. gear 6. lights 7. power 8. brake (boot state machine) 9. dcdc 10. queues 11. tasks
- DCDC and 12V relay enabled only if not ESTOP at boot
- WDT armed after safety_task starts
- `ESP_LOGI("Ready")`
- **Test (ESP32):** Cold boot. All tasks running. `vTaskList()` shows correct priorities. No crash for 30 minutes.

## Phase 22: SYS host test — full simulation
**Files:** `sys-esp32/test/test_sys_full.cpp`
- Host-based integration test: mock all hardware (GPIO, ADC, TWAI, I2C)
- Inject: button presses, throttle voltage, CAN frames, heartbeat timestamps
- Verify: CAN frames out, GPIO outputs, mode transitions, brake stroke values
- **Test:** Run through complete scenarios: boot→MANUAL→AUTO→ESTOP→START→MANUAL. All outputs correct at each step.

---

## Phase 23: RT CAN driver — TWAI (low-level)
**Files:** `rt-esp32/src/can_driver_twai.cpp`, `can_driver_twai.h`
- `can_low_init()`: TWAI 500 kbit/s, GPIO5/4, normal mode
- `can_low_send()`, `can_low_receive()`: same API as SYS CAN driver
- **Test:** Loopback test with SYS ESP32 on low-level CAN bus.

## Phase 24: RT CAN driver — MCP2515 (high-level)
**Files:** `rt-esp32/src/can_driver_mcp2515.cpp`, `can_driver_mcp2515.h`
- SPI init: GPIO36(SCK)/37(MOSI)/38(MISO)/39(CS)/40(INT), 10 MHz
- MCP2515 init: reset, set 500 kbit/s, normal mode, RX buffer config, interrupt enable
- `can_high_send()`, `can_high_receive()`: SPI-based, same API shape as TWAI
- **Test:** Loopback test with Jetson CAN interface on high-level bus.

## Phase 25: RT main.cpp skeleton
**Files:** `rt-esp32/src/main.cpp`
- `app_main()`: init CAN low, CAN high, create 9 task stubs, start scheduler
- **Test:** ESP32 boots. Both CAN interfaces initialized. 9 tasks in `vTaskList()`.

## Phase 26: RT heartbeat task
**Files:** `rt-esp32/src/heartbeat.cpp`, `heartbeat.h`
- `heartbeat_task` at 2 Hz: send `0x7FD RT_HEARTBEAT` on both buses with `alive_ctr++`
- Receive and track: `0x7FE` (SYS, low), `0x7FC` (Jetson, high)
- Alive counter validation: frozen counter → treat as missed heartbeat
- **Test:** Verify HB frames on both buses. Freeze SYS counter → RT detects timeout at 1000ms.

## Phase 27: RT CAN gateway — dispatch + forwarding
**Files:** `rt-esp32/src/can_rx_router.cpp`, `can_rx_router.h`
- `dispatch_task`: blocks on `can_rx_low_queue` + `can_rx_high_queue` (16 each)
- Category 1 (transparent forward):
  - Low→High: `0x001`, `0x011`, `0x120`, `0x600` → push to `gw_tx_high_queue`
  - High→Low: `0x001`, `0x302` → push to `gw_tx_low_queue`
- Category 2 (consume):
  - `0x300` (high) → `xQueueOverwrite(cmd_queue)`
  - `0x301` (high) → `g_brake_request_kpa` (atomic)
- Category 3 (local): `0x110`, `0x201`, `0x721`, `0x7FD`, `0x7FE` — consume only
- `0x001` any bus: `mode_set(Estop)` + forward to other bus
- **Test (host):** Inject frames on both buses. Verify routing, forwarding, queue contents.

## Phase 28: RT CAN TX tasks
**Files:** `rt-esp32/src/can_tx_low.cpp`, `can_tx_high.cpp`
- `can_tx_low_task`: serialize `setpoint_queue` → `0x202 RT_DRIVE_CMD` at 100 Hz, `0x200 VCU_SES_REQ` at 50 Hz (steer SM gated), `0x302` from gateway queue
- `can_tx_high_task`: serialize telemetry → `0x011`, `0x120`, `0x210`, `0x220`, `0x400`, `0x600`
- Gateway queues (8 deep, drop-if-full)
- **Test (host):** Verify correct CAN IDs, rates, payloads on each bus.

## Phase 29: RT tricycle kinematics
**Files:** `rt-esp32/src/physics_model.cpp`, `physics_model.h`
- `physics_resolve(DriveCmd)` → `ResolvedSetpoint`
- Inverse bicycle: `δ = atan2(L·ω, |v|)`, L=1500mm
- Speed clamp: [-500, 3000] mm/s
- Steering clamp: ±45° with dynamic limit (speed-dependent, stub for now)
- Low-speed threshold: 50 mm/s → hold last angle, decay ×0.8
- Gear derivation: v>0→D, v=0→N, v<0→R
- **Test (host):** `g++ test_physics.cpp physics_model.cpp`. Known inputs → expected outputs. Edge cases: v=0, extreme yaw, negative speed.

## Phase 30: RT steering control — boot sync + CAN TX
**Files:** `rt-esp32/src/steering_control.cpp`, `steering_control.h`
- `SteerState` machine: BOOT_WAIT (500ms) → LISTEN_SYNC (await `0x201`) → ACTIVE
- LISTEN_SYNC: read `SES_StrAngle` from `0x201`, set `active_target = current_physical`
- ACTIVE: transmit `0x200` at 50 Hz with angle + slew rate + rolling counter + checksum
- Unit conversion: internal mdeg ↔ SYNTREE decideg (÷100)
- **Test (host):** Inject `0x201` with angle=+15°. Verify first `0x200` commands +15°. Then follow Jetson targets. Verify roll cnt + checksum algorithm.

## Phase 31: RT steering safety mechanisms
**Files:** update `steering_control.cpp`
- Software hard-stops: clamp commanded to ±40° regardless of Jetson
- Dynamic angle clamp: max angle = f(speed). 2km/h→40°, 25km/h→5°, linear between
- Following error: `abs(cmd − SES_StrAngle) > 5° for 300ms → mode_set(Estop)`
- Alignment check: `SES_INF_Angle_Status == 1` before AUTO engages
- **Test (host):** Inject extreme Jetson targets → verify clamping. Inject feedback mismatches → verify ESTOP timing.

## Phase 32: RT obstacle sensor
**Files:** `rt-esp32/src/obstacle_sensor.cpp`, `obstacle_sensor.h`
- `obstacle_init()`: GPIO7 (TRIG, OUT), GPIO8 (ECHO, IN)
- `obstacle_task` at 10 Hz: 10µs trigger pulse → measure echo pulse width → convert to mm
- Timeout: echo >30ms → UINT32_MAX (no reading)
- Store to `g_obstacle_mm` (atomic)
- Send `0x400 RT_OBSTACLE_RPT`
- **Test (host):** Mock echo pulse widths → verify distance calculation. Timeout → UINT32_MAX.

## Phase 33: RT obstacle speed limiting
**Files:** update `rt-esp32/src/control_logic.cpp` (or inline in control_task)
- `obstacle_limit(target_speed, obstacle_mm)`: 300mm→0, 3000mm→full, linear between
- Applied in `control_task` before `0x202` TX
- **Test (host):** Known distance + speed combinations → verify clamped output.

## Phase 34: RT brake arbitration
**Files:** `rt-esp32/src/brake_arbitration.cpp`, `brake_arbitration.h`
- `brake_arbitrate(rt_obstacle_kpa, jetson_request_kpa)` → `max(r, j)`
- RT obstacle: distance < 300mm → hard brake, 300–1000mm → proportional
- Jetson request: `g_brake_request_kpa` from `0x301`
- Result → `0x203 RT_BRAKE_CMD` at 50 Hz on low bus
- **Test (host):** Various obstacle distances + Jetson requests. Verify max-select. Obstacle=200mm → outputs high kPa regardless of Jetson value.

## Phase 35: RT command staleness watchdog
**Files:** `rt-esp32/src/watchdog.cpp`, `watchdog.h`
- `watchdog_task` at 10 Hz: check `esp_timer_get_time() - last_0x300_feed > 500ms`
- On stale: zero `cmd_queue` → control_task produces zero `0x202` + stops `0x200`
- Send `0x001` on both buses after 1000ms of staleness (delayed ESTOP for controlled stop first)
- **Test (host):** Feed timestamps, verify state transitions. No feed for 500ms → zero setpoints. No feed for 1000ms → ESTOP.

## Phase 36: RT startup sequence
**Files:** update `rt-esp32/src/main.cpp`
- Init order: can_low, can_high, obstacle, physics, steering SM, watchdog, queues (6), 9 tasks
- Steering SM starts in BOOT_WAIT
- **Test (ESP32):** Cold boot. All 9 tasks running. Both CAN buses active. Steering enters LISTEN_SYNC.

## Phase 37: RT host test — full simulation
**Files:** `rt-esp32/test/test_rt_full.cpp`
- Mock both CAN buses, HC-SR04, EPS-C feedback
- Inject `0x300` drive commands, `0x201` steering feedback, obstacle distances
- Verify `0x202`, `0x200`, `0x203`, `0x400`, gateway forwards
- **Test:** Drive scenario: Jetson commands speed=2000, yaw=100 → verify output setpoints, steering angle, gear selection, forwarding.

---

## Phase 38: SYS + RT integration — low-level CAN
**Files:** `sys-esp32/test/test_integration_low.cpp` (or ESP32-to-ESP32 test)
- Both ESP32s on same low-level CAN bus
- SYS sends `0x110`, `0x120`, `0x600`, `0x7FE`, `0x011`, `0x012`
- RT sends `0x202`, `0x200`, `0x203`, `0x302` (forwarded), `0x7FD`
- SYS receives `0x202`, `0x203`, `0x302`, `0x7FD` → acts on them
- RT receives `0x110`, `0x120`, `0x600`, `0x7FE` → forwards/acts
- **Test (ESP32):** 30-minute run. All CAN frames at expected rates. TEC/REC=0. Mode transitions propagate correctly.

## Phase 39: SYS + RT — manual mode end-to-end
**Files:** integration test on hardware
- SYS in MANUAL. Throttle pot → ADC → MCP4725 → measure voltage.
- Gear 72V → TLP281 → relays → verify ECU wires.
- Brake lever → CAN → verify `0x720` at correct rate with correct stroke values.
- Handlebar switches → lamps.
- RT monitoring telemetry on both buses.
- **Test (hardware):** Full manual-mode operation with real signals. All CAN frames verified on logic analyzer.

## Phase 40: Jetson ROS 2 node — CAN bridge
**Files:** `jetson/src/can_bridge_node.cpp` (ROS 2)
- Subscribe to `/cmd_vel` → convert to `0x300 HOST_DRIVE_CMD` (speed_mmps = linear.x×1000, yaw_mrad_s = angular.z×1000)
- Subscribe to `/emergency_stop` → send `0x001` on high CAN
- Publish `/light_cmd` → send `0x302` on high CAN
- Subscribe to CAN telemetry → publish ROS 2 topics: `/etrike/safety_status`, `/etrike/throttle`, `/etrike/state`, `/etrike/obstacle`, `/etrike/diag`
- Send `0x7FC JETSON_HEARTBEAT` at 2 Hz
- **Test:** `ros2 topic pub /cmd_vel` → CAN frame appears. CAN telemetry → `ros2 topic echo`.

## Phase 41: AUTO mode — drive + steer
- SYS in AUTO. Jetson publishes `/cmd_vel`.
- Verify full pipeline: `/cmd_vel` → `0x300` → RT kinematics → `0x202` + `0x200` → SYS MCP4725 + EPS-C angle.
- Dynamic angle clamp active.
- **Test (hardware):** Jetson commands speed=1500, yaw=200 → MCP4725 voltage correct, EPS-C angle correct, speed clamp applies, following error monitored.

## Phase 42: AUTO mode — brake
- Jetson sends brake request → `0x301` → RT max-select → `0x203` → SYS SEB Pressure Mode
- Lever override test: while `0x203 > 0`, press lever → SEB switches to Stroke Mode (lever wins)
- Obstacle-triggered brake: place object <300mm → RT outputs `0x203` with high kPa → SEB brakes
- **Test (hardware):** Each brake trigger path verified. Lever override works. ESTOP → max brake always.

## Phase 43: Safety validation — every ESTOP path
**Files:** `test/test_safety_paths.cpp` (host or scripted hardware)
- Test each ESTOP trigger:
  1. ESTOP button (GPIO1 LOW)
  2. CAN `0x001` from Jetson
  3. CAN `0x001` from RT (SYS HB timeout)
  4. CAN `0x001` from SYS (RT HB timeout)
  5. Steering following error >5° for 300ms
  6. External watchdog timeout (MCU reset)
- Each test verifies: motor=0V, gears=OFF, brake=max, DCDC=OFF, 12V relay=OFF
- **Test (hardware):** Scripted test harness injects faults, measures response time, verifies safe state.

## Phase 44: Endurance — 4-hour soak
- All 3 nodes online, AUTO mode cycling: speed ramp 0→2000→0, steering weave ±10°, periodic braking
- Monitor: stack high-water marks, heap free, TEC/REC, queue depths
- Log every CAN frame to SD card for post-run analysis
- **Test:** Zero crashes, zero CAN bus errors, zero watchdog resets, zero queue overflows. Heap stable (±1KB). All CAN frames at expected rates without gaps.

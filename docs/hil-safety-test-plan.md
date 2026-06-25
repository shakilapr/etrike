# HIL Safety Test Plan — E-Trike

## Overview

19 Hardware-in-the-Loop safety test scenarios covering ESTOP, heartbeat loss, CAN bus-off, stale commands, steering following error, SEB comm loss, dynamic angle clamp, brake monitoring, and power sequencing. Tests are prioritized into 4 tiers based on hardware requirements and risk criticality.

Each scenario specifies concrete CAN IDs, payload values, timing tolerances, and measurable pass/fail criteria. All results are initially marked "Not tested."

**References:**
- Architecture: `architecture.md`
- CAN protocol: `shared/can/can_protocol.h`, `shared/can/can_signals.yaml`
- RT config: `rt-esp32/src/config.h`
- SYS config: `sys-esp32/src/config.h` (constants in `architecture.md` section 8.9)
- Emergency safety analysis: `docs/emergency-safety-analysis.md`

**Equipment pool (shared across tiers):**
- CAN bus analyzer / USB-CAN adapter (e.g., CANalyst-II, PCAN-USB)
- 12V bench power supply (MCU + CAN transceiver rail)
- 72V bench power supply (SEB + EPS-C power)
- Digital oscilloscope (2+ channels, 100 MHz minimum)
- DC electronic load or multimeter for current measurement
- SEB unit with pressure gauge (0--5 MPa) and brake caliper
- EPS-C steering unit with physical steering rack
- MTR STM32 board with MCP4725 DAC output probe
- RT ESP32-S3 board (production firmware)
- SYS ESP32-S3 board (production firmware)
- Breakout/termination board for low-level CAN bus
- Terminal resistors (120 ohm each end)

**Test harness conventions:**
- All CAN trace timestamps use the analyzer's hardware clock (sub-ms resolution).
- "ESTOP initiated" means the ESTOP button GPIO (SYS GPIO1, active-low) is pulled LOW, or CAN 0x001 DLC=0 is transmitted, whichever is specified.
- Timing measurements are taken at the CAN transceiver pin (SN65HVD230 RXD) unless otherwise noted.
- Ambient temperature: 20--25 degC unless noted.

---

## Tier 1 — Bench-Test Blockers (MUST pass before road testing)

Tests that verify fundamental hardware behavior of the SYNTREE actuators. These must pass before any powered integration test is conducted. They require only the actuator unit, a CAN analyzer, and a power supply -- no firmware needed.

---

### T1.1 — Steering Angle Offset Validation
- **Objective**: Verify EPS-C responds correctly to commanded angles across full range with correct offset (raw 30000 = 0 deg).
- **Equipment**: EPS-C unit, CAN analyzer (with 0x169/0x201 support), 12V power supply.
- **Procedure**:
  1. Power EPS-C on bench CAN bus (12V, 500 kbit/s). Wait 2 s for boot.
  2. Send 0x169 VCU_SES_REQ with align_enable=1, control_enable=1, target_angle=0 (raw 30000), target_speed=328, roll_cnt_enable=1, checksum_enable=1, rolling_counter=0, checksum correct.
  3. Listen for 0x201 SES_STATUS at 100 Hz. Record SES_StrAngle once aligned (SES_AngleStatus=1).
  4. Send target_angle=+100 (raw 31000, +10.0 deg). Wait 500 ms. Record SES_StrAngle.
  5. Send target_angle=-100 (raw 29000, -10.0 deg). Wait 500 ms. Record SES_StrAngle.
  6. Send target_angle=+400 (raw 34000, +40.0 deg, software limit). Wait 500 ms. Record SES_StrAngle.
  7. Send target_angle=-400 (raw 26000, -40.0 deg). Wait 500 ms. Record SES_StrAngle.
  8. Send target_angle=+700 (raw 37000, +70.0 deg, unit capability). Wait 1 s. Record SES_StrAngle and note whether software clamp or physical end-stop applies.
- **Pass/Fail**:
  - 0 deg command: SES_StrAngle reads 30000 +/- 10 raw (+/- 1.0 deg).
  - +10 deg command: SES_StrAngle reads 31000 +/- 10 raw.
  - -10 deg command: SES_StrAngle reads 29000 +/- 10 raw.
  - +40 deg command: SES_StrAngle reads 34000 +/- 10 raw.
  - -40 deg command: SES_StrAngle reads 26000 +/- 10 raw.
  - +70 deg command: SES_StrAngle reads 37000 +/- 10 raw (unit capability). EPS-C must NOT report L2/L3 error.
  - SES_Error_Status must be 0 (Normal) at all test points.
- **Result**: Not tested.

---

### T1.2 — SEB Comm-Fault Behavior (Hold vs Release)
- **Objective**: Determine whether SEB holds or releases hydraulic pressure on CAN loss. This is critical to gap #8 and determines whether an NC brake-hold relay is needed.
- **Equipment**: SEB unit, CAN analyzer, pressure gauge (0--5 MPa), 72V power supply (SEB power), 12V power (CAN/logic), brake line + caliper.
- **Procedure**:
  1. Power SEB on bench CAN bus (72V for hydraulic pump, 12V for logic/CAN). Wait 3 s for boot and alignment (SEB_AlignStatus=1 on 0x721).
  2. Send 0x7B9 VCU_SEB_REQ with align_enable=1, control_enable=1, control_mode=0 (Stroke), stroke_req=1140 (27 mm, max stroke) for 2 seconds at 50 Hz. Verify via 0x721 SEB_Stroke_Value reaches >= 1100.
  3. Record SEB_Pressure_Value (0x721 byte 3) at t=0 (last frame before stop).
  4. Stop all CAN transmission -- physically disconnect CAN_H/CAN_L wires from SEB.
  5. Read external pressure gauge at t=0, 100 ms, 500 ms, 1 s, 2 s, 5 s, 10 s after disconnect.
  6. Reconnect CAN and send stroke_req=600 (0 mm) to verify SEB still responsive.
- **Pass/Fail**:
  - PASS if pressure holds >80% of initial value for >1 s after CAN loss (SEB defaults to hold on comm-fault). This validates gap #8 assumption that watchdog reset window is only 20 ms.
  - FAIL if pressure drops below 20% within 500 ms (SEB releases on comm-fault). An NC brake-hold relay gated by TPS3850 RST line is REQUIRED before vehicle testing.
- **Result**: Not tested.

---

## Tier 2 — Bench Power + CAN Analyzer Tests

Tests that require all MCUs (RT, SYS, MTR) on a bench CAN bus with CAN analyzer. No physical actuators needed for the core timing measurements (though SEB/EPS-C may be connected for end-to-end verification).

---

### T2.1 — ESTOP Button Hardware Path Latency
- **Objective**: Measure end-to-end latency from ESTOP button press (SYS GPIO1) to each downstream actuator effect.
- **Equipment**: RT board, SYS board, MTR board, CAN analyzer, oscilloscope (4 channels), 12V power supply.
- **Procedure**:
  1. Connect scope probes: Ch1 = SYS GPIO1 (ESTOP button input, pull-up to 3.3V), Ch2 = CAN_H (low bus), Ch3 = MTR MCP4725 DAC output, Ch4 = SYS GPIO21 (brake light output).
  2. All boards powered and running production firmware in MANUAL mode. Verify heartbeats established.
  3. Press and hold ESTOP button (pull GPIO1 LOW). Capture rising/falling edges on all 4 channels.
  4. Measure:
     a) t_gpio_to_can: time from GPIO1 falling edge to CAN 0x001 DLC=0 frame appearing on bus (first bit of SOF).
     b) t_can_to_mtr_dac: time from CAN 0x001 to MTR DAC output reaching <0.1V (0V target).
     c) t_can_to_brake_light: time from CAN 0x001 to SYS GPIO21 going HIGH.
     d) t_can_to_sys_mode: time from CAN 0x001 to SYS sending 0x110 with mode=2 (ESTOP).
  5. Repeat 10 times. Record min, max, mean for each measurement.
  6. Run the same test in AUTO mode (Jetson simulator sending 0x300 continuously).
- **Pass/Fail**:
  - t_gpio_to_can: mean < 5 ms (worst case < 20 ms -- hardware ISR + TWAI arbitration).
  - t_can_to_mtr_dac: mean < 15 ms (MTR receives 0x001, cuts throttle).
  - t_can_to_brake_light: mean < 10 ms (SYS safety task polls at 20 Hz = 50 ms worst case, but ESTOP ISR should be faster; target: 1--2 ms).
  - t_can_to_sys_mode: mean < 50 ms (mode task at 10 Hz adds 100 ms worst-case polling delay; target < 100 ms).
  - No measurement exceeds 200 ms in any trial.
- **Result**: Not tested.

---

### T2.2 — CAN 0x001 Propagation (Bidirectional Gateway)
- **Objective**: Verify 0x001 originates on either bus and reaches all nodes on both buses within deterministic latency.
- **Equipment**: RT board with TWAI (low) + MCP2515 (high), CAN analyzer on both buses, scope, 12V power.
- **Procedure**:
  1. Connect CAN analyzer to both high and low buses. Scope Ch1 = low CAN_H, Ch2 = high CAN_H.
  2. Boot RT, SYS, and (simulated) Jetson. Verify all heartbeats established.
  3. **Low-to-high test**: Use SYS to transmit 0x001 on low bus. Capture frame arrival on low bus (Ch1) and high bus (Ch2).
     a) Measure t_low_to_high: time from low-bus SOF to high-bus SOF of the forwarded 0x001 frame.
     b) Verify the forwarded frame has same CAN ID 0x001, DLC=0, no payload.
  4. **High-to-low test**: Use simulated Jetson node to transmit 0x001 on high bus. Capture frame arrival on high bus (Ch2) and low bus (Ch1).
     a) Measure t_high_to_low: time from high-bus SOF to low-bus SOF.
  5. **Race test**: Transmit 0x001 on both buses simultaneously. Verify no duplicate forwarding and no collision lockup.
  6. Repeat each direction 20 times.
- **Pass/Fail**:
  - t_low_to_high mean < 2 ms (RT forwards in dispatch task, no queue blocking for 0x001).
  - t_high_to_low mean < 2 ms.
  - Single 0x001 frame appears on each bus per transmission (no echo, no duplication).
  - Forwarded frame has DLC=0 (the forwarding preserves the empty payload).
  - No CAN error frames observed during any single-source test.
- **Result**: Not tested.

---

### T2.3 — Heartbeat Loss RT-to-SYS (0x7FD on Low Bus)
- **Objective**: Verify SYS detects RT heartbeat loss and escalates to ESTOP within 1000 ms timeout.
- **Equipment**: RT board, SYS board, CAN analyzer (low bus), scope, 12V power.
- **Procedure**:
  1. Boot RT and SYS on low bus. Verify 0x7FD (RT, 2 Hz) and 0x7FE (SYS, 10 Hz) are both present.
  2. Verify SYS 0x011 SYS_SAFETY_STS byte 1 (SYS_HeartbeatOk) = 1.
  3. Stop RT CAN transmission (power off RT or remove RT CAN transceiver from bus).
  4. Start timer at last valid 0x7FD frame.
  5. Measure:
     a) t_hb_loss: time from last 0x7FD to SYS 0x011 byte 1 transitioning from 1 to 0.
     b) t_estop: time from last 0x7FD to SYS transmitting 0x110 with mode=2 (ESTOP).
     c) t_0x001: time from last 0x7FD to SYS transmitting 0x001.
     d) t_brake: time from last 0x7FD to SYS 0x7B9 stroke_req reaching max (1140 = 27 mm).
  6. Capture scope: Ch1 = SYS GPIO1 (not asserted -- ESTOP is software-triggered here), Ch2 = CAN_H.
  7. Record CAN trace for 5 s after RT stop. Verify no 0x7FD appears.
  8. Repeat with RT transmitting but alive counter frozen (same counter value each frame). This simulates a hung MCU with a functioning CAN controller.
- **Pass/Fail**:
  - t_hb_loss: 1000 ms +/- 200 ms (SYS kHeartbeatTimeoutMsRt = 1000 ms, counting from last valid frame; 2 Hz period = 500 ms, 2 missed frames = 1000 ms).
  - t_estop: < 1200 ms (includes CS task + CAN TX).
  - t_brake (stroke=max): < 1300 ms (brake task at 50 Hz = 20 ms period).
  - ESTOP must NOT trigger during normal heartbeat operation (continuous 60 s monitoring).
  - Frozen-counter test must also trigger ESTOP within 1000 ms +/- 200 ms (same timeout, same response).
- **Result**: Not tested.

---

### T2.4 — Heartbeat Loss SYS-to-RT (0x7FE on Low Bus, gap #12)
- **Objective**: Verify RT detects SYS heartbeat loss and takes over brake command within 200 ms timeout.
- **Equipment**: RT board, SYS board, CAN analyzer (low bus), scope, 12V power.
- **Procedure**:
  1. Boot RT and SYS on low bus in AUTO mode (simulate Jetson sending 0x300 on high bus). Verify 0x7FE (SYS) is present at 10 Hz.
  2. Verify RT is sending 0x205 RT_BRAKE_CMD at 50 Hz and 0x7FD at 2 Hz.
  3. Stop SYS CAN transmission (remove SYS from bus).
  4. Start timer at last valid 0x7FE frame.
  5. Measure:
     a) t_detect: time from last 0x7FE to RT's internal SYS_heartbeat_ok flag clearing (measure via diagnostic CAN message or GPIO toggle -- may require debug firmware).
     b) t_brake_takeover: time from last 0x7FE to RT transmitting 0x7B9 on low bus with stroke_req=max (1140 = 27 mm). This tests the gap #12 mitigation.
     c) t_estop: time from last 0x7FE to RT transmitting 0x001 (if RT escalates).
  6. For 5 seconds after SYS goes silent, verify:
     a) RT continues transmitting 0x7B9 at 50 Hz with valid rolling counter and checksum.
     b) RT continues transmitting 0x205 (if configured to dual-path).
     c) The 0x7B9 frames maintain stroke_req=max throughout (no oscillation back to 0).
  7. Repeat with SYS alive counter frozen (same counter value each frame -- hung MCU simulation).
- **Pass/Fail**:
  - t_brake_takeover: < 300 ms from last 0x7FE (200 ms timeout + 20 ms to first 0x7B9 frame at 50 Hz + margin). This is the gap #12 mitigation: 220 ms worst case.
  - 0x7B9 stroke_req must be >= 1140 within 300 ms and stay >= 1140 for the entire silence period.
  - 0x7B9 rolling counter must increment 0 -> 15 -> 0 continuously at 50 Hz.
  - 0x7B9 checksum must be valid for every frame.
  - Frozen-counter test must trigger brake takeover with same timing (+/- 100 ms).
  - No 0x001 frames should be sent if RT successfully takes over brake (ESTOP is redundant once brake is already max, but is acceptable).
- **Result**: Not tested.

---

### T2.5 — Heartbeat Loss Host-to-RT (0x7FC on High Bus)
- **Objective**: Verify RT detects Host (Jetson) heartbeat loss and executes assisted stop.
- **Equipment**: RT board, CAN analyzer (high bus), simulator node to generate 0x7FC and 0x300, 12V power.
- **Procedure**:
  1. Boot RT on both buses. Simulate Jetson sending 0x300 (HOST_DRIVE_CMD, speed=1000 mm/s, yaw=0, gear=D) at 100 Hz and 0x7FC (heartbeat) at 2 Hz.
  2. Verify RT is forwarding drive commands to low bus (0x204 speed > 0, 0x169 steering active).
  3. Stop Host heartbeat (0x7FC) only. Continue sending 0x300 for 2 s (to test staleness interaction -- see T2.6).
  4. Stop Host 0x300 as well (both heartbeat + command gone).
  5. Measure:
     a) t_assisted_stop: time from last 0x7FC to 0x205 brake_pressure_kpa reaching >= 2000 kPa (target: 2000 kPa moderate brake).
     b) t_zero_speed: time from last 0x7FC to 0x204 motor_speed_mmps reaching 0.
     c) t_steer_stop: time from last 0x7FC to 0x169 target_angle reaching 0 (ramp-down at 20 deg/s).
     d) t_mode_manual: time from last 0x7FC to SYS (if bridged) sending 0x110 mode=0 (MANUAL).
  6. Verify that 0x205 brake pressure does NOT exceed 5000 kPa (must not be full ESTOP brake -- assisted stop only).
  7. Verify DC-DC stays on (0x012 enable=1) and brake light is illuminated.
- **Pass/Fail**:
  - t_assisted_stop: < 2000 ms from last valid 0x7FC (1500 ms timeout + 500 ms margin).
  - t_zero_speed: < 2000 ms.
  - 0x205 brake pressure: 2000 kPa +/- 500 kPa at steady state (moderate deceleration, not wheel lockup).
  - 0x205 is zeroed when rider resumes control (if SYS transitions to MANUAL, lever override works).
  - 0x012 enable stays 1 (DC-DC maintains 12V).
  - SYS brake light GPIO21 is HIGH within 100 ms of assisted stop command.
- **Result**: Not tested.

---

### T2.6 — Command Staleness (0x300 HOST_DRIVE_CMD)
- **Objective**: Verify RT's 500 ms command-staleness watchdog zeros setpoints when Host drive commands stop.
- **Equipment**: RT board, CAN analyzer (high and low buses), simulator node for 0x300, 12V power.
- **Procedure**:
  1. Boot RT, SYS, MTR on bench. Simulate Jetson sending 0x300 at 100 Hz with speed=2000 mm/s, yaw=0, gear=D, plus 0x7FC at 2 Hz.
  2. Verify normal operation: 0x204 speed > 0 on low bus, 0x169 target_angle active.
  3. Stop 0x300 transmissions while continuing 0x7FC heartbeats (staleness is independent of heartbeat).
  4. Measure:
     a) t_stale_detect: time from last 0x300 to 0x204 speed reaching 0.
     b) t_steer_stop: time from last 0x300 to 0x169 target_angle reaching 0 (ramp-down at 20 deg/s from whatever angle was last commanded).
     c) t_brake: time from last 0x300 to 0x205 brake pressure ramping up (if any -- staleness only zeros setpoints; brake may not engage unless obstacle or 0x301 also present).
  5. Verify that 0x7FC heartbeats continue normally throughout (staleness response must NOT affect heartbeat liveness checks).
  6. Resume 0x300 at 100 Hz. Verify normal operation resumes within 100 ms.
- **Pass/Fail**:
  - t_stale_detect: 500 ms +/- 100 ms (RT kCmdStaleTimeoutMs = 500 ms, checked at 10 Hz in watchdog task).
  - 0x204 motor_speed_mmps = 0 and gear = N (0) within 600 ms of last 0x300 frame.
  - 0x169 target_angle reaches 0 within 2000 ms (ramp at 20 deg/s from any angle up to 40 deg).
  - Steering must NOT silent-stop -- ramp must be active (transmitting 0x169 during ramp) per architecture section 7.6.
  - On resume, 0x204 reflects new 0x300 command within 100 ms.
- **Result**: Not tested.

---

### T2.7 — CAN Bus-Off Recovery
- **Objective**: Verify each node can recover from CAN bus-off without requiring power cycle or permanent fault state.
- **Equipment**: RT board, SYS board, MTR board, CAN analyzer, variable resistor or fault injection tool (can short CAN_H to CAN_L), 12V power.
- **Procedure**:
  1. Boot all nodes on low bus. Verify normal CAN traffic for 30 s.
  2. **Inject errors**: Short CAN_H to CAN_L for 1 s using a relay or fault switch. Monitor TEC counters:
     a) On SYS via 0x600 SYS_DIAG_RPT bytes 6 (TEC) and 7 (REC).
     b) On RT via debug serial output (TEC/REC counters).
  3. After removing short, count time until each node resumes normal CAN TX:
     a) SYS 0x7FE at 10 Hz.
     b) RT 0x7FD at 2 Hz.
     c) Any other periodic message.
  4. **Bus-off threshold verification**: Inject controlled burst of errors to push TEC past 255. Verify node enters bus-off state (stops transmitting). Remove fault and verify auto-recovery (or manual recovery if configured).
  5. Repeat for high bus (RT MCP2515 side).
- **Pass/Fail**:
  - Normal CAN traffic resumes within 5 s of fault removal.
  - TEC/REC counters are monotonic increasing during faults and decreasing after (per CAN specification, TWAI decrements 1 per 125 good frames after error).
  - Bus-off state is entered when TEC > 255.
  - Bus-off recovery (TWAI auto-recovery mode) happens within 200 ms of fault removal if bus idle.
  - No node enters a persistent fault state that requires power cycle to recover.
  - During bus-off period, MTR must NOT have unintended throttle (DAC output < 0.1V).
  - During bus-off period, SEB (if connected) must maintain last commanded state (or hold, per T1.2).
- **Result**: Not tested.

---

### T2.8 — Power Sequencing
- **Objective**: Verify all nodes boot without false ESTOP, unintended actuation, or CAN bus disruption regardless of power-on order.
- **Equipment**: RT board, SYS board, MTR board, CAN analyzer, three independently switched 12V supplies (or one supply with per-node switches), 72V supply for SEB.
- **Procedure**:
  1. **Cold boot all simultaneously**: Power all nodes at once. Verify:
     a) No 0x001 ESTOP frames for first 3 s (startup grace period).
     b) No unintended throttle (MTR DAC output < 0.1V throughout boot).
     c) No unintended brake (SEB stroke < 610, or if SEB absent, no 0x7B9 with stroke > 610).
     d) Heartbeats establish within 5 s of power-on.
     e) Steering boot sequence: STEER_BOOT_WAIT (500 ms) -> STEER_LISTEN_SYNC -> STEER_ACTIVE (if EPS-C present).
     f) Brake boot sequence: BRAKE_BOOT_WAIT (500 ms) -> BRAKE_LISTEN_SYNC -> BRAKE_ACTIVE (if SEB present).
  2. **RT first, then SYS**: Power RT. Wait 2 s. Power SYS. Verify 0x204 staleness check (200 ms) does NOT trigger ESTOP -- the startup grace mask should cover this. SYS must see first 0x7FD after powering on and establish heartbeat.
  3. **SYS first, then RT**: Power SYS. Wait 2 s. Power RT. Verify SYS 0x204 staleness fires harmlessly (speed=0 is already default), then transitions to normal when 0x204 begins.
  4. **MTR first**: Power MTR standalone. Verify no CAN traffic from MTR (it listens only, sends 0x120/0x206 only when configured).
  5. **Brown-out / voltage ramp**: Slowly ramp 12V supply from 0V to 12V over 500 ms. Verify no spurious 0x001 frames, no unintended actuation.
  6. **Power cycle**: Power off all nodes, wait 3 s, power back on. Repeat 10 times. Verify no failure to boot across 10 cycles.
- **Pass/Fail**:
  - No unintended actuation in any power sequence variation (DAC < 0.1V, 0x7B9 stroke < 610, gear relays all OFF).
  - Startup grace period (3 s) is respected: no ESTOP triggered for heartbeat misses during boot.
  - Heartbeats of all powered nodes present within 5 s.
  - No CAN bus lock or persistent error frames in any sequence.
  - All 10 cold power cycles boot successfully with no faults logged in 0x600.
- **Result**: Not tested.

---

## Tier 3 — Full-Bench Integration Tests

Tests that require all nodes and at least one physical actuator (EPS-C or SEB) on the bench. These verify the safety mechanisms that depend on actuator feedback.

---

### T3.1 — Steering Following Error (Speed-Scaled Threshold)
- **Objective**: Verify RT detects steering following error using speed-scaled threshold and triggers ESTOP within 300 ms.
- **Equipment**: RT board, EPS-C unit, CAN analyzer, variable resistor or mechanical stop to prevent EPS-C from reaching commanded angle, 12V + 72V power.
- **Procedure**:
  1. Boot RT, establish steering ACTIVE state via 0x201 sync. Set initial command to 0 deg.
  2. **Low-speed test**: Command 30 deg at 2 km/h (555 mm/s). Dynamic limit at this speed = 40 deg, so 30 deg is valid. Following error threshold = max(2.0, 0.25 x 40) = 10.0 deg.
     a) Block EPS-C from reaching beyond 5 deg (physical stop or by injecting fake 0x201 angle feedback).
     b) Measure t_follow_err: time from when cmd=30 and actual < cmd-threshold persistently to RT sending 0x001 ESTOP.
  3. **High-speed test**: Command 10 deg at 25 km/h (6944 mm/s). Dynamic limit at this speed = 5 deg, so 10 deg would be clamped by RT to 5 deg. Following error threshold at 25 km/h = max(2.0, 0.25 x 5) = 2.0 deg.
     a) Set target to 5 deg (clamped).
     b) Block EPS-C from reaching beyond 1 deg.
     c) Measure t_follow_err.
  4. **Mid-speed test**: Command 20 deg at 10 km/h (2778 mm/s). Dynamic limit at this speed = 40 - (10-2) x (35/23) = 40 - 8 x 1.52 = 27.8 deg. Following error threshold = max(2.0, 0.25 x 27.8) = 6.95 deg.
     a) Block EPS-C at 2 deg.
     b) Measure t_follow_err.
  5. **Below-threshold test**: Same as low-speed test but allow EPS-C to track within 8 deg error (below 10 deg threshold). Verify NO ESTOP triggers for 5 s.
- **Pass/Fail**:
  - All t_follow_err measurements: 300 ms +/- 100 ms from error onset to ESTOP (kSteerFollowingErrMs = 300 ms, checked at 100 Hz control loop, 30 ticks).
  - ESTOP triggered is 0x001 + mode set to ESTOP (0x110 mode=2 if SYS involved, or internal RT state change).
  - Steering behavior during ESTOP depends on trigger: non-obstacle -> ramp to 0 deg at 20 deg/s via active 0x169.
  - Below-threshold test must NOT trigger ESTOP for >= 5 s continuous.
  - For speed-scaled threshold verification: low-speed threshold (~10 deg) must be larger than high-speed threshold (~2 deg), measured empirically.
- **Result**: Not tested.

---

### T3.2 — Dynamic Angle Clamp
- **Objective**: Verify RT clamps commanded steering angle proportionally to vehicle speed, preventing rollover.
- **Equipment**: RT board, CAN analyzer (high and low buses), simulator for 0x300 (speed + yaw commands).
- **Procedure**:
  1. Boot RT in AUTO mode with simulated Jetson 0x300 commands and EPS-C on bench (or with simulated 0x201 feedback).
  2. **High-speed clamp**: Send 0x300 with speed=6944 mm/s (25 km/h) and yaw_rate=1500 mrad/s (max steering demand). Verify via 0x169 target_angle that output is clamped to <= 5 deg (50 raw). Dynamic limit formula at 25 km/h: 40.0 - (25-2) x (35/23) = 40 - 23 x 1.52 = 40 - 34.96 = 5.04 deg.
  3. **Mid-speed clamp**: Send speed=2778 mm/s (10 km/h) with same yaw demand. Verify clamp <= 27.8 deg (278 raw).
  4. **Low-speed clamp**: Send speed=555 mm/s (2 km/h) with same yaw demand. Verify clamp <= 40 deg (400 raw).
  5. **Below-2-km/h**: Send speed=0 mm/s or 100 mm/s. Verify clamp still at 40 deg (kAngleClampBaseDeg).
  6. **Saturation flag**: At each speed, verify 0x210 RT_STATE_RPT byte 1 (RT_SteerValid) behavior -- should be 1 when commanded angle is within clamp, 0 when saturated.
  7. **Transition**: Ramp speed from 2 km/h to 25 km/h over 5 s while holding constant yaw. Verify clamp changes smoothly (no step jumps > 2 deg per 100 ms).
- **Pass/Fail**:
  - 0x169 target_angle_raw must match dynamic limit +/- 5 raw at each speed step.
  - At 25 km/h: target_angle_raw <= 55 (5.5 deg, allowing slight rounding).
  - At 10 km/h: target_angle_raw <= 283 (28.3 deg).
  - At 2 km/h: target_angle_raw <= 405 (40.5 deg).
  - All clamps are below the software hard-stop of 40 deg (400 raw) -- only the low-speed clamp reaches it.
  - Saturated flag (if implemented) correctly indicates when yaw input would have produced angle > clamp.
  - No step discontinuities during speed ramp.
- **Result**: Not tested.

---

### T3.3 — SEB BRAKE_DEGRADED Mode
- **Objective**: Verify that when SEB boot sync fails (no 0x721 within 2 s), SYS enters BRAKE_DEGRADED and maintains lever-based brake function.
- **Equipment**: SYS board, CAN analyzer, brake lever switch (SYS GPIO2), 12V power, optionally SEB unit.
- **Procedure**:
  1. Boot SYS alone on bench CAN bus (no SEB connected). Verify SYS enters BRAKE_LISTEN_SYNC and waits for 0x721.
  2. **Timeout test**: Without any SEB on the bus, wait for 0x721 timeout. Measure t_deg: time from boot to SYS entering BRAKE_DEGRADED (observe via 0x600 SYS_DIAG_RPT diagnostic flag, or via 0x7B9 transmission starting).
     a) Expected: >= 2000 ms (kBrakeTimeoutMs for LISTEN_SYNC).
  3. Once in BRAKE_DEGRADED, toggle brake lever (GPIO2):
     a) Lever pressed (LOW): Verify SYS transmits 0x7B9 with stroke_req = 900 (15 mm, kBrakeManualStroke).
     b) Lever released (HIGH): Verify SYS transmits 0x7B9 with stroke_req = 600 (0 mm).
  4. **Recovery test**: Add SEB to bus (or simulate 0x721 frames via CAN analyzer):
     a) Send 0x721 with valid SEB_Stroke_Value (e.g., raw 800) and SEB_AlignStatus=1.
     b) Verify SYS reads current stroke, updates internal target, and transitions to BRAKE_ACTIVE.
     c) Verify 0x7B9 now reflects commanded stroke synchronized to actual SEB position.
  5. **Fault diagnostic**: Verify 0x600 SYS_DIAG_RPT indicates BRAKE_DEGRADED condition for post-incident analysis.
- **Pass/Fail**:
  - t_deg timeout: 2000 ms +/- 500 ms (SYS boot wait 500 ms + LISTEN_SYNC wait up to 2 s).
  - In BRAKE_DEGRADED, brake lever immediately produces 0x7B9 commands with correct stroke:
    - Lever pressed: stroke_req = 900 +/- 10 raw (15 mm).
    - Lever released: stroke_req = 600 +/- 10 raw (0 mm).
  - 0x7B9 rolling counter increments 0 -> 15 at 50 Hz even in DEGRADED mode.
  - 0x7B9 checksum is valid for every frame.
  - On SEB 0x721 arrival, SYS transitions to BRAKE_ACTIVE within 100 ms and syncs to measured stroke.
  - The brake lever remains functional at ALL times (the defining requirement of BRAKE_DEGRADED).
- **Result**: Not tested.

---

### T3.4 — Brake Following Error
- **Objective**: Verify SYS detects mismatch between commanded stroke (0x7B9) and actual stroke (0x721) and logs brake fault.
- **Equipment**: SYS board, SEB unit (or SEB simulator on CAN analyzer), CAN analyzer, 72V + 12V power.
- **Procedure**:
  1. Boot SYS and SEB on bench CAN. Establish BRAKE_ACTIVE state. Verify normal 0x7B9/0x721 exchange.
  2. **Stroke mismatch test**: Command stroke=900 (15 mm) via 0x7B9. Simulate stuck SEB by feeding back a false 0x721 with stroke stuck at 610 (0.5 mm) or by physically blocking the SEB actuator.
     a) Measure t_brake_err: time from sustained error > 3 mm to SYS logging brake fault in 0x600.
  3. **Pressure mismatch test**: Repeat in Pressure Mode (control_mode=1). Command pressure_req=50 (2.5 MPa). Feed back false pressure of 10 (0.5 MPa). Verify fault detection.
  4. **Recovery test**: After fault logged, restore correct feedback (stroke=900 or pressure=50). Verify fault flag clears (or remains latched per design) and normal operation continues.
  5. **No-false-positive test**: Run normal brake cycling for 60 s: lever pressed 2 s, released 2 s, repeated. Log stroke command vs actual. Verify no false faults triggered.
- **Pass/Fail**:
  - t_brake_err: < 200 ms from sustained error > 3 mm (architecture section 8.10 specifies 100 ms debounce, verified at brake task 50 Hz).
  - Fault is logged to 0x600 SYS_DIAG_RPT (persistent fault flag).
  - ESTOP is NOT triggered by brake following error (architecture explicitly notes "cannot escalate (ESTOP is already max brake)" -- but brake following error while NOT at max stroke should still only log, not ESTOP).
  - No false positives during normal cycling (60 s, 15 cycles).
  - Brake lever remains fully functional during and after fault condition.
- **Result**: Not tested.

---

### T3.5 — SEB L3 Fault Injection
- **Objective**: Verify SYS correctly detects and escalates SEB Level 3 (severe) faults to ESTOP.
- **Equipment**: SYS board, CAN analyzer, power.
- **Procedure**:
  1. Boot SYS with SEB (or SEB simulator) on bench CAN. Establish normal operation in MANUAL mode.
  2. **Inject L3 via 0x721 SEB_STATUS**: Send 0x721 with SEB_Error_Status = 3 (L3_Severe) in byte 0 bits 6-7. Keep other signals valid (stroke, checksum, rolling counter).
     a) Measure t_escalate_721: time from L3 status frame to SYS sending 0x001 ESTOP.
  3. **Inject L3 via 0x731 SEB_ErrInfo**: Set SEB_CanComErr flag (byte 0 bit 2 = 1, fault level L3 per signal dictionary). Send 0x731 with this flag.
     a) Measure t_escalate_731: time from L3 ErrInfo to SYS sending 0x001 ESTOP.
  4. **Inject L2 fault (non-escalating)**: Set SEB_ECUUnderVolt flag (byte 0 bit 0 = 1, fault level L2). Verify SYS logs but does NOT send 0x001.
     a) Verify 0x600 SYS_DIAG_RPT records the fault.
     b) Verify no ESTOP within 3 s of L2 injection.
  5. **Clear fault**: Remove L3 flags, send normal 0x721 with SEB_Error_Status = 0. Verify ESTOP is not cleared (ESTOP requires hardware START button or power cycle -- verify 0x110 mode stays 2).
- **Pass/Fail**:
  - t_escalate_721: < 200 ms (SYS dispatch processes 0x721 at 100 Hz, 10 ms period; safety task at 20 Hz = 50 ms; target < 150 ms).
  - t_escalate_731: < 200 ms (0x731 at 10 Hz max period 100 ms + SYS processing).
  - L2 faults: SYS 0x600 records fault details (hex flags), no ESTOP within 3 s, mode unchanged.
  - Once ESTOP triggered, 0x110 mode = 2 (ESTOP) persists until START button (GPIO32) pressed.
  - 0x7B9 stroke = max (1140 = 27 mm) during ESTOP.
  - 0x012 DCDC enable stays 1 (ESTOP does NOT kill 12V per gap #17 fix).
- **Result**: Not tested.

---

## Tier 4 — Vehicle-Level Tests

Tests that require the full vehicle (or a full-bench integration with all actuators and simulated perception). These validate end-to-end safety scenarios that span multiple subsystems.

---

### T4.1 — Obstacle ESTOP Hold-Angle with Dynamic Clamp
- **Objective**: Verify obstacle-triggered ESTOP holds current steering angle clamped to dynamic limit, preventing rollover during cornering + braking.
- **Equipment**: Full vehicle or full bench with EPS-C, RT, SYS, CAN analyzer, obstacle simulator (0x400).
- **Procedure**:
  1. Boot all nodes in AUTO mode. Establish steering ACTIVE. Set speed at 6944 mm/s (25 km/h) via 0x300.
  2. Command 30 deg steering angle (yaw rate producing > 40 deg bicycle model output, clamped by software hard-stop to 40 deg, but further clamped by dynamic clamp to ~5 deg at this speed).
     a) Verify through 0x169 that dynamic clamp limits angle to ~5 deg (50 raw).
     b) EPS-C tracks to clamped angle; actual reads ~5 deg from 0x201.
  3. **Trigger obstacle ESTOP**: Send 0x400 HOST_OBSTACLE_DIST = 200 mm (below kObstacleStopMM = 300). This is obstacle-triggered ESTOP.
     a) Verify steering state enters ESTOP_HOLD_THEN_SILENT.
     b) Verify hold angle is clamped to dynamic limit (~5 deg, raw ~50). If current actual is 5 deg and limit is 5 deg, no ramp needed.
     c) Verify 0x169 continues transmitting with hold angle for 500 ms (kSteerEstopHoldMs).
     d) After 500 ms, verify steering enters FAULT (silent-stop) -- 0x169 stops transmitting.
  4. **Cornering high-angle test**: Reduce speed to 555 mm/s (2 km/h). Command 35 deg steering. Dynamic limit at 2 km/h = 40 deg, so 35 deg is valid. Trigger obstacle ESTOP via 0x400.
     a) Verify hold angle = 35 deg (no clamping needed, within limit).
     b) Verify hold for 500 ms then silent-stop.
  5. **Over-limit hold test**: Set speed to 6944 mm/s (25 km/h). Command 5 deg steering (at clamp limit). Trigger obstacle ESTOP. Hold angle should be 5 deg. Verify no ramp needed.
- **Pass/Fail**:
  - Hold angle in obstacle ESTOP is clamped to dynamic limit (not the raw command).
  - 0x169 transmits at 50 Hz during the 500 ms hold phase.
  - 0x169 stops transmitting after hold expires (silent-stop / FAULT).
  - No rollover condition: lateral acceleration a_y at clamped angle + speed must be below rollover threshold (a_y = v^2 / L * tan(delta) < g * w / (2h), with g=9.81, w=track_width_m, h=cg_height_m -- documented in physics model).
  - Brake (0x7B9) is applied at max stroke simultaneously with steering hold.
  - 0x120 speed feedback shows deceleration (vehicle slowing during ESTOP).
- **Result**: Not tested.

---

### T4.2 — MTR ESTOP ACK
- **Objective**: Verify MTR STM32 acknowledges ESTOP by setting ESTOP_ACTIVE bit in 0x206 fault_flags within 100 ms.
- **Equipment**: RT board, SYS board, MTR board, CAN analyzer, 12V power.
- **Procedure**:
  1. Boot all nodes in MANUAL mode. Verify MTR sends 0x206 at 50 Hz with MTR_FaultFlags = 0 (byte 3).
  2. **Trigger ESTOP**: Pull SYS GPIO1 LOW (ESTOP button). Alternatively, send CAN 0x001 on low bus.
  3. Measure:
     a) t_ack: time from ESTOP trigger (GPIO falling edge or CAN 0x001 SOF) to MTR 0x206 byte 3 (MTR_FaultFlags) having bit 0 = 1 (ESTOP_ACTIVE).
     b) t_throttle_cut: time from ESTOP to MTR MCP4725 DAC output dropping below 0.1V.
     c) t_gear_off: time from ESTOP to all gear relay outputs (if accessible) going LOW.
  4. **Confirm SYS monitoring**: Verify SYS 0x600 logs MTR ESTOP ACK status (or lack thereof) via diagnostic byte.
  5. **No-ACK test**: If MTR firmware is deliberately unresponsive (or removed), verify SYS detects missed ACK within 200 ms and logs MTR comms loss.
- **Pass/Fail**:
  - t_ack: < 100 ms (architecture section 8.10: "MTR sets ESTOP_ACTIVE bit in 0x206 fault_flags when it has locally cut throttle+gear"). 0x206 is 50 Hz = 20 ms period, so ACK in 1--2 frames is expected (20--40 ms).
  - t_throttle_cut: < 10 ms (MTR hardware ISR on ESTOP GPIO).
  - t_gear_off: < 20 ms (relay switching time + GPIO).
  - SYS diagnostic (0x600) correctly reports MTR ACK status.
  - If MTR is missing, SYS logs fault within 500 ms.
- **Result**: Not tested.

---

### T4.3 — ESTOP Exit Race Condition (gap #6)
- **Objective**: Verify pressing START during non-obstacle ESTOP centering ramp does NOT interrupt 0x169 transmission before ramp completes, preventing off-center steering lock.
- **Equipment**: RT board, EPS-C unit, CAN analyzer, START button (SYS GPIO32), 12V power.
- **Procedure**:
  1. Boot all nodes in AUTO mode at 555 mm/s (2 km/h). Command 30 deg steering angle. Establish steady state.
  2. Trigger non-obstacle ESTOP (e.g., stop 0x7FC heartbeats per T2.5, or inject command staleness per T2.6). Verify steering enters ESTOP_RAMP_TO_ZERO.
  3. **Race condition test**: Within 100 ms of ESTOP trigger, press START button (GPIO32) to attempt exit.
     a) Monitor 0x169 continuously during ramp.
     b) Verify 0x169 continues transmitting at 50 Hz throughout the ramp (architecture: "RT defers ESTOP->MANUAL steering transition until ramp completes").
     c) Measure ramp duration from trigger to target_angle = 0. At 20 deg/s from 30 deg, ramp = 1.5 s.
     d) Verify 0x169 target_angle reaches 0 within 2 s.
  4. **Post-ramp behavior**: After target_angle=0, verify steering transitions to ACTIVE (or LISTEN_SYNC, depending on design). Verify 0x169 continues transmitting at 0 deg hold.
  5. **START before ESTOP**: Press START in normal ACTIVE mode (no ESTOP). Verify no effect (START only exits ESTOP, architecture section 8.6).
- **Pass/Fail**:
  - START button during ESTOP ramp: 0x169 must NOT stop transmitting prematurely. Ramp must complete to 0 deg.
  - Ramp rate: 20 deg/s +/- 2 deg/s (as measured by 0x169 target_angle change over time).
  - If START pressed during ramp, steering must still reach 0 deg before transitioning.
  - After ramp completes (0 deg reached), steering may transition to ACTIVE (if EPS-C aligned) or remain at 0 deg hold.
  - Brake 0x205/0x7B9 must remain at max stroke -- brake/motor/lights transition immediately per architecture, only steering defers.
  - Pressing START without ESTOP active: no mode change.
- **Result**: Not tested.

---

### T4.4 — Startup Grace Period (gap #16)
- **Objective**: Verify the 3-second startup grace period prevents false ESTOP during boot while correctly enabling heartbeat and staleness monitoring after grace expires.
- **Equipment**: RT board, SYS board, MTR board, CAN analyzer, oscilloscope, 12V power.
- **Procedure**:
  1. **Cold boot with no heartbeats**: Power all nodes simultaneously. Capture all CAN traffic for 5 s.
     a) Verify no 0x001 ESTOP frames are sent during the first 3 s after power-on.
     b) Verify SYS 0x011 byte 1 (SYS_HeartbeatOk) = 1 during grace period even if 0x7FD not yet received.
     c) After 3 s, if 0x7FD has not been received, verify SYS detects heartbeat loss and triggers ESTOP within 1000 ms of grace expiry.
  2. **0x204 staleness during boot**: Power RT first, wait 500 ms, then power SYS. SYS will see no 0x204 for > 200 ms (staleness threshold). Verify 0x204 staleness does NOT cause false ESTOP or unintended actuation during the 3 s grace period. Speed=0 is the expected default.
  3. **Grace expiry timeliness**: After all nodes booted, verify that a genuine heartbeat loss (remove one node) after grace expiry triggers correctly within the required timeout (T2.3, T2.4, T2.5).
  4. **Rapid power cycle**: Power off for 1 s, power on. Verify grace timer resets (does not retain stale timestamp).
- **Pass/Fail**:
  - No ESTOP during first 3 s of any boot sequence.
  - SYS_HeartbeatOk byte in 0x011 = 1 during grace period.
  - After grace expiry, heartbeat loss detection works correctly (same timing as T2.3/T2.4).
  - 0x204 staleness is benign during grace (zero speed = safe default).
  - Grace timer resets on each power cycle (no stale timestamp carryover from previous boot).
  - Repeat 10 rapid power cycles with no false ESTOP in any cycle.
- **Result**: Not tested.

---

## Summary Table

| ID | Scenario | Tier | Requirement Type | Pass Condition | Status |
|----|----------|------|-----------------|---------------|--------|
| T1.1 | Steering Angle Offset | 1 | Hardware validation | SES_StrAngle matches cmd +/- 1 deg across full range | Not tested |
| T1.2 | SEB Comm-Fault Behavior | 1 | Hardware characterization | Pressure holds >80% for >1 s after CAN loss | Not tested |
| T2.1 | ESTOP Button Latency | 2 | Timing | GPIO -> CAN < 5 ms mean, CAN -> MTR DAC < 15 ms | Not tested |
| T2.2 | CAN 0x001 Propagation | 2 | Timing | Bus-to-bus forwarding < 2 ms, no echo | Not tested |
| T2.3 | HB Loss RT -> SYS | 2 | Safety function | ESTOP within 1000 ms +/- 200 ms of HB loss | Not tested |
| T2.4 | HB Loss SYS -> RT | 2 | Safety function (gap #12) | RT brake takeover < 300 ms | Not tested |
| T2.5 | HB Loss Host -> RT | 2 | Safety function | Assisted stop (2000 kPa brake) < 2000 ms | Not tested |
| T2.6 | Command Staleness 0x300 | 2 | Safety function | Zero setpoints within 500 ms +/- 100 ms | Not tested |
| T2.7 | CAN Bus-Off Recovery | 2 | Robustness | Auto-recovery within 5 s, no permanent fault | Not tested |
| T2.8 | Power Sequencing | 2 | Robustness | No false ESTOP or unintended actuation | Not tested |
| T3.1 | Steering Following Error | 3 | Safety function | ESTOP within 300 ms, speed-scaled threshold | Not tested |
| T3.2 | Dynamic Angle Clamp | 3 | Safety function | Angle clamped: 5 deg at 25 km/h, 40 deg at 2 km/h | Not tested |
| T3.3 | SEB BRAKE_DEGRADED | 3 | Safety function | Lever works without SEB sync, recovers on 0x721 | Not tested |
| T3.4 | Brake Following Error | 3 | Safety function | Fault logged for >3 mm error < 200 ms | Not tested |
| T3.5 | SEB L3 Fault Injection | 3 | Safety function | ESTOP within 200 ms of L3 fault | Not tested |
| T4.1 | Obstacle ESTOP Hold-Angle | 4 | Safety function | Hold clamped to dynamic limit, silent-stop after 500 ms | Not tested |
| T4.2 | MTR ESTOP ACK | 4 | Safety function (gap #15) | ACK bit in 0x206 within 100 ms | Not tested |
| T4.3 | ESTOP Exit Race Condition | 4 | Safety function (gap #6) | Ramp completes before steering handoff | Not tested |
| T4.4 | Startup Grace Period | 4 | Robustness (gap #16) | No false ESTOP during 3 s grace, works after | Not tested |

---

## Notes for Test Execution

1. **Tier ordering is intentional**: Tier 1 must pass before any powered test. Tier 2 must pass before full-bench. Tier 3 must pass before vehicle-level. Tier 4 is the final integration validation.
2. **Test harness firmware**: Some tests require debug firmware that exposes internal state (e.g., heartbeat_ok flag) on a GPIO or diagnostic CAN message. Prepare a test build that exports these signals.
3. **CAN analyzer setup**: Configure hardware timestamping at sub-millisecond resolution. For timing-critical tests (T2.1--T2.6), use scope + CAN analyzer concurrently and cross-reference timestamps.
4. **Safety during test execution**: All bench tests involving 72V power (SEB, EPS-C) must have a physical emergency disconnect within reach. For tests that inject CAN faults (T2.7), use a current-limited supply.
5. **Result documentation**: For each test, record date, firmware version, equipment serial numbers, raw measurements (CSV/log), and scope captures. Update the Result field with date and outcome.
6. **Gap regression**: Tests T2.4 (gap #12), T3.3 (gap #7), T4.1 (gap #9), T4.3 (gap #6), T4.4 (gap #16) specifically validate that documented design gaps have been resolved. If these fail, the corresponding gap fix has not been correctly implemented.

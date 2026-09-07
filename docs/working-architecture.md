# Working Architecture

Implementation-level description of how the etrike distributed controllers actually work:
**RM** (operator remote / command source), **RT** (realtime motion master + CAN gateway),
**SYS** (system safety authority), **MTR** (motor actuator), and the steering/brake actuators
**SES/EPS-C** and **SEB**. This document is grounded in the firmware source (file:line references
given) and describes the *as-built* behaviour, including a few places where the code diverges from
older markdown specs — those are called out explicitly.

> **Trust note.** Where this document disagrees with `architecture.md`, `new-architecture.md`, or
> `CONTROLLER.md`, the firmware is authoritative. Two notable divergences:
> 1. **RM does not emit `HMI_MODE_REQ`/`HMI_PWR_REQ` (0x111/0x112).** It *emulates* the authoritative
>    `SYS_MODE_CMD` (0x110) and `SYS_PWR_CMD` (0x113) instead (`rm-esp32/src/main.cpp:194-198`).
> 2. **MTR is an open-loop throttle emulator.** There is no speed sensor, no PID, no current/temp/voltage
>    sensing in MTR firmware. `0x206.actual_speed_mmps` is the *commanded setpoint echoed back*, not a
>    measurement (`mtr-stm32/src/motor_manager.h:312-331`).

---

## 1. Nodes at a glance

| Node | HW | OS | Bus | Role |
|------|----|----|-----|------|
| **RM**  | ESP32 (classic) | FreeRTOS/ESP-IDF | Low CAN 500 k | Operator RC → CAN command source; asserts `0x001` on link loss |
| **RT**  | ESP32-S3 | FreeRTOS/ESP-IDF | **Low + High CAN** | Kinematics, steering authority, dual-bus gateway, safety monitor, ESTOP latch |
| **SYS** | ESP32-S3 | FreeRTOS/ESP-IDF | Low CAN 500 k | Mode/power authority, brake-by-wire (SEB) command, body control, **system ESTOP broadcast** |
| **MTR** | STM32G431 | superloop (no RTOS) | Low CAN 500 k | Motor actuator: relays + 12-bit DAC throttle; ESTOP latch; 500 ms comms watchdog |
| **SES / EPS-C** | steer-by-wire actuator | — | Low CAN | Steering angle actuator (consumes `0x169`) |
| **SEB** | brake-by-wire actuator | — | Low CAN | Brake stroke/pressure actuator (consumes `0x7B9`) |

Three compute classes (Jetson / RT / SYS) are intentional: different failure modes, different
realtime guarantees, different safety criticality, independent power domains
(`docs/architecture/distributed-architecture.md`).

---

## 2. CAN bus topology

```
                         HIGH CAN (500 k)                        LOW CAN (500 k)
  Jetson/Host ──────────── RT (MCP2515 via SPI) ───────────────┐
   (0x300 drive,            TWAI         │                       │
    0x301 brake,            built-in     │                       │
    0x303 steer,            ◄─────────────┤                       │
    0x400 obstacle)                      │                       │
                                         │   RT bridges Low<->High
                                         ▼                       ▼
                                   ┌─────────────── Low CAN backbone ───────────────┐
                                   │   RM ──┐                                        │
                                   │   SYS ─┤ 0x110/0x113 mode+pwr, 0x011 safety,    │
                                   │   MTR ─┤ 0x001 ESTOP (broadcast on whole bus),  │
                                   │   SES ─┤ 0x169 steer cmd, 0x201/0x202/0x6FA fbk  │
                                   │   SEB ─┘ 0x7B9 brake cmd, 0x721/0x6FB/0x731 fbk  │
                                   └────────────────────────────────────────────────┘
```

- **RT is the only node with two CAN interfaces** (`docs/architecture/distributed-architecture.md:104`):
  built-in TWAI on Low (lowest jitter for 50 Hz steering), external MCP2515 over SPI on High.
- `0x001 SAFETY_ESTOP` is a **bus-wide broadcast (DLC 0)**. Any node can originate it; every node
  that receives it latches a stop. This is the single unifying safety primitive.

---

## 3. End-to-end: how a top-level command reaches the wheels

### 3.1 Production (AUTO) path — Host → RT → MTR

```
 Operator / Jetson
      │  0x300 HOST_DRIVE_CMD {speed_mmps, yaw_rate_mrad_s, gear}   [High CAN]
      ▼
   RT ESP32  (t_control @ 100 Hz)
      │  resolve kinematics  ->  target speed (mm/s) + steer angle (0.1°)
      │  safety clamp / watchdog / obstacle limit
      │  0x204 RT_DRIVE_CMD {motor_speed_mmps, gear}                [Low CAN]
      ▼
   MTR STM32  (tick @ 5 ms)
      │  decode speed+gear; gated by mode(0x110)/power(0x113)/safety(0x011)
      │  speed(mm/s) --open-loop--> DAC voltage (0.8–2.4 V)  +  relays (Ign/Drive/Reverse)
      ▼
   External motor controller  --PWM-->  traction motor  --mechanical-->  wheels
```

The drive command **never originates at SYS**. SYS supplies *authority* (mode `0x110`, power
`0x113`, persistent safety `0x011`) and the ESTOP broadcast `0x001`. RT is the actuator authority
for MTR (via `0x204`) and for steering (via `0x169`) and brake (via `0x7B9` in AUTO). SYS *monitors*
`0x204`/`0x205` for EGAS L2 and brake-watchdog checks but does not re-transmit them
(`sys-esp32/src/main.cpp:223-475`).

### 3.2 Manual / bench (RM bypass) path — RM → MTR directly

```
 Operator RC (FlySky) ──PWM──► RM ESP32
      │  0x204 RT_DRIVE_CMD {motor_speed_mmps, gear}   [Low CAN, 20 ms]
      │  0x110 SYS_MODE_CMD / 0x113 SYS_PWR_CMD (emulated authority)
      ▼
   MTR STM32  (same as above)
```

In standalone/bench mode RM *emulates* the SYS authority frames so the stack runs without a real
SYS node (`rm-esp32/src/main.cpp:194-198`). In MANUAL production, RT is silent on the low bus and
SYS/RM drive the actuators directly.

> **Duty — RM is the isolated / bench vehicle controller.** RM is connected **only** when SYS, RT,
> and Host are absent (it is never on the bus simultaneously with them), so in that configuration it is
> the sole authority and command source for the whole vehicle. It directly commands all three actuators
> — `0x204`→MTR (motor), `0x169`→SES (steer), `0x7B9`→SEB (brake) — and emulates the SYS authority
> frames MTR expects: `0x110 SYS_MODE_CMD`, `0x113 SYS_PWR_CMD`, and `0x011 SYS_SAFETY_STS`
> (`main.cpp:206-224` for `0x110`/`0x113`, `0x011` at `main.cpp:227-240`). With **RM + MTR + SES + SEB** alone the trike is fully drivable: throttle, gear,
> steering, braking, mode/power arming, and ESTOP-on-link-loss all function, and a latched MTR ESTOP is
> released by RM's `0x011` two-frame (`estop_active==0`) sequence + the `0x113` OFF→ON REARM
> (`motor_manager.h:161-192,110-118`). RM's RC-reset sequence (Ignition OFF + Gear N, `main.cpp:89-94`)
> clears RM's local `g_can_estop_latched`; the `0x011` two-frame sequence then releases MTR's latch.
> RT/Host/SYS are not required for RM-alone operation.
>
> **ESTOP assertion:** on RC link-loss RM broadcasts `0x001` (latches MTR via A1) **and** sets
> `0x011.estop_active=1` (latches via A2); on recovery it sends two consecutive `0x011` frames with
> `estop_active=0` to release MTR, plus the `0x113` OFF→ON edge for REARM. The `0x011` E2E CRC is
> computed over bytes [0..3] (encoder does not auto-fill it, `main.cpp:227-240`, validated at
> `motor_manager.h:137`).
>
> **No self-arbitration needed:** because RM is only ever connected in isolation, the absence of a
> mode-gate (it does not read `0x110`/`0x210`) is a non-issue by deployment — there are no other senders
> to contend with. (If RM were ever co-located with SYS/RT, last-writer-wins contention would occur, but
> that configuration is not used.)

### 3.3 Steering and braking sub-paths

- **Steering:** RT computes target angle → `0x169 VCU_SES_REQ` → SES/EPS-C → road wheel angle.
- **Braking:** in AUTO, RT owns `0x7B9 VCU_SEB_REQ` (pressure/stroke). SYS suppresses its own `0x7B9`
  while RT is healthy, and *resumes* braking (max stroke) on RT-dead / ESTOP / MANUAL
  (`sys-esp32/src/main.cpp:686-756`, `brake_control.h:90-136`).

---

## 4. Controller pipelines (with block diagrams)

### 4.1 RM — operator remote / command source

**HW:** FlySky FS-i6 6-ch RC receiver → ESP32 RMT (6 ch, 50 Hz). 4 FreeRTOS tasks
(`rc_capture` p8, `can_tx` p4, `can_ctrl` p2, `heartbeat` p1). Entry `app_main()`
`rm-esp32/src/main.cpp:304`.

```
 RC PWM (RMT, 1 µs tick)
   │  rc_receiver.cpp: 4 µs glitch filter, 8 ms idle threshold
   ▼
 RcSnapshot (atomic)  rc_decoder.h
   │  signal_valid = fresh edge on CH0/1/2/4/5 within 100 ms + pulse in [900,2100] µs
   │  steering_deg = deadband±30µs -> norm -> *45°
   │  brake_stroke_mm = (raw-1520)/450 -> *27 mm
   │  throttle_norm = (raw-1050)/900  (idle cutoff 1050 µs)
   │  ignition = raw>=1500 ; gear = R/D/N by 3-pos switch
   ▼
 task_can_tx (50 Hz)  main.cpp:76-229
   │  estop_or_signal_loss = !signal_valid || estop_latched
   │  drive_active = !estop && ignition && (gear==D||R)
   │  brake-over-throttle interlock: brake>5mm -> speed=0
   ▼
 ┌─ 0x169 VCU_SES_REQ  (steer; angle_raw=round(steer_deg*10)+30000, clamp[29550,30450])
 ├─ 0x7B9 VCU_SEB_REQ  (brake; stroke_raw=(stroke+30)*20)
 ├─ 0x204 RT_DRIVE_CMD (motor; D->throttle*3000, R-> -throttle*500, gear)
 ├─ 0x110 SYS_MODE_CMD (emulated; Auto iff drive_active)
 ├─ 0x113 SYS_PWR_CMD  (emulated; On iff !estop && ignition)
 └─ 0x001 SAFETY_ESTOP  (on link-loss edge, once) ───────► whole bus
```

- **No CAN heartbeat frame is produced by RM** (`task_heartbeat` only logs locally).
- **RX:** only acts on external `0x001` (latches `g_can_estop_latched`); drops its own high-rate
  echoes (`rm-esp32/src/can_driver.cpp:21`). RM does *not* consume SYS status/heartbeat.

### 4.2 RT — realtime motion master + gateway

**HW:** ESP32-S3, dual CAN. 8 FreeRTOS tasks (`rx_low`/`rx_high` p5, `t_dispatch` p4,
`t_control` p4 @100 Hz, `t_can_tx_low`/`t_can_tx_high` p3, `t_watchdog` p1 @10 Hz,
`t_heartbeat` p1 @2 Hz). Entry `app_main()` `rt-esp32/src/main.cpp:877`.

```
 HIGH CAN 0x300/0x301/0x303/0x400/0x7FC        LOW CAN 0x001/0x011/0x110/0x201..0x206/0x721/0x6FB/0x7FE
        │                                            │
        ▼  can_dispatch.h (router + gateway)         ▼
   g_cmd_q (overwrite)  +  g_watchdog.feed(0x300)   g_safety_evt_q / stream-validity / E2E
        │                                            │
        ▼  t_control @ 100 Hz  (main.cpp:324)
   ┌─────────────────────────────────────────────────────────────┐
   │ resolver.resolve({speed_mmps, yaw}) -> (steer_angle_mdeg, sp)│
   │   PhysicsModel: v=speed/1000, w=yaw/1000, L=1.5m             │
   │     steer = atan(L*w/v) clamped ±40°  (low-speed decay ×0.8) │
   │   DirectResolver: yaw*15 mdeg/(mrad/s) clamp ±45000 mdeg     │
   │ dynamic angle clamp: 40°-(kmh-2)*(35/23) clamp[5,40]°        │
   │ obstacle_limit(speed) + obstacle_to_kpa -> brake_arbitrate() │
   │ run_safety_checks() -> may zero_setpoints / disable_steering │
   │ PID (shadow=telemetry; active=adds correction to motor_speed)│
   │ g_steering.set_target(angle, mtr_speed)  [AUTO only]         │
   └─────────────────────────────────────────────────────────────┘
        │
        ▼  t_can_tx_low (100/50 Hz)
   ┌─ 0x204 RT_DRIVE_CMD {speed_out, gear_out}  -> MTR   (speed forced 0 unless AUTO + steer OK)
   ├─ 0x169 VCU_SES_REQ  {angle_raw=angle+30000, slew 125–525°/s} -> SES
   ├─ 0x7B9 VCU_SEB_REQ  (AUTO brake / takeover on SYS-HB loss)   -> SEB
   ├─ 0x210 RT_STATE_RPT {safety_state, estop_reason, ...}
   ├─ 0x7FD RT_HEARTBEAT (2 Hz)
   └─ 0x001 SAFETY_ESTOP (on trigger, rate-limited)
   HIGH: 0x121 RT_MOTION_RPT, 0x310/0x311 diag, 0x220 PID, 0x621 diag events
```

- **Steering mapping:** internal signed 0.1° (+right); to actuator `target_angle_raw = angle + 30000`
  (`steering_control.h:234`); from actuator `angle = raw - 30000` (`can_dispatch.h:209`).
  Slew `rate = 125 + (kmh-2)*(400/23)` °/s clamp `[125,525]`
  (`steering_control.h:235-239`); ESTOP ramp-to-zero 20°/s.
- **Watchdog (who RT watches):** `g_watchdog` fed **only by `0x300`**; stale >500 ms → zero cmd +
  steering ramp (`watchdog.h:8-12`). RT also monitors SYS heartbeat `0x7FE` (200 ms → brake
  takeover), Host heartbeat `0x7FC` (1500 ms → assist stop), and `0x011` stream (700 ms → ESTOP
  latch). RT does **not** watchdog MTR (`0x206` staleness only affects the speed estimator).

### 4.3 SYS — system safety authority

**HW:** ESP32-S3. 13 FreeRTOS tasks (safety p5, dispatch p4, mode p4, gear/brake p3, lights p3,
indicator/power/can_tx p2, can_rx p5, can_control p2, diag p1, hb p1). Entry `app_main()`
`sys-esp32/src/main.cpp:1069`. Role comment: *"Safety, Motor Actuation & Body Control"*
(`main.cpp:1`).

```
 HMI/RT frames (0x111/0x112/0x204/0x205/0x206/0x210/0x7FD/0x721/0x6FB/0x731 ...)  [Low CAN]
        │  task_dispatch (StreamValidity: rolling-counter + 5 s freshness)
        ▼
 ModeManager  (MANUAL / AUTO / ESTOP)   <- sole validator of HMI request streams
        │  task_mode (10 Hz)
        ▼
 ┌─ 0x110 SYS_MODE_CMD  (mode; ESTOP clamps to MANUAL)        -> MTR / RT
 ├─ 0x113 SYS_PWR_CMD   (power_state = hmi_pwr && !estop)      -> MTR
 ├─ 0x011 SYS_SAFETY_STS (estop_active + E2E CRC-8)            -> MTR / RT   [persistent ESTOP authority]
 ├─ 0x600 SYS_DIAG_RPT, 0x7FE SYS_HEARTBEAT, lights/indicators
 └─ 0x7B9 VCU_SEB_REQ  (brake; suppressed in AUTO while RT healthy) -> SEB
 task_safety (20 Hz): hardware ESTOP btn, RT-HB loss, MTR ESTOP, SEB L3, EGAS L2, bus-off
        │  force_estop()  +  broadcast 0x001  ───────────────► whole bus
 task_brake (50 Hz): 0x205 -> SEB Command (pressure raw=(kPa+25)/50, stroke raw=(mm+30)*20)
```

- **SYS disables actuation by five coordinated signals** (no separate "disable" frame): broadcast
  `0x001`, set `0x110=MANUAL`, set `0x113=OFF`, drive `0x7B9` max stroke (~27 mm), cut 12 V relay
  (`main.cpp:858`).
- **EGAS L2:** `|0x204.setpoint − 0x206.actual| > 500 mm/s` for >500 ms in AUTO → ESTOP
  (`main.cpp:516-546`).

### 4.4 MTR — motor actuator

**HW:** STM32G431, **superloop** (no RTOS), 16 MHz HSI. Single `while(1)` with `HAL_Delay(1)`
(`mtr-stm32/src/main.cpp:96`). CAN RX ISR → 32-deep ring → drained each loop. Entry `main()`
`main.cpp:67`.

```
 CAN RX ring (filtered: only 0x001, 0x011, 0x110, 0x113, 0x204)   [Low CAN 500 k]
   │  g_can.poll_rx -> g_motor.handle_frame
   ▼
 motor_manager.tick(now_ms)  @ 5 ms   (main.cpp:106)
   ┌─ global fail-safe gate: estop || comms_timeout || !power || !safety || un-rearmed
   │      -> target=0, relays Off, DAC force_zero()        (motor_manager.h:219-225)
   ├─ mode gate: !mode_valid -> target=0 (power kept)
   ├─ shift interlock: D<->R change inserts 50 ms dwell @ N, DAC=0 (kShiftDwellMs)
   ├─ direction sign verify: D needs speed>0, R needs speed<0
   └─ calculate_dac_code_(speed_mag):
         norm = clamp(|speed|/max, 0..1)  (max 3000 fwd / 500 rev)
         code = 700 + norm*(1966-700), clamp [655, 1966]
   ▼
 DAC (MCP4725, SW-I2C)  -> analog throttle 0.8–2.4 V  ──► external motor controller
 Relays (active-low): Ignition(PA4) / Drive(PA2) / Reverse(PA0); D & R mutually exclusive
   ▼
 External motor controller --PWM--> traction motor --mechanical--> wheels
   TX: 0x120 SYS_THROTTLE_STS (100 Hz), 0x206 MTR_MOTOR_FBK (50 Hz), 0x631 diag (event)
```

- **Open-loop:** no PID, no speed feedback, no current/temp/voltage sensing. The "actual speed" in
  `0x206` is the commanded setpoint echoed back.
- **Comms watchdog:** 500 ms silence on *any* frame → `comms_timed_out_` → fail-safe disable
  (`motor_manager.h:197`). No hardware IWDG exists.

### 4.5 Actuators (brief)

- **SES / EPS-C (steering):** receives `0x169`; reports `0x201 SbwStatus` (angle = raw−30000),
  `0x202 SbwErrInfo` (L3 → RT ESTOP), `0x6FA SbwTest` (current/temp/V), `0x203 version`.
- **SEB (brake):** receives `0x7B9` (stroke raw=(mm+30)*20 or pressure raw=(kPa+25)/50); reports
  `0x721 SebStatus` (error_status L3 → RT/SYS ESTOP), `0x6FB SebTest`, `0x731 SebErrInfo` (16 L3
  bits → SYS ESTOP), `0x741 version`.

---

## 5. Data manipulation / signal scaling

All numeric transforms are open-loop mappings (no closed-loop correction inside MTR/RT except the
shadow PID in RT telemetry). Key scaling tables:

| Quantity | Formula / mapping | Source |
|----------|------------------|--------|
| MTR speed → DAC | `code = 700 + clamp(\|v\|/max,0,1)*(1966−700)`, clamp `[655,1966]`; max=3000 fwd / 500 rev; <50 mm/s deadband | `motor_manager.h:348-366`, `shared_config.h` |
| DAC code → voltage | 12-bit, ~0.8 V (655) … 2.4 V (1966); floor 700 ≈ 0.855 V clears deadband | `config.h:32-33` |
| Steering angle | to actuator `raw = angle_0.1deg + 30000` (0°→30000); from actuator `angle = raw − 30000` | `steering_control.h:234`, `can_dispatch.h:209` |
| Steering slew | `125 + (kmh−2)*(400/23)` °/s, clamp `[125,525]` | `steering_control.h:235-239` |
| Steering dynamic clamp | `40 − (kmh−2)*(35/23)` °, clamp `[5,40]` | `config.h:33-37`, `main.cpp:432` |
| Brake stroke (mm→raw) | `raw = (mm + 30) / 0.05` → 0 mm=600, 27 mm=1140 | `shared_config.h:36-37`, `brake_control.h` |
| Brake pressure (kPa→raw) | `raw = (kPa + 25) / 50`, clamp 100 | `brake_control.h:122-123`, `shared_config.h:39` |
| Yaw → steer (direct) | `steer_mdeg = yaw_mrad_s * 15`, clamp ±45000 mdeg | `direct_resolver.cpp:17,23` |
| Yaw → steer (bicycle) | `steer = atan(L*w/v)`, L=1.5 m | `physics_model.cpp:52` |
| RC steering | `norm = clamp((raw−1500)/450,−1,1) * 45°`, ±30 µs deadband | `rc_decoder.h:46-54` |
| RC throttle | `norm = clamp((raw−1050)/900,0,1)`, idle cutoff 1050 µs | `rc_decoder.h:66-74` |
| RC brake | `norm = clamp((raw−1520)/450,0,1) * 27 mm` | `rc_decoder.h:56-64` |
| EGAS L2 | `|cmd−actual| > 500 mm/s` for >500 ms | `config.h:67-68` |
| SEB ECU temp | `raw*0.5 − 40` °C; warn > 80 °C | `main.cpp:411-415` |
| E2E | `0x011` CRC-8 (poly 0x2F, Data-ID 0x3C11) over bytes [0..3] | `e2e.hpp:21-51`, `motor_manager.h:137` |

**Stream freshness (`StreamValidity`):** rolling-counter sequence (delta 1/2 accept, 0 = duplicate,
>2 = fault) + timeout. MTR: `0x110`/`0x113` 500 ms, `0x011` 700 ms. SYS HMI: 5000 ms.

---

## 6. ESTOP

### 6.1 ESTOP propagation model

`0x001 SAFETY_ESTOP` (DLC 0) is the universal stop primitive. Two flavours of stop exist:

- **Latched ESTOP** (vehicle must be explicitly re-armed): asserted by `0x001` or `0x011.estop_active`
  at MTR/RT; by hardware button / RT-loss / MTR-loss / SEB-L3 / EGAS / bus-off at SYS; by RM link-loss.
- **Fail-safe disable (not latched)**: e.g. MTR 500 ms command-timeout, RT `0x300` stale, SYS MTR
  feedback staleness — auto-clears on next valid frame.

```
 any node asserts 0x001 ──broadcast──► all nodes latch/disable
 SYS also broadcasts 0x001 on its own triggers (rate-limited 250 ms/ECU)
 RT forwards 0x001 across the Low<->High bridge
```

> **Known inconsistency (flagged):** SYS `0x011.estop_active` / `0x7FE.estop_active` reflect **only the
> hardware ESTOP button** (`g_safety.m_estop`), not CAN `0x001`/SEB-L3/EGAS/bus-off ESTOPs — those only
> latch the *mode*. MTR/RT consume `0x011.estop_active` as the authoritative stop, so a SYS software
> ESTOP disables MTR via `0x110=MANUAL` + `0x113=OFF` + `0x001`, but `0x011.estop_active` stays false
> until the physical button is pressed (`sys-esp32/src/main.cpp:882,985`).

### 6.2 ESTOP triggers per controller

#### MTR (`mtr-stm32`)

| # | Trigger | Source frame / signal | Condition | Action |
|---|---------|-----------------------|-----------|--------|
| A1 | Explicit safety ESTOP | `0x001` (DLC0) | any receipt | **Latched** ESTOP: relays Off, DAC 0 |
| A2 | Persistent ESTOP authority | `0x011.estop_active==1` | asserted | **Latched** ESTOP (`trigger_estop`) |
| B1 | Command timeout (deadman) | no frame for 500 ms | any ID | Fail-safe disable (not latched); `0x206` flag `0x02` |
| B2 | `0x011` silent | `0x011` absent > 700 ms | freshness | `safety_state_valid_=false` → disable |
| B3 | `0x011` E2E CRC error | `0x011` bad CRC | `motor_manager.h:138` | `safety_state_valid_=false` → disable |
| B4 | `0x011` counter stale | duplicate/frozen/reorder | `StreamValidity` | disable |
| B5 | Mode authority lost | `0x110` stale/sequence-fault | `motor_manager.h:76` | inhibit drive (power kept) |
| B6 | Power authority lost | `0x113` stale/sequence-fault | `motor_manager.h:105` | power-safe disable (relays Off, DAC 0) |
| B7 | Un-rearmed recovery | after clear, REARM not met | `motor_manager.h:219` | disable even if latch released |
| B8 | `0x204` while mode invalid | `0x204` + `!mode_valid_` | `motor_manager.h:91` | inhibit; raises `MtrCmdStreamUnauthorised` |
| B9 | CAN bus-off | FDCAN PSR BO | recovery | hardware reset; if frames stop → B1 |
| B10 | RX ring overflow | >32 frames | `can_driver.h:163` | raises `MtrFdcanRxOverflow` (possible missed cmd → B1) |

*Not detectable in MTR firmware (no ADC/current/temp/V input):* overcurrent, overtemperature,
undervoltage, hardware safety-line. Those must arrive as `0x001`/`0x011` from upstream.

#### SYS (`sys-esp32`)

| # | Trigger | Source frame / signal | Condition | Resulting action |
|---|---------|-----------------------|-----------|------------------|
| 1 | Hardware ESTOP button | GPIO1 (active-high, NC) | level==1 | `force_estop()` + broadcast `0x001` |
| 2 | RT heartbeat loss | `0x7FD` | >1000 ms (3 s grace) | `force_estop()` + `0x001` |
| 3 | MTR ESTOP-active flag | `0x206.fault_flags` bit `0x01` | set & mode≠ESTOP | `force_estop()` + `0x001` |
| 4 | CAN `0x001` from any node | `0x001` | receipt | `force_estop()` (frame itself is the broadcast) |
| 5 | SEB L3 fault | `0x731` (16 L3 bits) | any set | `force_estop()` + `0x001` |
| 6 | EGAS L2 mismatch | `0x204` vs `0x206` | `>500 mm/s` for >500 ms (AUTO) | `force_estop()` + `0x001` |
| 7 | MTR ESTOP-ACK timeout | `0x206` | ESTOP sent but MTR hasn't ACKed in 100 ms | retrigger `force_estop()` + `0x001` |
| 8 | MTR feedback staleness | `0x206` | absent > 200 ms | zero speed+neutral (drive disabled, **not** full ESTOP) |
| 9 | SEB `error_status` L3 | `0x721` byte0 b6-7 | `es>=3` | `brake_fault=true` (logged, not full ESTOP) |
| 10 | Brake following-error | `0x721` vs cmd | `>60 raw` (3 mm) for >100 ms | `brake_fault=true` (logged) |
| 11 | CAN bus-off persistent | TWAI `BusOff` | ≥5 consecutive | `force_estop()` + `0x001` |
| 12 | SEB status/test loss | `0x721`/`0x6FB` | >100 ms | warn only |

*Monitored but not a disable:* gear mismatch in AUTO (logged, 500 ms debounce); RT internal-ESTOP
(`0x210`) → SYS *resumes* brake, not SYS ESTOP; task-deadline miss (logged only); CAN error-passive
(warn only).

#### RT (`rt-esp32`)

| # | Trigger | Source frame / signal | Condition | Resulting action |
|---|---------|-----------------------|-----------|------------------|
| 1 | CAN `0x001` | any node | receipt | ESTOP event, latch, forward cross-bus |
| 2 | Persistent ESTOP authority | `0x011.estop_active` | asserted | **Latched** ESTOP (asymmetric 2-frame clear) |
| 3 | EPS-C L3 fault | `0x202 SbwErrInfo` | angle/torque L3 | Internal ESTOP (`kEstopReasonInternal`) |
| 4 | SEB L3 fault | `0x721 SebStatus` | `error_status==3` | Internal ESTOP |
| 5 | `0x011` stream loss | `0x011` absent > 700 ms | freshness | Fail-safe ESTOP latch |
| 6 | Host drive stale (watchdog) | `0x300` absent > 500 ms | `g_watchdog` | zero cmd + steering ramp (disable, not latched) |
| 7 | SYS heartbeat loss | `0x7FE` | > 200 ms | SEB brake takeover (assist stop) |
| 8 | Host heartbeat loss | `0x7FC` | > 1500 ms | assist stop |
| 9 | Steering safety check | `run_safety_checks` | angle out of dynamic clamp / following error | zero setpoints / disable steering |
| 10 | Obstacle limit | `0x400` | distance < threshold | speed limit + brake arbitrate |

#### RM (`rm-esp32`)

| # | Trigger | Source | Condition | Action |
|---|---------|--------|-----------|--------|
| 1 | RC signal loss (deadman) | decoder | no fresh edge on CH0/1/2/4/5 within 100 ms | broadcast `0x001` once + safe outputs (brake 27 mm, motor TX off, mode Manual, power 0) |
| 2 | External `0x001` | any peer | receipt (after loopback credit) | latch `g_can_estop_latched` (persistent stop) |
| — | Ignition OFF | operator | `ignition==false` | `drive_active=false`, `0x113=0` (normal, not ESTOP) |
| — | Gear Neutral | operator | `gear==N` | `0x204` gear=N (normal, not ESTOP) |
| — | Brake > 5 mm | operator | brake override | motor target speed=0 (interlock, not ESTOP) |

*Not present in RM:* SYS-heartbeat-loss ESTOP, low-battery monitor, mode-violation ESTOP, internal
watchdog that asserts `0x001` (only the MTR-side 500 ms watchdog is referenced).

### 6.3 Reset / recovery per controller

| Controller | How ESTOP / disable is cleared |
|------------|--------------------------------|
| **MTR** | Latched ESTOP (A1/A2) cleared **only** by `0x011.estop_active==0` on **two consecutive fresh, advancing frames** (`clear_confirm_>=2`) → `authorized_clear()`, which then requires a `0x113` **OFF→ON edge** with a fresh `0x110` (REARM sequence) before motion resumes (`motor_manager.h:161-192`, `110-118`). Fail-safe B-class (timeout/CRC/counter) **auto-clears on next valid frame**. Power cycle re-runs `init()` and clears all state. |
| **SYS** | ESTOP latch cleared **only** by the physical **START button (GPIO41)** falling edge, or **MODE button 3 s long-press (GPIO11)**, or power cycle. CAN `0x111`/`0x110` **cannot** clear ESTOP (`mode_manager.cpp:72-85`). `brake_fault` auto-clears after 3 s of all-healthy. |
| **RT** | Vehicle ESTOP latch (`0x001`/`0x011`) cleared by the `0x011` **two-frame `estop_active==0`** sequence (asymmetric). Steering/internal ESTOP (`kEstopReasonInternal`, `run_safety_checks`) is additionally released by a valid Host drive command (`g_steering_exit_request` from `0x300`). Power cycle clears all. |
| **RM** | External-ESTOP latch (`g_can_estop_latched`) cleared **only** by the RC reset sequence: valid link **+ Ignition OFF + Gear Neutral** (`main.cpp:89-94`). Signal-loss deadman auto-clears when the RC link returns. Own `0x001` loopback is consumed via a 50 ms credit window so it cannot re-latch. |

---

## 7. Watchdog & liveness summary

| Controller | What it watches | Timeout | On trip |
|-----------|-----------------|---------|---------|
| MTR | any CAN frame (deadman) | 500 ms | fail-safe disable (auto-clear) |
| MTR | `0x011` safety stream | 700 ms | disable |
| MTR | `0x110`/`0x113` authority | 500 ms | inhibit / power-safe disable |
| SYS | RT heartbeat `0x7FD` | 1000 ms (3 s grace) | ESTOP |
| SYS | MTR feedback `0x206` | 200 ms | zero speed + neutral (not full ESTOP) |
| SYS | SEB status `0x721`/`0x6FB` | 100 ms | warn |
| SYS | multi-task aliveness | per task | logged only |
| RT | Host drive `0x300` (`g_watchdog`) | 500 ms | zero cmd + steer ramp |
| RT | Host heartbeat `0x7FC` | 1500 ms | assist stop |
| RT | SYS heartbeat `0x7FE` | 200 ms | SEB brake takeover |
| RT | `0x011` stream | 700 ms | ESTOP latch |
| RM | RC link (per-channel edge) | 100 ms | broadcast `0x001` + safe outputs |

No hardware independent watchdog (IWDG) is implemented on MTR or SYS; liveness is purely
software/communications-based. SYS's external `g_wdt.tick()` on GPIO23 is present but
commented-out (`sys-esp32/src/main.cpp:40,514`).

---

## 8. Key file index

- **MTR:** `mtr-stm32/src/main.cpp`, `motor_manager.h`, `can_driver.h`, `relay_controller.h`,
  `dac_controller.h`, `config.h`.
- **SYS:** `sys-esp32/src/main.cpp`, `mode_manager.cpp`, `safety_monitor.cpp`, `brake_control.h`,
  `light_control.h`, `can_driver.cpp`, `config.h`.
- **RT:** `rt-esp32/src/main.cpp`, `can_dispatch.h`, `steering_control.h`, `phase2_motion.h`,
  `safety_monitor.h`, `watchdog.h`, `can_health.h`, `physics_model.cpp`, `direct_resolver.cpp`.
- **RM:** `rm-esp32/src/main.cpp`, `rc_receiver.cpp`, `rc_decoder.h`, `can_driver.cpp`, `config.h`.
- **Protocol:** `protocol/generated/cpp/etrike_protocol.hpp`, `protocol/codecs/{ses,seb}.hpp`,
  `protocol/compat/can_protocol.hpp`, `shared/{stream_validity.h,diagnostics.h,shared_config.h}`.
- **Architecture reference:** `docs/architecture/distributed-architecture.md`,
  `docs/communications/controller-io-can-details.md`, `docs/architecture/diagnostic-event-plane.md`.

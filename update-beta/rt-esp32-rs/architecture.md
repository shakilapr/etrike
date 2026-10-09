# `rt-esp32-rs` — RT Firmware Architecture

**Target:** ESP32-S3.
**Layer:** Shell (top). Owns hardware; delegates all decisions to `etrike-core`.
**Status:** Phase −1 design. Implementation begins at P4, after SYS-RS (P3) is
on the bench.

---

## 1. Role

RT is the **realtime motion master** and the fleet's only **dual-bus gateway**.
It:

- Runs the tricycle kinematics: converts Host drive commands into an RT drive
  command (`0x204`) and a steering command (`0x169`).
- Owns the steering-by-wire state machine and the dynamic angle clamp / slew
  limit.
- Expresses brake **intent** on `0x205` (SYS arbitrates and owns `0x7B9`).
- Bridges the High bus (Host/Jetson) and the Low bus (SYS/MTR/SES/SEB).
- Runs the command-staleness watchdog and obstacle speed limiter.
- Reports motion (`0x121`), state (`0x210`), node status (`0x501`), steering/
  brake diagnostics (`0x310`/`0x311`), and heartbeat (`0x7FD`).

RT powers up with **no drive authority** and cannot command motion until SYS
publishes a valid `0x011` safety stream and a valid `0x110` mode stream.

RT is **not** a normal `0x7B9` producer. The C++ `brake_fallback.h` emergency
takeover is dead code and is **dropped** (see §8).

---

## 2. Hardware map

CAN — Low bus, built-in TWAI, 500 kbit/s:

| Signal | GPIO |
|--------|------|
| CAN TX | 5 |
| CAN RX | 4 |

CAN — High bus, external MCP2515 over SPI, 500 kbit/s:

| Signal | GPIO |
|--------|------|
| SPI SCK | 15 |
| SPI MOSI | 16 |
| SPI MISO | 17 |
| SPI CS | 18 |
| MCP2515 INT | 47 |

Other:

| Signal | GPIO | Notes |
|--------|------|-------|
| WS2812 RGB | 48 | onboard DevKitC-1 |
| Dev override | 42 | bench control-path selection |
| Encoders (optional) | 1/2, 10/6, 9/12, 13/14 | quadrature PCNT; vehicle has none fitted |

---

## 3. Task model (embassy)

The C++ firmware ran 3 tasks (`can_high`, `can_low`, `control`), all globals in
one translation unit. RT-RS keeps the same three logical roles but with a
single-owner core.

| Task | Core | Owns | Behaviour |
|------|------|------|-----------|
| `can_low` | 0 | TWAI (Low) | RX verify → events; drain Low TX channel; gateway L→H forwarding |
| `can_high` | 0 | MCP2515 (SPI + INT) | RX verify → events; drain High TX channel; gateway H→L queue |
| `control` | 1 | `RtCore` (sole owner) | Fixed 100 Hz tick; `step()`; publish outputs to both buses |
| `led` | 1 | WS2812 | Render status pattern from `RtCore` output |
| `watchdog` | 1 | task liveness | Feed WDT; missed deadline → `TaskDeadlineMissed` input |

```
can_low  ─RawFrame(low)─┐
can_high ─RawFrame(high)┤──► ch(RAW/EVT) ──► control(RtCore::step) ──► ch(TX_LOW)  ──► can_low
io/hb    ─InputEvent────┘                        │                    └► ch(TX_HIGH) ──► can_high
                                                 └──► ch(LED) ──► led
```

- `RtCore` is owned by `control`; `can_low`/`can_high` never share logic state.
  The C++ `g_estop_reason` (written by four contexts) and the dual
  queue/atomic safety path cannot exist.
- Single SPI owner: only `can_high` touches the MCP2515, so the C++ SPI-mutex
  contention and priority inversion are gone by construction.

---

## 4. The control task and `RtCore`

```rust
loop {
    let deadline = tick_at(100.hz());
    let inputs = assemble_inputs(&rx_low, &rx_high, &sensors, now());
    let outputs = core.step(inputs, now());
    publish_low(&tx_low, outputs.low());
    publish_high(&tx_high, outputs.high());
    publish_led(&led, outputs.led());
    Timer::after_until(deadline).await;
}
```

`RtInputs`/`RtOutputs` follow the same shape as SYS; decision logic lives in
`etrike-core::rt` (`steering`, `kinematics`, `obstacle`, `brake_intent`,
`supervisor`).

---

## 5. Authority and safety supervisor

RT mirrors SYS's authority rather than deciding its own mode.

- `0x011` (SYS_SAFETY_STS) is verified (CRC + rolling counter) and tracked by
  `StreamValidity`. On boot RT has **no authority**; it acquires authority only
  after a valid, advancing `0x011` stream and a valid `0x110` mode stream.
- **Asymmetric ESTOP clear:** one frame enters ESTOP; clearing requires **two
  consecutive advancing `0x011` frames with `estop_active == 0`**. RT never
  publishes a clear based on its own state.
- `0x7FE` (SYS heartbeat) loss beyond the timeout withholds motion authority.
- `0x7FD` (RT heartbeat) is published independently on each bus so Host and SYS
  can supervise RT.

These become a `DriveAuthority` token: motion outputs (`0x204`, `0x169`) can
only be produced while the token is present.

---

## 6. Steering and kinematics

- `SteeringController` is a state machine: `BootWait → ListenSync → Active →
  {EstopRampToZero, EstopHoldThenSilent} → SilentStop/Fault`. Alignment requires
  an aligned `0x201` from the SES; a sync timeout faults.
- `Kinematics` selects a resolver at compile time:
  - **Physics** — inverse bicycle model with the dynamic angle clamp
    `limit = 40 − (kmh − 2)·(35/23)`, clamped `[5, 40]`°, and slew rate
    `125 + (kmh − 2)·(275/23)`, clamped `[125, 400]`°/s.
  - **Direct** — yaw→steer passthrough for bench/HIL.
- The obstacle limiter maps `0x400` obstacle distance to a speed cap and brake
  escalation, then hands a brake **intent** to the arbiter.
- RT sends intent only: `0x204` (drive) and `0x205` (brake kPa). It never writes
  `0x7B9`.

---

## 7. Gateway

RT is the only dual-bus node. Routing is driven by the generated `ROUTES` table
(never ad-hoc ID comparisons):

- **Low → High (transparent, `same_frame`):** `0x001`, `0x011`, `0x120`,
  `0x206`, `0x500`, `0x600`, `0x601`, `0x502`.
- **High → Low (transparent, `same_frame`):** `0x001`, `0x111`, `0x112`,
  `0x302`, `0x114`.
- **Consumed + regenerated on the other bus:** `0x300`→`0x204`+`0x169`,
  `0x301`→`0x205`.
- **Bus-local / independent:** heartbeats, `0x210`, `0x501`, `0x621`.

Implementation: two `Channel<RawFrame>` (H→L and L→H) with receive timestamps.
Frames older than the freshness window are dropped rather than retried. The C++
`gw_pump` retry daemon and its five-slot reclaim hack are removed; the TWAI
driver runs with hardware auto-retransmit.

---

## 8. `0x7B9` decision (single-producer)

The canonical contract (`protocol/contracts/seb.yaml`) declares SYS as the only
sender of `0x7B9`. The C++ `brake_fallback.h` described an RT emergency takeover
but was never instantiated, and `g_last_0x7B9_rx_us` was never written — the
feature did not exist in the shipped firmware.

RT-RS adopts the single-producer model:

- RT emits `0x205` (intent) only.
- RT never emits `0x7B9`, not on startup, not on SYS heartbeat loss, not on
  SEB comms loss.
- If SYS's brake stream is absent, RT withholds motion authority and escalates
  its own safety outputs; brake actuation remains SYS's responsibility.

This removes a dead, contradictory code path and the `kSebFallbackGuardMs` /
`kSebHandoffGraceMs` ambiguity.

---

## 9. CAN contract

**RX (High bus):** `0x7FC` HOST_HEARTBEAT, `0x001`, `0x300` HOST_DRIVE_CMD,
`0x303` HOST_STEER_CMD, `0x301` HOST_BRAKE_REQ, `0x400` HOST_OBSTACLE_DIST,
plus gateway H→L frames listed in §7.

**RX (Low bus):** `0x7FE` SYS_HEARTBEAT, `0x011` SYS_SAFETY_STS, `0x110`
SYS_MODE_CMD, `0x001`, `0x206` MTR_MOTOR_FBK, `0x201` SES_STATUS, `0x202`
SES_ERR_INFO, `0x6FA` SES_TEST, `0x721` SEB_STATUS, `0x6FB` SEB_TEST.

**TX (Low bus):** `0x204` RT_DRIVE_CMD (100 Hz), `0x205` RT_BRAKE_CMD (50 Hz),
`0x169` VCU_SES_REQ (50 Hz), `0x501` RT_NODE_STATUS (50 Hz), `0x7FD`
RT_HEARTBEAT (2 Hz), `0x210` RT_STATE_RPT (10 Hz), gateway H→L frames.

**TX (High bus):** `0x121` RT_MOTION_RPT (100 Hz), `0x210` RT_STATE_RPT (10 Hz),
`0x501` RT_NODE_STATUS (10 Hz), `0x310` STEER_DIAG (10 Hz), `0x311` BRAKE_DIAG
(10 Hz), `0x620` RT_DIAG_RPT (1 Hz), `0x621` RT_DIAG_EVENT_RPT (event), `0x7FD`
RT_HEARTBEAT (2 Hz), forwarded L→H frames.

`TX_OWNERS` from the generated codec asserts that no ID is sent by two owners.

---

## 10. C++ → Rust mapping

| C++ (rt-esp32/src) | Rust |
|--------------------|------|
| `main.cpp` task bodies (3 tasks) | `rt-esp32-rs/src/tasks/*` + `etrike-core::rt` |
| `steering_control.h` | `etrike-core::rt::steering` |
| `physics_model.{h,cpp}`, `direct_resolver.{h,cpp}` | `etrike-core::rt::kinematics` |
| `brake_arbitration.h`, `seb_request.h` | `etrike-core::rt::brake_intent` |
| `safety_monitor.h`, `safety_stream_loss.h` | `etrike-core::rt::supervisor` |
| `watchdog.h`, `heartbeat.h` | `etrike-core::rt` (+ shell timers) |
| `can_driver_twai.{h,cpp}` | `rt-esp32-rs` shell (esp-hal TWAI) |
| `can_driver_mcp2515.{h,cpp}` | `rt-esp32-rs` shell (embedded-hal-async SPI) |
| `rt_status_led.h` | `rt-esp32-rs` shell (`led` task) |
| `controller` / `diag_rt.h` | `0x210`/`0x620` outputs + P5 diagnostics |
| `brake_fallback.h`, `can_rx_router.h` | **removed** (dead code) |
| `encoder_pcnt.{h,cpp}` | deferred (no encoders fitted); clean PCNT module when needed |
| `pid_controller.h`, `speed_controller.h` | deferred (shadow PID disabled) |

Resolved defects: multi-writer globals, the dual queue/atomic safety path, SPI
contention, unwired `0x7B9` takeover, and dead routing code.

---

## 11. Deferred features

- PID speed loop and `0x220 RT_PID_RPT` (disabled in C++).
- Encoders / `0x122` wheel speed.
- Full diagnostics event plane (`0x620`/`0x621`).
- LED priority-cascade patterns (simple state pattern first).

---

## 12. Test plan

- **Host:** `etrike-core::rt` steering/kinematics/obstacle/supervisor invariants
  (governor tests derived from `rt-esp32/test/*`); protocol round-trips.
- **Bench:** TWAI (Low) loopback; MCP2515 (High) driver via SPI with INT;
  gateway round-trip both directions; heartbeat supervision; steering ramp on
  ESTOP; asymmetric `0x011` clear requires two clean frames.
- **Parity:** replay RT CAN logs through the C++ and Rust cores; compare
  `RtOutputs` (P5).

---

## 13. References

- `update-beta/architecture.md` — workspace overview and invariants.
- `update-beta/crates/etrike-core/architecture.md` — `RtCore` module (reserved).
- `update-beta/crates/etrike-protocol/architecture.md` — frame validation.
- `update-beta/sys-esp32-rs/architecture.md` — peer authority semantics.
- `rt-esp32/architecture.md`, `rt-esp32/src/config.h` — pins and behaviour
  (note: the C++ doc's 8-task description is stale).
- `protocol/contracts/rt.yaml`, `protocol/contracts/network.yaml` — routes.
- `can-dictionary.md` — High/Low bus message catalog.

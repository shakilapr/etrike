# `sys-esp32-rs` — SYS Firmware Architecture

**Target:** ESP32-S3-DevKitC-1-N16R8.
**Layer:** Shell (top). Owns hardware; delegates all decisions to `etrike-core`.
**Status:** Phase −1 design. Implementation begins at P3, after the pure core is
proven (P2).

---

## 1. Role

SYS is the vehicle's **safety, mode, and power authority** and the body
controller. It:

- Owns the ESTOP latch and the only authenticated exit from ESTOP.
- Owns the MANUAL/AUTO mode decision and the 12 V / HV power command.
- Is the **sole normal producer** of `0x7B9` (brake-by-wire actuator command).
- Reads the hardwired ESTOP button, brake lever, START latch, MODE button, and
  handlebar light switches.
- Drives the body outputs: 12 V relay, mode/ready/ESTOP/bypass bulbs, brake
  lamp, and the onboard WS2812 status LED.
- Reports status on `0x011`, `0x110`, `0x113`, `0x500`, `0x600`, `0x7FE`.

SYS does **not** command motor or steering. It consumes RT's `0x205` brake
intent and arbitrates the final brake command.

---

## 2. Hardware map

CAN — Low bus only, built-in TWAI, 500 kbit/s:

| Signal | GPIO |
|--------|------|
| CAN TX | 5 |
| CAN RX | 4 |

Inputs (all fail-safe polarities per `sys-esp32/src/config.h`):

| Signal | GPIO | Notes |
|--------|------|-------|
| ESTOP button | 1 | NC to GND, pull-up; open/press reads HIGH = active |
| Brake lever | 2 | active-low, pull-up |
| START latch | 41 | NC latching; latched reads HIGH = run enabled |
| MODE button | 11 | momentary, toggles MANUAL↔AUTO; 5 s hold = validated reset |
| Switch: left turn | 9 | |
| Switch: right turn | 6 | |
| Switch: headlight | 7 | |

Outputs:

| Signal | GPIO | Notes |
|--------|------|-------|
| Brake lamp | 21 | |
| Bulb AUTO | 10 | |
| Bulb MANUAL | 39 | |
| Bulb READY | 17 | |
| Bulb ESTOP | 18 | |
| Bulb BYPASS | 14 | |
| 12 V relay | 40 | active-LOW module |
| WS2812 RGB | 48 | onboard DevKitC-1 |

Relay outputs are **active-low**: a LOW pin energises the relay/lamps. All
polarity inversion happens in the shell's `set_relay`; the core works in
positive logic.

---

## 3. Task model (embassy)

The C++ firmware ran 12 tasks. SYS-RS runs a small, single-owner set. No task
touches `SysCore` except the control task.

| Task | Core | Owns | Behaviour |
|------|------|------|-----------|
| `twai_rx` | 0 | TWAI RX | Receive → `verify()` → send `RawFrame`/event to control |
| `io` | 0 | GPIO inputs | Debounce buttons/switches → `InputEvent` to control |
| `twai_tx` | 0 | TWAI TX | Drain output channel → serialise → send (retry safety-critical) |
| `body` | 0 | lamps/relays + WS2812 | Apply `GpioOutputs`; render LED pattern |
| `control` | 1 | `SysCore` (sole owner) | Fixed 100 Hz tick + event wake; `step()`; publish outputs |
| `watchdog` | 1 | task liveness | Feed task WDT; on missed deadline send `TaskDeadlineMissed` to control |

```
twai_rx ─RawFrame─┐
io/btn  ─InputEvent┤──► ch(RAW/EVT) ──► control(SysCore::step) ──► ch(TX) ──► twai_tx
watchdog─TaskMissed┘                        │
                                            └──► ch(BODY) ──► body (lamps/LED)
```

Design consequences:

- **No shared logic state.** `SysCore` is owned by `control`; other tasks have no
  reference to it. The C++ multi-writer globals cannot exist.
- **No priority inversion.** RX never blocks TX; both are independent embassy
  tasks, and the decision task is separate. The 5 ms `xQueueSend` workaround is
  unnecessary.
- **The watchdog cannot lie.** A missed deadline is an input to the core, and
  the core — not a task — decides to latch ESTOP.

---

## 4. Boot sequence

1. Configure all outputs to their **safe (de-energised) state** before enabling
   any driver. Lamps/relays off.
2. Read the run-mode / bypass configuration (production vs bench).
3. Initialise TWAI; enable the transceiver only after RX is ready.
4. Initialise the WS2812 status LED.
5. Construct `SysCore` in a defined initial state: ESTOP **not** latched, mode
   MANUAL, power OFF, no authority granted, all `node_presence` bits clear.
6. Spawn tasks; hand the output channels to `twai_tx` and `body`.
7. The core grants no authority until it has observed the required streams (RT
   heartbeat, SEB status) for the configured acquisition windows.

This mirrors the C++ intent (outputs off first, no authority at boot) without
the ad-hoc ordering the C++ relied on.

---

## 5. The control task and `SysCore`

```rust
loop {
    let deadline = tick_at(100.hz());
    // drain events + verified frames into SysInputs
    let inputs = assemble_inputs(&rx, &io, now());
    let outputs = core.step(inputs, now());
    publish(&tx, &body, outputs);
    Timer::after_until(deadline).await;
}
```

`SysInputs`/`SysOutputs` are defined in
`update-beta/crates/etrike-core/architecture.md` §4. The shell only:

- builds `SysInputs` from `Valid<TypedMsg>` (never raw frames),
- feeds debounced `PhysicalInputs`,
- serialises `SysOutputs` to CAN frames and GPIO.

---

## 6. CAN contract

**RX (Low bus), consumed locally** — all decoded via generated codecs and
validated before entering `SysInputs`:

| ID | Name | Used for |
|----|------|----------|
| `0x204` | RT_DRIVE_CMD | setpoint mirror, staleness, optional EGAS |
| `0x205` | RT_BRAKE_CMD | brake intent for arbitration |
| `0x111` | HMI_MODE_REQ | mode request (StreamValidity-gated) |
| `0x112` | HMI_PWR_REQ | power request (StreamValidity-gated) |
| `0x114` | HOST_ESTOP_RESET_REQ | authenticated reset request |
| `0x206` | MTR_MOTOR_FBK | applied command, fault flags, ESTOP_ACTIVE ack |
| `0x302` | HOST_LIGHT_CMD | light command bits |
| `0x001` | SAFETY_ESTOP | external ESTOP |
| `0x721` | SEB_STATUS | brake actuator status/alignment |
| `0x731` | SEB_ERR_INFO | SEB L3 faults (latched) |
| `0x741` | SEB_VERSION | one-time log |
| `0x210` | RT_STATE_RPT | RT safety state |
| `0x122` | RT_WHEEL_SPEED_STS | optional physical EGAS |
| `0x7FD` | RT_HEARTBEAT | RT liveness |
| `0x501` | RT_NODE_STATUS | RT READY / presence |
| `0x502` | MTR_NODE_STATUS | MTR READY / presence |

**TX (Low bus)** — SYS is the single owner of each; enforced by `TX_OWNERS`:

| ID | Name | Rate |
|----|------|------|
| `0x001` | SAFETY_ESTOP | event, rate-limited |
| `0x110` | SYS_MODE_CMD | 100 ms |
| `0x113` | SYS_PWR_CMD | 100 ms |
| `0x115` | SYS_ESTOP_RESET_RSP | event |
| `0x011` | SYS_SAFETY_STS | 200 ms |
| `0x500` | SYS_NODE_STATUS | 200 ms |
| `0x600` | SYS_DIAG_RPT | 1000 ms |
| `0x7FE` | SYS_HEARTBEAT | 100 ms |
| `0x7B9` | VCU_SEB_REQ | 50 ms (sole producer) |

No other node sends these IDs on the Low bus. RT forwards `0x011`, `0x500`,
`0x600`, `0x115` to the High bus unchanged.

---

## 7. ESTOP semantics

**Entry (any of):** hardware button, CAN `0x001`, MTR fault / `0x206`
ESTOP_ACTIVE, SEB L3 latched fault, RT heartbeat loss, task deadline miss, CAN
bus-off. Entry is instant and idempotent; it sets the latch and records the
reason.

**During ESTOP:** mode is reported MANUAL, power is dropped through the
authority resolver, brake arbitration selects maximum stroke, and SYS
rebroadcasts `0x001` (rate-limited) and reports `estop_active` on `0x011`/
`0x7FE`.

**Exit (only through the validated reset transaction):**

```rust
// inside step(), when a valid reset request is present
let blockers = compute_reset_blockers(&state, &inputs);   // all 7 categories
let evidence = ResetEvidence { source, blockers, now };
match state.estop.try_reset(evidence) {
    ResetOutcome::Cleared { .. } => { /* report ACCEPTED on 0x115, clear tx_estop */ }
    ResetOutcome::Rejected(b)    => { /* report REJECTED with blocker mask on 0x115 */ }
}
```

Reset sources: START latch release/transition (policy), MODE 5 s long-press, or
remote `0x114` carrying the `0x5253` token and a sequence number. **All** paths
use the same `compute_reset_blockers`, so a reset cannot succeed while the
button is still pressed, a latched fault is active, the vehicle is moving, a
heartbeat is missing, MTR has not acknowledged ESTOP release, a transient
inhibit is asserted, or the token is wrong. This is the fix for the C++
`MtrEstopAckWatchdog` being unreachable and for the fragmented 11-site ESTOP
logic.

---

## 8. Mode and power authority

`resolve_authority` (see `etrike-core/architecture.md` §6.2) is the single
decision point. Host `0x111`/`0x112` are subordinate requests, gated by
`StreamValidity` so a frozen or replayed request cannot change mode. Physical
START/MODE inputs always take precedence. During ESTOP the resolver clamps to
MANUAL and drops power.

The output `0x110` reports the resolved mode; `0x113` reports the resolved power
state. RT and MTR follow these; they never negotiate back.

---

## 9. Fault model (two masks, one owner)

`InhibitModel` holds:

- **Transient inhibits** — MTR feedback loss, SEB comms loss, brake following
  error. Cleared after N consecutive healthy frames (hysteresis).
- **Latched faults** — brake-following confirmed, SEB L3. Cleared only through
  the validated reset transaction.

Both masks live in `SysCore` and are updated only in `step`. A transient inhibit
adds a reset blocker and is reported in `block_mask`; a latched fault cuts
traction, clamps MANUAL, preserves steering, and blocks reset. Unlike the C++,
these masks are always written by the logic that reads them — no write-never
fields, no dormant inhibits.

---

## 10. C++ → Rust mapping

| C++ (sys-esp32/src) | Rust |
|---------------------|------|
| `main.cpp` task bodies (12 tasks) | `sys-esp32-rs/src/tasks/*` + `etrike-core::sys` |
| `mode_manager.{h,cpp}` | `etrike-core::sys::mode` |
| `inhibit_state.{h,cpp}` | `etrike-core::shared::inhibit` |
| `safety_monitor.{h,cpp}` | `etrike-core::shared::estop` + `sys` (hb watchdogs) |
| `brake_control.h` | `etrike-core::sys::brake` |
| `mtr_estop_ack.h` | `etrike-core::sys::mtr_ack` (now ticked) |
| `light_control.h`, `indicator_control.h` | `etrike-core::sys::lights` |
| `relay_pipeline.h` | `sys-esp32-rs` shell (`set_relay`) |
| `sys_status_led.h` | `sys-esp32-rs` shell (`body` task) |
| `system_ready.h` | `etrike-core::sys::ready` (observational) |
| `can_driver.{h,cpp}` | `sys-esp32-rs` shell (esp-hal TWAI) |
| `physical_egas.h` | deferred (no wheel encoder fitted) |
| `wdt_toggle.h` | removed (was disabled) |

Resolved defects: SYS-BUG-01 (ack watchdog now reachable), SYS-BUG-02
(no RX/TX inversion), SYS-BUG-03 (single ESTOP entry), SYS-BUG-04 (NVS not
written every boot), SYS-BUG-06 (reset grace handled by the blockers).

---

## 11. Deferred features

Not in the first SYS-RS milestone; added only after the core is verified:

- LED priority-cascade patterns (the `body` task renders a simple state pattern
  first).
- Diagnostics event plane (`0x600`, `0x601`).
- Bypass / bench run modes beyond a compile-time selector.
- Physical wheel-speed EGAS (no encoder on the vehicle today).

---

## 12. Test plan

- **Host:** all core invariants via `cargo test -p etrike-core`; protocol
  round-trips via `etrike-protocol`.
- **Bench:** TWAI loopback, GPIO input debounce, relay/lamps, WS2812; ESTOP
  enter from each source; reset rejected while each blocker is held; reset
  accepted only when all clear.
- **Parity:** replay recorded CAN logs through the C++ and Rust cores and
  compare `SysOutputs` (P5).

---

## 13. References

- `update-beta/architecture.md` — workspace overview and invariants.
- `update-beta/crates/etrike-core/architecture.md` — `SysCore`, invariants.
- `update-beta/crates/etrike-protocol/architecture.md` — frame validation.
- `sys-esp32/architecture.md`, `sys-esp32/src/config.h` — pins and behaviour.
- `protocol/contracts/sys.yaml` — SYS message contract.
- `docs/safety/` — ESTOP, brake, and defense-in-depth rationale.

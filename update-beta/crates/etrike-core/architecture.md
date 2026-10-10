# `etrike-core` — Pure Domain Logic Crate

**Layer:** Domain (middle).
**Status:** Phase −1 design.
**Responsibility:** All decision logic for SYS and RT as pure, deterministic
state machines. No hardware, no I/O, no CAN driver, no globals.

---

## 1. Purpose

`etrike-core` holds the reasoning that the C++ firmwares scattered across free
functions, globals, and atomics. It is deliberately boring: a value with a
`step` method, driven by inputs, producing outputs.

```rust
pub struct SysCore { /* private */ }

impl SysCore {
    pub fn new(config: SysConfig) -> Self;
    pub fn step(&mut self, inputs: SysInputs, now: Instant) -> SysOutputs;
}
```

Everything that mutates decision state happens inside `step`. There is no other
entry point. This is what makes the logic testable on the host and races
impossible.

---

## 2. Position in the workspace

```
sys-esp32-rs / rt-esp32-rs  (shell)
   │  build Inputs from verified frames + debounced hardware
   │  apply Outputs to CAN + GPIO
   ▼
etrike-core  (this crate)
   │  uses only typed messages and validation results
   ▼
etrike-protocol  (wire types)
```

- Depends on `etrike-protocol` for message types and validity results.
- Does **not** depend on `esp-hal`, `embassy`, or anything with a HAL.
- Must build and run under plain `cargo test` on the host.

---

## 3. Module layout

```
src/
  lib.rs
  shared/           # semantics used by both nodes
    estop.rs        # EstopLatch, ResetEvidence, ResetBlockers, ResetOutcome
    inhibit.rs      # InhibitModel (transient + latched two-mask model)
    presence.rs     # NodePresence bits and freshness
    authority.rs    # shared Authority/Mode/Power value types
  sys/              # SYS-only policy
    mod.rs          # SysCore, SysInputs, SysOutputs, bench bypass
    mode.rs         # resolve_authority
    brake.rs        # brake arbitration (priority chain)
    lights.rs       # body lights
    wire.rs         # frame <-> core mapping
  rt/               # RT-only policy
    mod.rs
    gate.rs         # AuthorityGate (asymmetric ESTOP clear)
    wire.rs         # frame <-> core mapping
```

`sys/` and `rt/` never import each other. Shared semantics live in `shared/`.

---

## 4. The step contract

Inputs and outputs are plain data. They are the *only* interface between the
core and the shell.

```rust
pub struct SysInputs {
    pub now: Instant,
    // verified CAN
    pub rt_drive: Option<Valid<RtDriveCmd>>,
    pub rt_brake: Option<Valid<RtBrakeCmd>>,
    pub rt_heartbeat: HeartbeatState,
    pub rt_node_status: Option<Valid<RtNodeStatus>>,
    pub mtr_fbk: Option<Valid<MtrMotorFbk>>,
    pub seb_status: Option<Valid<SebStatus>>,
    pub seb_err: Option<Valid<SebErrInfo>>,
    pub hmi_mode_req: RequestState<ModeReq>,
    pub hmi_pwr_req: RequestState<PwrReq>,
    pub host_reset_req: Option<Valid<HostEstopResetReq>>,
    pub host_light: Option<Valid<HostLightCmd>>,
    pub can_estop: Option<Instant>,
    // debounced hardware
    pub hw: PhysicalInputs,
    // shell-observed health
    pub task_deadline_missed: bool,
    pub can_bus_off: bool,
}

pub struct SysOutputs {
    pub sys_safety_sts: SysSafetySts,        // 0x011
    pub sys_mode_cmd: SysModeCmd,            // 0x110
    pub sys_pwr_cmd: SysPwrCmd,              // 0x113
    pub sys_estop_reset_rsp: Option<SysEstopResetRsp>, // 0x115
    pub seb_req: VcuSebReq,                  // 0x7B9 (SYS is sole producer)
    pub sys_heartbeat: SysHeartbeat,         // 0x7FE
    pub sys_node_status: SysNodeStatus,      // 0x500
    pub estop_frame: bool,                   // emit 0x001 this tick
    pub gpio: GpioOutputs,                   // relays, lamps, LED pattern class
    pub diag_events: [Option<DiagEvent>; N],
}
```

Properties of `step`:

- **Pure** with respect to the outside world: same `(state, inputs, now)` ⇒
  same `(state', outputs)`.
- **Total**: it always returns; no panics on input ranges (inputs are already
  validated).
- **No allocation**: fixed-size arrays/`Option`s only.

---

## 5. State ownership

One writer per piece of state. Every field below is private and mutated only
inside `step`.

| State | Type | Owner (written by) | Read by |
|-------|------|--------------------|---------|
| ESTOP latch | `EstopLatch` | `step` via `enter` / `try_reset` | mode, brake, outputs |
| Latched faults | `InhibitModel` (latched mask) | `step` | mode |
| Transient inhibits | `InhibitModel` (transient mask) | `step` | mode |
| Mode / power | `Authority` | `resolve_authority` inside `step` | outputs |
| Brake command | `brake::arbitrate` | `step` | brake output |
| Lights | `lights::evaluate` | `step` | GPIO output |
| Heartbeat counter | `heartbeat` field | `step` | heartbeat output |

Contrast with the C++: there is no `g_estop_reason` written by four contexts,
no mailbox zeroed by three writers, and no write-never globals. A state with no
writer simply does not exist.

---

## 6. Type-enforced invariants

### 6.1 ESTOP latch

```rust
pub struct EstopLatch { active: bool, reason: Option<EstopReason>, epoch: u32 }
pub struct ResetEvidence { source: ResetSource, blockers: ResetBlockers }

pub enum ResetOutcome {
    Cleared { epoch: u32 },
    Rejected(ResetBlocker),
}

impl EstopLatch {
    pub fn enter(&mut self, reason: EstopReason) { self.active = true; self.reason = Some(reason); self.epoch += 1; }
    pub fn is_active(&self) -> bool { self.active }
    pub fn reason(&self) -> Option<EstopReason> { self.reason }

    pub fn try_reset(&mut self, ev: ResetEvidence) -> ResetOutcome {
        if let Some(b) = ev.blockers.first() { return ResetOutcome::Rejected(b); }
        if !ev.source.is_valid() { return ResetOutcome::Rejected(ResetBlocker::InvalidToken); }
        self.active = false; self.reason = None;
        ResetOutcome::Cleared { epoch: self.epoch }
    }
}
```

`active` and `reason` are private; the only ways to change them are `enter` and
`try_reset`. `ResetBlockers` is deliberately tiny, in priority order:

1. `PhysicalEstop` — the button is still pressed.
2. `Moving` — the vehicle is not stationary.
3. `InvalidToken` — the remote reset token is wrong.

Latched faults do **not** block reset: a successful reset clears them. There is
no code path that clears the latch without passing every blocker.

### 6.2 Mode authority

```rust
pub enum Mode { Manual, Auto }        // two modes only

pub fn resolve_authority(i: ModeInputs) -> Authority {
    if i.estop_active || i.latched_fault {
        return Authority { mode: Mode::Manual, power: Power::Off, clamp: Clamp::Estop };
    }
    if i.physical_run && i.host_auto_request && i.allow_auto {
        Authority { mode: Mode::Auto, power: Power::On, clamp: Clamp::None }
    } else {
        Authority { mode: Mode::Manual, power: if i.physical_run { Power::On } else { Power::Off }, clamp: Clamp::None }
    }
}
```

This is the single place mode is decided. Host requests are subordinate to
physical inputs and the ESTOP latch. RT mirrors `0x110` and never resolves mode.

### 6.3 Brake arbitration (SYS)

Priority chain, highest first: ESTOP maximum stroke → physical brake lever →
RT `0x205` pressure intent → released. A single function returns the winning
source and value, so two sources cannot each write the command.

### 6.4 Bench safety bypass

`SysConfig::bypass` (and `RtState::bypass`) is a bench pass-through. When set:

- SYS skips peer/task/heartbeat faults, latched-fault clamps, and reset
  blockers, and relays the host: power defaults **ON** (bench) with `0x112` able
  to switch it off, and mode follows the `0x111` request (`AUTO`/`MANUAL`).
- The **physical ESTOP button still trips** and cuts power, even in bypass
  (the reset transaction remains available to clear it).
- The shells skip `verify()` (unvalidated frames are accepted).
- RT always grants motion, and withholds it (zeroes) when the host drive command
  is stale beyond `DRIVE_LOSS_MS`; a commanded Neutral gear forces zero speed.

The firmware sets it from a `SAFETY_BYPASS` constant. Intended for the hardware
bench only.

---

## 7. Determinism and ordering

`step` does not read a clock or the environment except through `now`. Within a
tick, inputs are processed in a fixed order:

1. Update transient inhibits and latch SEB L3.
2. Evaluate ESTOP entry (button, CAN `0x001`, bus-off, task deadline, heartbeat).
3. Reset transaction (before authority so a reset restores mode the same tick).
4. Resolve `Authority` (mode/power).
5. Arbitrate brake.
6. Drive body outputs (lights).
7. Build CAN outputs and heartbeat.

Because the order is fixed and inputs are values (not events raced across
tasks), `mode_is_deterministic` holds: permuting the arrival order of inputs
that land in the same tick yields the same result.

---

## 8. Testing

Host `cargo test` and `proptest`. The success criterion for P2 is this set:

| Test | Property |
|------|----------|
| `estop_never_resets_with_blocker` | For every non-empty `ResetBlockers`, `try_reset` returns `Rejected` and the latch stays active |
| `resets_only_with_valid_evidence` | Cleared iff all blockers clear **and** the source is valid |
| `host_auto_never_overrides_physical_manual` | Host AUTO with physical MANUAL/START-off resolves MANUAL |
| `no_id_sent_by_two_owners` | Generated `TX_OWNERS` has no duplicate `(bus, id)` |
| `deterministic_across_two_cores` | Same input set ⇒ same output regardless of order |
| `reset_rejected_at_extreme_speed` | `i32::MIN` speed must not overflow |
| `bypass_passes_everything_through` | Bypass ignores ESTOP and clamps |
| `brake_arbiter_priority` | ESTOP > lever > RT intent > released, exhaustively |

Fault-injection replay reuses `rt-esp32/test/test_fault_injection_matrix` and
the simulation vectors (P5).

---

## 9. Non-goals

- No CAN driver, no SPI, no GPIO, no timers.
- No logging side effects (diagnostics are returned as `Outputs`).
- No `unsafe`, no `static mut`, no interior mutability for logic state.
- No node cross-talk: `sys/` and `rt/` are independent.

---

## 10. References

- `update-beta/architecture.md` — workspace overview and invariants.
- `update-beta/crates/etrike-protocol/architecture.md` — the types this crate consumes.
- `sys-esp32/src/mode_manager.h`, `inhibit_state.h`, `safety_monitor.h`,
  `brake_control.h`, `mtr_estop_ack.h` — C++ logic being reimplemented.
- `rt-esp32/src/steering_control.h`, `physics_model.h`, `safety_monitor.h` —
  RT logic (P4).
- `docs/safety/` — safety rationale and FTTI budget.

# State Machine Design for Embedded Systems

A **state machine** (finite state machine, FSM) models a system that is always in exactly one *state* and transitions between states in response to *events*. In embedded firmware, state machines replace tangled `if/else` chains with explicit, testable behavior.

The E-Trike uses four state machines:
- **Mode FSM** — MANUAL → AUTO → ESTOP (architecture §3)
- **Steering boot FSM** — BOOT_WAIT → LISTEN_SYNC → ACTIVE → FAULT (§7.6)
- **Brake boot FSM** — BOOT_WAIT → LISTEN_SYNC → ACTIVE → FAULT (§8.6)
- **Gear FSM** — N → D → S → R transitions (§8.6)

---

## 1. Why State Machines?

Without a state machine, mode-dependent behavior looks like this:

```cpp
// Anti-pattern: flag soup
void motor_task() {
    if (estop_active) {
        dac_write(0);
        gear_off();
    } else if (auto_mode && drive_cmd_valid) {
        dac_write(speed_to_dac(setpoint));
        gear_set(setpoint.gear);
    } else if (manual_mode) {
        dac_write(adc_read());
        gear_mirror_tlp281();
    }
    // What about transitioning? What about startup? What if two flags are true?
}
```

Two problems: (1) the logic is scattered across many `if` blocks in many tasks, and (2) contradictory states (what if `auto_mode` AND `estop_active` are both true?) produce undefined behavior.

A state machine makes the behavior explicit:

```cpp
enum class Mode { Manual, Auto, Estop };

void motor_task() {
    switch (g_mode) {
        case Mode::Manual:  motor_manual();  break;
        case Mode::Auto:    motor_auto();    break;
        case Mode::Estop:   motor_estop();   break;
    }
}
```

**One variable, one switch, one behavior per state.** The FSM *is* the documentation.

---

## 2. States, Events, Transitions

A state machine has three ingredients:

| Ingredient | What it is | Example |
|-----------|------------|---------|
| **State** | A distinct mode of operation | `MANUAL`, `AUTO`, `ESTOP` |
| **Event** | Something that triggers a change | Button press, CAN frame, timeout |
| **Transition** | Moving from one state to another | `MANUAL ──[ESTOP btn]──► ESTOP` |

```
         ┌──────────┐
    ┌───►│  MANUAL  │◀───┐
    │    └─────┬────┘    │
    │     MODE btn       MODE btn
    │          │          │
    │    ┌─────▼────┐    │
    │    │   AUTO   │    │
    │    └─────┬────┘    │
    │          │          │
    │  ESTOP btn / CAN 0x001 / HB timeout
    │          │          │
    │    ┌─────▼────┐    │
    │    │  ESTOP   │────┘
    │    └─────┬────┘
    │         │ START btn
    │         ▼
    └──────── MANUAL
```

Key property: **ESTOP is an absorbing state** — once entered, only a deliberate START action exits. No automatic recovery.

---

## 3. Implementation Patterns in C/C++

### Pattern 1: Switch-case (flat FSM)

Best for simple machines with <8 states.

```cpp
enum class State { BootWait, ListenSync, Active, Fault };
State g_state = State::BootWait;

void steer_fsm() {
    switch (g_state) {
    case State::BootWait:
        if (millis_since_boot() > 500) {
            g_state = State::ListenSync;
        }
        break;

    case State::ListenSync:
        if (received_ses_status) {
            active_target = ses_angle;
            if (ses_aligned) g_state = State::Active;
        } else if (time_in_state() > 2000) {
            g_state = State::Fault;
        }
        break;

    case State::Active:
        send_steer_cmd();
        if (following_error_too_large()) {
            mode_set(Estop);  // triggers system-level ESTOP
        }
        break;

    case State::Fault:
        // Silent — actuator timeout-faults internally
        break;
    }
}
```

### Pattern 2: Function pointer table

Best when each state has complex behavior and you want separate functions.

```cpp
typedef void (*state_fn_t)();

void st_boot_wait()  { /* ... */ }
void st_listen_sync() { /* ... */ }
void st_active()     { /* ... */ }
void st_fault()      { /* ... */ }

state_fn_t state_table[] = { st_boot_wait, st_listen_sync, st_active, st_fault };

void steer_fsm() {
    state_table[g_state]();  // dispatch
}
```

### Pattern 3: Transition table

Best when transitions are complex and you want them in one place.

```cpp
struct Transition {
    State from, to;
    bool (*guard)();      // condition function
    void (*action)();     // what to do on transition
};

Transition table[] = {
    { BootWait, ListenSync, timeout_500ms,  nullptr },
    { ListenSync, Active,  ses_aligned,    nullptr },
    { Active, Fault,       timeout_2s,     log_fault },
    // ...
};
```

The E-Trike uses **Pattern 1 (switch-case)** — simple, readable, and sufficient for each FSM's complexity.

---

## 4. Entry and Exit Actions

When transitioning between states, you often need cleanup/initialization:

```cpp
void mode_set(Mode new_mode) {
    Mode old = g_mode;           // snapshot for exit action
    g_mode = new_mode;           // atomic — all tasks see the new mode immediately

    // Exit actions
    if (old == Mode::Auto) {
        steer_stop_transmitting();   // EPS-C comm fault on timeout
    }

    // Entry actions
    if (new_mode == Mode::Estop) {
        dac_write(0);                // motor off
        gear_all_off();              // neutral
        brake_full();                // 0x720 stroke = max
        dcdc_disable();              // 12V rail off
        lights_brake_on();           // brake light forced ON
    }
}
```

**Important:** `g_mode` is written *before* entry actions run. This is intentional — if a high-priority safety task reads `g_mode` mid-transition, it sees the new mode and acts accordingly, even if entry actions haven't finished yet.

---

## 5. Hierarchical State Machines

A flat FSM works for 3–5 states. When you have mode logic AND sub-mode logic, you need hierarchy:

```
MANUAL
  ├── Throttle: pass-through
  ├── Gear: pass-through
  └── Lights: handlebar switches

AUTO
  ├── Throttle: CAN setpoint → DAC
  ├── Gear: CAN setpoint → relays
  ├── Lights: CAN 0x302 → GPIOs
  └── Steering: active control with dynamic clamp

ESTOP
  ├── Throttle: 0V forced
  ├── Gear: all OFF
  ├── Brake: max stroke
  └── Lights: brake ON, all others OFF
```

The top-level mode FSM selects which sub-behavior runs. In the E-Trike, this is implemented by having each task check `g_mode` and branch accordingly, rather than a formal hierarchical statechart.

---

## 6. Common Pitfalls

| Pitfall | What happens | Fix |
|---------|-------------|-----|
| **Ghost state** | Code falls through to unintended behavior | Always have a `default:` case in switch, even if it just logs an error |
| **Hidden transitions** | A state change happens in a low-level function, not visible in the FSM | All `g_mode = X` assignments go through `mode_set()`. Grep for `g_mode =` to catch violations. |
| **Re-entrant transitions** | Entry action triggers another state change → infinite loop | Guard: `if (g_mode == new_mode) return;` at the top of `mode_set()` |
| **Non-atomic state write** | A 32-bit write on an 8-bit MCU is non-atomic | Use `uint8_t` or `enum class : uint8_t` for state variables — single-instruction write on ARM/XTensa |
| **Race between FSM and ISR** | ISR sets state while FSM is mid-entry-action | ISR only sets flags/queues. FSM checks them synchronously. |

---

## 7. The Boot→Listen→Active Pattern

A recurring FSM shape in the E-Trike, used for both steering and brake actuator startup:

```
BOOT_WAIT ──► LISTEN_SYNC ──► ACTIVE ──► FAULT
  500ms        await status    50 Hz TX    timeout
  no TX        read position   monitor     silent
```

This is a **safe bootstrapping pattern** for CAN actuators. Transmitting before the actuator is ready can latch a fault; transmitting the wrong position (e.g., 0° on a turned wheel) can cause dangerous transients. See [[listen-before-speaking]] for the full rationale.

---

*See also: [[listen-before-speaking]] for the boot sequence pattern, [[defense-in-depth-safety]] for the ESTOP absorbing state, `architecture.md` §3 for the mode FSM, §7.6 for steering FSM, §8.6 for brake FSM.*

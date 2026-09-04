# Low-level command-chain test cases — Host speed+gear → RT → Low actuator signals

Scope: live-bench verification that a Host speed+gear command (UI/API on the
High bus) produces — or deliberately withholds — the Low-bus actuator signals
RT is responsible for:

| Low frame | Node / actuator | RT role |
|---|---|---|
| `0x204 RT_DRIVE_CMD` | MTR (motor) | regenerate from Host `0x300`; only non-zero in AUTO |
| `0x169 VCU_SES_REQ` | SES (steer-by-wire) | command; mode-gated |
| `0x7B9 VCU_SEB_REQ` | SEB (brake) | command; mode-gated |
| `0x205 RT_BRAKE_CMD` | SYS brake setpoint | AUTO only |

Topology: SYS is physically on the **Low** bus only (single TWAI). RT is the
High↔Low gateway. The Control Toolkit Host transmits on High (CANalyst-II CH0).

Mode authority: **SYS** owns mode (physical buttons + HMI_MODE_REQ 0x111);
RT follows SYS_MODE_CMD 0x110 and reports mode on RT_STATE_RPT. **RT only
commands actuators in AUTO** (`rt-esp32/src/main.cpp`); in MANUAL it pins
`0x204 = {0, N}` as a keep-alive.

The backend exposes the derived mode/gate on control snapshots:
`POST/GET .../control/*` → `control.vehicle_mode` =
`{vehicle_mode, mode_source, frame_fresh, gated, motion_streaming, hard_brake, reason}`.

---

## Case list

Each case: precondition → stimulus → expected Low evidence. `C#` (continuous)
cases run under `low_level_chain_soak.py`; the rest under `low_level_chain_qa.py`.

### A. Command-source equivalence (AUTO; non-zero Low frames expected)

| # | Case | Precondition | Stimulus | Expected Low evidence |
|---|---|---|---|---|
| A1 | Keyboard forward | AUTO, TX armed | `/control/intent` throttle=0.6 steer=0 gear=D | `0x204` speed≈1800, gear=1 |
| A2 | Keyboard reverse | AUTO | intent throttle=−0.3 | `0x204` speed≈−150 (reverse bound), gear=3 (R) |
| A3 | Analysis host-drive | AUTO | `/analysis/host-drive` speed=1500 gear=1 | `0x204` speed=1500 gear=1 |
| A4 | Raw 0x300 injection | AUTO | `/injections host:host_drive_cmd` speed=1500 gear=1 (periodic) | `0x204` speed=1500 gear=1 |
| A5 | Steer + | AUTO | intent steer=+0.2 | `0x169` target_angle > center (30000), control_enable=1 |
| A6 | Steer − | AUTO | intent steer=−0.2 | `0x169` target_angle < center, control_enable=1 |
| A7 | Hard brake | AUTO | intent `hard_brake=true` | `0x204` {0,N}; brake req present |
| A8 | Speed/gear round-trip | AUTO | `0x300` speed=1800 gear=1 repeated | `0x204` DLC 5, decode speed 1800 gear 1 |

### B. Mode gating — core behaviour

| # | Case | Precondition | Stimulus | Expected Low evidence |
|---|---|---|---|---|
| B9 | **MANUAL + speed cmd** | MANUAL, TX armed | `0x300` speed=1500 gear=1 | `0x204` **stays {0,N}** — no motor signal (by design); RT alive |
| B10 | MANUAL→AUTO transition | MANUAL | `/hmi/mode` AUTO then drive | `0x204` 0 → shaped after mode=1 observed |
| B11 | AUTO→MANUAL mid-drive | AUTO, driving | `/hmi/mode` MANUAL | `0x204` returns to {0,N} |
| B12 | ESTOP during AUTO drive | AUTO, driving | inject SAFETY_ESTOP 0x001 | `0x204`→{0,N}; brake engaged |
| B13 | Steering-ready gate | AUTO | observe steer_state | `0x204` non-zero only while steer ACTIVE |

### C. Signal integrity / unit routing

| # | Case | Expected |
|---|---|---|
| C14 | `0x300` NOT bridged Low | RT consumes; no `0x300` on Low |
| C15 | SYS heartbeat stays ok during AUTO drive | `SYS_HEARTBEAT.heartbeat_ok=1` throughout |
| C16 | Gear→sign | D → `0x204>0`, R → `0x204<0`, N → `0x204=0` |

### D. Ownership / exclusivity

| # | Case | Expected |
|---|---|---|
| D17 | `drive_console` ↔ `control_keyboard` handoff | No ESTOP; `0x204` resumes; no gap >2 s |
| D18 | Direct low motor bypass | `/control/direct motor` works regardless of mode; `0x204` reflects direct values |
| D19 | Kinematics preempts direct | returns to high path; `0x204` RT-governed |
| D20 | Direct steer/brake bypass | `0x169`/`0x7B9` reflect direct values, control_enable forced ON |

### E. Continuous (6 of 26 ≈ 23%)

| # | Case | Method | Assertion |
|---|---|---|---|
| E21 | AUTO drive soak 30 s | 20 Hz stream; sample `/state` 500 ms | RT_HEARTBEAT(low) never stale; SYS hb_ok=1; no ESTOP; `0x204` non-zero throughout |
| E22 | Cadence AUTO drive | 3 s window | `0x204`≈100 Hz, `0x169`≈50 Hz, `0x7B9`≈50 Hz (tolerance) |
| E23 | HMI 1 Hz cadence | `/hmi/mode` job | `HMI_MODE_REQ` High & Low ≈1 Hz; `SYS_MODE_CMD` ≈1 Hz follows |
| E24 | Steer oscillation 20 s | sine steer | `0x169` tracks sign; no ESTOP; Low hb live |
| E25 | Heartbeat gap monitor | drive; watch `0x7FD`(low) | inter-frame gap never >0.9 s (SYS timeout 1 s) |
| E26 | Mode-toggle soak 20 s | MANUAL↔AUTO ×N | `0x204` follows mode each cycle; no lockup/ESTOP |

---

## Detection helpers

- Authoritative mode: `RT_STATE_RPT.mode` (fresh) else `SYS_MODE_CMD.mode`.
- Gateway `vehicle_mode.gated == true` when streaming motion while mode ≠ AUTO.
- Per-case teardown restores mode and releases/cancels jobs so a case never
  contaminates the next.

## Status column

Maps each row to a runnable check in the QA/soak scripts and is updated from
live runs (artifacts under `control-toolkit/test-results/api-qa/`).

# High-Level CAN I/O: Autoware.Auto Design vs Autoware Universe Requirement

Signal-by-signal comparison of the E-Trike **high-bus host boundary** as it exists today
(designed for Autoware.Auto) against what Autoware Universe actually requires.

**Evidence base**
- Current CAN: `protocol/generated/docs/buses/high.md` (17 IDs, 56 signals), `protocol/contracts/*.yaml`
- Current bridge: `jetson/src/autoware_vehicle_bridge/src/vehicle_bridge_node.cpp` (893 lines)
- Design intent: `docs/legacy/autoware-auto-0.0.4.md`, `docs/communications/autoware-auto-communication-architecture.md`
- Universe truth: `E:\work\av_project\autoware\src` (Humble), message/srv files read directly

---

## 0. Three findings that reframe the comparison

### 0.1 The original reference architecture was already Universe-shaped

`docs/communications/autoware-auto-communication-architecture.md` — the base document — specifies
`autoware_control_msgs/msg/Control`, `autoware_vehicle_msgs/*`, `tier4_vehicle_msgs/VehicleEmergencyStamped`,
and an `ActuationCommandStamped` path. **Those are Universe names.** The v0.0.4 audit then
deliberately rewrote them to Autoware.Auto:

> | Original | Corrected | Reason |
> | `autoware_control_msgs` | `autoware_auto_control_msgs` | Missing `.auto` prefix |
> | `Control` message | `AckermannControlCommand` | Current canonical type |
> | `tier4_vehicle_msgs/*` | *(removed)* | Not Autoware.Auto |
>
> — `docs/legacy/autoware-auto-0.0.4.md` §1.3

So this migration is substantially a **reversal of the v0.0.4 decision**, not new ground. The
reference doc's hypothetical gear enum (`0=NONE, 1=NEUTRAL, 2=DRIVE, 3=LOW, 10=REVERSE, 20=PARK`)
was also closer to Universe than what E-Trike implemented (`0=N, 1=D, 2=S, 3=R`).

### 0.2 The v0.0.4 decision was "CAN stays exactly as it is"

> "**What changes:** Jetson gets a new ROS node. The CAN bus stays exactly as it is."
> "Everything below the red line stays identical to today."

The high-bus protocol was therefore **never designed against the Autoware contract** — it was
designed first, and the bridge was made to absorb every impedance mismatch. That is why all
Universe gaps land on signals that the bridge currently *fabricates* (heading_rate=0),
*discards* (steering at standstill), or *cannot represent* (PARK, emergency=false, NOT_READY).

### 0.3 The current bridge appears never to have been compiled

`vehicle_bridge_node.cpp` reads fields that exist in **neither** message set:

| Line | Field used | Autoware.Auto | Universe |
|---|---|---|---|
| 189 | `longitudinal.is_defined_speed` | not present | not present |
| 192 | `lateral.is_defined_steering_tire_angle` | not present | not present |
| 210, 668 | `longitudinal.is_defined_acceleration` | not present | **present** (`Longitudinal.msg`) |

Universe has `is_defined_acceleration`, `is_defined_jerk`,
`is_defined_steering_tire_rotation_rate` — but never `is_defined_speed` or
`is_defined_steering_tire_angle`. There are no build artifacts, no `compile_commands.json`,
no colcon output, and no CI referencing `jetson/` anywhere in the repo.

**Consequence for planning:** the Jetson work is not "port a working node" — it is
"write the node, reusing the existing CAN encode/decode logic as a specification."
The conversion maths, safety timers and heartbeat logic are sound and reusable; the ROS
plumbing is unverified. Treat prior estimates for `jetson/` as write-from-scratch.

---

## 1. Direction A — Host → Vehicle (commands)

### 1.1 `0x300 HOST_DRIVE_CMD` — DLC 8, 10 ms

| | Value |
|---|---|
| Signals | `speed_mmps` i32 b0-3 [-500,3000] · `yaw_rate_mrad_s` i24 b4-6 [-3000,3000] · `gear` u8 b7 {0=N,1=D,2=S,3=R} |
| Designed for | `AckermannControlCommand.longitudinal.speed` + `lateral.steering_tire_angle` → inverse tricycle kinematics → yaw rate; `GearCommand.command` |
| Universe source | `Control.longitudinal.velocity` + `Control.lateral.steering_tire_angle`; `GearCommand.command` |

| Signal | Autoware.Auto fit | Universe fit | Verdict |
|---|---|---|---|
| `speed_mmps` | `longitudinal.speed` × 1000 | `longitudinal.velocity` × 1000 — **rename only**, same semantics, same units, same sign | **Identical** (field rename) |
| `yaw_rate_mrad_s` | angle→yaw conversion acceptable: `.Auto` had no standstill-steering contract | **Insufficient.** `Lateral.steering_tire_angle` is unconditionally valid; Universe expects the tire angle to be tracked at v=0 (pull-out, parking, `enable_engage_on_driving:false` means engage *always* happens from standstill). Bridge zeroes yaw below 0.05 m/s (`:172`); RT decays steering to zero below 50 mm/s (`physics_model.cpp:66-70`). Angle→yaw→angle is also lossy and non-invertible at low v | **Gap — needs new signal** |
| `gear` | `.Auto` GearCommand: NONE=0, **DRIVE=1**, REVERSE=20, PARK=22, LOW=23 | Universe: NONE=0, NEUTRAL=1, **DRIVE=2**, REVERSE=20, PARK=22, LOW=23 | **Silent value collision.** Bridge hardcodes `DRIVE=1` (`:69`). Under Universe, 1 = NEUTRAL. CAN enum itself is fine; the *bridge mapping table* is wrong | **Bridge bug, not CAN gap** |
| `gear` (PARK) | `.Auto` — PARK mapped to N, tolerated | `autoware_shift_decider.cpp:78` **actively commands PARK** on arrival. CAN enum has no PARK and no way to report refusal | **Gap — enum + reject path** |

Byte budget: **fully packed** (b0-3 speed, b4-6 yaw, b7 gear). No spare bits for a steering-angle
signal. Any in-place fix requires repacking — see §5.

### 1.2 `0x301 HOST_BRAKE_REQ` — DLC 4, event

| Signal | Autoware.Auto | Universe | Verdict |
|---|---|---|---|
| `brake_pressure_kpa` i32 [0,20000] | `longitudinal.acceleration` < 0 → `(-a/max_decel)·max_kpa` | Same field, same semantics, `is_defined_acceleration` now genuinely exists and must be honoured | **Identical** |

Note: Universe's canonical deceleration path is `Control.longitudinal.acceleration`; the
optional `raw_vehicle_cmd_converter` → `ActuationCommandStamped{accel_cmd,brake_cmd,steer_cmd}`
path is **not** required and should stay unused for E-Trike. No CAN change either way.

### 1.3 `0x302 HOST_LIGHT_CMD` — DLC 1, event

| Signal | Autoware.Auto | Universe | Verdict |
|---|---|---|---|
| `left_turn` b0 | `TurnIndicatorsCommand::ENABLE_LEFT` (=2) | identical constant | **Identical** |
| `right_turn` b1 | `ENABLE_RIGHT` (=3) | identical constant | **Identical** |
| `brake_light` b2 | derived from `acceleration < 0` | same | **Identical** |
| `headlight` b3 | unused/reserved | still no Autoware source | Orphan (harmless) |

`TurnIndicatorsCommand`/`HazardLightsCommand` constants are byte-identical between .Auto and
Universe (`NO_COMMAND=0, DISABLE=1, ENABLE_LEFT=2, ENABLE_RIGHT=3`; hazard `ENABLE=2`).

### 1.4 `0x7FC HOST_HEARTBEAT` — DLC 2, 500 ms

| Signal | Verdict |
|---|---|
| `alive_ctr`, `health_flags` | No Autoware equivalent in either version — it is an E-Trike safety mechanism, not an interface obligation. **Identical / unaffected** |

### 1.5 `0x001 SAFETY_ESTOP` — DLC 0, event

| | Autoware.Auto | Universe |
|---|---|---|
| Source | `VehicleEmergencyStamped` (tier4 — *removed* by the v0.0.4 audit as "not Autoware.Auto", yet the bridge subscribes to it anyway at `:419`) | `tier4_vehicle_msgs/VehicleEmergencyStamped{stamp, bool emergency}`, published by `vehicle_cmd_gate` on `/control/command/emergency_cmd` (transient_local) |
| Payload | DLC=0 — pure event, assert-only | Needs to distinguish `emergency=true` from `emergency=false` |

Bridge `:601-612` sends ESTOP for **every** message received, including `emergency=false`.
DLC=0 cannot encode de-assertion, and there is no latch/source/clear-permitted state.

**Verdict: Gap — insufficient payload.** Critically, `decode` enforces strict DLC equality
(`etrike_protocol.hpp:409` pattern), so widening 0x001 in place would cause **unflashed
receivers to reject the ESTOP frame entirely**. Must be an additive companion message.

### 1.6 `0x400 HOST_OBSTACLE_DIST` — DLC 4, 100 ms

Defined in `host.yaml`, RT consumes it for speed limiting / brake ramp
(`physics_model.cpp:87-101`). **The bridge never publishes it** — not in the CAN-ID constant
list (`:51-61`), not in `tick_control()`.

Verdict: **Orphan in both worlds.** Neither .Auto nor Universe has a vehicle-interface topic
for obstacle distance (that is perception/planning's job, expressed as a trajectory).
Either wire it to a perception source or mark it explicitly non-Autoware.

### 1.7 Missing entirely — mode request

| | Autoware.Auto | Universe |
|---|---|---|
| Mechanism | `ControlModeCommand` **message** on `/control/control_mode_request` | `ControlModeCommand` **service**, `mode ∈ {NO_COMMAND, AUTONOMOUS, AUTONOMOUS_STEER_ONLY, AUTONOMOUS_VELOCITY_ONLY, MANUAL}` → `bool success` |
| CAN carrier | none — bridge collapses it to a local `engaged_` bool (`:594-599`) | none |

`0x110 SYS_MODE_CMD` is SYS→RT/MTR (SYS is the authority); `0x111 HMI_MODE_REQ` is HMI→SYS
with Host as a *receiver*. **There is no Host→vehicle mode request frame in the entire
high-bus dictionary.**

Under .Auto a local boolean was defensible (fire-and-forget message). Under Universe it is
not: the service demands a synchronous accept/reject, and `operation_mode_transition_manager`
cancels the transition if `success=false` (`node.cpp:104-110`) or if
`/vehicle/status/control_mode` fails to reach AUTONOMOUS within `transition_timeout: 10.0 s`.

**Verdict: Gap — missing message.**

---

## 2. Direction B — Vehicle → Host (feedback)

### 2.1 `0x120 SYS_THROTTLE_STS` — DLC 2, 10 ms → `VelocityReport`

| Universe field | Current source | Verdict |
|---|---|---|
| `longitudinal_velocity` | `speed_mmps` i16 / 1000 | **Identical** (100 Hz, well above need) |
| `lateral_velocity` | hardcoded `0.0f` (`:271`) | Acceptable — tricycle has no lateral sensor; 0 is honest |
| `heading_rate` | hardcoded `0.0f` (`:272`) | **Gap.** `.Auto` tolerated this. Universe pipes it into localization: `autoware_vehicle_velocity_converter` copies `msg.heading_rate → twist.angular.z` and sets `covariance[YAW_YAW] = stddev_wz²` (`vehicle_velocity_converter.cpp`), feeding gyro_odometer. Publishing a constant 0 with a finite covariance asserts "the vehicle is not rotating" to the state estimator |
| `header.stamp` / `header.frame_id` | **never set** — `VelocityReport` has a `std_msgs/Header` and the converter passes it through verbatim | **Gap.** Requires `base_link` + real stamp |

`.Auto`'s `VelocityReport` also had a header, so the missing stamp was already a latent bug;
Universe makes it load-bearing for localization.

### 2.2 `0x310 STEER_DIAG` — DLC 8, 100 ms → `SteeringReport`

| Aspect | Autoware.Auto | Universe | Verdict |
|---|---|---|---|
| Value | `angle_0_1deg` u16, ×0.1, offset −3000 → deg → rad | same conversion | Encoding **identical** |
| **Sign** | `.Auto` `SteeringReport`: positive = left | Universe `SteeringReport.msg`: *"Positive: left, Negative: right"* — explicit | E-Trike internals are **+right** (`physics_model.h:19`, `steering_control.h:162`). Bridge `:839-844` publishes the raw value with **no negation** |
| Rate | 100 ms (10 Hz) | Consumed by MPC (`~/input/current_steering`), `vehicle_cmd_gate` steer-rate limiter, `operation_mode_transition_manager` stability check | 10 Hz is marginal for a control-rate feedback signal |
| `stamp` | not set | required | Gap |

**Verdict: sign is a latent inversion in both worlds** (Universe merely documents it
explicitly); rate and stamp are Universe-specific tightenings.

### 2.3 `0x210 RT_STATE_RPT` — DLC 6, 100 ms → `ControlModeReport` + `GearReport`

| Universe target | Current source | Verdict |
|---|---|---|
| `ControlModeReport.mode` | `mode` u8 {0=MANUAL, 1=AUTO, 2=ESTOP} → {MANUAL(4), AUTONOMOUS(1), DISENGAGED(5)} (`:285-289`) | **Insufficient.** Universe defines 7 states; E-Trike can express 3. Missing `NOT_READY(6)` (SYS transitioning / steering not centred) and a true `DISENGAGED(5)` distinct from ESTOP. `AUTONOMOUS_STEER_ONLY(2)` / `AUTONOMOUS_VELOCITY_ONLY(3)` unrepresentable |
| mode request ack | none | Service needs `mode_requested` + `reject_reason` to answer `success` truthfully and to gate the AUTONOMOUS report on confirmation | **Gap** |
| `GearReport.report` | `reversing` bit → D or R (`:291-297`) | Coarse fallback; real gear comes from 0x206 | Acceptable |
| `stamp` | not set | required | Gap |

`.Auto`'s `ControlModeReport` had the same 7 constants, so this under-representation predates
Universe — but under Universe it is actively harmful, because
`getCurrentControlMode()` (`node.cpp:353-358`) drives the whole engage state machine off it.

### 2.4 `0x206 MTR_MOTOR_FBK` — DLC 4, 20 ms → `GearReport`

| Signal | Mapping | Verdict |
|---|---|---|
| `gear_state` u8 {0..3} | bridge `:778-784`: 0→NONE(0), 1→DRIVE(**1**), 2→LOW(23), 3→REVERSE(20) | Under Universe DRIVE=**2**; emitting 1 reports NEUTRAL. Same collision as §1.1 |
| `actual_speed_mmps` | unused by bridge | redundant with 0x120 |
| `fault_flags` | unused by bridge | candidate for diagnostics |

**Verdict: bridge mapping bug; CAN encoding fine.** No PARK representation (§1.1).

### 2.5 `0x011 SYS_SAFETY_STS` — DLC 3, 200 ms → turn/hazard reports + liveness

| Signal | Universe target | Verdict |
|---|---|---|
| `light_left/right/brake/head` | `TurnIndicatorsReport`, `HazardLightsReport` | **Identical** — constants unchanged between .Auto and Universe. Still an open-loop echo of 0x302 (no relay sensor) — a pre-existing limitation, not a Universe gap |
| `estop_active` | needed for `DISENGAGED` + emergency latch state | Present but no `source` / `clear_permitted` → cannot implement `emergency=false` semantics (§1.5) |
| `heartbeat_ok` | diagnostics | Identical |

Bridge `:800-807` maps both-bits-set to `TurnIndicatorsReport::DISABLE` and hazard ENABLE —
correct for both versions.

### 2.6 `0x600 SYS_DIAG_RPT` — DLC 8, 1000 ms → `DiagnosticArray`

Nine signals → `diagnostic_msgs/DiagnosticArray` (`:301-357`). **Identical** in both worlds;
`diagnostic_msgs` is unchanged. Only convention change: publish on `/diagnostics` (or via
launch remap) rather than `~/output/diagnostics`.

### 2.7 `0x7FD RT_HEARTBEAT` — DLC 2, 500 ms

E-Trike-internal liveness, no Autoware equivalent. **Identical / unaffected.**

### 2.8 `0x311 BRAKE_DIAG` — DLC 8, 100 ms

Bridge converts to diagnostics key-values (`:848-867`). No Autoware contract in either
version. **Identical / unaffected.** (Note: Universe *does* define
`ActuationStatusStamped{accel_status, brake_status, steer_status}`, but only
`raw_vehicle_cmd_converter` consumes it — not required here.)

### 2.9 `0x220 RT_PID_RPT` (`availability: reserved`) and `0x111 HMI_MODE_REQ`

Both list Host as a receiver; the bridge decodes neither. **Orphans in both worlds.**
`0x111` is arguably useful — it is the closest existing precedent for a mode-request frame
and its `parse_hmi_mode` arbitration in SYS (`mode_manager.h:26`) is the pattern the Universe
host request should reuse.

### 2.10 Missing entirely — kinematic state

| | Autoware.Auto | Universe |
|---|---|---|
| Type | `autoware_auto_vehicle_msgs/VehicleKinematicState` | **Does not exist.** Absent from `autoware_vehicle_msgs`; only CHANGELOG references remain |
| Bridge | publishes to `/vehicle/status/kinematic_state` from dead reckoning (`:757-767`) | Must be deleted; localization owns `/localization/kinematic_state` |

**Verdict: obsolete output — remove.** Optional replacement: `nav_msgs/Odometry`
(`odom`→`base_link`), only if a consumer exists. No CAN change.

---

## 3. Consolidated verdict matrix

| CAN ID | Signal(s) | .Auto → Universe | Change locus |
|---|---|---|---|
| 0x300 | `speed_mmps` | Identical (field rename `speed`→`velocity`) | Bridge |
| 0x300 | `yaw_rate_mrad_s` | **Insufficient** — no standstill steering | **CAN + RT** |
| 0x300 | `gear` | Enum OK; **DRIVE 1 vs 2** mapping wrong; **PARK** unrepresentable | Bridge + **CAN + RT/SYS** |
| 0x301 | `brake_pressure_kpa` | Identical | — |
| 0x302 | all light bits | Identical | — |
| 0x400 | `distance_mm` | Orphan (no Autoware source, either version) | Decide/document |
| 0x7FC | heartbeat | Identical | — |
| 0x001 | DLC=0 event | **Insufficient** — `emergency=false` unrepresentable | **CAN + SYS** |
| — | mode request | **Missing** — message→service, needs accept/reject | **CAN + SYS + RT** |
| 0x120 | `speed_mmps` | Identical | — |
| 0x120 | *heading_rate* | **Missing** — now a localization input | **CAN + RT** |
| 0x120 | header stamp/frame | Missing (latent in .Auto too) | Bridge |
| 0x310 | `angle_0_1deg` | Encoding identical; **sign inverted**; 10 Hz marginal | Bridge + **CAN timing** |
| 0x210 | `mode` | **Insufficient** — 3 of 7 states; no request ack | **CAN + RT/SYS** |
| 0x206 | `gear_state` | Mapping wrong (DRIVE), no PARK | Bridge + CAN |
| 0x011 | light bits | Identical | — |
| 0x011 | `estop_active` | Insufficient — no source/clear | **CAN + SYS** |
| 0x600 | all | Identical | — |
| 0x7FD | heartbeat | Identical | — |
| 0x311 | all | Identical (diagnostics only) | — |
| 0x220 | all | Orphan (reserved) | — |
| — | `VehicleKinematicState` | **Obsolete** — type deleted upstream | Bridge (remove) |

**Tally of 56 high-bus signals:** 41 identical/unaffected · 6 insufficient · 3 missing ·
1 obsolete · 5 orphaned. **≈ 73 % of the high-bus surface transfers unchanged.**

---

## 4. Gaps by severity

| Sev | Gap | Failure mode under Universe |
|---|---|---|
| **S1** | No mode-request carrier (§1.7) | `operation_mode_transition_manager` never confirms → autonomous engage impossible, or bridge lies `success=true` and Autoware believes it is in control when it is not |
| **S1** | `emergency=false` unrepresentable (§1.5) | Every `VehicleEmergencyStamped` triggers ESTOP, including de-assertions → spurious emergency stops on a transient_local topic that replays on subscribe |
| **S1** | Steering sign not negated (§2.2) | Vehicle steers **opposite** to command. Caught instantly in sim, catastrophic if not |
| **S2** | `DRIVE=1` vs `DRIVE=2` (§1.1, §2.4) | Commanded DRIVE is interpreted as NEUTRAL; reported gear reads NEUTRAL while driving |
| **S2** | No standstill steering (§1.1) | Cannot execute pull-out/parking; engage-from-standstill is the *only* engage path (`enable_engage_on_driving:false`) |
| **S2** | `ControlModeReport` 3-of-7 states (§2.3) | No `NOT_READY` → Autoware may request AUTONOMOUS while steering is uncentred |
| **S2** | PARK unrepresentable (§1.1) | `shift_decider` commands PARK on arrival; silently becomes N with no hold |
| **S3** | `heading_rate` = 0 (§2.1) | Localization fuses a false "zero yaw rate" with finite covariance |
| **S3** | Missing header stamp/frame (§2.1, §2.2) | Converter emits unstamped twist; TF/time-sync degradation |
| **S3** | 10 Hz steering feedback (§2.2) | MPC lateral loop under-sampled |
| **S4** | `VehicleKinematicState` (§2.10) | Will not compile — type does not exist |
| **S4** | `0x400`, `0x220`, `0x111` orphans | None (dead weight) |

---

## 5. Minimum additive CAN delta

Because `decode_*` enforces **strict DLC equality**, no in-service frame may be widened —
an unflashed receiver would reject the frame outright rather than ignore trailing bytes.
Conversely RT's router returns `Ok` for unknown IDs (`can_rx_router.h:63-73`), so new IDs
are safely ignored by old firmware. Every gap therefore resolves as a **new message**:

| New ID | Name | Payload | Closes |
|---|---|---|---|
| `0x303` | `HOST_STEER_CMD` @10 ms | `steer_angle_mdeg` i16 (±45000, +right), `valid` u8 | §1.1 standstill steering |
| `0x304` | `HOST_MODE_REQ` @event+1 s | `req_mode` u8, `req_ctr` u8 | §1.7 mode service |
| `0x305` | `HOST_ESTOP_CMD` @event | `assert` u8, `source` u8 | §1.5 emergency assert/clear |
| `0x211` | `RT_MODE_ACK` @100 ms | `mode_confirmed` u8 (7-state), `mode_requested` u8, `reject_reason` u8 | §2.3 mode confirmation |
| `0x121` | `RT_MOTION_RPT` @10 ms | `yaw_rate_mrad_s` i16, `gear_actual` u8 (incl. PARK/PARK_REJECTED) | §2.1 heading_rate, §1.1 PARK |
| — | `0x310` cycle 100→20 ms | *(timing only, no layout change)* | §2.2 rate |

Zero changes to `0x300`, `0x301`, `0x302`, `0x120`, `0x206`, `0x011`, `0x210`, `0x600`,
`0x7FC`, `0x7FD`, `0x001`. All existing golden vectors, byte-layout tests, decoders,
simulation models and debug-tool templates remain valid. Deployment can be staged in any
order rather than as a flag day.

Estimated added high-bus load: `0x303` + `0x121` at 100 Hz plus `0x310` at 50 Hz ≈ **+5 %**
utilisation at 500 kbit/s (from roughly 7 % to 12 % by frame-time estimate) — measure on the
analyser before committing.

---

## 6. Bridge-side changes with no CAN impact

These are pure ROS-layer corrections; the wire format is already correct:

1. `speed` → `velocity`; drop non-existent `is_defined_speed` / `is_defined_steering_tire_angle`; honour real `is_defined_acceleration`
2. Gear constants from message headers, never literals (fixes DRIVE=1→2)
3. Negate steering on both TX and RX
4. Populate `header.stamp` + `frame_id="base_link"` on `VelocityReport`; `stamp` on all reports
5. Delete `VehicleKinematicState`; optional `nav_msgs/Odometry`
6. `ControlModeCommand` subscription → `rclcpp::Service`
7. Honour `msg->emergency` boolean
8. Engage on `/vehicle/engage` (not `/control/command/engage`)
9. Volatile QoS depth 1 against transient_local publishers
10. Turn/hazard reports must never publish `0` (constants begin at `DISABLE=1`)

# Optimal Upgrade Strategy — v0.0.4-alpha

**Based on:** 8-agent audit (116+ findings across 2 rounds)
**Supersedes:** `docs/autoware-auto-upgrade-plan-0.0.4.md` (which attempted a single-layer approach)
**Date:** 2026-06-23

---

## The Core Insight

The original document (`autoware-auto-communication-architecture.md`) serves TWO roles that the plan conflated:

1. **Autoware.Auto Interface Standard** — the ROS 2 topics, message types, CAN ID namespace, and encoding abstraction that makes this node Autoware.Auto-compatible
2. **Vehicle-Specific Implementation** — the particular CAN IDs, parameter values, encoding formulas, and hardware paths of one specific vehicle

The optimal strategy separates these into **three layers**:

```
┌─────────────────────────────────────────────┐
│ LAYER 1: Autoware.Auto Interface Standard   │  ← Portable. Any vehicle.
│   ROS topics, message types, CAN namespace,  │
│   ratio abstraction, safety contract         │
├─────────────────────────────────────────────┤
│ LAYER 2: E-Trike Vehicle Adaptation          │  ← Our vehicle only.
│   RT translation formulas, low-bus CAN IDs,   │
│   trike parameters, actuator specifics       │
├─────────────────────────────────────────────┤
│ LAYER 3: Out of Scope / Separate Docs        │
│   micro-ROS, HIL serial, CARLA simulation    │
└─────────────────────────────────────────────┘
```

**Layers 1 and 2 stay in the main document, clearly separated.** Layer 3 moves to separate files.

---

## Layer 1: Autoware.Auto Interface Standard (THE SPEC)

This layer defines the contract between Autoware.Auto planning and ANY vehicle. It must be clean, consistent, and free of vehicle-specific assumptions.

### L1.1 ROS 2 Topic Interface

**Subscriptions (Inputs):**

| Topic | Message Type | Purpose |
|:---|:---|:---|
| `~/input/control_cmd` | `autoware_auto_control_msgs/msg/AckermannControlCommand` | Primary motion command from planning |
| `~/input/gear_cmd` | `autoware_auto_vehicle_msgs/msg/GearCommand` | Gear override (D/N/R/P) |
| `~/input/turn_indicators_cmd` | `autoware_auto_vehicle_msgs/msg/TurnIndicatorsCommand` | Turn signal request |
| `~/input/hazard_lights_cmd` | `autoware_auto_vehicle_msgs/msg/HazardLightsCommand` | Hazard lights request |
| `~/input/engage` | `autoware_auto_vehicle_msgs/msg/Engage` | Engage/disengage autonomy |

**Publications (Outputs):**

| Topic | Message Type | Purpose |
|:---|:---|:---|
| `~/output/velocity_status` | `autoware_auto_vehicle_msgs/msg/VelocityReport` | Speed, heading rate |
| `~/output/steering_status` | `autoware_auto_vehicle_msgs/msg/SteeringReport` | Actual tire angle |
| `~/output/gear_status` | `autoware_auto_vehicle_msgs/msg/GearReport` | Current gear |
| `~/output/control_mode` | `autoware_auto_vehicle_msgs/msg/ControlModeReport` | Autonomy state |
| `~/output/turn_indicators_status` | `autoware_auto_vehicle_msgs/msg/TurnIndicatorsReport` | Light confirmation |
| `~/output/hazard_lights_status` | `autoware_auto_vehicle_msgs/msg/HazardLightsReport` | Hazard confirmation |

**Key decisions:**
- **Package prefix is `autoware_auto_*_msgs`** — this is the canonical Autoware.Auto namespace. The original document's `autoware_*_msgs` (without `.auto`) is from a transitional version.
- **`AckermannControlCommand` replaces `Control`** — the current canonical type. Sub-messages: `AckermannLateralCommand` (steering_tire_angle, steering_tire_rotation_rate) + `LongitudinalCommand` (speed, acceleration, jerk). Each has `is_defined_*` flags.
- **`Engage` message replaces the non-existent `VehicleEmergencyStamped`** — `engage=false` triggers soft disengagement. Hard ESTOP uses CAN `0x001` directly.
- **`tier4_vehicle_msgs` removed** — all tier4-specific types replaced with `autoware_auto_*` equivalents.
- **`ActuationCommandStamped` removed** — Control converts directly to CAN. No intermediate pedal-ratio message.
- **No raw CAN pass-through topics** — the node translates typed messages, not raw frames.
- **Field name `longitudinal.speed` IS correct** — the earlier audit was wrong to change it. The `LongitudinalCommand.speed` field is `speed`, not `velocity`.

### L1.2 High-Bus CAN Interface (Ratio Abstraction)

The high-level CAN bus uses **normalized ratio encoding** as the abstraction layer. This decouples the Autoware.Auto ROS interface from vehicle-specific physical units.

**Commands (Jetson → RT):**

| CAN ID | Name | Encoding | Range | Rate |
|:---|:---|:---|:---|:---|
| `0x200` | STEER_CMD | `raw = (δ_rad + π) × 1000` → int16 (bytes 0-1)<br>`raw = (δdot_rad_s + 10.0) × 100` → int16 (bytes 2-3)<br>`u8 enable` (byte 4) | δ: ±0.698 rad (±40°) → raw 2443–3840<br>δdot: 2.18–9.16 rad/s → raw 1218–1916 | 50 Hz |
| `0x201` | BRAKE_CMD | `raw = brake_ratio × 10000` → uint16 (bytes 0-1)<br>`u8 enable` (byte 2) | ratio 0.0–1.0 → raw 0–10000 | 50 Hz |
| `0x202` | THROTTLE_CMD | `raw = throttle_ratio × 10000` → uint16 (bytes 0-1)<br>`u8 enable` (byte 2) | ratio 0.0–1.0 → raw 0–10000 | 100 Hz |
| `0x203` | GEAR_CMD | `u8 gear` — Autoware.Auto constants | NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, PARK=22, LOW=23 | On change |
| `0x204` | TURN_CMD | `u8 signal` — 0=NONE, 1=LEFT, 2=RIGHT, 3=HAZARD | | On change |
| `0x205` | LIGHT_CMD | `u8 bitfield` — bit0=headlight, bit1=brake_light | | On change |
| `0x206` | AUTONOMY_CTRL | `u8 enable` — 0x01=enable, 0x00=disable (within AUTO mode, not mode change) | | On change |
| `0x001` | SAFETY_ESTOP | DLC=0 — no payload (event signal) | | Event |

**Feedback (RT → Jetson):**

| CAN ID | Name | Encoding | Range | Rate |
|:---|:---|:---|:---|:---|
| `0x300` | STEER_FBK | `δ_rad = raw × 0.001 − π` (inverse of command)<br>`δdot_rad_s = raw × 0.01 − 10.0`<br>`u8 fault` — 0=OK, 1=fault | Same as command | 100 Hz |
| `0x301` | BRAKE_FBK | `ratio = raw / 10000`<br>`u8 fault` | Same as command | 100 Hz |
| `0x302` | THROTTLE_FBK | `ratio = raw / 10000`<br>`u8 fault` | Same as command | 100 Hz |
| `0x303` | GEAR_FBK | `u8 gear` — Autoware.Auto constants | | 10 Hz |
| `0x320` | SPEED_YAW_FBK | `v_m_s = raw_speed × 0.01` → int16<br>`ω_rad_s = raw_yaw × 0.001` → int16 | speed: ±327.67 m/s<br>yaw: ±32.767 rad/s | 100 Hz |
| `0x340` | SYS_STATUS | `u8 flags` — bit0=auto_enabled, bit1=override, bit2=fault, bit3=estop<br>`u8 fault_code` | | 5 Hz |

**Why ratio encoding?**
- The high bus carries a dimensionless abstraction. Any Autoware.Auto vehicle_interface can encode to this format.
- RT translates ratios to vehicle-specific physical units (degrees, kPa, mm/s) for the low bus.
- The encoding symmetry (command ↔ feedback are inverse formulas) means the Jetson can validate its commands against feedback without knowing the vehicle's physical ranges.

**CAN IDs follow a clean namespace:** `0x2xx` = commands, `0x3xx` = feedback. Heartbeats and ESTOP are at their own canonical IDs (`0x7FC`/`0x7FD`/`0x001`).

### L1.3 Autoware.Auto Compliance Fixes

These are the ONLY changes to the original document that correct actual Autoware.Auto compatibility errors:

| Original | Fixed | Reason |
|:---|:---|:---|
| `autoware_control_msgs` | `autoware_auto_control_msgs` | Missing `.auto` prefix |
| `autoware_vehicle_msgs` | `autoware_auto_vehicle_msgs` | Missing `.auto` prefix |
| `Control` message | `AckermannControlCommand` | Current canonical type |
| `VehicleEmergencyStamped` | `Engage` message | Original type doesn't exist |
| Gear `10=REVERSE, 20=PARK` | `REVERSE=20, PARK=22, LOW=23` | Match Autoware constants |
| Gear `3=LOW` | `LOW=23` | Match Autoware constants |

### L1.4 Lifecycle Node

The vehicle_interface node is an Autoware.Auto lifecycle node with standard transitions:
- **configure()**: Initialize CAN socket, load parameters, verify CAN bus health
- **activate()**: Start command publishing loop, start feedback monitoring, begin heartbeat
- **deactivate()**: Stop command sending, stop publishing, maintain monitoring
- **cleanup()**: Close CAN socket, release resources
- **error()**: Transition to safe state, stop all actuation commands, log diagnostic

---

## Layer 2: E-Trike Vehicle Adaptation

This layer documents how the E-Trike specifically implements Layer 1. All vehicle-specific values, RT translation formulas, and low-bus CAN IDs live here.

### L2.1 ROS → CAN Encoding (Jetson vehicle_interface)

The Jetson converts Autoware.Auto ROS messages to Layer 1's ratio-based CAN encoding:

```cpp
// Steering: radians → ratio-with-offset
int16_t encode_steering_angle(float delta_rad) {
    return (int16_t)((delta_rad + M_PI) * 1000.0f);  // 0 rad → 3142
}

// Throttle: m/s → ratio
uint16_t encode_throttle(float speed_m_s) {
    float ratio = std::clamp(speed_m_s / kMaxSpeedMS, 0.0f, 1.0f);
    return (uint16_t)(ratio * 10000.0f);
}

// Brake: m/s² → ratio
uint16_t encode_brake(float accel_m_s2) {
    float decel = std::max(0.0f, -accel_m_s2);  // negative accel = deceleration
    float ratio = std::clamp(decel / kMaxDecelMS2, 0.0f, 1.0f);
    return (uint16_t)(ratio * 10000.0f);
}
```

### L2.2 RT Translation Layer (Ratio → Trike Physical)

RT ESP32-S3 receives Layer 1's ratio-based CAN frames and translates to trike-specific physical units for the low bus:

| High CAN | RT Decodes Ratio | Applies Safety | Converts to Physical | Low CAN | Actuator |
|:---|:---|:---|:---|:---|:---|
| `0x200` STEER_CMD | `δ_rad = raw × 0.001 − π`<br>`δdot_rad_s = raw × 0.01 − 10.0` | Dynamic angle clamp (speed-dependent)<br>Software hard-stops (±40°)<br>Following error check | `eps_angle_raw = δ_deg × 10` (plus verify offset=-3000 per SYNTREE CSV)<br>`eps_rate = δdot_deg_s` (clamp 125–525 °/s) | `0x169` VCU_SES_REQ @50Hz | SYNTREE EPS-C |
| `0x201` BRAKE_CMD | `ratio = raw / 10000.0f` | Max-select: `max(rt_obstacle_pressure, ratio × 5000 kPa)` | `kPa = ratio × 5000` → `seb_raw = kPa × 0.02` | `0x205` RT_BRAKE_CMD → SYS → `0x7B9` VCU_SEB_REQ @50Hz | SYS → SYNTREE SEB |
| `0x202` THROTTLE_CMD | `ratio = raw / 10000.0f` | Clamp [0.0, 1.0]. If gear=REVERSE, negate. | `speed_mmps = ratio × 3000` (signed if reverse) | `0x204` RT_DRIVE_CMD @100Hz | SYS → MTR STM32 |
| `0x203` GEAR_CMD | Autoware constant (e.g., REVERSE=20) | N/A | Map: DRIVE(2)→D(1), REVERSE(20)→R(3), PARK(22)/NEUTRAL(1)→N(0) | `0x204` gear field | SYS → MTR STM32 |
| `0x204` TURN_CMD | Direct | N/A | Pass through turn bits | `0x302` HOST_LIGHT_CMD | SYS → GPIO |
| `0x205` LIGHT_CMD | Direct | N/A | Pass through headlight+brake bits | `0x302` HOST_LIGHT_CMD | SYS → GPIO |
| `0x206` AUTONOMY_CTRL | Direct | N/A | Enable/disable command sending. Does NOT change mode. | — (internal) | — |
| `0x001` ESTOP | DLC=0 (event) | Immediate | Forward to low bus + mode_set(Estop) | `0x001` SAFETY_ESTOP | All low-bus nodes |

**Steering conversion detail (BLOCKER):** The SYNTREE EPS-C CSV declares `VCU_SES_Tgt_StrAngle` with `scale=0.1, offset=-3000` (unsigned 16-bit). This means: `raw = δ_deg × 10 + 30000`. At 0° → raw=30000, at +40° → raw=30400, at -40° → raw=29600. This encoding must be verified against live CAN bus traffic from the physical EPS-C unit before Phase 5 implementation.

### L2.3 RT Feedback Translation (Trike Physical → Ratio)

| Low CAN Source | RT Extracts | Converts to Ratio | High CAN Output |
|:---|:---|:---|:---|
| `0x201` SES_STATUS (EPS-C) | `SES_StrAngle` (u16, offset=-3000 per CSV) → `δ_deg` | `δ_rad = δ_deg × π/180` → encode: `raw = (δ_rad + π) × 1000` | `0x300` STEER_FBK @100Hz |
| `0x201` SES_STATUS | `SES_Error_Status` (bits 6-7): 0=OK→0, 1/2/3→1 | Direct map | `0x300` fault byte |
| `0x721` SEB_STATUS (requires adding to RT RX) | `SEB_Pressure_Value` (u8, 0.05 MPa/bit) → kPa | `ratio = kPa / 5000` → `raw = ratio × 10000` | `0x301` BRAKE_FBK @100Hz |
| `0x721` SEB_STATUS | `SEB_Error_Status` (bits 6-7): 0=OK→0, 1/2/3→1 | Direct map | `0x301` fault byte |
| `0x120` SYS_THROTTLE_STS | `speed_mmps` (i16) | `ratio = speed_mmps / 3000` → `raw = ratio × 10000` | `0x302` THROTTLE_FBK @100Hz |
| `0x120` SYS_THROTTLE_STS | `speed_mmps` (i16) | `raw_speed = speed_mmps / 10` (0.01 m/s per bit) | `0x320` speed field @100Hz |
| (IMU or inverse kinematics — TBD) | yaw rate (rad/s) | `raw_yaw = yaw_rad_s × 1000` (0.001 rad/s per bit) | `0x320` yaw field @100Hz |
| `0x210` (internal RT state) | trike gear enum | Map: N(0)→NONE(0), D(1)→DRIVE(2), S(2)→LOW(23), R(3)→REVERSE(20) | `0x303` GEAR_FBK @10Hz |
| Mode state + fault flags | Pack bitfield | bit0=auto&&engaged, bit1=override, bit2=any_L3_fault, bit3=estop | `0x340` SYS_STATUS @5Hz |

### L2.4 E-Trike Parameters

These parameters configure Layer 2. They override no Layer 1 standard — they ARE the trike-specific values:

```yaml
/**:
  ros__parameters:
    # Tricycle geometry
    wheel_base: 1.5              # meters (single front wheel to rear axle)
    is_tricycle: true
    
    # Speed limits
    max_speed_forward: 3.0       # m/s (3000 mm/s)
    max_speed_reverse: 0.5       # m/s (500 mm/s)
    max_deceleration: 5.0        # m/s² (for ratio calculation)
    low_speed_threshold: 0.05    # m/s (below this, steering hold-decays)
    
    # Steering limits
    max_steering_angle: 0.698    # rad (40° — EPS-C software hard-stop)
    max_steering_rate: 9.16      # rad/s (525°/s — EPS-C max)
    min_steering_rate: 2.18      # rad/s (125°/s — EPS-C min, may reject below)
    
    # Brake limits
    max_brake_pressure_kpa: 5000 # kPa (5 MPa — SEB max)
    emergency_brake_stroke_mm: 27.0  # ESTOP full brake
    
    # Timing
    loop_rate: 100.0             # Hz (match RT control loop)
    command_timeout_ms: 500      # ms (architecture §7.6)
    
    # CAN
    can_interface: "can0"        # High bus only (SocketCAN)
    can_bitrate: 500000
    
    # RT interface
    rt_heartbeat_id: 0x7FD
    rt_heartbeat_timeout_ms: 1500
    jetson_heartbeat_id: 0x7FC
    
    # SYNTREE CAN IDs (for reference — actual TX by RT/SYS)
    eps_cmd_id: 0x169            # VCU_SES_REQ (SYNTREE factory — LOCKED)
    eps_status_id: 0x201         # SES_STATUS
    seb_cmd_id: 0x7B9            # VCU_SEB_REQ (SYNTREE factory — LOCKED)
    seb_status_id: 0x721         # SEB_STATUS
```

**Parameters NOT included** (not applicable to trike):
- VGR coefficients — no variable gear ratio steering
- wheel_radius — not used in tricycle kinematics
- margin_time_for_gear_change — relay-driven, no mechanical chattering
- use_actuation_cmd / convert_steer_cmd — Nav2 patterns, removed

### L2.5 Low-Bus CAN IDs (Not in Layer 1)

These CAN IDs exist only on the low bus. Layer 1 never references them. They are documented here for Layer 2 implementation:

**Locked (SYNTREE factory — cannot change):**
`0x169` VCU_SES_REQ, `0x201` SES_STATUS, `0x202` SES_ErrInfo, `0x203` SES_Version, `0x6FA` SES_Test, `0x7B9` VCU_SEB_REQ, `0x721` SEB_STATUS, `0x731` SEB_ErrInfo, `0x741` SEB_Version, `0x6FB` SEB_Test

**Constrained (ours, on low bus, must not collide with SYNTREE):**
`0x001` ESTOP, `0x011` SYS_SAFETY_STS, `0x012` DCDC_CMD, `0x110` MODE_CMD, `0x120` THROTTLE_STS, `0x204` DRIVE_CMD, `0x205` BRAKE_CMD, `0x206` MOTOR_FBK, `0x302` LIGHT_CMD, `0x600` DIAG_RPT, `0x7FD` RT_HEARTBEAT, `0x7FE` SYS_HEARTBEAT

**High bus and low bus are physically separate.** RT has two different CAN controllers (TWAI for low, MCP2515/SPI for high). Same CAN ID on both buses is safe — RT distinguishes by controller. But for debugging clarity, high-bus IDs use `0x2xx`/`0x3xx` namespace, low-bus IDs are scattered per SYNTREE assignment.

### L2.6 Safety Architecture (Layer 2 specific)

All safety mechanisms described in `architecture.md` operate in Layer 2 and are unchanged:

| Mechanism | Location | Status |
|:---|:---|:---|
| ESTOP button → motor kill | SYS GPIO1 → MTR | Unchanged |
| Dynamic steering clamp | RT control_task | Unchanged |
| Steering following error | RT (5° for 300ms → ESTOP) | Unchanged |
| Brake max-select arbitration | RT (max of obstacle + Jetson request) | Unchanged |
| Brake lever override | SYS (always wins) | Unchanged |
| SYS heartbeat → RT brake takeover | RT (200ms timeout, takes over 0x7B9) | Unchanged |
| External watchdog | TPS3850 on each ESP32 (100ms window) | Unchanged |
| EGAS 3-level separation | MTR(L1) / SYS(L2) / ESTOP button(L3) | Unchanged |

**Three-frame split risk:** Layer 1 splits the combined drive command into `0x200`+`0x202`+`0x203`. RT must use queue-based assembly (`PendingSetpoint` populated by dispatch from three queues, consumed atomically by control_task at 100 Hz). Add per-field timestamps: if any field is older than 100ms, flag warning; if older than 500ms, trigger controlled stop. Do NOT use shared struct writes without synchronization.

**RT input validation (defense in depth):** RT clamps ALL high-bus input ratios to [0.0, 1.0] before conversion, regardless of what Jetson sends. A buggy or compromised Jetson cannot command out-of-range values.

---

## Layer 3: Out of Scope

### L3.1 micro-ROS Events (§4 of original)

**Extract to:** `docs/micro-ros-event-reference.md` (or delete)

The original document defines micro-ROS event flags for STM32 firmware. The E-Trike does not use micro-ROS. The ESP32-S3 runs raw FreeRTOS with direct CAN. The event flag concept (mode transitions, data availability) is implemented via CAN messages and FreeRTOS queues, not micro-ROS. The spec is preserved for reference but is not part of the E-Trike implementation.

### L3.2 HIL Serial Protocol (§5 of original)

**Extract to:** `docs/hil-simulation-protocol.md`

CARLA simulation interface. Not used in production. Separate document.

### L3.3 VGR, wheel_radius, margin_time_for_gear_change

These parameters exist in the original document for passenger-car vehicles. They are not applicable to the E-Trike and are not included in the trike parameter file. The document's parameter table retains them as optional parameters (marked "not applicable to tricycle").

---

## Implementation Phases

### Phase 0: Document Rewrite (THIS PHASE)
- [ ] Rewrite `autoware-auto-communication-architecture.md` in the 3-layer structure
- [ ] Layer 1: Clean Autoware.Auto interface with `.auto` namespace, ratio encoding, correct message types
- [ ] Layer 2: E-Trike adaptation with RT translation formulas, trike parameters, low-bus IDs
- [ ] Layer 3: Extract micro-ROS and HIL to separate files

### Phase 1: Verify Autoware.Auto Compliance
- [ ] Confirm `autoware_auto_*_msgs` package availability on target Autoware version
- [ ] Verify `AckermannControlCommand` field names match Layer 1 encoding logic
- [ ] Verify `Engage` message path for soft disengagement
- [ ] Test ratio encoding round-trip (ROS→CAN→RT→CAN→ROS) in simulation

### Phase 2: RT Firmware Updates
- [ ] Add `0x721` to RT low-bus RX list (needed for brake feedback)
- [ ] Implement ratio→physical translation for all command frames
- [ ] Implement physical→ratio translation for all feedback frames
- [ ] Implement queue-based PendingSetpoint assembly (3 queues, one per command frame)
- [ ] Add per-field staleness monitoring (100ms warning, 500ms controlled stop)
- [ ] Add RT input ratio clamping (defense in depth)
- [ ] Remove old Category 1 forwarding for `0x011`, `0x120`, `0x302`

### Phase 3: Jetson vehicle_interface Node
- [ ] Create `jetson/src/autoware_vehicle_bridge/` package
- [ ] Implement lifecycle node (configure/activate/deactivate/cleanup)
- [ ] Implement ROS→CAN encoding (Layer 1 formulas)
- [ ] Implement CAN→ROS decoding (inverse formulas)
- [ ] Implement heartbeat (send `0x7FC`, monitor `0x7FD`)
- [ ] Implement ESTOP path (Engage=false→`0x206` disable; hard emergency→`0x001`)

### Phase 4: SYNTREE Verification (BLOCKER)
- [ ] **BLOCKER:** Capture live CAN bus traffic from physical EPS-C
- [ ] Verify steering angle encoding (offset=0 vs offset=-3000)
- [ ] Verify EPS-C minimum slew rate (125°/s rejection threshold)
- [ ] Verify checksum algorithm (XOR vs SUM)
- [ ] Document confirmed encoding in Layer 2

### Phase 5: Integration & Safety Validation
- [ ] End-to-end: Planning → ROS → CAN → RT → actuator → feedback → CAN → ROS
- [ ] All 8 ESTOP trigger paths
- [ ] Heartbeat loss scenarios (RT, SYS, Jetson)
- [ ] 4-hour endurance soak

---

## What This Strategy Resolves

| Problem from Earlier Plan | Resolution |
|:---|:---|
| §2C vs §5 encoding contradiction | Layer 1 exclusively uses ratio encoding. Layer 2 defines RT translation. No contradiction. |
| Plan replaces original spec | Layer 1 preserves the interface standard. Layer 2 documents trike adaptation. Clear separation. |
| `longitudinal.speed` "fixed" to `velocity` | Reverted. `speed` IS correct per `LongitudinalCommand.msg`. |
| Package namespace missing `.auto` | Fixed: `autoware_auto_*_msgs` throughout. |
| `AckermannControlCommand` not recognized | Adopted as Layer 1 canonical type. Includes sub-message structure and `is_defined_*` flags. |
| `Engage` message not used | Added to Layer 1 as standard soft disengagement path. |
| Document Purist: 12 HIGH findings about replacing spec | All resolved — spec preserved, adaptation layered. |
| Integration gaps: 25 data flow issues | Resolved by consistent ratio encoding + RT translation formulas for all 6 loops. |
| Steering offset BLOCKER | Explicitly in Phase 4 as blocking prerequisite. Both encodings documented until verified. |
| `0x721` missing from RT RX | Added to Phase 2 as required firmware change. |

---

*Strategy version: 0.0.4-alpha. Based on 8-agent audit (116+ findings, 2 rounds).*

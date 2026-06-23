# Autoware.Auto Integration — E-Trike v0.0.4-alpha

**Supersedes:** `autoware-auto-upgrade-plan-0.0.4.md`, `autoware-auto-upgrade-strategy-0.0.4.md`, `pipeline-gap-fixes-0.0.4.md`
**Base document to upgrade:** `docs/autoware-auto-communication-architecture.md`
**Audit basis:** 11 agents across 3 rounds (161+ findings)
**Date:** 2026-06-23

---

## 1. Architecture

### 1.1 Three-Layer Model

The original document serves two roles that must be separated:

```
LAYER 1: Autoware.Auto Interface Standard     ← Portable. Same for any vehicle.
  ROS topics, message types, CAN namespace, ratio encoding
  This IS the spec. We adapt our system to match IT.

LAYER 2: E-Trike Vehicle Adaptation            ← Our vehicle only.
  RT translation formulas (ratio↔physical), low-bus CAN IDs,
  trike parameters, actuator specifics

LAYER 3: Out of Scope                          ← Separate files.
  micro-ROS (§4 of original), HIL serial (§5)
```

**Principle:** The original document's interface IS the Autoware.Auto-compatible target. We don't rewrite it to match our hardware — we add a translation layer (Layer 2) that maps between the standard interface and our vehicle.

### 1.2 Physical Topology

```
┌─ Jetson Orin (Linux + ROS 2) ─────────────────────────┐
│  autoware_planning → AckermannControlCommand           │
│  vehicle_interface node: ROS ↔ CAN translation         │
└────────────┬───────────────────────────────────────────┘
             │ SocketCAN can0
    High CAN │ 500 kbit/s
             │
┌────────────┴─── RT ESP32-S3 (FreeRTOS, 8 tasks) ──────┐
│  CAN Gateway: ratio decode → safety → physical encode  │
│  Steering LBS, dynamic clamp, brake max-select         │
└────────────┬───────────────────────────────────────────┘
             │ TWAI
    Low CAN  │ 500 kbit/s
             │
    ┌────────┼──────────┬──────────────┐
    ▼        ▼          ▼              ▼
  EPS-C    SEB      SYS ESP32-S3   MTR STM32
  (steer)  (brake)  (safety,       (motor DAC,
   0x169    0x7B9    lights)         gear relays)
```

---

## 2. Layer 1: Autoware.Auto Interface Standard

### 2.1 ROS 2 Topics

**Subscriptions (Inputs):**

| Topic | Message Type | Maps To | Notes |
|:---|:---|:---|:---|
| `~/input/control_cmd` | `autoware_auto_control_msgs/msg/AckermannControlCommand` | `0x200` steer + `0x202` throttle + `0x201` brake | `lateral.steering_tire_angle` → steer. `longitudinal.speed` → throttle. `longitudinal.acceleration` (negative) → brake. Check `is_defined_*` flags before each. |
| `~/input/gear_cmd` | `autoware_auto_vehicle_msgs/msg/GearCommand` | `0x203` gear | Autoware constants: DRIVE=2, REVERSE=20, PARK=22 |
| `~/input/turn_indicators_cmd` | `autoware_auto_vehicle_msgs/msg/TurnIndicatorsCommand` | `0x204` turn signal | LEFT=2, RIGHT=3 |
| `~/input/hazard_lights_cmd` | `autoware_auto_vehicle_msgs/msg/HazardLightsCommand` | `0x204` value=3 (HAZARD) | |
| `~/input/engage` | `autoware_auto_vehicle_msgs/msg/Engage` | `0x206` autonomy ctrl | `engage=true` → enable, `false` → soft disengage |

**Publications (Outputs):**

| Topic | Message Type | Source CAN | Notes |
|:---|:---|:---|:---|
| `~/output/velocity_status` | `autoware_auto_vehicle_msgs/msg/VelocityReport` | `0x320` | `longitudinal_velocity` from speed. `heading_rate` from yaw (TBD). `lateral_velocity` = 0 (no sensor). |
| `~/output/steering_status` | `autoware_auto_vehicle_msgs/msg/SteeringReport` | `0x300` | `steering_tire_angle` in radians |
| `~/output/gear_status` | `autoware_auto_vehicle_msgs/msg/GearReport` | `0x303` | Autoware constants |
| `~/output/control_mode` | `autoware_auto_vehicle_msgs/msg/ControlModeReport` | `0x340` flags | Manual→MANUAL(4), Auto+engaged→AUTONOMOUS(1), Estop→DISENGAGED(5) |

**Removed from original:** `ActuationCommandStamped` (Nav2 anti-pattern), `VehicleEmergencyStamped` (doesn't exist in Autoware.Auto), raw CAN pass-through topics, `tier4_vehicle_msgs` dependencies.

**Package namespace:** `autoware_auto_*_msgs` (with `.auto` prefix). Not `autoware_*_msgs` (transitional version without prefix).

### 2.2 High-Bus CAN Protocol (Ratio Encoding)

The high bus uses normalized ratio encoding as the abstraction layer. This decouples Autoware.Auto ROS messages from vehicle-specific physical units.

**Commands (Jetson → RT):**

| CAN ID | Name | Encoding | DLC | Rate |
|:---|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | DLC=0 (event) | 0 | Event |
| `0x200` | STEER_CMD | `{i16 angle_raw, i16 rate_raw, u8 enable}` — `angle_raw = (δ_rad + π) × 1000`, `rate_raw = (δdot_rad_s + 10.0) × 100` | 5 | 50 Hz |
| `0x201` | BRAKE_CMD | `{u16 ratio, u8 enable}` — `ratio = brake_ratio × 10000` (0.0–1.0 → 0–10000) | 3 | 50 Hz |
| `0x202` | THROTTLE_CMD | `{u16 ratio, u8 enable}` — `ratio = throttle_ratio × 10000` (0.0–1.0 → 0–10000) | 3 | 100 Hz |
| `0x203` | GEAR_CMD | `{u8 gear}` — Autoware constants: NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, PARK=22, LOW=23 | 1 | Change |
| `0x204` | TURN_CMD | `{u8 signal}` — 0=NONE, 1=LEFT, 2=RIGHT, 3=HAZARD | 1 | Change |
| `0x205` | LIGHT_CMD | `{u8 bits}` — bit0=headlight, bit1=brake_light | 1 | Change |
| `0x206` | AUTONOMY_CTRL | `{u8 enable}` — 0x01=enable within AUTO, 0x00=disable (stays in AUTO, stops commands) | 1 | Change |
| `0x400` | OBSTACLE_DIST | `{u32 distance_mm}` — Jetson perception → RT. UINT32_MAX = no reading | 4 | 10 Hz |
| `0x7FC` | JETSON_HEARTBEAT | `{u8 alive_ctr}` | 1 | 2 Hz |

**Feedback (RT → Jetson):**

| CAN ID | Name | Encoding | DLC | Rate |
|:---|:---|:---|:---|:---|
| `0x300` | STEER_FBK | `{i16 angle_raw, i16 rate_raw, u8 fault}` — inverse of `0x200` encoding. fault: 0=OK, 1=any error | 5 | 100 Hz |
| `0x301` | BRAKE_FBK | `{u16 ratio, u8 fault}` — inverse of `0x201` encoding | 3 | 100 Hz |
| `0x302` | THROTTLE_FBK | `{u16 ratio, u8 fault}` — inverse of `0x202` encoding | 3 | 100 Hz |
| `0x303` | GEAR_FBK | `{u8 gear}` — Autoware constants | 1 | 10 Hz |
| `0x320` | SPEED_YAW_FBK | `{i16 speed_raw, i16 yaw_raw}` — `speed_raw = v_m_s / 0.01`, `yaw_raw = ω_rad_s / 0.001` | 4 | 100 Hz |
| `0x340` | SYS_STATUS | `{u8 flags, u8 fault_code}` — bit0=auto_enabled, bit1=override, bit2=fault, bit3=estop | 2 | 5 Hz |
| `0x600` | SYS_DIAG_RPT | `{u8 mode, u8 brake, u8 hb, u8 estop, u16 heap, u8 tec, u8 rec}` | 8 | 1 Hz |
| `0x7FD` | RT_HEARTBEAT | `{u8 alive_ctr}` — separate counter per bus, NOT bridged | 1 | 2 Hz |

**Why ratio encoding:**
- High bus carries dimensionless abstraction. Any vehicle_interface can encode to this format.
- RT translates ratios to trike-specific physical units (degrees, kPa, mm/s) for low bus.
- Encoding symmetry (command↔feedback are inverse formulas) lets Jetson validate commands against feedback without knowing vehicle physical ranges.

---

## 3. Layer 2: E-Trike Vehicle Adaptation

### 3.1 RT Translation: Ratio → Physical → Low Bus

| High CAN In | RT Decodes | Safety Check | Converts to Physical | Low CAN Out | Actuator |
|:---|:---|:---|:---|:---|:---|
| `0x200` STEER_CMD | `δ_rad = raw×0.001−π`<br>`δdot_rad_s = raw×0.01−10.0` | Dynamic clamp (speed-dep.)<br>Hard-stop ±40°<br>Following error 5°/300ms→ESTOP<br>LBS state machine | `eps_raw = δ_deg×10 + 30000` (offset=-3000 per SYNTREE CSV)<br>`eps_rate = clamp(δdot_deg_s, 125, 525)`<br>`veh_spd_kmh = measured_speed_mmps × 0.0036` | `0x169` VCU_SES_REQ @50Hz | SYNTREE EPS-C |
| `0x201` BRAKE_CMD | `ratio = raw/10000`<br>`jetson_kpa = ratio×5000` | Max-select: `max(rt_obstacle_kpa, jetson_kpa)` | `seb_raw = kPa × 0.02` (clamp 0–100)<br>Mode: Pressure if >0, else Stroke | `0x205` RT_BRAKE_CMD→SYS→`0x7B9` VCU_SEB_REQ @50Hz | SYS→SYNTREE SEB |
| `0x202` THROTTLE_CMD | `ratio = raw/10000`<br>`speed_mmps = ratio×3000` | Clamp [0,3000]. Negate if gear=REVERSE. | `speed_mmps` (signed i32) + gear derivation | `0x204` RT_DRIVE_CMD @100Hz | SYS→MTR STM32 |
| `0x203` GEAR_CMD | Autoware constant (e.g., REVERSE=20) | Priority: CAN explicit > auto-derived | Map: DRIVE(2)→D(1), REVERSE(20)→R(3), PARK(22)→N(0), LOW(23)→S(2) | `0x204` gear field | SYS→MTR STM32 |
| `0x204` TURN_CMD | Direct pass-through | — | Forward to low bus turn bits | `0x302` HOST_LIGHT_CMD | SYS→GPIO |
| `0x205` LIGHT_CMD | Direct pass-through | — | Forward headlight+brake bits | `0x302` HOST_LIGHT_CMD | SYS→GPIO |
| `0x206` AUTONOMY_CTRL | Direct | — | Enable/disable command sending (stays in AUTO) | — (internal state) | — |
| `0x001` ESTOP | DLC=0 event | Immediate | Forward to low bus + mode_set(Estop) | `0x001` SAFETY_ESTOP | All low-bus nodes |

**Dynamic angle clamp formula:**
```
limit_deg = 40.0 − (speed_kmh − 2.0) × (35.0 / 23.0)   [linear, 2→25 km/h]
limit_deg = clamp(limit_deg, 5.0, 40.0)
```

**Following error threshold (speed-scaled):**
```
threshold_deg = max(2.0, 0.25 × dynamic_limit_deg)
```

**Obstacle→kPa formula:**
```
dist = min(jetson_0x400_mm, rt_ultrasonic_mm)
kPa = (dist ≤ 300) ? 5000 : (dist ≥ 3000) ? 0 : 5000 × (1 − (dist−300)/2700)
```

**Steering slew rate (speed-dependent):**
```
rate_deg_s = 125.0 + (speed_kmh − 2.0) × (400.0 / 23.0)   [linear, 2→25 km/h]
rate_deg_s = clamp(rate_deg_s, 125.0, 525.0)
```

**Gear priority:**
```
auto_gear = (speed > 50) ? D : (speed < −50) ? R : N
final_gear = (can_gear_cmd != NONE) ? can_gear_cmd : auto_gear
```

### 3.2 RT Feedback Translation: Physical → Ratio → High Bus

| Low CAN Source | RT Extracts | Converts to Ratio | High CAN Out |
|:---|:---|:---|:---|
| `0x201` SES_STATUS | `SES_StrAngle` (u16, offset=-3000) → `δ_deg`<br>`SES_Error_Status` (bits 6-7): 0→OK, else→fault | `δ_rad = δ_deg × π/180`<br>`raw = (δ_rad + π) × 1000` | `0x300` STEER_FBK @100Hz |
| `0x721` SEB_STATUS | `SEB_Pressure_Value` (u8, 0.05 MPa/bit) → kPa<br>`SEB_Error_Status`: 0→OK, else→fault | `ratio = kPa / 5000`<br>`raw = ratio × 10000` | `0x301` BRAKE_FBK @100Hz |
| `0x120` SYS_THROTTLE_STS | `speed_mmps` (i16) | `ratio = speed_mmps / 3000`<br>`raw = ratio × 10000` | `0x302` THROTTLE_FBK @100Hz |
| `0x120` SYS_THROTTLE_STS | `speed_mmps` (i16) | `raw = speed_mmps / 10` (0.01 m/s/bit) | `0x320` speed field @100Hz |
| (IMU or inverse kinematics) | yaw_rate (rad/s) | `raw = yaw_rad_s × 1000` | `0x320` yaw field @100Hz |
| RT internal gear state | trike gear enum | Map to Autoware constants | `0x303` GEAR_FBK @10Hz |
| Mode + fault flags | Pack bitfield | bit0=auto&&engaged, bit1=override, bit2=any_L3, bit3=estop | `0x340` SYS_STATUS @5Hz |

**⚠️ `0x721` must be added to RT's low-bus RX list** (architecture.md §7.3 currently omits it).

### 3.3 Low-Bus CAN IDs (Not in Layer 1)

These exist only on the low bus. Layer 1 never references them.

**Locked (SYNTREE factory — cannot change):**
`0x169`, `0x201`, `0x202`, `0x203`, `0x6FA` (EPS-C), `0x7B9`, `0x721`, `0x731`, `0x741`, `0x6FB` (SEB)

**Constrained (ours, must not collide with SYNTREE):**
`0x001`, `0x011`, `0x012`, `0x110`, `0x120`, `0x204`, `0x205`, `0x206`, `0x302`, `0x600`, `0x7FD`, `0x7FE`

High bus and low bus are physically separate — same CAN ID on both buses is safe (RT has two controllers: TWAI + MCP2515/SPI).

### 3.4 E-Trike Parameters

```yaml
/**:
  ros__parameters:
    # Tricycle geometry
    wheel_base: 1.5              # m (single front wheel to rear axle)
    is_tricycle: true
    
    # Speed limits
    max_speed_forward: 3.0       # m/s
    max_speed_reverse: 0.5       # m/s
    max_deceleration: 5.0        # m/s² (for brake ratio calculation)
    low_speed_threshold: 0.05    # m/s
    
    # Steering limits
    max_steering_angle: 0.698    # rad (40° — EPS-C software hard-stop)
    max_steering_rate: 9.16      # rad/s (525°/s)
    min_steering_rate: 2.18      # rad/s (125°/s — EPS-C may reject below)
    
    # Brake limits
    max_brake_pressure_kpa: 5000 # kPa (5 MPa — SEB max)
    emergency_brake_stroke_mm: 27.0
    
    # Timing
    loop_rate: 100.0             # Hz
    command_timeout_ms: 500
    
    # CAN
    can_interface: "can0"
    can_bitrate: 500000
    
    # Heartbeats
    rt_heartbeat_id: 0x7FD
    rt_heartbeat_timeout_ms: 1500
    jetson_heartbeat_id: 0x7FC
    sys_heartbeat_id: 0x7FE
    sys_heartbeat_timeout_ms: 200    # 10 Hz SYS heartbeat
    
    # SYNTREE CAN IDs (for reference — actual TX by RT/SYS)
    eps_cmd_id: 0x169            # LOCKED
    eps_status_id: 0x201         # LOCKED
    seb_cmd_id: 0x7B9            # LOCKED
    seb_status_id: 0x721         # LOCKED
```

**Parameters removed** (not applicable to tricycle):
VGR coefficients, wheel_radius, margin_time_for_gear_change, use_actuation_cmd, convert_steer_cmd, max_throttle (pedal ratio), max_brake (pedal ratio), emergency_brake (pedal ratio)

---

## 4. Pipeline: Autoware Command → Actuator

### 4.1 Steering

```
AckermannControlCommand.lateral.steering_tire_angle (rad)
  → Jetson: check is_defined_steering_tire_angle
  → Jetson: raw = (δ + π) × 1000 → 0x200 @50Hz
  → RT MCP2515: decode δ = raw × 0.001 − π
  → RT control_task: dynamic clamp (speed-dependent)
  → RT: hard-stop ±40°, following error check (speed-scaled)
  → RT LBS: BOOT_WAIT(500ms) → LISTEN_SYNC(await 0x201) → ACTIVE
  → RT: eps_raw = δ_deg × 10 + 30000 (offset=-3000 per CSV)
  → RT: build 0x169 (rolling cnt++, XOR^0xFF cksum, Veh_Spd km/h)
  → EPS-C: validate counter+cksum, PID to target
  → EPS-C → 0x201 → RT: extract SES_StrAngle, following error check
  → RT → 0x300 → Jetson → SteeringReport
```

### 4.2 Brake

```
AckermannControlCommand.longitudinal.acceleration (m/s²)
  → Jetson: check is_defined_acceleration
  → Jetson: ratio = clamp(|min(0,accel)|/5.0, 0, 1) → raw = ratio×10000 → 0x201 @50Hz
  → RT: decode ratio, kPa = ratio × 5000
  → RT: max-select(kPa, obstacle_kpa)
  → RT → 0x205 → SYS: i32 kPa @50Hz
  → SYS: seb_raw = kPa × 0.02, Mode=Pressure if >0 else Stroke
  → SYS → 0x7B9 → SEB: rolling cnt++, XOR^0xFF cksum, mode-muxed byte 3
  → SEB: validate counter+cksum, internal PID
  → SEB → 0x721 → SYS (and RT after adding to RX list)
  → RT → 0x301 → Jetson
```

### 4.3 Throttle

```
AckermannControlCommand.longitudinal.speed (m/s)
  → Jetson: check is_defined_speed
  → Jetson: ratio = clamp(speed/3.0, 0, 1) → raw = ratio×10000 → 0x202 @100Hz
  → RT: decode ratio, speed_mmps = ratio × 3000
  → RT: gear: CAN override > auto-derived (v>0→D, v<0→R, v≈0→N)
  → RT → 0x204 → SYS: {i32 speed_mmps, u8 gear} @100Hz
  → SYS: MCP4725 = abs(speed)/3000 × 4095 (0-5V)
  → SYS: gear relays (GPIO33/34/35)
  → MTR STM32: analog → motor controller, 72V → ECU
  → MTR → 0x120 → RT: i16 speed_mmps @100Hz
  → RT → 0x302 + 0x320 → Jetson → VelocityReport
```

### 4.4 ESTOP (8 Paths)

| Path | Trigger | Detection | Actuator Response |
|:---|:---|:---|:---|
| A: Physical button | GPIO1 NC opens | <1ms MTR, 45ms SEB | Motor kill + full brake |
| B: Jetson CAN 0x001 | ROS emergency | ~10ms + 45ms | Same as A |
| C: 0x001 any node | Frame on bus | <5ms/hop | Immediate ESTOP |
| D: RT heartbeat loss | SYS: 0x7FD frozen | 1000ms | ESTOP (preceded by 200ms 0x204 staleness) |
| E: SYS heartbeat loss | RT: 0x7FE frozen @10Hz | 200ms | RT takes over 0x7B9 stroke=max, 220ms total gap |
| F: Steering following error | RT: |cmd−actual|>threshold | 300ms | Obstacle: hold→silent-stop. Normal: ramp to 0° |
| G: External watchdog | TPS3850, 100ms toggle gap | ~100ms | MCU reset → safe state (~2.5s reboot) |
| H: 0x204 staleness | SYS: >200ms since last | 200ms | Zero speed + neutral (NOT ESTOP, controlled stop) |

### 4.5 Heartbeats

| ID | Sender | Bus | Receiver | Period | Timeout | Action on Loss |
|:---|:---|:---|:---|:---|:---|:---|
| `0x7FC` | Jetson | High | RT | 2 Hz | 1500ms | Assisted stop: zero 0x204 + stop 0x169 + 0x205=2000kPa |
| `0x7FD` | RT | Low | SYS | 2 Hz | 1000ms | SYS enters ESTOP |
| `0x7FD` | RT | High | Jetson | 2 Hz | 1500ms | Jetson stops publishing commands |
| `0x7FE` | SYS | Low | RT | **10 Hz** | **200ms** | RT takes over 0x7B9 stroke=max + sends 0x001 |

**⚠️ SYS heartbeat is 10 Hz (not 2 Hz).** This was the gap #12 fix. Can-dictionary.md `0x7FE` and architecture.md §2.1 still show stale 2 Hz values. Must be updated.

---

## 5. Blockers & Fixes

### 5.1 CRITICAL — Must Resolve Before Any Firmware

**B1 — Steering Angle Offset (Phase 4 blocker)**
- EPS-C CSV says `offset=-3000` (raw=30000 at 0°). Architecture.md says `raw = mdeg/100` (raw=0 at 0°).
- **Fix:** Bench test with CAN analyzer. Turn EPS-C to known angles, capture `0x201 SES_STATUS`. Trust CSV (manufacturer DBC) until verified.
- Use: `eps_raw = uint16_t(δ_deg × 10 + 30000)`. Type must be u16, not i16 (raw 37000 exceeds i16 max).

**B2 — SEB Comm-Fault Behavior (Phase 4 blocker)**
- When CAN stops: does SEB HOLD pressure or RELEASE? Untested.
- **Fix:** Bench test with pressure gauge. Command pressure, cut CAN, observe.
- If releases: add NC brake-hold relay gated by TPS3850 RST line.
- Currently: RT takeover covers SYS failure (220ms gap). If SEB holds >220ms, takeover is seamless.

**B3 — Protocol Migration**
- Current RT handles `0x300`/`0x301` (physical units, combined). Strategy defines `0x200`/`0x201`/`0x202` (ratio, split).
- **Fix:** RT adds dual-protocol RX. Jetson switches to new IDs after RT deployed. Legacy removed after validation.

### 5.2 HIGH — Must Resolve Before Vehicle Is Rideable

**H1 — Dynamic Angle Clamp Formula:** `limit = 40 − (speed−2)×(35/23)`. Linear 2→25 km/h.

**H2 — Obstacle→kPa Formula:** `kPa = 5000×(1−(dist−300)/2700)`. Linear 300→3000mm. Min of Jetson `0x400` + RT ultrasonic.

**H3 — 0x721 in RT RX:** Add to architecture.md §7.3. RT dispatch parses SEB_STATUS for `0x301` brake feedback.

**H4 — SYS Heartbeat 10Hz Everywhere:** Update can-dictionary.md `0x7FE`, architecture.md §§2.1, 8.4, 8.7.

**H5 — Following Error Speed-Scaled:** `max(2°, 0.25×dynamic_limit)` replaces fixed 5°.

**H6 — 0x206 "Forward to High" Stale Comment:** Remove from architecture.md §7.3.

**H7 — 0x206 in can-dictionary:** Add `0x206 MTR_MOTOR_FBK` entry with bit layout.

**H8 — 0x400 Direction:** Jetson→RT. Fix architecture.md §7.4 (remove from RT TX).

**H9 — Obstacle Source Priority:** Jetson `0x400` primary, RT ultrasonic secondary. RT uses `min(jetson_dist, ultrasonic_dist)`.

### 5.3 MEDIUM

| # | Gap | Fix |
|:---|:---|:---|
| M1 | `is_defined_*` flags unchecked | Jetson: if false, hold last valid value (angle/speed) or zero (accel) |
| M2 | Gear priority unspecified | CAN explicit override > auto-derived from speed sign |
| M3 | VCU_Veh_Spd_Value source | Use measured speed (0x120 MTR feedback), not commanded |
| M4 | Rate-to-speed mapping | Linear: 125°/s @2km/h → 525°/s @25km/h |
| M5 | `0x120` sender: MTR vs SYS | MTR is sole sender. Fix architecture.md §8.4, can-dictionary.md |
| M6 | Atomic vs queue for brake kPa | Documented exception: single-writer, always-want-latest, 32-bit atomic on ESP32 |
| M7 | MCP4725 DAC no readback | Add periodic I2C register readback (1 Hz) with mismatch warning |
| M8 | S gear undefined | S=D for now. GPIO34 reserved for future sport mode |
| M9 | Checksum XOR vs SUM | Use XOR(bytes 0-6)^0xFF. Verify on live CAN (Phase 4) |
| M10 | Turn signal confirmation | Open-loop for 0.0.4. Accept echoed state. |

### 5.4 LOW

| # | Fix |
|:---|:---|
| L1 | "switch mode to 1 (Stroke)" → "mode bit to 0 (Stroke)" in architecture.md §8.6 |
| L2 | 0x204 staleness comment: "2 missed frames" → "20 missed frames" |
| L3 | `emergency_brake=0.7` removal documented as replaced by ESTOP stroke=27mm |
| L4 | EPS-C rate <125°/s: RT clamps floor at 125. Verify rejection behavior in Phase 4 |
| L5 | 0x001 DLC=0 vs DLC=1 with sender ID: keep DLC=0 for 0.0.4 |

---

## 6. Document Changes to `autoware-auto-communication-architecture.md`

### 6.1 Fixes (correct Autoware.Auto compliance errors)

| Original | Corrected | Reason |
|:---|:---|:---|
| `autoware_control_msgs` | `autoware_auto_control_msgs` | Missing `.auto` prefix |
| `autoware_vehicle_msgs` | `autoware_auto_vehicle_msgs` | Missing `.auto` prefix |
| `Control` message | `AckermannControlCommand` | Current canonical type |
| `VehicleEmergencyStamped` | `Engage` message | Original type doesn't exist |
| Gear `10=REVERSE, 20=PARK` | `REVERSE=20, PARK=22, LOW=23` | Match Autoware constants |
| Gear `3=LOW` | `LOW=23` | Match Autoware constants |
| `longitudinal.speed` | *(keep — IS correct per LongitudinalCommand.msg)* | Earlier audit was wrong |
| `1=Autonomous, 4=Manual` | *(keep — matches Autoware constants)* | Original was correct |

### 6.2 Additions (missing from original)

| Addition | Location |
|:---|:---|
| `0x001` SAFETY_ESTOP to CAN command table | §2A |
| `0x7FC`/`0x7FD` heartbeats to CAN tables | §2 (new liveness subsection) |
| `0x205` HOST_LIGHT_AUX (headlight + brake light) | §2A |
| `0x400` OBSTACLE_DIST (Jetson→RT) | §2A |
| `0x600` SYS_DIAG_RPT (RT→Jetson) | §2B |
| RT gateway translation table (high→low mapping) | §2 (new subsection) |
| `Engage` message subscription | §1A |
| Lifecycle node state transitions | §1 |
| E-Trike parameter values | §3 (as Layer 2 override, not replacement) |

### 6.3 Removals (not applicable to E-Trike)

| Removal | Destination |
|:---|:---|
| `tier4_vehicle_msgs` dependencies | Replaced with `autoware_auto_*_msgs` |
| `ActuationCommandStamped` subscription | Removed — Nav2 pattern |
| Raw CAN pass-through topics | Removed — security |
| §4 micro-ROS events | Extracted to `docs/micro-ros-event-reference.md` |
| §5 HIL serial protocol | Extracted to `docs/hil-simulation-protocol.md` |
| VGR, wheel_radius, margin_time_for_gear_change params | Marked "not applicable to tricycle" |

---

## 7. Implementation Phases

### Phase 0: Document Cleanup (now)
- [ ] Fix all Autoware.Auto compliance errors (§6.1)
- [ ] Add missing CAN IDs (§6.2)
- [ ] Extract micro-ROS and HIL to separate files (§6.3)
- [ ] Fix can-dictionary.md: `0x7FE`→10Hz, add `0x206`, fix `0x120` sender
- [ ] Fix architecture.md: remove `0x206` "Forward to high", fix `0x400` direction

### Phase 1: Formula Definitions
- [ ] Dynamic angle clamp linear formula (H1)
- [ ] Obstacle→kPa linear formula (H2)
- [ ] Following error speed-scaled threshold (H5)
- [ ] Gear priority: CAN override > auto-derived (M2)
- [ ] Rate-to-speed linear mapping (M3)
- [ ] VCU_Veh_Spd_Value: use measured speed (M4)

### Phase 2: Reference Doc Updates
- [ ] can-dictionary.md: `0x7FE` 10Hz, `0x206` entry, `0x120` sender→MTR
- [ ] architecture.md §§2.1, 7.3, 7.4, 8.4, 8.7: sync with Phase 0-1 changes
- [ ] Add `0x721` to architecture.md §7.3 RT RX list

### Phase 3: RT Firmware
- [ ] Add dual-protocol RX (B3): new `0x200`/`0x201`/`0x202` + keep legacy `0x300`/`0x301`
- [ ] Implement ratio→physical translation (steering, brake, throttle)
- [ ] Implement physical→ratio feedback translation functions **(do NOT enable CAN TX yet — see gate below)**
- [ ] Add `0x721` SEB_STATUS parsing for brake feedback
- [ ] Queue-based PendingSetpoint assembly (3 queues)
- [ ] Per-field staleness: 100ms warning, 500ms controlled stop
- [ ] Input ratio clamping: all ratios [0.0, 1.0] before conversion
- [ ] Remove old Category 1 forwards: `0x011`, `0x120` → Category 2 translations
- [ ] MCP4725 DAC readback (1 Hz, M7)
- [ ] **GATE: Feedback CAN TX on `0x300`/`0x301`/`0x302` must remain DISABLED.** These IDs change direction (old: Jetson→RT commands, new: RT→Jetson feedback). If RT transmits them before Jetson stops, both nodes TX the same ID on the same high bus → CAN error. Enable in Phase 6.

### Phase 4: BLOCKERS — Bench Testing (GATE)
- [ ] **B1:** Verify steering angle offset on live EPS-C CAN bus
- [ ] **B2:** Verify SEB comm-fault behavior (hold vs release)
- [ ] Verify EPS-C minimum slew rate (125°/s rejection)
- [ ] Verify checksum algorithm (XOR vs SUM)
- [ ] If B2=release: add NC brake-hold relay

### Phase 5: Jetson vehicle_interface Node
- [ ] Create `jetson/src/autoware_vehicle_bridge/` package
- [ ] Lifecycle node (configure/activate/deactivate/cleanup)
- [ ] ROS→CAN encoding (Layer 1 formulas)
- [ ] CAN→ROS decoding (inverse formulas)
- [ ] Heartbeat (send `0x7FC`, monitor `0x7FD`)
- [ ] ESTOP path: Engage=false→`0x206` disable; hard emergency→`0x001`
- [ ] `is_defined_*` flag handling (M1)

### Phase 6: Switch Jetson to New Protocol
- [ ] Jetson TX: `0x200`/`0x201`/`0x202`/`0x203` instead of `0x300`/`0x301`
- [ ] Jetson RX: `0x300`/`0x301`/`0x302`/`0x303`/`0x320`/`0x340` instead of forwarded `0x011`/`0x120`
- [ ] **After Jetson switch confirmed:** Enable RT feedback TX on `0x300`/`0x301`/`0x302` (gated since Phase 3)
- [ ] Remove legacy `0x300`/`0x301` RX from RT dispatch

### Phase 7: Integration & Safety Validation
- [ ] End-to-end: Planning → ROS → CAN → RT → actuator → feedback → CAN → ROS
- [ ] All 8 ESTOP trigger paths (A–H)
- [ ] Heartbeat loss scenarios (RT, SYS, Jetson)
- [ ] 4-hour endurance soak

---

## 8. CAN ID Quick Reference

### High Bus (Jetson ↔ RT)

| ID | Name | Direction | Content |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | Bidir (bridged) | DLC=0 |
| `0x200` | STEER_CMD | → | `{i16 angle, i16 rate, u8 enable}` |
| `0x201` | BRAKE_CMD | → | `{u16 ratio, u8 enable}` |
| `0x202` | THROTTLE_CMD | → | `{u16 ratio, u8 enable}` |
| `0x203` | GEAR_CMD | → | `{u8 gear}` |
| `0x204` | TURN_CMD | → | `{u8 signal}` |
| `0x205` | LIGHT_CMD | → | `{u8 bits}` |
| `0x206` | AUTONOMY_CTRL | → | `{u8 enable}` |
| `0x300` | STEER_FBK | ← | `{i16 angle, i16 rate, u8 fault}` |
| `0x301` | BRAKE_FBK | ← | `{u16 ratio, u8 fault}` |
| `0x302` | THROTTLE_FBK | ← | `{u16 ratio, u8 fault}` |
| `0x303` | GEAR_FBK | ← | `{u8 gear}` |
| `0x320` | SPEED_YAW_FBK | ← | `{i16 speed, i16 yaw}` |
| `0x340` | SYS_STATUS | ← | `{u8 flags, u8 fault_code}` |
| `0x400` | OBSTACLE_DIST | → | `{u32 mm}` |
| `0x600` | SYS_DIAG_RPT | ← | `{8 bytes}` |
| `0x7FC` | JETSON_HEARTBEAT | → | `{u8 ctr}` |
| `0x7FD` | RT_HEARTBEAT | ← | `{u8 ctr}` |

### Low Bus (RT/SYS/Actuators)

| ID | Name | Sender | Locked? |
|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | Any | — |
| `0x011` | SYS_SAFETY_STS | SYS | — |
| `0x012` | SYS_DCDC_CMD | SYS | — |
| `0x110` | SYS_MODE_CMD | SYS | — |
| `0x120` | SYS_THROTTLE_STS | MTR | — |
| `0x169` | VCU_SES_REQ | RT | SYNTREE |
| `0x201` | SES_STATUS | EPS-C | SYNTREE |
| `0x202` | SES_ErrInfo | EPS-C | SYNTREE |
| `0x203` | SES_Version | EPS-C | SYNTREE |
| `0x204` | RT_DRIVE_CMD | RT | — |
| `0x205` | RT_BRAKE_CMD | RT | — |
| `0x206` | MTR_MOTOR_FBK | MTR | — |
| `0x302` | HOST_LIGHT_CMD | RT (fwd) | — |
| `0x600` | SYS_DIAG_RPT | SYS | — |
| `0x6FA` | SES_Test | EPS-C | SYNTREE |
| `0x6FB` | SEB_Test | SEB | SYNTREE |
| `0x721` | SEB_STATUS | SEB | SYNTREE |
| `0x731` | SEB_ErrInfo | SEB | SYNTREE |
| `0x741` | SEB_Version | SEB | SYNTREE |
| `0x7B9` | VCU_SEB_REQ | RT/SYS (mode-gated) | SYNTREE |
| `0x7FD` | RT_HEARTBEAT | RT | — |
| `0x7FE` | SYS_HEARTBEAT | SYS | — |

---

## 9. Transition Complications & Mitigations

### 9.1 Real Complication: CAN ID Direction Reversal on High Bus

Three CAN IDs change direction during the upgrade:

| ID | Old (Jetson→RT) | New (RT→Jetson) |
|:---|:---|:---|
| `0x300` | HOST_DRIVE_CMD | STEER_FBK |
| `0x301` | HOST_BRAKE_REQ | BRAKE_FBK |
| `0x302` | HOST_LIGHT_CMD | THROTTLE_FBK |

Both Jetson and RT transmit on the same high bus. If RT enables feedback TX before Jetson stops command TX, both nodes transmit the same CAN ID simultaneously → CAN bit error → error frames → bus degradation.

**Mitigation:** RT feedback TX on `0x300`/`0x301`/`0x302` is gated behind a compile-time flag disabled in Phase 3, enabled in Phase 6 only after Jetson switch is confirmed. Phase 3 implements the translation functions but does NOT call `can_tx_high()` for these IDs. Phase 6 un-gates them.

**Safe deployment sequence:**
1. Phase 3: RT firmware with dual-protocol RX, feedback TX disabled. Jetson unchanged. Vehicle drives normally on old protocol.
2. Phase 5: Jetson vehicle_interface node built and tested (not yet activated).
3. Phase 6: Jetson switched to new protocol. RT feedback TX un-gated. Old RT RX removed.
4. At no point do both nodes transmit `0x300`/`0x301`/`0x302` simultaneously.

### 9.2 Non-Issues (Verified Against Two-Bus Topology)

These were raised as concerns during audit but are not real problems. RT has two physically separate CAN controllers (TWAI for low bus, MCP2515/SPI for high bus). Same CAN ID on different buses is not a collision.

| Claim | Why Not a Problem |
|:---|:---|
| **Cross-cycle frame mixing (no sequence numbers)** | Jetson sends `0x200`/`0x201`/`0x202` back-to-back from one ROS callback. Entire burst <1ms at 500kbit/s. RT's 100Hz control loop (10ms period) sees all three together. Even pathological Linux jitter produces at most 10ms staleness — negligible for vehicle dynamics. Sequence numbers are unnecessary overhead. |
| **Vehicle dead during Phase 3** | Old dispatch path (`0x300`→cmd_queue) stays intact throughout Phase 3. Jetson unchanged. New RX handlers for `0x200`/`0x201`/`0x202` listen for IDs not yet on the bus. Vehicle drives normally. |
| **Ratio encoding obscures candump** | Deliberate Layer 1 abstraction, not a bug. `0x320` carries physical speed. Entire low bus carries physical units — tap there for debugging. Formulas documented in §3.1–§3.2. |
| **`0x302` dual-role conflict** | Forward (Jetson→RT→SYS) uses TWAI on low bus. Feedback (RT→Jetson) uses MCP2515 on high bus. Different controllers, different wires. MCP2515 doesn't echo own TX into RX buffer. No self-receive. |
| **`0x204` on both buses** | High bus: TURN_CMD (Jetson→RT, MCP2515). Low bus: RT_DRIVE_CMD (RT→SYS, TWAI). Separate controllers, separate wires. |
| **`0x201`/`0x202`/`0x203` collide with SYNTREE** | High bus (Jetson↔RT) vs low bus (actuators). Separate physical CAN networks. SYNTREE IDs are low-bus only and never forwarded (Category 3). |
| **CAN bus load doubling** | Old: ~5%. New: ~10%. Both well under 50% safe limit at 500kbit/s. Transitional dual-protocol peak: ~11%. |
| **Work plan conflicts** | The work plan (`work-plan.md`) was written for the pre-upgrade protocol. It must be revised to incorporate the new CAN IDs. This is a project management sequencing concern, not a technical complication. |

### 9.3 Optional Enhancement: Sequence Number (Future)

If Jetson Linux jitter is found to cause frame interleaving beyond 10ms in practice (e.g., under heavy GPU load from perception), add a 1-byte sequence counter to `0x200`/`0x201`/`0x202`. All three frames from the same planning cycle share the same counter. RT control loop rejects frames with mismatched counters. Cost: +1 byte DLC per frame. Not required for 0.0.4 — monitor in Phase 7 soak test first.

---

*Version: 0.0.4-alpha. Single source of truth — supersedes all previous 0.0.4 documents.*

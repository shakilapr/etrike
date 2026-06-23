# Autoware.Auto Communication Architecture — Upgrade Plan (v0.0.4-alpha)

**Document under review:** [`docs/autoware-auto-communication-architecture.md`](autoware-auto-communication-architecture.md)
**Previous version (0.0.3-alpha):** Fixed drive-by-wire codes (CSV ↔ architecture alignment, CAN ID corrections, SYNTREE protocol signals)
**This version (0.0.4-alpha):** Adapt our system to match the Autoware.Auto-compatible `vehicle_interface` spec — closing the gap between what the document defines and what we currently implement

---

## 1. Correct Framing: The Document IS the Spec

The `autoware-auto-communication-architecture.md` describes an **Autoware.Auto-compatible `vehicle_interface` node** that bridges ROS 2 planning messages to vehicle CAN commands. This is the target. Our job is to adapt OUR system (Jetson ROS 2 node, RT CAN gateway, CAN IDs, encoding) to match this spec.

**How the document maps to our hardware:**

| Document Term | Our Hardware | Notes |
|:---|:---|:---|
| Tier 2 Node (`vehicle_interface`) | Jetson Orin running ROS 2 | The `vehicle_interface` lifecycle node |
| Tier 3 Actuation ECUs | RT ESP32-S3 (CAN gateway) | RT receives high-bus commands, translates to low-bus SYNTREE + custom CAN IDs |
| CAN Bus (§2) | **High-Level CAN bus** only | This is the Jetson ↔ RT interface. Low bus is RT's internal domain. |
| micro-ROS (§4) | N/A — not applicable | We use raw FreeRTOS on ESP32-S3, not micro-ROS |
| HIL Serial (§5) | N/A — simulation only | Separate document |

---

## 2. Gap Analysis: Document Spec vs. Current Implementation

### 2A. ROS 2 Topic Interface (§1) — What We Need to Implement

The document defines the Autoware.Auto-compatible ROS 2 topic interface. We currently have NO such node — Jetson sends raw CAN frames directly. This is the primary gap.

| Document Spec | Our Current State | Gap |
|:---|:---|:---|
| Subscribe to `autoware_control_msgs/msg/Control` | Jetson publishes raw CAN `0x300` from `/cmd_vel` | **Need to build the `vehicle_interface` node** |
| Subscribe to `autoware_vehicle_msgs/msg/GearCommand` | Gear derived by RT from speed direction | **Need to implement gear override** |
| Subscribe to turn/hazard indicators | Lights via CAN `0x302` | **Implement ROS→CAN mapping** |
| Subscribe to `~/input/emergency_cmd` | ESTOP via CAN `0x001` directly | **Implement ROS→CAN mapping** |
| Subscribe to `~/input/actuation_cmd` (tier4) | N/A | **Remove** — Nav2 anti-pattern |
| Publish `VelocityReport`, `SteeringReport`, `GearReport`, `ControlModeReport` | No ROS publications — CAN frames only | **Need to build CAN→ROS conversion** |
| Publish `ActuationStatusStamped` (tier4) | N/A | **Remove** — replace with individual Autoware.Auto reports |

### 2B. High-Level CAN IDs — Adopt the Document's Namespace

The document uses a clean namespace: **`0x2xx` = commands**, **`0x3xx` = feedback**. Our current high-bus CAN IDs are ad-hoc and don't follow this pattern.

**Commands (Jetson → RT):**

| Document CAN ID | Document Purpose | Our Current ID | Our Current Purpose | Action |
|:---|:---|:---|:---|:---|
| `0x200` | Steering target angle + rate + enable | (none — steering resolved by RT from `0x300` yaw) | — | **Adopt.** Jetson sends tire angle target + rate on `0x200`. RT can use this directly instead of resolving from yaw. |
| `0x201` | Brake target pressure + enable | `0x301` | HOST_BRAKE_REQ (i32 kPa) | **Adopt `0x201`.** Keep kPa encoding (not pedal ratio). |
| `0x202` | Throttle target position + enable | `0x300` (combined with steering) | HOST_DRIVE_CMD (speed+yaw+gear) | **Adopt `0x202`.** Split from combined drive command. |
| `0x203` | Gear selection | `0x300` byte 7 | Gear field | **Adopt `0x203`.** Separate frame for gear. |
| `0x204` | Turn signal command | `0x302` | HOST_LIGHT_CMD (turn+brake+head) | **Adopt `0x204`** for turn signals. Keep `0x302` or new ID for headlight/brake light. |
| `0x206` | System autonomous control | `0x110` (on low bus, SYS→RT) | SYS_MODE_CMD | **Adopt `0x206`** for Jetson→RT autonomy enable/disable. Separate from SYS mode control on low bus. |
| (missing) | ESTOP | `0x001` | SAFETY_ESTOP | **Keep `0x001`** — it's the standard ESTOP ID. Add to document. |
| (missing) | Heartbeat | `0x7FC` | JETSON_HEARTBEAT | **Add to document.** |

**Feedback (RT → Jetson):**

| Document CAN ID | Document Purpose | Our Current ID | Our Current Purpose | Action |
|:---|:---|:---|:---|:---|
| `0x300` | Actual steer angle + rate + fault | (none — RT monitors `0x201` EPS-C internally) | — | **Adopt.** RT translates EPS-C `0x201` → high-bus `0x300`. |
| `0x301` | Actual brake pressure + fault | (none — SYS monitors SEB internally) | — | **Adopt.** RT translates SEB status → high-bus `0x301`. |
| `0x302` | Actual throttle/speed position + fault | `0x120` | SYS_THROTTLE_STS (i16 mm/s) | **Adopt `0x302`** for speed feedback. Replace `0x120` on high bus. |
| `0x303` | Actual gear | `0x210` (combined with mode) | RT_STATE_RPT | **Adopt `0x303`** for gear status. |
| `0x320` | Longitudinal speed + yaw rate | `0x120` (speed) + (none for yaw) | SYS_THROTTLE_STS | **Adopt `0x320`** — cleaner: speed + yaw in one frame. |
| `0x340` | System status flags | `0x011` + `0x210` | SYS_SAFETY_STS + RT_STATE_RPT | **Adopt `0x340`** — consolidate into one status frame. |
| (missing) | Obstacle distance | `0x400` | HOST_OBSTACLE_DIST | **Add to document.** |
| (missing) | Diagnostics | `0x600` | SYS_DIAG_RPT | **Add to document.** |
| (missing) | Heartbeat | `0x7FD` | RT_HEARTBEAT | **Add to document.** |

### 2C. Encoding Formulas — Document's Standard Is the Autoware.Auto Interface

The document's encoding formulas define the **Autoware.Auto-compatible high-bus standard**. We keep them. The vehicle_interface ROS 2 node on Jetson encodes ROS messages into these standard formats. RT ESP32-S3 translates between the standard high-bus encoding and our trike-specific low-bus encoding. This preserves Autoware.Auto compatibility at the high-bus interface while allowing RT to handle all hardware-specific conversion.

**Principle:** High bus = Autoware.Auto standard. Low bus = trike-specific. RT = translation layer.

#### 2C1. Command Encodings (Jetson → RT, standard format kept)

| CAN ID | Standard Encoding (kept) | Physical Meaning | Jetson Conversion (ROS → CAN) | RT Translation (CAN → low bus) |
|:---|:---|:---|:---|:---|
| `0x200` Steering angle | `raw = (δ_rad + π) / 0.001` → int16 | Tire angle in radians, offset by +π to keep raw positive. δ=0 (straight) → raw≈3142. δ=+0.698rad (40° right) → raw≈3840. δ=-0.698rad (40° left) → raw≈2444. | `raw = (control.lateral.steering_tire_angle + M_PI) / 0.001` | Extract `δ_rad = raw × 0.001 - π` → convert to EPS-C decideg: `eps_raw = δ_deg × 10` (plus verify offset=-3000 per CSV). Apply dynamic clamp + LBS SM. |
| `0x200` Steering rate | `raw = (δdot_rad_s + 10.0) / 0.01` → int16 | Slew rate in rad/s, +10 offset. EPS-C range 2.18–9.16 rad/s → raw 1218–1916. | `raw = (control.lateral.steering_tire_rotation_rate + 10.0) / 0.01` | Extract `δdot_rad_s = raw × 0.01 - 10.0` → convert to °/s for EPS-C `VCU_SES_Tgt_StrAngleSpd`. Clamp to 125–525 °/s. |
| `0x201` Brake | `raw = brake_ratio × 10000` → uint16 | Normalized brake: 0.0 = no brake, 1.0 = max brake (5 MPa). | `brake_ratio = abs(min(0, control.longitudinal.acceleration)) / max_decel` → `raw = brake_ratio × 10000` | Extract `ratio = raw / 10000` → `kPa = ratio × 5000` → `seb_raw = kPa × 0.02` → send via `0x205`/`0x7B9`. Max-select with obstacle emergency. |
| `0x202` Throttle | `raw = throttle_ratio × 10000` → uint16 | Normalized throttle: 0.0 = stop, 1.0 = max forward speed (3.0 m/s). | `throttle_ratio = control.longitudinal.velocity / 3.0` (clamp 0.0–1.0) → `raw = ratio × 10000` | Extract `ratio = raw / 10000` → `speed_mmps = ratio × 3000` → send via `0x204`. Reverse handled by gear. |
| `0x203` Gear | `raw = gear_constant` → uint8 | Autoware.Auto gear constants: NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, PARK=22. | `raw = gear_cmd.command` (already Autoware constant) | Map Autoware constants → trike enum: DRIVE(2)→D(1), REVERSE(20)→R(3), PARK(22)/NEUTRAL(1)→N(0). Override auto-derived gear in `0x204`. |

**Note on gear encoding:** The original document used `10=REVERSE, 20=PARK`. This was non-standard. Corrected to Autoware.Auto constants (`REVERSE=20, PARK=22, etc.`) per `autoware_vehicle_msgs/GearCommand.msg` / `GearReport.msg`. The trike's internal gear enum (`N=0,D=1,S=2,R=3`) is used only on the low bus. RT maps between the two.

**Note on reverse:** The throttle ratio (`0x202`) controls speed magnitude only (0 to max forward). Reverse is engaged via gear (`0x203` value REVERSE=20). RT applies the negative sign to speed_mmps when gear=REVERSE. This matches the Autoware.Auto convention where `longitudinal.velocity` is always positive and direction is determined by gear.

#### 2C2. Feedback Encodings (RT → Jetson, standard format kept)

| CAN ID | Standard Encoding (kept) | Physical Meaning | RT Conversion (low bus → CAN) | Jetson Conversion (CAN → ROS) |
|:---|:---|:---|:---|:---|
| `0x300` Steer angle | `θ_rad = raw × 0.001 - π` → int16 | Inverse of command encoding. δ=0 → raw≈3142. | Extract EPS-C angle (decideg) → `δ_rad = angle_deg × π/180` → `raw = (δ_rad + π) / 0.001` | `steering_tire_angle = raw × 0.001 - π` → publish SteeringReport |
| `0x300` Steer rate | `θdot_rad_s = raw × 0.01 - 10.0` → int16 | Inverse of command encoding. | Extract EPS-C rate (°/s) → `δdot_rad_s = rate_deg_s × π/180` → `raw = (δdot_rad_s + 10.0) / 0.01` | Published in SteeringReport |
| `0x301` Brake pressure | `ratio = raw / 10000` → uint16 | Inverse of command encoding. | Extract SEB actual pressure (kPa) → `ratio = kPa / 5000` → `raw = ratio × 10000` | Published as diagnostic |
| `0x302` Throttle/speed | `ratio = raw / 10000` → uint16 | Inverse of command encoding. | Extract MTR actual speed (mm/s) → `ratio = speed_mmps / 3000` → `raw = ratio × 10000` | Published as diagnostic |
| `0x320` Speed | `v = raw × 0.01` [m/s] → int16 | Speed in m/s, 0.01 resolution. Range ±327.67 m/s. | Extract MTR speed (mm/s) → `raw = speed_mmps / 10` (since 10 mm/s = 0.01 m/s) | `longitudinal_velocity = raw × 0.01` → publish VelocityReport |
| `0x320` Yaw rate | `ω = raw × 0.001` [rad/s] → int16 | Yaw rate in rad/s, 0.001 resolution. | Extract yaw (mrad/s) → `raw = yaw_mrad_s` (since 1 mrad/s = 0.001 rad/s). Yaw source TBD (IMU or inverse kinematics). | `heading_rate = raw × 0.001` → publish VelocityReport |
| `0x303` Gear | `raw = gear_constant` → uint8 | Autoware.Auto gear constants (NONE=0, NEUTRAL=1, DRIVE=2, REVERSE=20, etc.) | Map trike enum → Autoware constants: N(0)→NONE(0), D(1)→DRIVE(2), S(2)→LOW(23), R(3)→REVERSE(20) | `report = raw` → publish GearReport |
| `0x340` System status | `flags` bitfield | bit0=auto_enabled, bit1=override, bit2=fault, bit3=estop. Fault code in byte 1. | Pack from mode state + EPS/SEB fault flags + heartbeat status | Publish ControlModeReport (mapped: Manual→MANUAL(4), Auto→AUTONOMOUS(1), Estop→DISENGAGED(5)) |

#### 2C3. Encoding Not Changed from Original Document

The document's ratio-based encoding approach IS kept for the high bus. This is the Autoware.Auto-compatible abstraction layer:
- The vehicle_interface on Jetson converts physical Autoware.Auto ROS messages (radians, m/s, m/s²) to normalized ratios for CAN transmission
- RT converts normalized ratios to trike-specific physical units (degrees, mm/s, kPa) for low-bus actuator commands
- This separation means the high-bus interface remains standard and portable — any Autoware.Auto-compatible vehicle_interface could work with our RT gateway

**Only encoding fixes made to the document:**
1. Gear values corrected from `10=REVERSE, 20=PARK` to match actual Autoware.Auto constants
2. Feedback encodings made symmetric with command encodings (inverse formulas)
3. Added missing feedback CAN IDs (`0x320` speed+yaw, `0x340` system status, `0x400` obstacle)

### 2D. What Stays Exactly As-Is

| Element | Why |
|:---|:---|
| `autoware_control_msgs/msg/Control` as primary subscription | Correct Autoware.Auto message |
| `autoware_vehicle_msgs/msg/GearCommand`, `TurnIndicatorsCommand`, `HazardLightsCommand` | Correct Autoware.Auto message family |
| `autoware_vehicle_msgs/msg/VelocityReport`, `SteeringReport`, `GearReport`, `ControlModeReport` | Correct Autoware.Auto reporting messages |
| `autoware_vehicle_msgs/msg/TurnIndicatorsReport`, `HazardLightsReport` | Correct reporting messages |
| CAN ID scheme: `0x2xx` = commands, `0x3xx` = feedback | Clean namespace convention |
| YAML parameter structure in §3 | Correct Autoware pattern |
| Safety limits concept in §6 | Correct approach |
| `can_msgs::msg::Frame` for raw CAN handling | Standard ROS 2 CAN type |

### 2E. What Gets Removed

| Element | Reason |
|:---|:---|
| `~/input/actuation_cmd` (tier4_vehicle_msgs/ActuationCommandStamped) | Nav2 `raw_vehicle_cmd_converter` anti-pattern. Control→CAN directly. |
| `~/input/emergency_cmd` using `tier4_vehicle_msgs/VehicleEmergencyStamped` | Use `autoware_vehicle_msgs` equivalent |
| `~/output/actuation_status` (tier4_vehicle_msgs/ActuationStatusStamped) | Replace with individual Autoware.Auto reports |
| `~/input/from_can_bus` and `~/output/to_can_bus` raw CAN pass-through | Security: raw CAN proxy through ROS 2 bypasses all validation |
| §4 micro-ROS events (entire section) | Not applicable — ESP32-S3 runs raw FreeRTOS, no micro-ROS |
| §5 HIL serial protocol (entire section) | Move to `docs/hil-simulation-protocol.md` |
| `use_actuation_cmd` parameter | Removed with ActuationCommandStamped |
| `convert_steer_cmd` / VGR parameters | No variable gear ratio on trike |
| `wheel_radius` parameter | Tricycle kinematics don't use wheel radius |

### 2F. What Gets Added

| New Element | Location | Purpose |
|:---|:---|:---|
| `0x001` SAFETY_ESTOP (DLC=0) | §2A Commands | Emergency stop — highest priority CAN frame |
| `0x7FC` JETSON_HEARTBEAT (DLC=1, u8 alive_ctr, 2 Hz) | §2 Liveness | Jetson→RT liveness. Loss at 1500ms → RT controlled stop. |
| `0x7FD` RT_HEARTBEAT (DLC=1, u8 alive_ctr, 2 Hz) | §2 Liveness | RT→Jetson liveness. Loss at 1500ms → stop publishing. |
| `0x400` HOST_OBSTACLE_DIST (DLC=4, u32 mm) | §2 Feedback | Jetson perception → RT distance. RT applies speed limit. |
| `0x600` SYS_DIAG_RPT (DLC=8) | §2 Feedback | System diagnostics forwarded from SYS via RT |
| RT gateway translation table | §2 (new subsection) | Maps document's high-bus CAN to low-bus CAN |
| Tricycle kinematics note | §3 Parameters | `wheel_base: 1.5`, `is_tricycle: true` |
| EPS-C/SEB protocol reference | §2 (new subsection) | Cross-reference to `can-dictionary.md` for low-bus details |
| ESTOP CAN ID (`0x001`) | §2A, §6 | Currently missing from CAN tables |

---

## 3. CAN ID Changeability (Updated)

| Tier | CAN IDs | Bus | Changeable? |
|:---|:---|:---|:---|
| **Document's IDs** | `0x200`–`0x206`, `0x300`–`0x340` | High | ✅ **Yes** — all under our control. Can reorganize if needed. |
| **Our high-bus IDs** | `0x001`, `0x7FC`, `0x7FD`, `0x400`, `0x600`, `0x210`, `0x220` | High | ✅ **Yes** — all ours. |
| **SYNTREE IDs** | `0x169`, `0x201`, `0x202`, `0x203`, `0x6FA`, `0x7B9`, `0x721`, `0x731`, `0x741`, `0x6FB` | Low | ❌ **No** — factory preprogrammed. |
| **Our low-bus IDs** | `0x011`, `0x012`, `0x110`, `0x120`, `0x204`, `0x205`, `0x206`, `0x302`, `0x600`, `0x7FD`, `0x7FE`, `0x001` | Low | ⚠️ **Constrained** — ours but must not collide with SYNTREE. |

**Key point:** High bus and low bus are physically separate. RT has two different CAN controllers. Same CAN ID on both buses is NOT a collision — RT distinguishes by which controller (`twai` vs `mcp2515`) the frame arrived on.

- Document's `0x201` (brake cmd, high bus) and EPS-C `0x201` (status, low bus) coexist safely — RT receives the command on MCP2515/SPI, and separately receives EPS-C status on TWAI.
- Document's `0x202` (throttle cmd, high bus) and EPS-C `0x202` (error info, low bus) — same separation.
- Document's `0x203` (gear cmd, high bus) and EPS-C `0x203` (version, low bus) — same.

**We do NOT need to change every CAN ID.** The document's `0x2xx`/`0x3xx` scheme is already clean. We only need to:
1. Adopt the document's CAN ID scheme for the high bus (replacing our current ad-hoc high-bus IDs)
2. RT translates between document's high-bus IDs and the low-bus SYNTREE + custom IDs
3. Add missing IDs (ESTOP `0x001`, heartbeats, obstacle, diagnostics)

---

## 4. RT ESP32-S3 — High-to-Low CAN Translation Table

The RT becomes a **protocol translator** between the Autoware.Auto CAN namespace (high bus) and the vehicle-specific CAN namespace (low bus).

| High Bus Input | RT Action | Low Bus Output | Actuator |
|:---|:---|:---|:---|
| `0x200` Steering cmd `{i16 angle_deg, i16 rate_deg_s, u8 enable}` | Convert angle to EPS-C raw format. Apply dynamic clamp. Run LBS state machine. Build SYNTREE frame with rolling counter + checksum. **⚠️ Steering offset TBD:** EPS-C CSV uses offset=-3000 encoding. Must verify against live CAN bus before Phase 5. Also populate `VCU_Veh_Spd_Value` (byte 6) — convert mm/s → km/h via `kmh = mmps × 0.0036`. | `0x169` VCU_SES_REQ @50Hz | SYNTREE EPS-C |
| `0x201` Brake cmd `{u16 pressure_kpa, u8 enable}` | Max-select with obstacle emergency. Convert kPa→SEB raw via `raw=kpa×0.02`. Mode=Pressure if >0, else Stroke. | `0x205` RT_BRAKE_CMD → SYS → `0x7B9` VCU_SEB_REQ @50Hz | SYS ESP32 → SYNTREE SEB |
| `0x202` Throttle cmd `{i16 speed_mmps, u8 enable}` | Clamp to [-500, 3000]. Derive gear from speed direction. | `0x204` RT_DRIVE_CMD @100Hz {i32 speed, u8 gear} | SYS ESP32 → MTR STM32 |
| `0x203` Gear cmd `{u8 gear}` | Override auto-derived gear (allows Sport gear from Jetson) | `0x204` RT_DRIVE_CMD gear field | SYS ESP32 → MTR STM32 |
| `0x204` Turn signal cmd `{u8 signal}` | Forward to low bus | `0x302` HOST_LIGHT_CMD (turn bits) | SYS ESP32 → GPIO |
| `0x206` Autonomy ctrl `{u8 enable, u8 clear}` | Enable/disable autonomy within AUTO mode (sub-mode). When disabled, RT stops sending commands but stays in AUTO. Clear override blockages. **Does NOT send `0x110`** — mode change is SYS physical button only. | — (internal state) | — |
| `0x001` SAFETY_ESTOP | Forward to low bus + `mode_set(Estop)` | `0x001` SAFETY_ESTOP | All low-bus nodes |
| `0x7FC` JETSON_HEARTBEAT | Monitor alive counter. Timeout 1500ms → zero `0x204` + stop `0x169` | — (internal action) | — |

| Low Bus Input | RT Action | High Bus Output |
|:---|:---|:---|
| `0x201` SES_STATUS (EPS-C) | Extract angle, rate, fault. Convert decideg→deg. | `0x300` Steer feedback `{i16 angle_deg, i16 rate_deg_s, u8 fault}` @100Hz |
| `0x721` SEB_STATUS + `0x205` echo | Extract pressure, fault. | `0x301` Brake feedback `{u16 pressure_kpa, u8 fault}` @100Hz |
| `0x120` SYS_THROTTLE_STS | Extract speed. | `0x302` Throttle feedback `{i16 speed_mmps, u8 fault}` + `0x320` Speed+yaw `{i16 speed, i16 yaw}` @100Hz |
| `0x210` (internal) | RT mode + gear state | `0x303` Gear feedback `{u8 gear}` @10Hz |
| `0x011` SYS_SAFETY_STS + internal flags | Pack estop, autonomous, override, fault bits | `0x340` System status `{u8 flags, u8 fault_code}` @5Hz |
| `0x600` SYS_DIAG_RPT | Forward to high bus | `0x600` SYS_DIAG_RPT @1Hz |
| `0x7FE` SYS_HEARTBEAT | Monitor. **10 Hz (100ms period), 200ms timeout** (2 missed frames). Loss → RT takes over `0x7B9` + sends `0x001`. | — (internal action) |

---

## 5. Updated CAN ID Map (High Bus Only)

### Commands (Jetson → RT)

| CAN ID | Name | DLC | Signals | Rate |
|:---|:---|:---|:---|:---|
| `0x001` | SAFETY_ESTOP | 0 | (no payload — event signal) | Event |
| `0x200` | HOST_STEER_CMD | 8 | `{i16 target_angle_deg, i16 target_rate_deg_s, u8 enable, u8[3] reserved}` | 50 Hz |
| `0x201` | HOST_BRAKE_CMD | 5 | `{u16 pressure_kpa, u8 enable, u8[2] reserved}` | 50 Hz |
| `0x202` | HOST_THROTTLE_CMD | 4 | `{i16 speed_mmps, u8 enable, u8 reserved}` | 100 Hz |
| `0x203` | HOST_GEAR_CMD | 1 | `{u8 gear}` — 0=N, 1=D, 2=S, 3=R | On change |
| `0x204` | HOST_TURN_CMD | 1 | `{u8 signal}` — 0=NONE, 1=LEFT, 2=RIGHT, 3=HAZARD | On change |
| `0x205` | HOST_LIGHT_AUX | 1 | `{u8 bitfield}` — bit0=headlight, bit1=brake_light, bits2-7=reserved | On change |
| `0x206` | HOST_AUTONOMY_CTRL | 2 | `{u8 enable, u8 clear_override}` — enable=0x01 (enable autonomy within AUTO), 0x00 (disable autonomy, RT stops sending commands but stays in AUTO mode). clear_override=0x01 (reset override blockages). **Does NOT change vehicle mode** (MANUAL↔AUTO is SYS physical button only). | On change |
| `0x400` | HOST_OBSTACLE_DIST | 4 | `{u32 distance_mm}` — Jetson perception → RT for speed limiting. UINT32_MAX = no reading. | 10 Hz |
| `0x7FC` | JETSON_HEARTBEAT | 1 | `{u8 alive_ctr}` | 2 Hz |

### Feedback (RT → Jetson)

| CAN ID | Name | DLC | Signals | Rate |
|:---|:---|:---|:---|:---|
| `0x300` | STEER_FEEDBACK | 5 | `{i16 actual_angle_deg, i16 actual_rate_deg_s, u8 fault}` | 100 Hz |
| `0x301` | BRAKE_FEEDBACK | 3 | `{u16 actual_pressure_kpa, u8 fault}` | 100 Hz |
| `0x302` | THROTTLE_FEEDBACK | 3 | `{i16 actual_speed_mmps, u8 fault}` | 100 Hz |
| `0x303` | GEAR_FEEDBACK | 1 | `{u8 actual_gear}` | 10 Hz |
| `0x320` | SPEED_YAW_FEEDBACK | 4 | `{i16 speed_mmps, i16 yaw_rate_mrad_s}` — **NEW for Autoware.Auto VelocityReport.** Yaw: TBD (derived from EPS-C angle+speed via inverse kinematics, or IMU when fitted, or 0). | 100 Hz |
| `0x340` | SYSTEM_STATUS | 2 | `{u8 flags, u8 fault_code}` — **Mode→bit mapping:** bit0 `auto_enabled` = (mode==Auto && engaged), bit1 `override` = (manual override active), bit2 `fault` = (EPS L3 fault || SEB L3 fault || following error), bit3 `estop` = (mode==Estop). Trike Mode enum: 0=Manual, 1=Auto, 2=Estop. | 5 Hz |
| `0x600` | SYS_DIAG_RPT | 8 | `{u8 mode, u8 brake, u8 hb, u8 estop, u16 heap, u8 tec, u8 rec}` | 1 Hz |
| `0x7FD` | RT_HEARTBEAT | 1 | `{u8 alive_ctr}` | 2 Hz |

---

## 6. ROS 2 Topic → CAN Mapping (Updated)

### Subscriptions (Inputs to vehicle_interface)

| Topic Name | ROS 2 Message Type | Maps To High CAN | Notes |
|:---|:---|:---|:---|
| `~/input/control_cmd` | `autoware_control_msgs/msg/Control` | `0x200` (steering angle+rate) + `0x202` (speed) + `0x201` (brake pressure from deceleration) | `lateral.steering_tire_angle` → `0x200` angle. `lateral.steering_tire_rotation_rate` → `0x200` rate (skip if `is_defined_steering_tire_rotation_rate==false`). `longitudinal.velocity` → `0x202` speed_mmps. `longitudinal.acceleration` (negative) → `0x201` brake kPa via deceleration model (skip if `is_defined_acceleration==false`). |
| `~/input/gear_cmd` | `autoware_vehicle_msgs/msg/GearCommand` | `0x203` gear | Override auto-derived gear |
| `~/input/turn_indicators_cmd` | `autoware_vehicle_msgs/msg/TurnIndicatorsCommand` | `0x204` turn signal | LEFT/RIGHT/NONE |
| `~/input/hazard_lights_cmd` | `autoware_vehicle_msgs/msg/HazardLightsCommand` | `0x204` turn signal = HAZARD | Both indicators on |
| `~/input/emergency_cmd` | *(no standard Autoware.Auto emergency message exists)* | `0x001` ESTOP (DLC=0) | **NOTE:** `autoware_vehicle_msgs` has no `VehicleEmergencyStamped`. Until Autoware standardizes an emergency message, ESTOP is triggered via CAN `0x001` directly. The Jetson bridge node sends `0x001` on CAN when it detects an internal emergency condition. An `Engage` message with `engage=false` may serve as a soft emergency via the `0x206` autonomy disable path. |

### Publications (Outputs from vehicle_interface)

| Topic Name | ROS 2 Message Type | Source High CAN | Notes |
|:---|:---|:---|:---|
| `~/output/velocity_status` | `autoware_vehicle_msgs/msg/VelocityReport` | `0x320` speed + yaw | `longitudinal_velocity` = speed/1000 m/s. `heading_rate` = yaw/1000 rad/s. `lateral_velocity` = 0 (no sensor yet). |
| `~/output/steering_status` | `autoware_vehicle_msgs/msg/SteeringReport` | `0x300` steer angle | `steering_tire_angle` = angle_deg × π/180 rad |
| `~/output/gear_status` | `autoware_vehicle_msgs/msg/GearReport` | `0x303` gear | **Mapping:** Trike N(0)→Autoware NONE(0), D(1)→DRIVE(2), S(2)→LOW(23), R(3)→REVERSE(20). CAN bus uses trike enum internally; ROS publication maps to Autoware constants. |
| `~/output/control_mode` | `autoware_vehicle_msgs/msg/ControlModeReport` | `0x340` system status + `0x210` RT state | **Mapping:** Trike Manual(0)→Autoware MANUAL(4), Auto(1)+engaged→AUTONOMOUS(1), Estop(2)→DISENGAGED(5). Engagement = AUTO mode + steer aligned + no faults + hb alive. |
| `~/output/turn_indicators_status` | `autoware_vehicle_msgs/msg/TurnIndicatorsReport` | `0x204` echo (open-loop) | Commanded state echoed back. No physical confirmation CAN ID yet. |
| `~/output/hazard_lights_status` | `autoware_vehicle_msgs/msg/HazardLightsReport` | `0x204` echo (open-loop) | Commanded state echoed back. |
| `~/output/diagnostics` | `diagnostic_msgs/msg/DiagnosticArray` | `0x340` fault_code + `0x600` diag | Aggregated from system status + SYS diagnostics |

### REMOVED from original document

| Topic | Reason |
|:---|:---|
| ❌ `~/input/actuation_cmd` (tier4_vehicle_msgs/ActuationCommandStamped) | Nav2 anti-pattern. Control converts directly to CAN. |
| ❌ `~/output/actuation_status` (tier4_vehicle_msgs/ActuationStatusStamped) | Replaced with individual Autoware.Auto reports from real CAN sources. |
| ❌ `~/input/from_can_bus` (can_msgs::msg::Frame) | Raw CAN pass-through is a security hole. |
| ❌ `~/output/to_can_bus` (can_msgs::msg::Frame) | Raw CAN pass-through. |

---

## 7. Parameters — Adapted for E-Trike

| Parameter | Document Value | Corrected Value | Notes |
|:---|:---|:---|:---|
| `loop_rate` | 30.0 Hz | **100.0 Hz** | Match RT control loop |
| `command_timeout_ms` | 1000 ms | **500 ms** | Architecture §7.6 |
| `max_steering_angle` | 1.0 rad | **0.698 rad (40°)** | EPS-C software hard-stop |
| `max_steering_rate` | 5.0 rad/s | **9.16 rad/s (525°/s)** | EPS-C max slew rate |
| `min_steering_rate` | *(not in original)* | **2.18 rad/s (125°/s)** | EPS-C minimum — commands below this may be rejected |
| `wheel_base` | 2.79 m | **1.5 m** | Tricycle |
| `wheel_radius` | 0.383 m | *(removed)* | Not used in tricycle kinematics |
| `vgr_coef_a/b/c` | 15.713/0.053/0.042 | *(removed)* | No VGR on trike |
| `max_throttle` | 0.4 (ratio) | **3000 (mm/s)** | Speed-based, not ratio |
| `max_brake` | 0.8 (ratio) | **5000 (kPa)** | Pressure-based, not ratio |
| `emergency_brake` | 0.7 (ratio) | **27.0 (mm stroke)** | ESTOP full brake stroke |
| `margin_time_for_gear_change` | 2.0 s | *(removed)* | Relay gear — no chattering |
| `base_frame_id` | `"base_link"` | `"base_link"` | ✓ Keep |
| `can_interface` | `"can0"` | `"can0"` | ✓ Keep — high bus only |
| `use_actuation_cmd` | true | *(removed)* | Removed with ActuationCommandStamped |
| `convert_steer_cmd` | true | *(removed)* | No VGR |

---

## 8. Sections to Remove / Replace

### §4 — micro-ROS Events → REPLACE

**Delete entirely.** The E-Trike does not use micro-ROS. The ESP32-S3 runs raw FreeRTOS with direct CAN.

**Replace with** a reference section:

> ## 4. RT ESP32-S3 Internal Architecture (Reference)
> 
> The RT ESP32-S3 translates between this document's high-level CAN protocol (§2) and the vehicle-specific low-level CAN bus (SYNTREE EPS-C, SEB, MTR STM32, SYS ESP32-S3). It runs 8 FreeRTOS tasks with NO ROS 2 middleware. For the complete RTOS task layout, steering Listen-Before-Speaking state machine, tricycle kinematics algorithm, and CAN gateway forwarding rules, see [`architecture.md`](../architecture.md) §7.

### §5 — HIL Serial Protocol → EXTRACT

**Move to** `docs/hil-simulation-protocol.md`. Add a brief reference:

> ## 5. Simulation Interface
> 
> For SIL/HIL testing with CARLA, see [`docs/hil-simulation-protocol.md`](hil-simulation-protocol.md). Not used in production.

### §6 — Safety Limits → ADAPT

Keep the safety limits table structure. Update "Source Node / Task" references from non-existent `vehicle_interface` C++ / `microAutoware` STM32 to the real architecture:

| Original Reference | Replace With |
|:---|:---|
| `vehicle_interface` (C++) → Throttle/Brake/Steering clamping | `vehicle_interface` ROS 2 node on Jetson — clamps before CAN TX |
| `vehicle_interface` (C++) → Command timeout | `vehicle_interface` ROS 2 node — 500ms staleness |
| `vehicle_interface` (C++) → Manual override | `0x340` bit1 + RT internal `override_active` flag |
| `vehicle_interface` (C++) → System fault | `0x340` bit2 — RT escalates low-bus faults |
| `microAutoware` (STM32) → HIL timeouts | *(removed — HIL moved to separate doc)* |
| *(missing)* → Steering following error | RT ESP32-S3: abs(cmd−actual)>5° for 300ms → ESTOP. See architecture.md §7.6. |
| *(missing)* → Heartbeat loss | Jetson: `0x7FD` timeout 1500ms → stop publishing. RT: `0x7FC` timeout 1500ms → controlled stop. SYS: `0x7FE` timeout 200ms → ESTOP. |
| *(missing)* → External watchdog | TPS3850 on each ESP32. 100ms window. See architecture.md §8.9. |
| *(missing)* → SYNTREE protocol safety | Rolling counter + checksum on `0x169` and `0x7B9`. Both enable bits must be 1. |

---

## 9. Issues Found in Current Document

### Critical (blocks implementation)

| # | Issue | Location | Fix |
|:---|:---|:---|:---|
| C1 | **`tier4_vehicle_msgs` mixed with `autoware_*_msgs`** — `ActuationCommandStamped`, `VehicleEmergencyStamped`, `ActuationStatusStamped` are tier4-specific, not Autoware.Auto | §1A lines 15, 19; §1B line 34 | Remove all tier4 dependencies. Use `autoware_vehicle_msgs` exclusively. |
| C2 | **`ActuationCommandStamped` subscription** — Nav2 two-step conversion (Control→pedal ratios→CAN) loses precision | §1A line 15 | Remove. vehicle_interface converts Control directly to CAN. |
| C3 | **Encoding formulas use pedal ratios** — `ratio × 10000` for brake/throttle doesn't match our pressure/speed-based system | §2A lines 49, 52 | Use kPa for brake, mm/s for throttle (see §2C above). |
| C4 | **Steering encoding adds π offset** — `(δ + π) / 0.001` is unusual and overcomplicated | §2A line 45 | Use simple `δ_deg × 10` or `δ_rad × 1000` (see §2C). |
| C5 | **Gear encoding uses non-standard values** — `10=REVERSE, 20=PARK` | §2A line 55 | Use `0=N, 1=D, 2=S, 3=R`. |
| C6 | **`~/input/from_can_bus` raw CAN subscription** — allows any ROS node to inject arbitrary CAN frames, bypassing safety | §1A line 20 | Remove. No raw CAN pass-through. |
| C7 | **`~/output/to_can_bus` raw CAN publication** — same security concern | §1B line 27 | Remove. |
| C8 | **Micro-ROS on non-existent hardware** — STM32 Nucleo-H753ZI doesn't exist in our vehicle | §4 | Delete entire section. Replace with RT architecture reference. |
| C9 | **Source location references point to files that don't exist** — `vehicle_interface/include/...`, `microAutoware/src/...`, etc. | Header comments §§1-6 | Replace with actual file paths or architecture.md references. |

### High (incorrect values / missing elements)

| # | Issue | Location | Fix |
|:---|:---|:---|:---|
| H1 | **`control_mode` values wrong** — `1=Autonomous, 4=Manual` doesn't match trike `0=Manual, 1=Auto, 2=ESTOP` | §1B line 31 | Use trike Mode enum. |
| H2 | **`wheel_base: 2.79` is passenger car** — should be `1.5m` for tricycle | §3 line 108 | Fix to 1.5m. |
| H3 | **VGR coefficients irrelevant** — trike has no variable gear ratio steering | §3 lines 105-107 | Remove `vgr_coef_a/b/c`. |
| H4 | **Pedal-ratio parameters** — `max_throttle: 0.4`, `max_brake: 0.8`, `emergency_brake: 0.7` are ratios, not physical units | §3 lines 99-101 | Use mm/s, kPa, mm stroke. |
| H5 | **`loop_rate: 30 Hz` too slow** — SYNTREE requires 50 Hz continuous; RT runs 100 Hz | §3 line 97 | Change to 100 Hz. |
| H6 | **`command_timeout_ms: 1000` too long** — architecture uses 500ms staleness | §3 line 98 | Change to 500ms. |
| H7 | **No ESTOP CAN ID** — `0x001` SAFETY_ESTOP is the highest-priority frame in the system but not defined in CAN tables | §2 | Add `0x001` to command table. |
| H8 | **No heartbeat CAN IDs** — `0x7FC` (Jetson), `0x7FD` (RT) are essential for liveness monitoring | §2 | Add heartbeat IDs. |
| H9 | **`margin_time_for_gear_change: 2.0s` unnecessary** — relay-driven gear has no mechanical chattering | §3 line 110 | Remove or set to 0. |
| H10 | **HIL serial protocol in production document** — belongs in simulation docs | §5 | Extract to `docs/hil-simulation-protocol.md`. |
| H11 | **No `0x400` obstacle distance** — Jetson perception feedback to RT for speed limiting | §2 | Add to feedback table. |
| H12 | **No `0x600` diagnostics** — SYS diagnostic report forwarded to Jetson | §2 | Add to feedback table. |

### Medium/Low (documentation quality)

| # | Issue | Fix |
|:---|:---|:---|
| M1 | `~/output/turn_indicators_status` marked "Stubbed" — no confirmation path | Note as open-loop (commanded state echoed). Physical confirmation TBD. |
| M2 | `is_engaged_` used but never defined | Define: AUTO mode + steer aligned + no faults + heartbeats alive. |
| M3 | `VelocityReport` includes `lateral_velocity` — no sensor for this | Set to 0.0 until IMU/wheel encoders fitted. |
| M4 | `STEERING_RPT 0x213` referenced nowhere but implied | No `0x213` exists. Steering feedback is on `0x300`. |
| M5 | `wheel_radius: 0.383` unused | Remove — tricycle kinematics don't use wheel radius. |

---

## 10. What Happens to Our Current High-Bus CAN IDs

When we adopt the document's CAN ID scheme, these current IDs change:

| Current ID | Current Name | Becomes | Notes |
|:---|:---|:---|:---|
| `0x300` | HOST_DRIVE_CMD (speed+yaw+gear) | Split into `0x200` (steering) + `0x202` (throttle) + `0x203` (gear) | Combined drive command disaggregated |
| `0x301` | HOST_BRAKE_REQ | `0x201` | Same purpose, new ID |
| `0x302` | HOST_LIGHT_CMD (combined lights) | `0x204` (turn) + (headlight/brake light — TBD) | Split turn signals from other lights |
| `0x120` (high) | SYS_THROTTLE_STS (forwarded) | `0x302` (throttle feedback) + `0x320` (speed+yaw) | Speed+throttle consolidated into document's feedback IDs |
| `0x011` (high) | SYS_SAFETY_STS (forwarded) | `0x340` (system status) | Safety status merged into system status frame |
| `0x210` | RT_STATE_RPT (mode+steer+reversing) | `0x340` bits + `0x303` gear | Mode+state in system status; gear in separate frame |
| `0x220` | RT_PID_RPT | Keep or merge into `0x600` | PID telemetry is reserved/inactive |
| `0x400` | HOST_OBSTACLE_DIST | `0x400` | **Keep same ID** |
| `0x600` | SYS_DIAG_RPT (forwarded) | `0x600` | **Keep same ID** |
| `0x7FC` | JETSON_HEARTBEAT | `0x7FC` | **Keep same ID** |
| `0x7FD` (high) | RT_HEARTBEAT | `0x7FD` | **Keep same ID** |
| `0x001` (high) | SAFETY_ESTOP | `0x001` | **Keep same ID** |

**Low-bus IDs do NOT change.** The document's CAN IDs are the high-bus interface only. RT's translation layer handles the high→low mapping.

---

## 11. Implementation Phases (0.0.4-alpha)

### Phase 0: Document Cleanup (this phase)
- [ ] Remove `tier4_vehicle_msgs` references → use `autoware_vehicle_msgs`
- [ ] Remove `~/input/actuation_cmd` and `~/output/actuation_status`
- [ ] Remove `~/input/from_can_bus` and `~/output/to_can_bus`
- [ ] Fix gear encoding values (`10=REVERSE` → `3=R`)
- [ ] Fix `control_mode` values (`4=Manual` → `0=Manual`)
- [ ] Delete §4 (micro-ROS) — replace with RT architecture reference
- [ ] Extract §5 (HIL) to separate document
- [ ] Fix source location references — remove non-existent file paths

### Phase 1: Fix Encoding Formulas & RT Translation Spec
- [ ] **Keep document's standard ratio-based encodings** for high bus (§2C) — this is the Autoware.Auto-compatible interface
- [ ] Fix gear encoding values: `10=REVERSE, 20=PARK` → Autoware.Auto constants (`REVERSE=20, PARK=22`, etc.)
- [ ] Make feedback encodings symmetric with command encodings (inverse formulas)
- [ ] Define RT translation layer: standard ratio → trike physical unit → low-bus CAN
- [ ] Fix `longitudinal.speed` → `longitudinal.velocity` throughout
- [ ] Fix `control_mode` values to match Autoware constants (Manual→4, Auto→1, Estop→5)
- [ ] Fix all parameter values for trike (§7)
- [ ] Add `min_steering_rate` parameter (125°/s = 2.18 rad/s)
- [ ] Remove VGR, wheel_radius, margin_time_for_gear_change
- [ ] Add missing parameters (tricycle wheelbase, SYNTREE IDs, `is_defined_*` flag checks)
- [ ] Add ROS↔CAN gear constant mapping table

### Phase 2: Add Missing CAN IDs & Fix Reference Docs
- [ ] Add `0x001` SAFETY_ESTOP
- [ ] Add `0x7FC` JETSON_HEARTBEAT, `0x7FD` RT_HEARTBEAT
- [ ] Add `0x205` HOST_LIGHT_AUX (headlight + brake light command)
- [ ] Add `0x600` SYS_DIAG_RPT
- [ ] Add RT gateway translation table (§2 new subsection)
- [ ] Add CAN ID changeability note (high bus = free, low bus = constrained)
- [ ] **Fix `0x7FE` SYS heartbeat:** set to 10 Hz (100ms period), 200ms timeout in can-dictionary.md
- [ ] **Fix `0x400` direction:** consistently Jetson→RT in architecture.md §7.4
- [ ] **Remove** `0x206` "Forward to high" comment from architecture.md §7.3 line 367
- [ ] **Add `0x721`** to architecture.md §7.3 RT low-bus RX list

### Phase 3: Adopt Document's CAN ID Scheme on High Bus
- [ ] Update Jetson CAN TX: use `0x200`/`0x201`/`0x202`/`0x203`/`0x204`/`0x205`/`0x206`/`0x400` instead of `0x300`/`0x301`/`0x302`
- [ ] Update RT CAN RX dispatch: parse document's CAN IDs. **Redesign dispatch for split frames:** each CAN ID updates a field in a shared `PendingSetpoint` struct. Control task assembles complete setpoint at 100 Hz. 500ms timeout on incomplete setpoints.
- [ ] **Remove old Category 1 forwarding rules:** `0x011` and `0x120` must NOT be transparently forwarded to high bus (replaced by Category 2 translations to `0x340`/`0x302`/`0x320`). `0x302` must NOT be forwarded high→low (replaced by `0x204`→`0x302` translation).
- [ ] Update RT CAN TX: send feedback on `0x300`/`0x301`/`0x302`/`0x303`/`0x320`/`0x340`
- [ ] Update Jetson CAN RX: parse document's feedback CAN IDs → ROS publications
- [ ] Update `can-dictionary.md` high-bus section

### Phase 4: Build vehicle_interface ROS 2 Node
- [ ] Create `jetson/src/autoware_vehicle_bridge/` package
- [ ] Implement Control → CAN conversion (§6 mapping)
- [ ] Implement CAN → VehicleReport conversion
- [ ] Implement heartbeat (send `0x7FC`, monitor `0x7FD`)
- [ ] Implement ESTOP transmission (`0x001`)
- [ ] Implement command timeout watchdog (500ms)

### Phase 5: RT Gateway Translation Layer
- [ ] **BLOCKER: Verify steering angle encoding** — capture live CAN bus traffic from EPS-C to confirm offset encoding (CSV offset=-3000 vs architecture offset=0). Phase 5 cannot proceed until resolved.
- [ ] Implement `0x200` → `0x169` steering translation (angle conversion + LBS SM + `VCU_Veh_Spd_Value` in km/h)
- [ ] Implement `0x201` → `0x205`/`0x7B9` brake translation (kPa→SEB raw, `raw=kPa×0.02`)
- [ ] Implement `0x202` → `0x204` throttle translation (speed+gear derivation)
- [ ] Implement `0x205` (high) → light relays translation (headlight+brake light bits)
- [ ] Implement low-bus status → high-bus feedback translation
- [ ] Unit tests for each translation

---

## 12. Risks and Open Questions

| Risk | Severity | Mitigation |
|:---|:---|:---|
| **Steering angle offset ambiguity** — EPS-C CSV uses offset=-3000 encoding; architecture.md uses offset=0. If wrong encoding is used, steering commands are catastrophically wrong. | **BLOCKER** | Must capture live CAN bus traffic from physical EPS-C unit and verify encoding before Phase 5. CSV (manufacturer DBC export) is the authoritative source — likely offset=-3000 is correct. |
| **`VehicleEmergencyStamped` doesn't exist** — Autoware has no standard emergency ROS message. | **HIGH** | Use CAN `0x001` ESTOP for hard emergency. Use `Engage` message (`engage=false`) for soft disengagement via `0x206`. Monitor Autoware releases for emergency message standardization. |
| **Gear constants incompatible** — trike uses `N=0,D=1,S=2,R=3`. Autoware uses `NONE=0,NEUTRAL=1,DRIVE=2,REVERSE=20,LOW=23`. | **HIGH** | CAN bus keeps trike enum. ROS↔CAN translation layer maps between the two. `GearReport.report` and `GearCommand.command` always use Autoware constants. |
| **ControlModeReport regression** — original doc had correct Autoware values `1=Autonomous,4=Manual` but plan originally changed them. | **HIGH** (fixed) | ✅ Fixed. Trike→Autoware mapping: Manual→4, Auto→1, Estop→5. |
| **Dispatch task redesign** — splitting `0x300` into 3 frames requires new RT dispatch logic. | **HIGH** | Documented in §2B and Phase 3. Shared `PendingSetpoint` struct, each CAN ID updates one field. Control task assembles at 100 Hz. 500ms timeout on incomplete setpoints. |
| **SYS heartbeat rate conflict** — architecture.md says 10 Hz in §8.6 but 2 Hz in §8.4/§8.7. can-dictionary.md says 2 Hz. | **HIGH** | Resolved: SYS heartbeat is **10 Hz, 200ms timeout** per architecture.md §8.6 design. can-dictionary.md `0x7FE` entry updated in Phase 2. |
| **`0x400` direction** — contradictory across architecture.md (§2.2 vs §7.4) and fixe.md D2. | **HIGH** (resolved) | ✅ `0x400` is Jetson→RT (perception data for speed limiting). Architecture.md §7.4 to be corrected. |
| **CAN ID overlap between high and low bus** — e.g., `0x201` on both buses, `0x204` on both buses | **MEDIUM** | Safe — buses are physically separate. But debugging confusion risk. `0x204` on high=TURN_CMD, low=DRIVE_CMD. Documented in §3. Option to offset high IDs to `0x100`–`0x1FF` in future. |
| **Split command frame sync** — three CAN frames may arrive at RT out of sync. | **MEDIUM** | RT queues commands, processes at 100Hz. Single tick assembles complete setpoint. 500ms staleness watchdog. Sequence numbers if issues observed. |
| **Jetson CAN TX jitter** — Linux is not realtime. | **MEDIUM** | RT queues commands. 500ms staleness watchdog. Acceptable for QM-level Jetson. |
| **`0x320` yaw rate** — no sensor currently provides yaw. | **MEDIUM** | Derive from EPS-C angle + speed (inverse kinematics), IMU (future, sensor TBD), or set to 0. Mark as TBD. |
| **`min_steering_rate` 125°/s** — EPS-C may reject commands below this. | **MEDIUM** | Added as parameter. RT must ensure commanded rate ≥ 125°/s. |
| **Headlight/brake light command** — split from turn signals needs new CAN ID. | **LOW** | Added `0x205` HOST_LIGHT_AUX on high bus. Does not collide with low-bus `0x205 RT_BRAKE_CMD` (separate buses). |
| **`VCU_Veh_Spd_Value` units** — steering-unit.md says km/h, system uses mm/s. | **LOW** | RT converts: `kmh = mmps × 0.0036`. |
| **Checksum algorithm** — XOR vs SUM ambiguous across SYNTREE docs. | **LOW** | Use XOR(bytes 0–6) ^ 0xFF per can-dictionary.md. Verify against physical unit. |

---

---

## 13. Multi-Agent Audit Results (2026-06-23)

Four independent agents audited this plan against all reference documents. Below is the consolidated report.

### 13A. CRITICAL Findings (must be resolved in this plan)

| # | Source | Issue | Resolution |
|:---|:---|:---|:---|
| **A1** | ROS 2 audit F1 | `longitudinal.speed` is wrong — Autoware field is `longitudinal.velocity` | ✅ Fixed in §6. All instances changed to `longitudinal.velocity`. |
| **A2** | ROS 2 audit F2 | `autoware_vehicle_msgs/msg/VehicleEmergencyStamped` does NOT exist | ✅ Fixed. Emergency uses `0x001` ESTOP CAN frame. ROS topic removed until Autoware adds an emergency message type. Document the direct CAN path. |
| **A3** | ROS 2 audit F3 | Gear values `0=N, 1=D, 2=S, 3=R` clash with Autoware constants `NEUTRAL=1, DRIVE=2, REVERSE=20` | ✅ Fixed. CAN bus keeps trike enum. ROS↔CAN mapping table added in §6. `GearReport.report` and `GearCommand.command` mapped to Autoware constants. |
| **A4** | Architecture F8 + Encoding H1 | `0x400` obstacle distance direction is contradictory in ALL documents (plan, architecture.md §2.2 vs §7.4, can-dictionary.md) | ✅ Resolved. `0x400` is Jetson→RT (perception→RT for speed limiting). RT also has local ultrasonic. Both feed obstacle speed limit. Plan §5 corrected. Architecture.md to be fixed separately. |
| **A5** | Encoding C1 | Steering angle offset ambiguous: architecture.md (offset=0) vs CSV/steering-unit.md (offset=-3000). Plan doesn't specify which RT uses for `0x169`. | ✅ Blocker added. RT translation must use the offset=-3000 encoding per SYNTREE CSV (manufacturer source of truth). Must verify against live CAN bus before Phase 5. Added to §12 risks. |
| **A6** | Architecture F10 | Splitting `0x300` into three frames (`0x200`+`0x202`+`0x203`) breaks RT's single-`cmd_queue` dispatch design | ✅ Dispatch redesign documented in §2B. Each frame updates a field in a shared `PendingSetpoint` struct. Control task assembles complete setpoint at 100 Hz. 500ms timeout on incomplete setpoints. |

### 13B. HIGH Findings

| # | Source | Issue | Resolution |
|:---|:---|:---|:---|
| **B1** | ROS 2 audit F4 | `ControlModeReport` values: plan says `0=Manual,1=Auto,2=ESTOP` but Autoware has `NO_COMMAND=0, AUTONOMOUS=1, MANUAL=4, DISENGAGED=5` | ✅ Fixed. ROS publication maps trike modes to Autoware constants: Manual→4, Auto→1, Estop→5. See §6 mapping. |
| **B2** | Architecture F1+F2 | Steering angle computation moved from RT to Jetson — undocumented architectural shift with realtime implications | ✅ Clarified. Autoware.Auto planning outputs `lateral.steering_tire_angle` directly. RT validates (dynamic clamp, hard-stops) and translates to EPS-C. RT does NOT compute kinematics — planning does. This is the Autoware.Auto convention and is safer (planner has full world model). |
| **B3** | Architecture F4 | `0x206`→`0x110` mode mapping allows Jetson to change vehicle mode, conflicting with SYS physical button exclusivity | ✅ Clarified. `0x206` is autonomy ENABLE within AUTO mode (sub-mode), NOT a full mode change. Does NOT map to `0x110`. When Jetson disables autonomy, RT stops sending commands but stays in AUTO. Mode change (MANUAL↔AUTO) remains SYS physical button only. |
| **B4** | Architecture F6 + CAN ID H2 + Encoding H4 | SYS heartbeat rate: architecture.md says 10 Hz/200ms in sections 8.6/8.9 but 2 Hz/500ms in section 8.4/8.7. can-dictionary.md says 2 Hz/1000ms. | ✅ Resolved. Plan explicitly requires SYS heartbeat at **10 Hz, 200ms timeout**. This is the architecture.md §8.6 design (documented rationale: brake safety FTTI). can-dictionary.md `0x7FE` entry is stale and must be updated as part of Phase 2. |
| **B5** | CAN ID H1 | `0x721` SEB_STATUS missing from RT's RX list in architecture.md §7.3 | ✅ Fixed. Plan §4 translation table includes `0x721`→`0x301`. Architecture.md §7.3 must be updated to add `0x721` to RT's low-bus RX list. |
| **B6** | Encoding H2 | Brake flow conflict: architecture.md §6.2 Option D says RT→SEB directly (1-hop), but CAN tables show RT→`0x205`→SYS→`0x7B9` | ✅ Resolved. Plan follows CAN tables: RT→`0x205`→SYS→`0x7B9`. Architecture.md §6.2 Option D prose is aspirational and predates the CAN table implementation. Option D text to be corrected. |
| **B7** | Encoding H5 | `VCU_Veh_Spd_Value` in `0x169` uses km/h per steering-unit.md, but rest of system uses mm/s | ✅ Fixed. RT must convert: `kmh = mmps × 3600 / 1,000,000`. Added to §4 translation table note. |

### 13C. MEDIUM Findings

| # | Source | Issue | Resolution |
|:---|:---|:---|:---|
| **C1** | ROS 2 audit F5+F6 | `is_defined_acceleration` and `is_defined_steering_tire_rotation_rate` flags not checked | ✅ Added to §6 mapping: skip brake/rate if undefined-flag is false. Use last valid value. |
| **C2** | CAN ID M1+M3+M4 | Category 1 forwarding rules for `0x011`, `0x120`, `0x302` must be explicitly removed when replaced by Category 2 translations | ✅ Added to Phase 3 checklist: remove old forwards. |
| **C3** | CAN ID M2 | `0x206` "Forward to high" comment in architecture.md §7.3 must be removed (conflicts with new high-bus `0x206`) | ✅ Added to Phase 2 checklist. |
| **C4** | Encoding H3 | `min_steering_rate` (125°/s) missing from parameter table | ✅ Added to §7 parameter table. |
| **C5** | Encoding M3 | Headlight/brake light high-bus CAN ID "TBD" — Jetson can't control lights in AUTO without it | ✅ Added `0x205` HOST_LIGHT_AUX (headlight + brake light) to §5 commands table. `0x204` = turn only, `0x205` = head+brake. Note: does NOT conflict with low-bus `0x205 RT_BRAKE_CMD` (separate buses). |
| **C6** | Architecture F9 | `0x320` speed+yaw is new feature, not an adoption | ✅ Noted in §5 as "NEW — added for Autoware.Auto VelocityReport compliance." |
| **C7** | ROS 2 audit F8 | Turn indicator `3=HAZARD` needs mapping to Autoware's separate HazardLights message family | ✅ Fixed. `0x204` value 3 (HAZARD) maps to publishing BOTH `TurnIndicatorsReport(ENABLE_LEFT+ENABLE_RIGHT)` AND `HazardLightsReport`. |
| **C8** | CAN ID L3 | `0x340` bitfield-to-mode mapping not documented | ✅ Added mapping table to §5 `0x340` entry. |

### 13D. LOW Findings (noted, no plan change needed)

| # | Issue |
|:---|:---|
| D1 | `0x204` on both buses (TURN_CMD high, DRIVE_CMD low) — debugging confusion risk. Noted in §12. |
| D2 | `0x220` "Keep or merge" underspecified — resolved as "Keep `0x220` as-is (reserved/inactive)." |
| D3 | `0x7B9` dual-sender overlap on heartbeat loss — cross-referenced to architecture.md §6.2 exception. |
| D4 | Steering rate type: plan uses i16, SYNTREE expects u16. Functionally identical for 125–525 range. Noted. |
| D5 | `SYS_ThrottleSpeed` min=0 but reverse is -500 mm/s. MTR firmware limitation, not plan issue. |
| D6 | Checksum algorithm (XOR vs SUM) ambiguous across documents. Plan references can-dictionary.md which calls for physical verification. |
| D7 | `0x206` MTR_MOTOR_FBK "Forward to high" comment in architecture.md is a documentation error — RT never forwards `0x206`. |

### 13E. Audit Coverage

| Dimension | Agent | Findings | CRITICAL | HIGH | MEDIUM | LOW |
|:---|:---|:---|:---|:---|:---|:---|
| CAN ID Mappings | ac05 | 10 | 0 | 2 | 4 | 4 |
| ROS 2 Messages | a143 | 9 | 3 | 1 | 3 | 2 |
| Encoding & Parameters | a75f | 16 | 1 | 5 | 5 | 5 |
| Architecture Consistency | a77b | 10 | 4 | 4 | 1 | 1 |
| **Total** | | **45** | **8** | **12** | **13** | **12** |

**Files referenced by audits:** `architecture.md`, `can-dictionary.md`, `docs/io-data.md`, `docs/by-wire - steering.csv`, `docs/by-wire - brake.csv`, `docs/steering-unit.md`, `docs/brake-unit.md`, `fixes.md`, `issues.md`, `work-plan.md`, `docs/distributed-architecture.md`

---

*Plan version: 0.0.4-alpha. Created 2026-06-23. Updated 2026-06-23 with 4-agent audit results (45 findings, 8 CRITICAL resolved).*

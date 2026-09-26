# RMESP32-T12D & System Architecture Bugs & Remediation Document

This document records the 22 identified architectural, timing, and safety issues across the `rm-esp32-t12d` receiver module gateway and inter-ECU contracts (`sys-esp32`, `mtr-stm32`), accompanied by the **simplest optimal solution**, logical rationale, and concrete C++ firmware implementation snippets.

---

## Summary Matrix

| # | Issue | Current Code Impact | Simplest Optimal Solution | Status |
|---|---|---|---|---|
|**1** | D <-> R changes while moving | Direct SWC gear assignment allows instant reverse torque at speed | `GearGuard`: Force `Gear::N` until speed < 100 mm/s | Recommended |
|**2** | `0x206` motor speed can be stale | No staleness tracking on received speed | 100 ms timestamp freshness check | Recommended |
|**3** | SWB Park engages while moving | Instantly clamps 15 mm stroke at full speed | Conditional park: only engage 15 mm hold if speed < 100 mm/s | Recommended |
|**4** | Park can release unexpectedly | SWB UP immediately drops stroke to 0 mm | Require throttle idle (<= 0.05) before park release | Recommended |
|**5** | Vehicle arms while in D or R | SWA arming allows immediate drive if gear switch is in D/R | Interlock: require `Gear::N` to arm | Recommended |
|**6** | SWC gear thresholds overlap | Microsecond boundaries (1300/1700) can chatter on noise | Discrete non-overlapping bands with retention | Recommended |
|**7** | Architecture says 'semantic' but sends final frames | Document says semantic, but frames are raw SES/SEB setpoints | Correct architecture: RM is **Manual Operator Command Gateway** | Recommended |
|**8** | SYS doesn't know RC link health | No dedicated RM heartbeat on CAN | Add 10 Hz `0x114 RM_STATUS` frame (2 bytes) | Recommended |
|**9** | Commands overwrite each other | Scattered priority across multiple `task_can_tx` blocks | Single `resolve_command()` function with cascading priority | Recommended |
|**10** | Manual/Auto ownership unclear | RM transmits drive commands continuously even in Auto mode | Multiplex in SYS: `mode == MANUAL ? RC : AUTO` | Recommended |
|**11** | `0x204` has no rolling counter | Frozen sender / repeated frames cannot be detected | Add 1-byte rolling counter to `0x204` | Recommended |
|**12** | Consumers have no timeout | Inconsistent staleness timeouts across nodes | Universal timeout rule: 5x period (100 ms for 20 ms frames) | Recommended |
|**13** | Bus-off recovery resumes old drive state | Auto-recovery restores TWAI without resetting arming state | Force disarm on `TWAI_ERROR_BUS_OFF`; require re-arming cycle | Recommended |
|**14** | CAN RX has lower riority than TX | RM has no CAN RX; SYS RX is lower priority than safety | Set CAN RX = 6, CAN TX = 4, RC Capture = 8 | Recommended |
|**15** | CAN RX polls unnecessarily | Periodic polling loop wastes CPU cycles and adds jitter | Event-driven blocking wait on TWAI receive | Recommended |
|**16** | ESTOP clearing is vague | Latch clearing condition not coupled to safe operator state | Clear latch only when SYS confirms clear AND SWA is Safe | Recommended |
|**17** | CAN byte order unspecified | Ambiguity between big-endian and little-endian signals | Standardize: Little-Endian default across all multi-byte signals | Recommended |
|**18** | DLC values are variable | Frames vary between DLC 4, 5, 8 creating inconsistent checks | Standardize standard control messages to DLC 8 | Recommended |
|**19** | Bit definitions vague in documentation | Bitmasks explained only in prose | Canonical bitmask constants in generated protocol headers | Recommended |
|**20** | Physical SBUS channel map not frozen | Channel indices scattered across files | Frozen `namespace rm::kCh*` constants as single source of truth | Recommended |
|**21** | Transmitter profile can change | Wrong T12D model profile could invert or offset channels | Startup plausibility gate: SWA Safe, SWC Neutral, Throttle Idle | Recommended |
|**22** | RF Degraded holds throttle | 1-frame drop keeps throttle active for up to 150 ms | Zero motor throttle immediately on Degraded or Lost link | Recommended |

---

## Detailed Problems, Logic, and Code Solutions

---

### 1. Problem: D <-> R or R <-> D Can Happen While Moving

#### Logic & Problem Rationale
Currently, the gear selector switch (`SWC`) directly controls `snap.gear`, which is copied directly into `drive_cmd.gear` transmitted via `0x204 RT_DRIVE_CMD`. If the operator flips `SWC` from `D` to `R` while the vehicle is traveling forward at 3000 mm/s, the motor controller receives a reverse command instantly. This can strip gear teeth, destroy the motor drive electronics, or lock the rear drive wheels.

#### Solution
Implement a minimal `GearGuard` state machine:
- Maintain `requested_gear` (physical switch position) and `active_gear` (commanded gear).
- If transitioning between `D` and `R`, force `active_gear = Gear::N` until vehicle speed is confirmed below 100 mm/s.
- Direct shifts to/from `N` (e.g. `N -> D` or `N -> R`) are permitted immediately when starting from rest.

```cpp
struct GearGuard {
    can::Gear active_gear{can::Gear::N};

    can::Gear update(can::Gear requested, int16_t vehicle_speed_mmps, bool speed_valid) {
        if (requested == active_gear) {
            return active_gear;
        }

        // Detect direction reversal across Neutral
        const bool is_reversal = (active_gear == can::Gear::D && requested == can::Gear::R) ||
                                  (active_gear == can::Gear::R && requested == can::Gear::D);

        if (is_reversal) {
            // Inhibit reversal until speed is confirmed near zero
            if (!speed_valid || std::abs(vehicle_speed_mmps) > 100) {
                active_gear = can::Gear::N;
            } else {
                active_gear = requested;
            }
        } else {
            active_gear = requested;
        }

        return active_gear;
    }
};
```

---

### 2. Problem: 0x206 Motor Speed Feedback Can Become Stale

#### Logic & Problem Rationale
`rm-esp32-t12d` currently doesn't listen to `0x206 MTR_MOTOR_FBK` on the CAN bus. Even if received, without a timestamp freshness check, a frozen MTR node or broken CAN link would cause RM to assume vehicle speed is 0 mm/s, bypassing speed-gated safety interlocks (such as `GearGuard` and Park engagement).

#### Solution
Track the arrival time of `0x206` and declare speed valid only within 100 ms (5x the 20 ms nominal period):

```cpp
struct SpeedTracker {
    int16_t  speed_mmps;0};
    uint32_t last_rx_ms{0};
    bool     received_ever{false};

    void on_0x206_received(int16_t speed, uint32_t now_ms) {
        speed_mmps = speed;
        last_rx_ms = now_ms;
        received_ever = true;
    }

    bool is_valid(uint32_t now_ms) const {
        return received_ever && ((now_ms - last_rx_ms) <= 100);
    }
};
```

---

### 3. Problem: SWB Park Can Engage While the Vehicle Is Moving

#### Logic & Problem Rationale
Currently in `rc_decoder.hh:
`x``cpp
snap.brake_stroke_mm = snap.park_hold_req ? std::max(stick_brake_mm, kParkBrakeStrokeMm) : stick_brake_mm;
```
Flipping `SWB` DOWN immediately commands 15.0 mm of brake stroke regardless of vehicle speed. At high speeds, clamping the mechanical brake instantly can lock the wheels or destabilize the vehicle.

#### Solution
Treat `SWB` as a **Park Request**. Hold the service brake setpoint until speed drops below 100 mm/s, then clamp to 15.0 mm:

```cpp
float resolve_brake_stroke(bool park_requested, float stick_brake_mm,
                           int16_t speed_mmps, bool speed_valid) {
    if (!park_requested) {
        return stick_brake_mm;
    }

    // Park requested: cut motor speed and apply appropriate brake level
    if (speed_valid && std::abs(speed_mmps) < 100) {
        // Vehicle stationary: lock park brake
        return std::max(stick_brake_mm, rm::kParkBrakeStrokeMm); // 15.0 mm
    } else {
        // Vehicle moving: maintain service brake or apply progressive deceleration
        return std::max(stick_brake_mm, 5.0f);
    }
}
```

---

### 4. Problem: Park Can Release Unexpectedly

#### Logic & Problem Rationale
If the operator accidentally moves `SWB` UP while the left stick (throttle) is pushed forward, the vehicle will instantly lurch forward at high acceleration because the park stroke drops to 0 mm immediately.

#### Solution
Require throttle idle (<= 0.05) and valid link before unlocking the park brake stroke:

```cpp
bool allow_park_release(bool park_requested, float throttle_norm, rm::LinkState link) {
    if (park_requested) return false;
    // Interlock: release only allowed if throttle is idle and link is healthy
    const bool throttle_idle = (throttle_norm <= 0.05f);
    const bool link_ok = (link == rm::LinkState::Normal);
    return throttle_idle && link_ok;
}
```

---

### 5. Problem: Vehicle Can Arm While SWC Is Already in D or R

#### Logic & Problem Rationale
In `main.cpp:97`:
```cpp
bool drive_active = snap.signal_valid && snap.drive_enable_req && !snap.park_hold_req;
```
Arming checks `SWA and `SWB`, but ignores `SWC`. If the vehicle is disarmed, the operator places `SWC` in `D`, pushes throttle, and then flips `SWAp OWN to arm, the vehicle surges forward immediately.

#### Solution
Enforce an arming interlock requiring Neutral before `drive_active` can transition to `true`:

```cpp
struct ArmingController {
    bool armed{false};

    bool update(bool swa_down, can::Gear requested_gear, float throttle_norm, bool link_ok) {
        if (!swa_down || !link_ok) {
            armed = false;
            return false;
        }

        // Arming edge: only allow initial arming when in Neutral with idle throttle
        if (!armed) {
            if (requested_gear == can::Gear::N && throttle_norm <= 0.05f) {
                armed = true;
            }
        }

        return armed;
    }
};
```

---

### 6. Problem: SWC Thresholds Overlap / Jitter

#### Logic & Problem Rationale
Currently `kGearRevMaxUs = 1300` and `kGearDriveMinUs = 1700`. If an RC receiver produces slight pulse jitter around 1300 us or 1700 us, the gear state can rapidly flicker between Neutral and Drive/Reverse.

#### Solution
Define non-overlapping bands with retention across deadbands:

```cpp
can::Gear decode_gear_switch(uint32_t pulse_us, can::Gear previous_gear) {
    if (pulse_us < 1250) {
        return can::Gear::R;
    } else if (pulse_us >= 1350 && pulse_us <= 1650) {
        return can::Gear::N;
    } else if (pulse_us > 1750) {
        return can::Gear::D;
    }
    // Retain previous state inside hysteresis guardbands (1250..1350 and 1650..1750)
    return previous_gear;
}
```

---

### 7. Problem: Architecture Calls Actuator Commands 'Semantic Requests'

#### Logic & Problem Rationale
`architecture.md` states RM sends 'semantic requests', yet frames `0x169 VCU_SES_REQ` and `0x7B9 VCU_SEB_REQ` contain direct actuator targets (steering degrees and brake stroke in mm). This creates confusion over which node owns actuator calibration.

#### Solution
Do not change the CAN IDs or protocol codecs. Update the architectural documentation:
- Designate RM as the **Manual Operator Command Gateway**.
- State explicitly that RM generates direct operator setpoints (`SES_REQ`, `SEB_RER`, `RT_DRIVE_CMD`), while `SYS` and `RT` act as supervisors that can limit, arbitrate, or override those setpoints.

---

### 8. Problem: SYS Doesn't Explicitly Know RC Link Health

#### Logic & Problem Rationale
SYS supervises overall vehicle safety, but does not receive an explicit heartbeat frame from RM. If RM drops communication, SYS only detects it indirectly through actuator timeout or lack of speed commands.

#### Solution
Add a lightweight 10 Hz heartbeat frame `0x114 RM_STATUS (DLC = 2):

```cpp
struct RmStatusMsg {
    // Byte 0: Health Flags
    uint8_t link_ok    : 1;  // bit 0: RC link active and valid
    uint8_t armed      : 1;  // bit 1: SWA armed
    uint8_t failsafe   : 1;  // bit 2: SBUS hardware failsafe active
    uint8_t frame_lost : 1;  // bit 3: Hardware frame dropped
    uint8_t reserved   : 4;  // bits 4..7
    // Byte 1: Rolling sequence
    uint8_t rolling_counter;
};
```

---

### 9. Problem: No Unified Priority Between Brake, Park, and Drive

#### Logic & Problem Rationale
Currently, decisions to cut throttle or apply brake stroke are dispersed across multiple code blocks in `task_can_tx`, increasing the risk of race conditions or contradictory CAN frames during edge cases.

#### Solution
Consolidate all arbitration into a single `resolve_command()` function with strict cascading priority:

```cpp
struct FinalCommand {
    float      steer_deg;
    float      brake_mm;
    int32_t    speed_mmps;
    can::Gear  gear;
    bool       ses_enable;
    bool       seb_enable;
};

FinalCommand resolve_command(const rm::RcSnapshot& snap, bool estop_active,
                             int16_t vehicle_speed_mmps, bool speed_valid) {
    FinalCommand cmd{};
    cmd.steer_deg = snap.steering_deg;
    cmd.ses_enable = snap.signal_valid;
    cmd.seb_enable = true;

    // 1. ESTOP Priority (Highest)
    if (estop_active) {
        cmd.brake_mm = rm::kMaxBrakeStrokeMm; // 27 mm full emergency brake
        cmd.speed_mmps = 0;
        cmd.gear = can::Gear::N;
        return cmd;
    }

    // 2. Link Loss / Signal Invalid Priority
    if (!snap.signal_valid || snap.link_state != rm::LinkState::Normal) {
        cmd.brake_mm = rm::kParkBrakeStrokeMm; // 15 mm fail-safe park stroke
        cmd.speed_mmps = 0;
        cmd.gear = can::Gear::N;
        return cmd;
    }

    // 3. Disarmed Priority (SWA UP)
    if (!snap.drive_enable_req) {
        cmd.brake_mm = snap.park_hold_req ? rm::kParkBrakeStrokeMm : snap.brake_stroke_mm;
        cmd.speed_mmps = 0;
        cmd.gear = can::Gear::N;
        return cmd;
    }

    // 4. Park Hold Request Priority (SWB OWN)
    if (snap.park_hold_req) {
        cmd.gear = can::Gear::N;
        cmd.speed_mmps = 0;
        if (speed_valid && std::abs(vehicle_speed_mmps) < 100) {
            cmd.brake_mm = std::max(snap.brake_stroke_mm, rm::kParkBrakeStrokeMm);
        } else {
            cmd.brake_mm = std::max(snap.brake_stroke_mm, 5.0f);
        }
        return cmd;
    }

    // 5. Service Brake Override Priority (Stick Brake > 5.0 mm)
    if (snap.brake_stroke_mm > rm::kBrakeThrottleCutoffMm) {
        cmd.brake_mm = snap.brake_stroke_mm;
        cmd.speed_mmps = 0;
        cmd.gear = snap.gear;
        return cmd;
    }

    // 6. Normal Drive Command
    cmd.brake_mm = snap.brake_stroke_mm;
    cmd.gear = snap.gear;
    cmd.speed_mmps = snap.target_speed_mmps;
    return cmd;
}
```

---

### 10. Problem: Manual / Auto Command Ownership Is Unclear

#### Logic & Problem Rationale
When the operator selects Auto mode via `SWD`, RM0½¹Ñ¥¹Õ•Ì‰É½…‘…ÍÑ¥¹œµ…¹Õ…°ÍÑ••É¥¹œ€¡€ÁàÄØå€¤…¹ÍÁ••€¡€ÁàÈÀÑ€¤½µµ…¹‘Ìİ¡¥±”Ñ¡”)•ÑÍ½¸…±Í¼‰É½…‘…ÍÑÌ…ÕÑ½¹½µ½ÕÌÑÉ…©•Ñ½Éä½µµ…¹‘Ì¸((ŒŒŒŒM½±ÕÑ¥½¸)%µÁ±•µ•¹Ğ„±•…¸µÕ±Ñ¥Á±•á•È¥¸MeM€€¡Ñ¡”ÍÕÁ•ÉÙ¥Í½Éä¹½‘”¤è(´]¡•¸µ½‘”€ôô5½‘”èé5…¹Õ…±€°MeLÉ½ÕÑ•ÌI4½µµ…¹‘ÌÑ¼…ÑÕ…Ñ½ÉÌ¸(´]¡•¸µ½‘”€ôô5½‘”èéÕÑ½€°MeLÉ½ÕÑ•Ì)•ÑÍ½¸½µµ…¹‘Ì€¡€ÁàÌÀÁ€¤Ñ¼…ÑÕ…Ñ½ÉÌ¸(´±¥ÁÁ¥¹œM]€‰…¬Ñ¼5…¹Õ…°…ÑÌ…Ì…¸¥µµ•‘¥…Ñ”¡…É‘İ…É”½Ù•ÉÉ¥‘”°¥¹ÍÑ…¹Ñ±äÉ•ÍÑ½É¥¹œI…ÕÑ¡½É¥Ñä¸((´´´((ŒŒŒ€ÄÄ¸AÉ½‰±•´è€ÁàÈÀĞIQ}I%Y}5!…Ì9¼É•Í¡¹•ÍÌ½Õ¹Ñ•È((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)U¹±¥­”€ÁàÄØå€°€Áàİå€°…¹€ÁàÄÄÁ€°™É…µ”€ÁàÈÀÑ€½¹Ñ…¥¹ÌÍÁ••…¹•…È°‰ÕĞ±…­Ì„É½±±¥¹œÍ•ÅÕ•¹”½Õ¹Ñ•È¸%˜Ñ¡”I4™¥Éµİ…É”¡…¹Ì½È„8ÑÉ…¹Í•¥Ù•È±½­ÌÕÀ°‘½İ¹ÍÑÉ•…´¹½‘•Ì…¹¹½Ğ‘•Ñ•Ğ„™É½é•¸™É…µ”™É½´Á…­•Ğ½¹Ñ•¹ÑÌ…±½¹”¸((ŒŒŒŒM½±ÕÑ¥½¸)UÍ”	åÑ”€Ô½˜€ÁàÈÀÑ€…Ì„É½±±¥¹œ½Õ¹Ñ•È€ À¸¸ÈÔÔ¤°¥¹É•µ•¹Ñ¥¹œ•Ù•Éä€ÄÀµÌè()ÁÀ)‘É¥Ù•}µ¹É½±±¥¹}½Õ¹Ñ•È€ô}É½±±}‘É¥Ù”¬¬ì)€((´´´((ŒŒŒ€ÄÈ¸AÉ½‰±•´è½¹ÍÕµ•ÉÌ!…Ù”9¼U¹¥™¥•Q¥µ•½ÕĞ	•¡…Ù¥½È((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)ÕÉÉ•¹Ñ±ä°‘¥™™•É•¹Ğ½¹ÍÕµ•ÉÌÕÍ”…É‰¥ÑÉ…ÉäÑ¥µ•½ÕĞÙ…±Õ•Ì€ ÈÀÀµÌ°€ÔÀÀµÌ°€ÄÔÀÀµÌ¤Ñ¼‘•Ñ•Ğµ¥ÍÍ¥¹œ™É…µ•Ì°±•…‘¥¹œÑ¼¥¹½¹Í¥ÍÑ•¹Ğ™…Õ±Ğ¡…¹‘±¥¹œ¸((ŒŒŒŒM½±ÕÑ¥½¸)MÑ…¹‘…É‘¥é”½¸Ñ¡”Õ¹¥Ù•ÉÍ…°¥¹‘ÕÍÑÉ¥…°ÉÕ±”è€¨©Q¥µ•½ÕĞ€ô€Õà¹½µ¥¹…°Á•É¥½¨¨¸()ğµ•ÍÍ…”ğ9½µ¥¹…°A•É¥½ğQ¥µ•½ÕĞQ¡É•Í¡½±ğÑ¥½¸½¸Q¥µ•½ÕĞğ)ğ´´µğ´´µğ´´µğ´´µğ)ğ€ÁàÈÀĞIQ}I%Y}5€ğ€ÄÀµÌğ€ÔÀµÌği•É¼µ½Ñ½ÈÍÁ••°Í¡¥™ĞÑ¼9•ÕÑÉ…°ğ)ğ€ÁàÄØäYU}MM}ID€ğ€ÈÀµÌğ€ÄÀÀµÌğ¥Í…‰±”ÍÑ••É¥¹œ…ÍÍ¥ÍĞğ)ğ€ÁàİäYU}M	}IE€ğ€ÈÀµÌğ€ÄÀÀµÌğÁÁ±ä™…¥°µÍ…™”‰É…­”ÍÑÉ½­”ğ)ğ€ÁàÈÀØ5QI}5=Q=I}	-€ğ€ÈÀµÌğ€ÄÀÀµÌğ5…É¬ÍÁ••¥¹Ù…±¥€¡ÍÁ••‘}Ù…±¥€ô™…±Í•€¤ğ)ğ€ÁàİMeM}!IQ	Q€ğ€ÄÀÀµÌğ€ÔÀÀµÌğQÉ¥•È‘•É…‘•µ½‘”€¼Í…™”ÍÑ½Àğ((´´´((ŒŒŒ€ÄÌ¸AÉ½‰±•´è8	ÕÌµ=™˜I•½Ù•ÉäI•ÍÕµ•Ì=±½¹ÑÉ½°MÑ…Ñ”((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)%¸…¹}‘É¥Ù•È¹ èÄÔİ€°…ÕÑ½µ…Ñ¥Œ‰ÕÌµ½™˜É•½Ù•ÉäÉ•ÍÑ½É•ÌÑ¡”M@ÌÈQ]$Á•É¥Á¡•É…°¸%˜Ñ¡”Ù•¡¥±”İ…Ì¥¸É¥Ù•€…Ğ™Õ±°ÍÁ••İ¡•¸Ñ¡”‰ÕÌ™…¥±•°É•ÍÕµ¥¹œÑÉ…¹Íµ¥ÍÍ¥½¸½Õ±ÍÕ‘‘•¹±ä½µµ…¹µ½Ñ¥½¸İ¥Ñ¡½ÕĞ½Á•É…Ñ½È½¹Í•¹Ğ¸((ŒŒŒŒM½±ÕÑ¥½¸)½É”„Í½™Ñİ…É”‘¥Í…É´İ¡•¹•Ù•È‰ÕÌµ½™˜½ÕÉÌè()ÁÀ)¥˜€¡…¹}‘É¥Ù•È¹¡•…±Ñ¡}Í¹…ÁÍ¡½Ğ ¤¹ÍÑ…Ñ”€ôô…¸èé…¹É¥Ù•Èèé!•…±Ñ¡MÑ…Ñ”èé	ÕÍ=™˜¤ì(€€€…Éµ•€ô™…±Í”ì(€€€‘É¥Ù•}…Ñ¥Ù”€ô™…±Í”ì)ô(¼¼I•½Ù•ÉäÉ•ÅÕ¥É•ÌÉ•ÑÕÉ¹¥¹œM]Ñ¼U@€¡M…™”¤…¹å±¥¹œ‰…¬Ñ¼=]8€¡Éµ•¤)€((´´´((ŒŒŒ€ÄĞ¸AÉ½‰±•´è8I`M…™•Ñä€¼½¹ÑÉ½°Q…Í¬!…Ì1½ÜAÉ¥½É¥Ñä((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)%¸É••IQ=LÍ¡•‘Õ±¥¹œè(´I…ÁÑÕÉ”èAÉ¥½É¥Ñä€à(´8Q`èAÉ¥½É¥Ñä€Ğ(´8I`€¼¥ÍÁ…Ñ èAÉ¥½É¥Ñä€È€¡½È¹½¸µ•á¥ÍÑ•¹Ğ¥¸I4¤()%˜¡¥ µÉ…Ñ”8ÑÉ…¹Íµ¥ÍÍ¥½¹ÌÍ…ÑÕÉ…Ñ”Ñ¡”ÍåÍÑ•´°¥¹½µ¥¹œÍ…™•ÑäµÉ¥Ñ¥…°™É…µ•Ì€¡ÍÕ …Ì€ÁàÀÀÄMQe}MQ=A€½È€ÁàÈÀØ5QI}5=Q=I}	-€¤…¸‰”‘•±…å•‰•¡¥¹½ÕÑ‰½Õ¹½µµ…¹ÑÉ…™™¥Œ¸((ŒŒŒŒM½±ÕÑ¥½¸)±¥¸Ñ…Í¬ÁÉ¥½É¥Ñ¥•Ì…É½ÍÌM@ÌÈµ½‘Õ±•Ìè(Ä¸I…ÁÑÕÉ•€è€¨©AÉ¥½É¥Ñä€à¨¨€¡!…ÉÉ•…°µÑ¥µ”UIPI`¤(È¸8I`€¼¥ÍÁ…Ñ¡€è€¨©AÉ¥½É¥Ñä€Ø¨¨€¡%µµ•‘¥…Ñ”Í…™•Ñä™É…µ”ÁÉ½•ÍÍ¥¹œ¤(Ì¸8Qa€è€¨©AÉ¥½É¥Ñä€Ğ¨¨€¡A•É¥½‘¥Œ½ÕÑ‰½Õ¹ÍÑÉ•…µ¥¹œ¤(Ğ¸!•…ÉÑ‰•…Ğ€˜¥…¹½ÍÑ¥Í€è€¨©AÉ¥½É¥Ñä€Ä¨¨€¡	…­É½Õ¹µ½¹¥Ñ½É¥¹œ¤((´´´((ŒŒŒ€ÄÔ¸AÉ½‰±•´è8I`U¹¹••ÍÍ…É¥±äA½±±Ì…Ğ€ÈÀµÌ((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)A½±±¥¹œÑİ…¥}É••¥Ù” ¥€¥¸„Á•É¥½‘¥Œ±½½À¥¹ÑÉ½‘Õ•ÌÕÀÑ¼€ÈÀµÌ½˜±…Ñ•¹ä…¹…ÕÍ•ÌATİ…­•ÕÁÌ•Ù•¸İ¡•¸Ñ¡”‰ÕÌ¥Ì¥‘±”¸((ŒŒŒŒM½±ÕÑ¥½¸)5…­”8É••ÁÑ¥½¸•Ù•¹Ğµ‘É¥Ù•¸‰ä‰±½­¥¹œ½¸Ñ¡”Q]$ÅÕ•Õ”è()ÁÀ)mm¹½É•ÑÕÉ¹utÍÑ…Ñ¥ŒÙ½¥Ñ…Í­}…¹}Éà¡Ù½¥¨¤ì(€€€Ñİ…¥}µ•ÍÍ…•}ĞµÍœì(€€€İ¡¥±”€ Ä¤ì(€€€€€€€€¼¼	±½¬¥¹‘•™¥¹¥Ñ•±äÕ¹Ñ¥°„™É…µ”…ÉÉ¥Ù•Ì€¡½È€ÄÀÁµÌÑ¥µ•½ÕĞ™½Èİ…Ñ¡‘½œ¤(€€€€€€€¥˜€¡Ñİ…¥}É••¥Ù” ™µÍœ°Á‘5M}Q=}Q%-L ÄÀÀ¤¤€ôôMA}=,¤ì(€€€€€€€€€€€ÁÉ½•ÍÍ}…¹}™É…µ”¡µÍœ¤ì(€€€€€€€ô(€€€ô)ô)€((´´´((ŒŒŒ€ÄØ¸AÉ½‰±•´èMQ=@±•…É¥¹œ%ÌY…Õ”((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)¹½‘”µ¥¡Ğ±•…È¥ÑÌ±½…°MQ=@ÍÑ…Ñ”İ¡¥±”Ñ¡”¥¹¥Ñ¥…Ñ¥¹œÍ…™•Ñä™…Õ±Ğ¥ÌÍÑ¥±°ÁÉ•Í•¹Ğ°±•…‘¥¹œÑ¼‘…¹•É½ÕÌ±•…É¥¹œ½Í¥±±…Ñ¥½¹Ì¸((ŒŒŒŒM½±ÕÑ¥½¸)¹™½É”…¸•áÁ±¥¥ĞÑİ¼µÍÑ•ÀÕ¹±…Ñ ÉÕ±”è(Ä¸¸MQ=@™É…µ”€¡€ÁàÀÀÅ€½È¡…É‘İ…É”±¥¹”¤Í•ÑÌ•ÍÑ½Á}±…Ñ¡•€ôÑÉÕ•€¸(È¸Q¡”±…Ñ ¥Ì±•…É•€¨©½¹±ä¨¨İ¡•¸MeM}MQe}MQM€É•Á½ÉÑÌ•ÍÑ½Á}…Ñ¥Ù”€ôô™…±Í•€€¨©9¨¨Ñ¡”Á¡åÍ¥…°M]ÀÍİ¥Ñ ¥Ì¥¸Ñ¡”M…™•€€¡U@¤Á½Í¥Ñ¥½¸¸((´´´((ŒŒŒ€ÄÜ¸AÉ½‰±•´è8¹‘¥…¹¹•ÍÌ€˜	åÑ”=É‘•ÈÉ”µ‰¥Õ½ÕÌ((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)M½µ”½±‘•È…ÑÕ…Ñ½È™É…µ•ÌÕÍ”‰¥œµ•¹‘¥…¸É•ÁÉ•Í•¹Ñ…Ñ¥½¹Ì°İ¡¥±”M@ÌÈ…¹MQ4ÌÈ¹…Ñ¥Ù•±ä½Á•É…Ñ”±¥ÑÑ±”µ•¹‘¥…¸¸]¥Ñ¡½ÕĞ…¸•áÁ±¥¥ĞÉÕ±”°µÕ±Ñ¤µ‰åÑ”™¥•±‘ÌÉ¥Í¬‰åÑ”µÍİ…ÁÁ¥¹œ•ÉÉ½ÉÌ¸((ŒŒŒŒM½±ÕÑ¥½¸)‘„‰¥¹‘¥¹œ±…ÕÍ”Ñ¼Ñ¡”ÁÉ½Ñ½½°½¹ÑÉ…Ğè(ø€¨©MÑ…¹‘…Éè¨¨±°µÕ±Ñ¤µ‰åÑ”¥¹Ñ••ÈÍ¥¹…±Ì…É½ÍÌÑ¡””µÑÉ¥­”8‰ÕÌ…É”€¨©1¥ÑÑ±”µ¹‘¥…¸¨¨Õ¹±•ÍÌ•áÁ±¥¥Ñ±äÉ•ÅÕ¥É•‰ä™É½é•¸€ÍÉµÁ…ÉÑä…ÑÕ…Ñ½È™¥Éµİ…É”€¡€ÁàÄØäMM€…¹€ÁàİäM	€¤¸((´´´((ŒŒŒ€Äà¸AÉ½‰±•´è1%Ì%¹½¹Í¥ÍÑ•¹ĞÉ½ÍÌ9½Éµ…°É…µ•Ì((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)€ÁàÈÀÑ€¥Ì1€Ô°€ÁàÈÀÙ€¥Ì1€Ğ°…¹€ÁàÀÄÅ€¥Ì1€à¸Y…É¥…‰±”1Ì½µÁ±¥…Ñ”¡…É‘İ…É”…•ÁÑ…¹”™¥±Ñ•É¥¹œ…¹±•…Ù”¹¼É½½´™½ÈÍ•ÅÕ•¹”½Õ¹Ñ•ÉÌİ¥Ñ¡½ÕĞ‰É•…­¥¹œ™É…µ”Í¥é”Ù…±¥‘…Ñ¥½¸¸((ŒŒŒŒM½±ÕÑ¥½¸(´MÑ…¹‘…É‘¥é”…±°Á•É¥½‘¥Œ½µµ…¹…¹Ñ•±•µ•ÑÉä™É…µ•ÌÑ¼€¨©1€ô€à¨¨€¡é•É¼µÁ…‘‘•¤¸(´AÉ•Í•ÉÙ”€ÁàÀÀÄMQe}MQ=A€…Ì€¨©1€ô€À¨¨Ñ¼½µÁ±äİ¥Ñ ™É½é•¸…ÑÕ…Ñ½ÈÍ…™•Ñä±½¥Œ¸((´´´((ŒŒŒ€Ää¸AÉ½‰±•´è	¥Ğ•™¥¹¥Ñ¥½¹ÌÉ”Y…Õ”¥¸½Õµ•¹Ñ…Ñ¥½¸((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)•ÍÉ¥‰¥¹œ‰¥Ñ™¥•±‘Ì½¹±ä¥¸µ…É­‘½İ¸Ñ…‰±•Ì±•…‘ÌÑ¼‘É¥™Ğ‰•Ñİ••¸½‘”…¹‘½Õµ•¹Ñ…Ñ¥½¸¸((ŒŒŒŒM½±ÕÑ¥½¸)5…¥¹Ñ…¥¸…¹½¹¥…°‘•™¥¹¥Ñ¥½¹Ì¥¸•¹•É…Ñ•¬¬¡•…‘•ÉÌ€¡•ÑÉ¥­•}ÁÉ½Ñ½½°¹¡ÁÁ€¤…¹ÁÉ½Ñ½½°e50™¥±•Ìè()ÁÀ)¹…µ•ÍÁ…”MåÍM…™•Ñå	¥ÑÌì(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­ÍÑ½ÁÑ¥Ù”€ô€ ÅÔ€ğğ€À¤ì(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­!•…ÉÑ‰•…Ñ=¬€ô€ ÅÔ€ğğ€Ä¤ì(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­	É…­•1¥¡Ğ€€ô€ ÅÔ€ğğ€È¤ì(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­!•…‘±¥¡Ğ€€€ô€ ÅÔ€ğğ€Ì¤ì)ô)€((´´´((ŒŒŒ€ÈÀ¸AÉ½‰±•´èPÄÉA¡åÍ¥…°µÑ¼µM	UL¡…¹¹•°5…ÁÁ¥¹œ%Í¸ĞÉ½é•¸((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)UÍ¥¹œ‰…É”¹Õµ•É¥Œ¡…¹¹•°¥¹‘•á•Ì€¡”¹œ¸°ÁÕ±Í•}ÕÍlÉu€¤¥¸µÕ±Ñ¥Á±”™¥±•Ì±•…‘ÌÑ¼‰ÕÌİ¡•¸¡…¹¹•±Ì…É”É•µ…ÁÁ•½¸Ñ¡”ÑÉ…¹Íµ¥ÑÑ•È¸((ŒŒŒŒM½±ÕÑ¥½¸)É••é”…±°¡…¹¹•°…ÍÍ¥¹µ•¹ÑÌ¥¸É´µ•ÍÀÌÈµĞÄÉ½ÍÉŒ½½¹™¥œ¹¡€è()ÁÀ)¹…µ•ÍÁ…”É´ì)¹…µ•ÍÁ…”I¡…¹¹•°ì(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­MÑ••É¥¹œ€€€€ô€Àì€€¼¼ ÄèI¥¡ĞMÑ¥¬`€ ¬¼´ĞÔ‘•œ¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­	É…­”€€€€€€€ô€Äì€€¼¼ ÈèI¥¡ĞMÑ¥¬d€¡¸¸Èİµ´¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­Q¡É½ÑÑ±”€€€€ô€Èì€€¼¼ Ìè1•™ĞMÑ¥¬d€¡¸¸ÄÀÀ”¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­É¥Ù•¹…‰±”€ô€Ğì€€¼¼ ÔèM]€¡U@õM…™”°=]8õÉµ•¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­A…É­!½±€€€€ô€Ôì€€¼¼ ØèM]€¡U@õI•±•…Í•°=]8õA…É¬¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­•…È€€€€€€€€ô€Øì€€¼¼ ÜèM]€¡U@õH°5%õ8°=]8õ¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­ÕÑ½I•ÅÕ•ÍĞ€ô€Üì€€¼¼ àèM]€¡U@õ5…¹Õ…°°=]8õÕÑ¼¤(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­ÕáYÉ„€€€€€€ô€àì€€¼¼ äèYI¥…°(€€€½¹ÍÑ•áÁÈÕ¥¹Ğá}Ğ­ÕáYÉˆ€€€€€€ô€äì€€¼¼ ÄÀèYI¥…°)ô)ô)€((´´´((ŒŒŒ€ÈÄ¸AÉ½‰±•´èQÉ…¹Íµ¥ÑÑ•ÈAÉ½™¥±”€¼5½‘•°M•±•Ñ¥½¸…¸¡…¹”((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)%˜…¸½Á•É…Ñ½ÈÍ•±•ÑÌÑ¡”İÉ½¹œµ½‘•°µ•µ½Éä½¸Ñ¡”I…‘¥½1¥¹¬PÄÉ°¡…¹¹•°‘¥É•Ñ¥½¹Ì½ÈÑÉ¥µÌ½Õ±‰”¥¹Ù•ÉÑ•°…ÕÍ¥¹œÑ¡”Ù•¡¥±”Ñ¼…•±•É…Ñ”İ¡•¸Ñ¡É½ÑÑ±”¥ÌÉ•±•…Í•¸((ŒŒŒŒM½±ÕÑ¥½¸)%µÁ±•µ•¹Ğ„€¨©	½½ĞA±…ÕÍ¥‰¥±¥Ñä…Ñ”¨¨è)Ğ‰½½ÑÕÀ°¥¹¡¥‰¥Ğ…Éµ¥¹œÕ¹Ñ¥°…±°½¹ÑÉ½±Ì…É”Ù•É¥™¥•¥¸Ñ¡•¥ÈÍ…™”Á¡åÍ¥…°É•ÍÑ¥¹œÁ½Í¥Ñ¥½¹Ìè(Ä¸M]€µÕÍĞ‰”¥¸M…™•€€¡U@°ÁÕ±Í”€ğ€ÄÌÀÀÕÌ¤¸(È¸M]€µÕÍĞ‰”¥¸9•ÕÑÉ…±€€¡5%°ÁÕ±Í”€ÄÌÔÀ¸¸ÄØÔÀÕÌ¤¸(Ì¸Q¡É½ÑÑ±”ÍÑ¥¬µÕÍĞ‰”%‘±•€€¡ÁÕ±Í”€ğ€ÄÄÀÀÕÌ¤¸(Ğ¸±°…Ñ¥Ù”¡…¹¹•±Ì€ À¸¸ä¤µÕÍĞ‰”İ¥Ñ¡¥¸Ù…±¥É…¹”€ äÀÀ¸¸ÈÄÀÀÕÌ¤¸((´´´((ŒŒŒ€ÈÈ¸AÉ½‰±•´èI•É…‘•½¹Ñ¥¹Õ•Ì!½±‘¥¹œQ¡É½ÑÑ±”M•ÑÁ½¥¹Ğ((ŒŒŒŒ1½¥Œ€˜AÉ½‰±•´I…Ñ¥½¹…±”)%¸É}‘•½‘•È¹ èàÀ´àÑ€„Í¥¹±”‘É½ÁÁ•™É…µ”Í•ÑÌ1¥¹­MÑ…Ñ”èé•É…‘•‘€İ¡¥±”Í¥¹…±}Ù…±¥‘€É•µ…¥¹ÌÑÉÕ•€¸Ì	©e result, the motor continues driving at the previous speed setpoint for up to 150 ms during signal degradation.

#### Solution
For remote-control operations, cut propulsion torque immediately on any degradation:
- Require `link_state == LinkState::Normal` for propulsion.
- If `link_state` is `Degraded` or `Lost`, force `target_motor_speed = 0` immediately, while continuing to permit steering and braking inputs during transient drops:

```cpp
bool drive_propulsion_allowed = (snap.link_state == rm::LinkState::Normal) &&
                                 snap.signal_valid &&
                                 snap.drive_enable_req &&
                                 !snap.park_hold_req;

int32_t target_motor_speed = drive_propulsion_allowed ? snap.target_speed_mmps : 0;
```

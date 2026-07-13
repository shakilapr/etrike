# Light System Design — E-Trike

All lights controlled by SYS ESP32-S3. Design covers all modes: MANUAL, AUTO, ESTOP. No other node directly drives lamps — SYS is the sole output stage.

---

## 1. Light inventory

| # | Light | GPIO | Type | Priority |
|---|-------|------|------|----------|
| 1 | Brake light | 21 | OUT — relay → 12V lamp | Safety-critical |
| 2 | Left turn lamp | 18 | OUT — relay → 12V lamp | Safety-relevant |
| 3 | Right turn lamp | 19 | OUT — relay → 12V lamp | Safety-relevant |
| 4 | Headlight | 22 | OUT — relay → 12V lamp | Non-safety |
| 5 | Reverse light | **TBD** | OUT — relay → 12V lamp | Non-safety |
| 6 | Position/running lights | **TBD** | OUT — relay → 12V lamp | Non-safety |
| 7 | AUTO mode bulb | 25 | OUT — relay → 12V bulb | Non-safety |
| 8 | MANUAL mode bulb | 26 | OUT — relay → 12V bulb | Non-safety |

GPIOs 8 and 9 are available for reverse and position lights. Need relay modules (same type as turn lamps).

---

## 2. Brake light — GPIO21

**Safety-critical. Must work in all modes without CAN dependency.**

```
brake_light = (brake_lever_pressed)        // GPIO2 — physical lever
           OR (mode == ESTOP)              // ESTOP — full brake
           OR g_light_state.brake_light;   // CAN 0x302 bit2 — Jetson supplemental
```

All three sources are local to SYS. The Jetson CAN bit is supplemental only — it can illuminate the brake light predictively (obstacle detected before pressure builds) but can never be the only reason the brake light is OFF when physical braking is active.

ESTOP forces the brake light ON regardless of all other inputs.

---

## 3. Turn signals — GPIO18/19

### 3.1 MANUAL mode — handlebar switches

| Switch | GPIO | Type | Action |
|--------|------|------|--------|
| Left turn | 3 | Momentary, active-low, pull-up | Press → toggles left blink. Press again → stops. |
| Right turn | 6 | Momentary, active-low, pull-up | Press → toggles right blink. Press again → stops. |

Toggle behavior:
- Press once: blinking starts (500ms on/off)
- Press same side again: blinking stops
- Press opposite side: cancels current side, starts new side
- Both pressed simultaneously: hazard flashers (sync blink)

No CAN involvement. Works even if both CAN buses are dead.

### 3.2 AUTO mode — two-tier control

```
                    ┌──────────────────────┐
                    │  0x302 HOST_LIGHT_CMD │
                    │  (Jetson → RT → SYS) │
                    │  bit0: left_turn     │
                    │  bit1: right_turn    │
                    │  bit4: auto_turn_en  │
                    └──────────┬───────────┘
                               │
            ┌──────────────────┼──────────────────┐
            ▼                  ▼                  ▼
    auto_turn_en = 0    auto_turn_en = 1    ESTOP
    Jetson direct       Low-level auto      all OFF
```

**Tier 1 — Low-level auto-logic** (`auto_turn_en = 1`):

RT monitors `SES_StrAngle` from EPS-C status (`0x201`) at 100 Hz. Logic runs on RT:

```
If abs(angle) > 15° for > 500ms → set turn signal direction
If abs(angle) < 5° for > 1000ms → cancel turn signal
```

RT communicates the result to SYS by originating `0x302` directly on the low bus — same CAN ID it normally forwards from Jetson on the high bus. The turn bits (0-1) are set based on steering angle. RT merges its auto-turn bits with forwarded Jetson bits via bitwise OR — Jetson can always add a turn signal even when auto-logic hasn't triggered.

This logic handles lane changes (short angle spikes don't trigger), gentle curves (angle stays elevated), and return-to-center after a turn. The thresholds (15°, 5°) and timings (500ms, 1000ms) are configurable via RT config.h.

**Tier 2 — Jetson direct control** (`auto_turn_en = 0`):

Jetson sets bits 0-1 of `0x302`. SYS uses them directly. No auto-logic. Jetson's planning stack knows intended trajectories and can signal BEFORE the turn starts (predictive signaling). This is the default when auto_turn_en is 0.

**Priority when both active:** `auto_turn_en = 1` uses OR logic — auto-turn bits OR Jetson bits. Jetson can always add a signal; it just can't suppress one that auto-logic has triggered (intentional — better to signal unnecessarily than miss a signal).

### 3.3 Blink pattern (all modes)

500ms ON, 500ms OFF. Managed by `lights_task` at 20 Hz. One timer for both sides — they blink in phase. Both active = hazard flashers (synchronized blink, not alternating).

### 3.4 ESTOP override

All turn signals OFF regardless of mode, switches, or CAN state.

---

## 4. Headlight — GPIO10

| Mode | Control |
|------|---------|
| MANUAL | Handlebar toggle switch (GPIO7). Each press toggles on/off. State persists across mode changes. |
| AUTO | `g_light_state.headlight` from CAN `0x302` bit3 overrides manual state. |
| ESTOP | OFF |

No auto-logic needed — headlight is purely manual or Jetson-commanded.

---

## 5. Reverse light — TBD GPIO

```
reverse_light = (current_gear == R)
```

Gear state comes from MTR via `0x206 MTR_MOTOR_FBK` (gear_state field, updated at 50 Hz). Works identically in MANUAL and AUTO — whenever the gear selector or CAN command puts the vehicle in reverse, the light illuminates. No CAN dependency for the decision (gear state is always local to SYS after receiving `0x206`).

Fails safe: if `0x206` stops arriving, last known gear state decays to N after timeout → reverse light turns off.

---

## 6. Position/running lights — TBD GPIO

```
position_lights = (mode != ESTOP)
```

ON whenever the vehicle is powered. OFF on ESTOP (12V rail may be dead anyway). Simple state-derived — no CAN, no mode switching. These are low-intensity markers visible from the side, required by most vehicle regulations.

Optional CAN override: if `g_light_state.position_lights` (new bit in `0x302`) is explicitly set to 0, the lights turn off for energy saving or stealth mode. But the default (no CAN signal present) is ON.

---

## 7. Mode indicator bulbs — GPIO48/26

| Mode | AUTO bulb (GPIO48) | MANUAL bulb (GPIO36) |
|------|-------------------|---------------------|
| MANUAL | OFF | ON |
| AUTO | ON | OFF |
| ESTOP | OFF | OFF |

Both OFF = ESTOP (visually distinct from both MANUAL and AUTO). Bulbs powered from 12V accessory rail via relays — they go dark on ESTOP regardless of MCU state.

---

## 8. CAN interface — 0x302 HOST_LIGHT_CMD

### Current (DLC=1)

| Byte | Bit | Signal |
|------|-----|--------|
| 0 | 0 | left_turn |
| 0 | 1 | right_turn |
| 0 | 2 | brake_light |
| 0 | 3 | headlight |
| 0 | 4-7 | (reserved) |

### Proposed expansion (DLC=2)

| Byte | Bit | Signal | Description |
|------|-----|--------|-------------|
| 0 | 0 | left_turn | Turn signal ON |
| 0 | 1 | right_turn | Turn signal ON |
| 0 | 2 | brake_light | Supplemental brake (OR with lever + ESTOP) |
| 0 | 3 | headlight | Headlight ON |
| 0 | 4 | auto_turn_enable | Enable Tier 1 auto-turn logic (AUTO mode only) |
| 0 | 5 | position_lights | Position lights ON (0=force off, 1=ON) |
| 0 | 6-7 | (reserved) | Set to 0 |
| 1 | 0-7 | (reserved) | Future use |

Byte 1 added for future needs without changing the ID. Backward compatible — receivers that expect DLC=1 just ignore the extra byte.

---

## 9. RT auto-turn signal path

RT monitors `0x201 SES_StrAngle` at 100 Hz. When `auto_turn_enable` is active, RT originates `0x302` on the low bus:

```
RT receives 0x302 from Jetson on high bus (forward to low)
  → Stores Jetson's bits for forwarding
  → If auto_turn_en = 1:
      Run steering angle logic
      merged = Jetson_bits OR auto_turn_bits
      Send merged as 0x302 on low bus
  → Else:
      Send Jetson_bits as-is on low bus
```

SYS receives `0x302` on low bus. It cannot tell whether the bits came from Jetson (forwarded) or RT (originated) — same ID, same payload. This is intentional — SYS doesn't need to know.

---

## 10. ESTOP behavior

| Light | ESTOP state |
|-------|------------|
| Brake | **ON** (forced) |
| Left turn | OFF |
| Right turn | OFF |
| Headlight | OFF |
| Reverse | OFF |
| Position | OFF |
| AUTO bulb | OFF |
| MANUAL bulb | OFF |

All lamps except brake go dark. The 12V accessory relay (GPIO37) is cut on ESTOP, so all relay-driven lights lose power at the source regardless of MCU GPIO state.
